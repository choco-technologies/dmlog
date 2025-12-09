#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include "monitor.h"
#include "trace.h"
#include "gdb.h"
#include <termios.h>
#include <fcntl.h>
#include <errno.h>

/**
 * @brief Configure terminal input mode (echo and line mode)
 * 
 * @param echo true to enable echo, false to disable
 * @param line_mode true to enable line mode, false to disable
 */
static void configure_input_mode(bool echo, bool line_mode)
{
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    if(echo)
    {
        tty.c_lflag |= ECHO;
    }
    else
    {
        tty.c_lflag &= ~ECHO;
    }

    if(line_mode)
    {
        tty.c_lflag |= ICANON;
    }
    else
    {
        tty.c_lflag &= ~ICANON;

        tty.c_cc[VMIN] = 1;  // Minimum number of characters to read
        tty.c_cc[VTIME] = 0; // No timeout

        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

/**
 * @brief Get the amount of data left in the dmlog ring buffer
 * 
 * @param ctx Pointer to the monitor context
 * @return uint32_t Number of bytes left in the buffer
 */
static uint32_t get_left_data_in_buffer(monitor_ctx_t* ctx)
{
    if(ctx->ring.head_offset >= ctx->tail_offset)
    {
        return ctx->ring.head_offset - ctx->tail_offset;
    }
    else
    {
        return ctx->ring.buffer_size - (ctx->tail_offset - ctx->ring.head_offset);
    }
}

/**
 * @brief Check if the dmlog ring buffer is empty
 * 
 * @param ctx Pointer to the monitor context
 * @return true if buffer is empty, false otherwise
 */
static bool is_buffer_empty(monitor_ctx_t* ctx)
{
    return ctx->ring.head_offset == ctx->tail_offset;
}

/**
 * @brief Read data from the dmlog ring buffer, handling wrap-around
 * 
 * @param ctx Pointer to the monitor context
 * @param dst Destination buffer to store read data
 * @param length Number of bytes to read
 * @return true on success, false on failure
 */
static bool read_from_buffer(monitor_ctx_t* ctx, void* dst, size_t length)
{
    dmlog_index_t available_data = get_left_data_in_buffer(ctx);
    if(available_data == 0)
    {
        TRACE_ERROR("Buffer is empty\n");
        return false;
    }
    length = length > available_data ? available_data : length;
    uint32_t left_size = ctx->ring.buffer_size - ctx->tail_offset;
    if(length <= left_size)
    {
        uint32_t address = (uint32_t)((uintptr_t)ctx->ring.buffer) + ctx->tail_offset;
        if(backend_read_memory(ctx->backend_type, ctx->socket, address, dst, length) < 0)
        {
            TRACE_ERROR("Failed to read %zu bytes from buffer at offset %u\n", length, ctx->tail_offset);
            return false;
        }
        ctx->tail_offset = (ctx->tail_offset + length) % ctx->ring.buffer_size;
    }
    else
    {
        // Read in two parts due to wrap-around
        uint32_t address = (uint32_t)((uintptr_t)ctx->ring.buffer) + ctx->tail_offset;
        if(backend_read_memory(ctx->backend_type, ctx->socket, address, dst, left_size) < 0)
        {
            TRACE_ERROR("Failed to read %u bytes from buffer at offset %u\n", left_size, ctx->tail_offset);
            return false;
        }
        size_t remaining = length - left_size;
        address = (uint32_t)((uintptr_t)ctx->ring.buffer);
        if(backend_read_memory(ctx->backend_type, ctx->socket, address, (uint8_t*)dst + left_size, remaining) < 0)
        {
            TRACE_ERROR("Failed to read %zu bytes from buffer at offset 0\n", remaining);
            return false;
        }
        ctx->tail_offset = remaining;
    }
    return true;
}

/**
 * @brief Connect to the monitor via backend and initialize context
 * 
 * @param addr Pointer to backend address structure
 * @param ring_address Address of the dmlog ring buffer in target memory
 * @param snapshot_mode Whether to use snapshot mode to reduce target reads
 * @return monitor_ctx_t* Pointer to initialized monitor context, or NULL on failure
 */
monitor_ctx_t *monitor_connect(backend_addr_t *addr, uint32_t ring_address, bool snapshot_mode)
{
    monitor_ctx_t *ctx = malloc(sizeof(monitor_ctx_t));
    if(ctx == NULL)
    {
        TRACE_ERROR("Failed to allocate memory for monitor context\n");
        return NULL;
    }
    memset(ctx, 0, sizeof(monitor_ctx_t));

    ctx->backend_type = addr->type;
    ctx->socket = backend_connect(ctx->backend_type, addr);
    if(ctx->socket < 0)
    {
        free(ctx);
        return NULL;
    }

    ctx->ring_address  = ring_address;
    ctx->snapshot_mode = snapshot_mode;

    if(!monitor_update_ring(ctx))
    {
        monitor_disconnect(ctx);
        return NULL;
    }
    if(snapshot_mode)
    {
        ctx->snapshot_size = dmlog_get_required_size(ctx->ring.buffer_size);
        ctx->dmlog_ctx = malloc(ctx->snapshot_size);
        if(ctx->dmlog_ctx == NULL)
        {
            TRACE_ERROR("Failed to allocate memory for local snapshot\n");
            monitor_disconnect(ctx);
            return NULL;
        }
    }
    else 
    {
        ctx->dmlog_ctx = NULL;
        ctx->snapshot_size = 0;
    }

    ctx->tail_offset = ctx->ring.tail_offset;
    ctx->input_file = NULL;  // No input file by default
    ctx->init_script_mode = false;  // No init script mode by default

    TRACE_INFO("Connected to dmlog ring buffer at 0x%08X\n", ring_address);
    return ctx;
}

/**
 * @brief Disconnect from the monitor and free resources
 * 
 * @param ctx Pointer to the monitor context
 */
void monitor_disconnect(monitor_ctx_t *ctx)
{
    if(ctx)
    {
        if(ctx->input_file)
        {
            fclose(ctx->input_file);
        }
        backend_disconnect(ctx->backend_type, ctx->socket);
        free(ctx);
        TRACE_INFO("Disconnected from monitor\n");
    }
}

/**
 * @brief Update the dmlog ring buffer metadata from the target
 * 
 * @param ctx Pointer to the monitor context
 * @return true on success, false on failure
 */
bool monitor_update_ring(monitor_ctx_t *ctx)
{
    dmlog_index_t previous_head = ctx->ring.head_offset;
    if(backend_read_memory(ctx->backend_type, ctx->socket, ctx->ring_address, &ctx->ring, sizeof(dmlog_ring_t)) < 0)
    {
        TRACE_ERROR("Failed to read dmlog ring buffer from target\n");
        return false;
    }
    if(ctx->ring.magic != DMLOG_MAGIC_NUMBER)
    {
        TRACE_ERROR("Invalid dmlog ring buffer magic number: 0x%08X != 0x%08X\n", ctx->ring.magic, DMLOG_MAGIC_NUMBER);
        return false;
    }
    dmlog_index_t number_of_new_bytes = ctx->ring.head_offset >= previous_head ?
    ctx->ring.head_offset - previous_head :
    ctx->ring.buffer_size - (previous_head - ctx->ring.head_offset);
    time_t current_time = time(NULL);
    double update_interval = difftime(current_time, ctx->last_update_time);
    ctx->last_update_time = current_time;
    double data_rate = update_interval > 0 ? (double)number_of_new_bytes / update_interval : 0.0;
    TRACE_VERBOSE("Dmlog Ring Buffer Updated: head_offset=%u, tail_offset=%u, new_bytes=%u, data_rate=%.2f bytes/sec\n",
        ctx->ring.head_offset,
        ctx->ring.tail_offset,
        number_of_new_bytes,
        data_rate);

    return true;
}

/**
 * @brief Wait until the dmlog ring buffer is no longer busy
 * 
 * This function continuously reads the flags of the dmlog ring buffer
 * from the target device until the BUSY flag is cleared after being set.
 * 
 * @param ctx Pointer to the monitor context
 * @return true if the buffer became not busy, false on error
 */
bool monitor_wait_until_not_busy(monitor_ctx_t *ctx)
{
    bool success = ctx->owns_busy_flag;

    while(!ctx->owns_busy_flag)
    {
        if(!(ctx->ring.flags & DMLOG_FLAG_BUSY))
        {
            success = true;
            break;
        }

        if(!monitor_update_ring(ctx))
        {
            success = false;
            break;
        }
        usleep(10000);
    }
    return success;
}

/**
 * @brief Wait for new data to be available in the dmlog ring buffer
 * 
 * Also returns early if firmware requests input, to avoid deadlock.
 * 
 * @param ctx Pointer to the monitor context
 * @return true on success, false on failure
 */
bool monitor_wait_for_new_data(monitor_ctx_t *ctx)
{
    monitor_wait_until_not_busy(ctx);
    bool empty = is_buffer_empty(ctx);
    while(empty)
    {
        usleep(10000);
        if(!monitor_update_ring(ctx))
        {
            TRACE_ERROR("monitor_update_ring failed in wait_for_new_data\n");
            return false;
        }
        // Check if firmware requested input - return early to handle it
        // This prevents deadlock when firmware requests input without producing output
        if(ctx->ring.flags & DMLOG_FLAG_INPUT_REQUESTED)
        {
            TRACE_VERBOSE("Input requested (flags=0x%08X), returning from wait\n", ctx->ring.flags);
            return true;
        }
        if(ctx->ring.flags & (DMLOG_FLAG_FILE_SEND_REQ | DMLOG_FLAG_FILE_SEND_REQ) )
        {
            TRACE_VERBOSE("File transfer requested (flags=0x%08X), returning from wait\n", ctx->ring.flags);
            return true;
        }
        empty = is_buffer_empty(ctx);

        // For GDB backend, briefly resume target so firmware can process the input
        if(ctx->backend_type == BACKEND_TYPE_GDB)
        {
            if(gdb_resume_briefly(ctx->socket) < 0)
            {
                TRACE_WARN("Failed to resume target briefly, input may not be processed\n");
                // Don't fail - the input is written, just might not be processed immediately
            }
        }
        
    }
    TRACE_VERBOSE("New data available, returning from wait\n");
    return true;
}

/**
 * @brief Update the current dmlog entry from the target
 * 
 * @param ctx Pointer to the monitor context
 * @param blocking_mode Whether to use blocking mode for reading log entries
 * @return true on success, false on failure
 */
bool monitor_update_entry(monitor_ctx_t *ctx, bool blocking_mode)
{
    if(blocking_mode && !monitor_send_busy_command(ctx))
    {
        TRACE_ERROR("Failed to send busy command to dmlog ring buffer\n");
        return false;
    }
    else if(!blocking_mode && !monitor_wait_until_not_busy(ctx))
    {
        TRACE_ERROR("Failed to wait until dmlog ring buffer is not busy\n");
        return false;
    }

    memset(ctx->entry_buffer, 0, sizeof(ctx->entry_buffer));
    uint32_t entry_address = (uint32_t)((uintptr_t)ctx->ring.buffer) + ctx->tail_offset;
    if(!read_from_buffer(ctx, ctx->entry_buffer, DMOD_LOG_MAX_ENTRY_SIZE) )
    {
        TRACE_ERROR("Failed to read dmlog entry data from target\n");
        return false;
    }

    if(blocking_mode && !monitor_send_not_busy_command(ctx))
    {
        TRACE_ERROR("Failed to send not busy command to dmlog ring buffer\n");
        return false;
    }

    return true;
}

/**
 * @brief Get a pointer to the current dmlog entry buffer
 * 
 * @param ctx Pointer to the monitor context
 * @return const char* Pointer to the current entry buffer
 */
const char *monitor_get_entry_buffer(monitor_ctx_t *ctx)
{
    return ctx->entry_buffer;
}

/**
 * @brief Load a snapshot of the dmlog ring buffer from the target
 * 
 * @param ctx Pointer to the monitor context
 * @param blocking_mode Whether to use blocking mode for reading
 * @return true on success, false on failure
 */
bool monitor_load_snapshot(monitor_ctx_t *ctx, bool blocking_mode)
{
    if(ctx->dmlog_ctx == NULL)
    {
        TRACE_ERROR("Snapshot mode not enabled, cannot load snapshot\n");
        return false;
    }

    if(blocking_mode && !monitor_send_busy_command(ctx))
    {
        TRACE_ERROR("Failed to send busy command to dmlog ring buffer\n");
        return false;
    }
    else if(!blocking_mode && !monitor_wait_until_not_busy(ctx))
    {
        TRACE_ERROR("Failed to wait until dmlog ring buffer is not busy\n");
        return false;
    }

    if(backend_read_memory(ctx->backend_type, ctx->socket, ctx->ring_address, ctx->dmlog_ctx, ctx->snapshot_size) < 0)
    {
        TRACE_ERROR("Failed to read dmlog snapshot from target\n");
        return false;
    }

    if(blocking_mode && !monitor_send_not_busy_command(ctx))
    {
        TRACE_ERROR("Failed to send not busy command to dmlog ring buffer\n");
        return false;
    }

    if(!dmlog_is_valid(ctx->dmlog_ctx))
    {
        TRACE_ERROR("Invalid dmlog snapshot received\n");
        return false;
    }

    dmlog_ring_t* ring = (void*)ctx->dmlog_ctx;
    ring->flags = 0;
    TRACE_VERBOSE("Dmlog Snapshot: head_offset=%u, tail_offset=%u, buffer_size=%x\n",
        ring->head_offset,
        ring->tail_offset,
        ring->buffer_size);

    TRACE_VERBOSE("Dmlog snapshot loaded successfully\n");
    return true;
}

/**
 * @brief Run the monitor loop (not implemented)
 * 
 * @param ctx Pointer to the monitor context
 * @param show_timestamps Whether to show timestamps with log entries
 * @param blocking_mode Whether to use blocking mode for reading log entries
 */
void monitor_run(monitor_ctx_t *ctx, bool show_timestamps, bool blocking_mode)
{
    if(ctx->snapshot_mode)
    {
        TRACE_INFO("Monitoring in snapshot mode\n");
        while(monitor_load_snapshot(ctx, blocking_mode))
        {
            while(dmlog_read_next(ctx->dmlog_ctx))
            {
                const char* entry_data = dmlog_get_ref_buffer(ctx->dmlog_ctx);
                if(show_timestamps)
                {
                    time_t now = time(NULL);
                    struct tm *local_time = localtime(&now);
                    printf("[%02d:%02d:%02d] %s", 
                           local_time->tm_hour, 
                           local_time->tm_min, 
                           local_time->tm_sec, 
                           entry_data);
                }
                else
                {
                    printf("%s", entry_data);
                }
                fflush(stdout);  // Ensure output is written immediately
            }
            
            // Check for input request from firmware (after printing all output)
            monitor_handle_input_request(ctx);
            
            usleep(300000); 
        }
        TRACE_INFO("Exiting snapshot monitoring loop\n");
    }
    else 
    {
        TRACE_INFO("Monitoring in live mode\n");
        const char* entry_data = monitor_get_entry_buffer(ctx);
        while(monitor_wait_for_new_data(ctx) )
        {
            while(!is_buffer_empty(ctx))
            {
                if(!monitor_update_entry(ctx, blocking_mode))
                {
                    if(!monitor_synchronize(ctx))
                    {
                        TRACE_ERROR("Failed to synchronize monitor context with target dmlog ring buffer\n");
                        if(!monitor_send_clear_command(ctx))
                        {
                            TRACE_ERROR("Failed to send clear command to dmlog ring buffer\n");
                            return;
                        }
                    }
                }
                if(strlen(entry_data) == 0)
                {
                    continue;
                }
                if(show_timestamps)
                {
                    time_t now = time(NULL);
                    struct tm *local_time = localtime(&now);
                    printf("[%02d:%02d:%02d] %s", 
                           local_time->tm_hour, 
                           local_time->tm_min, 
                           local_time->tm_sec, 
                           entry_data);
                }
                else
                {
                    printf("%s", entry_data);
                }
                fflush(stdout);  // Ensure output is written immediately
            }
            
            // Check for input request from firmware (after printing all output)
            bool input_requested = (ctx->ring.flags & DMLOG_FLAG_INPUT_REQUESTED) != 0;
            if(input_requested && !monitor_handle_input_request(ctx))
            {
                TRACE_ERROR("Failed to handle input request\n");
                return; // exit on EOF
            }

            bool send_file_requested = (ctx->ring.flags & DMLOG_FLAG_FILE_SEND_REQ) != 0;
            if(send_file_requested && !monitor_handle_send_file_request(ctx))
            {
                TRACE_ERROR("Failed to handle file send request\n");
                return; // exit on failure
            }
            bool receive_file_requested = (ctx->ring.flags & DMLOG_FLAG_FILE_RECV_REQ) != 0;
            if(receive_file_requested && !monitor_handle_receive_file_request(ctx))
            {
                TRACE_ERROR("Failed to handle file receive request\n");
                return; // exit on failure
            }

            if(ctx->ring.flags & DMLOG_FLAG_EXIT_REQUESTED)
            {
                TRACE_VERBOSE("Exit requested (flags=0x%08X), returning from wait\n", ctx->ring.flags);
                return;
            }

            usleep(100000); // Sleep briefly to allow data to accumulate
        }
    }
}

/**
 * @brief Write flags to the dmlog ring buffer on the target
 * 
 * @param ctx Pointer to the monitor context
 * @param flags Flags to write
 * @return true on success, false on failure
 */
bool monitor_write_flags(monitor_ctx_t *ctx, uint32_t flags)
{
    if(!monitor_wait_until_not_busy(ctx))
    {
        TRACE_ERROR("Dmlog ring buffer is busy, cannot write flags\n");
        return false;
    }

    if(backend_write_memory(ctx->backend_type, ctx->socket, ctx->ring_address + offsetof(dmlog_ring_t, flags), &flags, sizeof(uint32_t)) < 0)
    {
        TRACE_ERROR("Failed to write dmlog ring buffer flags to target\n");
        return false;
    }

    if(!monitor_update_ring(ctx))
    {
        TRACE_ERROR("Failed to update dmlog ring buffer after writing flags\n");
        return false;
    }

    return ctx->ring.flags == flags;
}

/**
 * @brief Send a clear command to the dmlog ring buffer on the target
 * 
 * @param ctx Pointer to the monitor context
 * @return true on success, false on failure
 */
bool monitor_send_clear_command(monitor_ctx_t *ctx)
{
    TRACE_INFO("Sending clear command to dmlog ring buffer\n");
    if(!monitor_write_flags(ctx, ctx->ring.flags | DMLOG_FLAG_CLEAR_BUFFER))
    {
        TRACE_ERROR("Failed to send clear command to dmlog ring buffer\n");
        return false;
    }

    TRACE_INFO("Waiting for clear command to be processed\n");

    while(ctx->ring.flags & DMLOG_FLAG_CLEAR_BUFFER || (ctx->ring.tail_offset != 0))
    {
        if(!monitor_update_ring(ctx))
        {
            TRACE_ERROR("Failed to update dmlog ring buffer after sending clear command\n");
            return false;
        }
        sleep(1);
    }

    TRACE_INFO("Clear command processed successfully\n");

    return true;
}

/**
 * @brief Send a busy command to the dmlog ring buffer on the target
 * 
 * @param ctx Pointer to the monitor context
 * @return true on success, false on failure
 */
bool monitor_send_busy_command(monitor_ctx_t *ctx)
{
    TRACE_INFO("Sending busy command to dmlog ring buffer\n");
    if( !monitor_write_flags(ctx, ctx->ring.flags | DMLOG_FLAG_BUSY) )
    {
        TRACE_ERROR("Failed to send busy command to dmlog ring buffer\n");
        return false;
    }
    ctx->owns_busy_flag = true;
    return true;
}

/**
 * @brief Synchronize the monitor context with the target dmlog ring buffer
 * 
 * This function sends a clear command to the target dmlog ring buffer
 * and updates the local tail offset to match the target's tail offset.
 * 
 * @param ctx Pointer to the monitor context
 * @return true on success, false on failure
 */
bool monitor_send_not_busy_command(monitor_ctx_t *ctx)
{
    TRACE_INFO("Sending not busy command to dmlog ring buffer\n");
    if(!monitor_write_flags(ctx, ctx->ring.flags & ~DMLOG_FLAG_BUSY) )
    {
        TRACE_ERROR("Failed to send not busy command to dmlog ring buffer\n");
        return false;
    }

    ctx->owns_busy_flag = false;
    return true;
}

/**
 * @brief Synchronize the monitor context with the target dmlog ring buffer
 * 
 * This function sends a clear command to the target dmlog ring buffer
 * and updates the local tail offset to match the target's tail offset.
 * 
 * @param ctx Pointer to the monitor context
 * @return true on success, false on failure
 */
bool monitor_synchronize(monitor_ctx_t *ctx)
{
    TRACE_INFO("Synchronizing monitor context with target dmlog ring buffer\n");
    if(!monitor_update_ring(ctx))
    {
        TRACE_ERROR("Failed to update dmlog ring buffer\n");
        return false;
    }

    TRACE_INFO("Searching for valid dmlog entry to synchronize tail offset\n");
    
    ctx->tail_offset    = ctx->ring.tail_offset;
    return true;
}

/**
 * @brief Send input data from PC to the firmware's input buffer
 * 
 * @param ctx Pointer to the monitor context
 * @param input Input string to send
 * @param length Length of the input string
 * @return true on success, false on failure
 */
bool monitor_send_input(monitor_ctx_t *ctx, const char* input, size_t length)
{
    if(input == NULL || length == 0)
    {
        TRACE_ERROR("Invalid input data\n");
        return false;
    }

    // Note: Do NOT wait for not-busy here! The firmware is intentionally
    // holding the BUSY flag while waiting for input. We need to write
    // directly to memory even while the firmware has the lock.

    // Update ring to get current state
    if(!monitor_update_ring(ctx))
    {
        TRACE_ERROR("Failed to update dmlog ring buffer before sending input\n");
        return false;
    }

    // Check available space in input buffer
    dmlog_index_t input_head = ctx->ring.input_head_offset;
    dmlog_index_t input_tail = ctx->ring.input_tail_offset;
    dmlog_index_t input_size = ctx->ring.input_buffer_size;
    
    dmlog_index_t free_space;
    if(input_head >= input_tail)
    {
        free_space = input_size - (input_head - input_tail);
    }
    else
    {
        free_space = input_tail - input_head;
    }
    free_space = free_space > 0 ? free_space - 1 : 0; // Leave one byte empty
    
    if(length > free_space)
    {
        TRACE_ERROR("Not enough space in input buffer: need %zu bytes, have %u bytes\n", length, free_space);
        return false;
    }

    // Write data to input buffer, handling wrap-around
    uint32_t input_buffer_addr = (uint32_t)((uintptr_t)ctx->ring.input_buffer);
    
    // Calculate how many bytes we can write before wrapping
    size_t bytes_before_wrap = input_size - input_head;
    size_t bytes_to_write_first = (length < bytes_before_wrap) ? length : bytes_before_wrap;
    
    // Write first chunk (up to buffer end or all bytes if no wrap)
    uint32_t write_addr = input_buffer_addr + input_head;
    if(backend_write_memory(ctx->backend_type, ctx->socket, write_addr, input, bytes_to_write_first) < 0)
    {
        TRACE_ERROR("Failed to write %zu bytes to input buffer at offset %u\n", bytes_to_write_first, input_head);
        return false;
    }
    
    input_head = (input_head + bytes_to_write_first) % input_size;
    
    // If there are remaining bytes after wrap, write them from the beginning
    if(bytes_to_write_first < length)
    {
        size_t remaining_bytes = length - bytes_to_write_first;
        write_addr = input_buffer_addr;  // Start from beginning of buffer
        if(backend_write_memory(ctx->backend_type, ctx->socket, write_addr, input + bytes_to_write_first, remaining_bytes) < 0)
        {
            TRACE_ERROR("Failed to write %zu remaining bytes to input buffer at offset 0\n", remaining_bytes);
            return false;
        }
        input_head = remaining_bytes;  // New head position after wrap
    }

    // Update input_head_offset in the ring structure
    uint32_t head_offset_addr = ctx->ring_address + offsetof(dmlog_ring_t, input_head_offset);
    if(backend_write_memory(ctx->backend_type, ctx->socket, head_offset_addr, &input_head, sizeof(dmlog_index_t)) < 0)
    {
        TRACE_ERROR("Failed to update input_head_offset\n");
        return false;
    }

    // Set INPUT_AVAILABLE flag (write directly without waiting for not-busy)
    uint32_t new_flags = ctx->ring.flags | DMLOG_FLAG_INPUT_AVAILABLE;
    if(backend_write_memory(ctx->backend_type, ctx->socket, ctx->ring_address + offsetof(dmlog_ring_t, flags), &new_flags, sizeof(uint32_t)) < 0)
    {
        TRACE_ERROR("Failed to set INPUT_AVAILABLE flag\n");
        return false;
    }

    // Update local cache
    ctx->ring.flags = new_flags;

    // For GDB backend, briefly resume target so firmware can process the input
    if(ctx->backend_type == BACKEND_TYPE_GDB)
    {
        if(gdb_resume_briefly(ctx->socket) < 0)
        {
            TRACE_WARN("Failed to resume target briefly, input may not be processed\n");
            // Don't fail - the input is written, just might not be processed immediately
        }
    }

    TRACE_VERBOSE("Sent %zu bytes to input buffer\n", length);
    return true;
}

/**
 * @brief Check for input request from firmware and prompt user
 * 
 * @param ctx Pointer to the monitor context
 * @return true if input was handled, false otherwise
 */
bool monitor_handle_input_request(monitor_ctx_t *ctx)
{
    // Check if firmware requested input
    if(!(ctx->ring.flags & DMLOG_FLAG_INPUT_REQUESTED))
    {
        return false;
    }

    // Read input from file or stdin (no prompt, firmware should print its own prompt)
    char input_buffer[512];
    // Always disable terminal echo - the firmware application is responsible for all echoing
    // via the dmlog output buffer. The DMLOG_FLAG_INPUT_ECHO_OFF flag tells the firmware
    // whether to echo characters back, not the monitor terminal.
    // If we enable terminal echo here, characters will be duplicated (terminal echo + app echo)
    bool echo_on = false;
    bool line_mode = (ctx->ring.flags & DMLOG_FLAG_INPUT_LINE_MODE) != 0;
    
    configure_input_mode(echo_on, line_mode);
    
    // Read input, potentially switching from init script to stdin
    while(true)
    {
        FILE* input_source = ctx->input_file ? ctx->input_file : stdin;
        if(line_mode || ctx->input_file)
        {
            if(fgets(input_buffer, sizeof(input_buffer), input_source) != NULL)
            {
                // Successfully read input
                break;
            }
        }
        else 
        {
            while(fgets(input_buffer, sizeof(input_buffer), input_source) == NULL);
            break;
        }
        
        // Failed to read - check why
        if(ctx->input_file)
        {
            // Reading from input file failed - check if EOF or error
            if(feof(ctx->input_file))
            {
                // Reached EOF on input file
                if(ctx->init_script_mode)
                {
                    // Init script completed - switch to stdin
                    TRACE_INFO("Init script completed, switching to stdin\n");
                    if(fclose(ctx->input_file) != 0)
                    {
                        TRACE_WARN("Failed to close init script file\n");
                    }
                    ctx->input_file = NULL;
                    // Loop will retry with stdin
                    continue;
                }
                else
                {
                    // Normal input file mode - exit on EOF
                    TRACE_ERROR("Input file ended\n");
                    if(fclose(ctx->input_file) != 0)
                    {
                        TRACE_WARN("Failed to close input file\n");
                    }
                    ctx->input_file = NULL;
                    configure_input_mode(true, true); // Restore terminal settings
                    return false;
                }
            }
            else
            {
                // I/O error on input file
                TRACE_ERROR("Failed to read from input file (I/O error)\n");
                if(fclose(ctx->input_file) != 0)
                {
                    TRACE_WARN("Failed to close input file\n");
                }
                ctx->input_file = NULL;
                configure_input_mode(true, true); // Restore terminal settings
                return false;
            }
        }
        // Reading from stdin failed - check if error first, then EOF
        else if(ferror(stdin))
        {
            // stdin I/O error - provide more detailed diagnostics
            TRACE_ERROR("stdin I/O error detected\n");
            TRACE_ERROR("  - Check if running in proper terminal environment\n");
            TRACE_ERROR("  - Ensure stdin is not redirected or closed\n");
            TRACE_ERROR("  - Try running interactively (not in background)\n");
            if(isatty(STDIN_FILENO))
            {
                TRACE_ERROR("  - stdin is connected to a terminal\n");
            }
            else
            {
                TRACE_ERROR("  - stdin is NOT connected to a terminal (pipe/redirect?)\n");
            }
            perror("  - System error: ");
            configure_input_mode(true, true); // Restore terminal settings
            return false;
        }
        else if(feof(stdin))
        {
            // stdin reached EOF
            TRACE_INFO("stdin reached EOF (Ctrl+D or pipe closed)\n");
            configure_input_mode(true, true); // Restore terminal settings
            return false;
        }
        // Other error - this should not happen in normal blocking mode
        // Continue trying to read
    }
    configure_input_mode(true, true); // Restore terminal settings

    // Send input to firmware
    size_t input_len = strlen(input_buffer);
    if(!monitor_send_input(ctx, input_buffer, input_len))
    {
        TRACE_ERROR("Failed to send input to firmware\n");
        return false;
    }

    // Clear the INPUT_REQUESTED flag (write directly without waiting for not-busy)
    uint32_t new_flags = ctx->ring.flags & ~DMLOG_FLAG_INPUT_REQUESTED;
    if(backend_write_memory(ctx->backend_type, ctx->socket, ctx->ring_address + offsetof(dmlog_ring_t, flags), &new_flags, sizeof(uint32_t)) < 0)
    {
        TRACE_ERROR("Failed to clear INPUT_REQUESTED flag\n");
        return false;
    }

    // Update local cache
    ctx->ring.flags = new_flags;

    // Return true to continue monitoring, false to exit
    // Exit conditions:
    // 1. Using --input-file (not init-script) and file has ended
    // 2. stdin EOF was handled above (returns false immediately)
    // Continue monitoring in all other cases:
    // - Using --init-script (ctx->input_file may be NULL after switching to stdin)
    // - Reading from stdin directly (no input file specified)
    if(ctx->input_file && !ctx->init_script_mode)
    {
        // Using --input-file: exit if file has ended
        return !feof(ctx->input_file);
    }
    // Continue monitoring
    return true;
}

/**
 * @brief Handle file send request from firmware
 * 
 * @param ctx Pointer to the monitor context
 * @return true on success, false on failure
 */
bool monitor_handle_send_file_request(monitor_ctx_t *ctx)
{
    if(!(ctx->ring.flags &(DMLOG_FLAG_FILE_SEND_REQ)))
    {
        return true; // No file transfer requested
    }

    TRACE_INFO("Handling file send request from firmware\n");
    dmlog_file_transfer_t file_transfer;
    uint64_t file_transfer_addr = ctx->ring.file_transfer;
    if(backend_read_memory(ctx->backend_type, ctx->socket, file_transfer_addr, &file_transfer, sizeof(dmlog_file_transfer_t)) < 0)
    {
        TRACE_ERROR("Failed to read file transfer structure from target at address 0x%lX\n", file_transfer_addr);
        return false;
    }

    if(file_transfer.total_size == 0 || file_transfer.chunk_size == 0 || file_transfer.buffer_address == 0)
    {
        TRACE_ERROR("Invalid file transfer parameters: total_size=%llu, chunk_size=%u, buffer_address = %llu\n",
            (unsigned long long)file_transfer.total_size,
            file_transfer.chunk_size, 
            (unsigned long long)file_transfer.buffer_address);
        return false;
    }

    void* file_data = Dmod_Malloc(file_transfer.chunk_size);
    if(file_data == NULL)
    {
        TRACE_ERROR("Failed to allocate memory for file transfer chunk\n");
        return false;
    }

    if(backend_read_memory(ctx->backend_type, ctx->socket, (uint32_t)file_transfer.buffer_address, file_data, file_transfer.chunk_size) < 0)
    {
        TRACE_ERROR("Failed to read file data from target buffer\n");
        Dmod_Free(file_data);
        return false;
    }

    const char* mode = file_transfer.offset == 0 ? "wb" : "wb+";
    void* file = Dmod_FileOpen(file_transfer.host_file_name, mode);
    if(file == NULL)
    {
        TRACE_ERROR("Failed to open local file '%s' for writing\n", file_transfer.host_file_name);
        Dmod_Free(file_data);
        return false;
    }

    if(Dmod_FileSeek(file, file_transfer.offset, SEEK_SET) != 0)
    {
        TRACE_ERROR("Failed to seek to offset %llu in local file '%s'\n",
            (unsigned long long)file_transfer.offset,
            file_transfer.host_file_name);
        Dmod_FileClose(file);
        Dmod_Free(file_data);
        return false;
    }

    size_t written = Dmod_FileWrite(file_data, 1, file_transfer.chunk_size, file);
    if(written != file_transfer.chunk_size)
    {
        TRACE_ERROR("Failed to write %u bytes to local file '%s', only wrote %zu bytes\n",
            file_transfer.chunk_size,
            file_transfer.host_file_name,
            written);
        Dmod_FileClose(file);
        Dmod_Free(file_data);
        return false;
    }

    Dmod_FileClose(file);
    Dmod_Free(file_data);
    TRACE_INFO("Wrote %u bytes to local file '%s' at offset %llu\n",
        file_transfer.chunk_size,
        file_transfer.host_file_name,
        (unsigned long long)file_transfer.offset);
    // Clear the FILE_SEND_REQ flag
    uint32_t new_flags = ctx->ring.flags & ~DMLOG_FLAG_FILE_SEND_REQ;
    if(backend_write_memory(ctx->backend_type, ctx->socket, ctx->ring_address + offsetof(dmlog_ring_t, flags), &new_flags, sizeof(uint32_t)) < 0)
    {
        TRACE_ERROR("Failed to clear FILE_SEND_REQ flag\n");
        return false;
    }

    // Update local cache
    ctx->ring.flags = new_flags;

    // For GDB backend, briefly resume target so firmware can process the input
    if(ctx->backend_type == BACKEND_TYPE_GDB)
    {
        if(gdb_resume_briefly(ctx->socket) < 0)
        {
            TRACE_WARN("Failed to resume target briefly, input may not be processed\n");
            // Don't fail - the input is written, just might not be processed immediately
        }
    }

    return true;
}

/**
 * @brief Handle file receive request from firmware
 * 
 * @param ctx Pointer to the monitor context
 * @return true on success, false on failure
 */
bool monitor_handle_receive_file_request(monitor_ctx_t *ctx)
{
    if(!(ctx->ring.flags &(DMLOG_FLAG_FILE_RECV_REQ)))
    {
        return true; // No file transfer requested
    }

    TRACE_INFO("Handling file receive request from firmware\n");
    dmlog_file_transfer_t file_transfer;
    uint64_t file_transfer_addr = ctx->ring.file_transfer;
    if(backend_read_memory(ctx->backend_type, ctx->socket, file_transfer_addr, &file_transfer, sizeof(dmlog_file_transfer_t)) < 0)
    {
        TRACE_ERROR("Failed to read file transfer structure from target\n");
        return false;
    }
    TRACE_INFO("File Transfer Request: host_file_name='%s', offset=%llu, chunk_size=%u, buffer_address=0x%08X\n",
        file_transfer.host_file_name,
        (unsigned long long)file_transfer.offset,
        file_transfer.chunk_size,
        (uint32_t)file_transfer.buffer_address);

    if(file_transfer.chunk_size == 0)
    {
        TRACE_ERROR("Invalid file transfer parameters: chunk_size=%u\n",
            file_transfer.chunk_size);
        file_transfer.status = -EINVAL;
        return monitor_send_file_transfer(ctx, &file_transfer, DMLOG_FLAG_FILE_RECV_REQ);
    }

    if(!Dmod_FileAvailable(file_transfer.host_file_name))
    {
        TRACE_ERROR("Local file '%s' does not exist for file transfer\n", file_transfer.host_file_name);
        file_transfer.status = -ENOENT;
        return monitor_send_file_transfer(ctx, &file_transfer, DMLOG_FLAG_FILE_RECV_REQ);
    }

    TRACE_INFO("Local file '%s' size: %llu bytes\n",
        file_transfer.host_file_name,
        (unsigned long long)file_transfer.total_size);

    void* file_data = Dmod_Malloc(file_transfer.chunk_size);
    if(file_data == NULL)
    {
        TRACE_ERROR("Failed to allocate memory for file transfer chunk\n");
        return false;
    }

    void* file = Dmod_FileOpen(file_transfer.host_file_name, "rb");
    if(file == NULL)
    {
        TRACE_ERROR("Failed to open local file '%s' for reading\n", file_transfer.host_file_name);
        Dmod_Free(file_data);
        return false;
    }

    file_transfer.total_size = Dmod_FileSize(file);
    file_transfer.status = 0;

    if(Dmod_FileSeek(file, file_transfer.offset, SEEK_SET) != 0)
    {
        TRACE_ERROR("Failed to seek to offset %llu in local file '%s'\n",
            (unsigned long long)file_transfer.offset,
            file_transfer.host_file_name);
        Dmod_FileClose(file);
        Dmod_Free(file_data);
        return false;
    }

    size_t read_bytes = Dmod_FileRead(file_data, 1, file_transfer.chunk_size, file);
    file_transfer.chunk_size = (uint32_t)read_bytes;
    file_transfer.status = 0;
    Dmod_FileClose(file);

    if(read_bytes == 0)
    {
        TRACE_WARN("Empty file read from local file '%s' at offset %llu\n",
            file_transfer.host_file_name,
            (unsigned long long)file_transfer.offset);
    }
    else if(backend_write_memory(ctx->backend_type, ctx->socket, (uint32_t)file_transfer.buffer_address, file_data, file_transfer.chunk_size) <= 0)
    {
        file_transfer.status = -EIO;
        TRACE_ERROR("Failed to write file data to target buffer\n");
        Dmod_Free(file_data);
        return monitor_send_file_transfer(ctx, &file_transfer, DMLOG_FLAG_FILE_RECV_REQ);
    }

    Dmod_Free(file_data);
    TRACE_INFO("Read %u bytes from local file '%s' at offset %llu and sent to target\n",
        file_transfer.chunk_size,
        file_transfer.host_file_name,
        (unsigned long long)file_transfer.offset);
    if(!monitor_send_file_transfer(ctx, &file_transfer, DMLOG_FLAG_FILE_RECV_REQ))
    {
        TRACE_ERROR("Failed to send file transfer response to target\n");
        return false;
    }

    return true;
}

/**
 * @brief Send a file transfer request to the firmware
 * 
 * @param ctx Pointer to the monitor context
 * @param transfer Pointer to the file transfer structure
 * @param flags Flags to set for the file transfer request
 * @return true on success, false on failure
 */
bool monitor_send_file_transfer(monitor_ctx_t* ctx, const dmlog_file_transfer_t* transfer, uint32_t flags)
{
    if(transfer == NULL)
    {
        TRACE_ERROR("Invalid file transfer parameters\n");
        return false;
    }

    TRACE_INFO("Sending file transfer response to target: host_file_name='%s', offset=%llu, chunk_size=%u, status=%d\n",
        transfer->host_file_name,
        (unsigned long long)transfer->offset,
        transfer->chunk_size,
        transfer->status);

    if(backend_write_memory(ctx->backend_type, ctx->socket, ctx->ring.file_transfer, transfer, sizeof(dmlog_file_transfer_t)) < 0)
    {
        TRACE_ERROR("Failed to write file transfer structure to target\n");
        return false;
    }

    // Set the appropriate flag
    uint32_t new_flags = ctx->ring.flags & ~(flags);
    if(backend_write_memory(ctx->backend_type, ctx->socket, ctx->ring_address + offsetof(dmlog_ring_t, flags), &new_flags, sizeof(uint32_t)) < 0)
    {
        TRACE_ERROR("Failed to set file transfer request flag\n");
        return false;
    }

    // Update local cache
    ctx->ring.flags = new_flags;

    // For GDB backend, briefly resume target so firmware can process the input
    if(ctx->backend_type == BACKEND_TYPE_GDB)
    {
        if(gdb_resume_briefly(ctx->socket) < 0)
        {
            TRACE_WARN("Failed to resume target briefly, input may not be processed\n");
            // Don't fail - the input is written, just might not be processed immediately
        }
    }

    TRACE_INFO("File transfer request sent successfully\n");
    return true;
}