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
        empty = is_buffer_empty(ctx);
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
 * @brief Handle file send operation (firmware to PC)
 * 
 * @param ctx Pointer to the monitor context
 * @return true on success, false on failure
 */
bool monitor_handle_file_send(monitor_ctx_t *ctx)
{
    if(!(ctx->ring.flags & DMLOG_FLAG_FILE_SEND))
    {
        return true; // Nothing to do
    }
    
    TRACE_INFO("File send operation detected\n");
    
    // Read the file transfer info structure from firmware memory
    dmlog_file_transfer_t transfer_info;
    if(backend_read_memory(ctx->backend_type, ctx->socket, ctx->ring.file_transfer_info, 
                          &transfer_info, sizeof(dmlog_file_transfer_t)) < 0)
    {
        TRACE_ERROR("Failed to read file transfer info structure\n");
        return false;
    }
    
    TRACE_INFO("Receiving file: %s -> %s (chunk %u, size %u, total %u)\n",
               transfer_info.file_path, transfer_info.file_path_pc,
               transfer_info.chunk_number, transfer_info.chunk_size, transfer_info.total_size);
    
    // Read chunk data from firmware memory
    void* chunk_buffer = malloc(transfer_info.chunk_size);
    if(!chunk_buffer)
    {
        TRACE_ERROR("Failed to allocate chunk buffer\n");
        return false;
    }
    
    if(backend_read_memory(ctx->backend_type, ctx->socket, transfer_info.chunk_buffer,
                          chunk_buffer, transfer_info.chunk_size) < 0)
    {
        TRACE_ERROR("Failed to read chunk data\n");
        free(chunk_buffer);
        return false;
    }
    
    // Open or create the file on PC side
    FILE* file = fopen((const char*)transfer_info.file_path_pc, 
                      transfer_info.chunk_number == 0 ? "wb" : "ab");
    if(!file)
    {
        TRACE_ERROR("Failed to open file: %s\n", transfer_info.file_path_pc);
        free(chunk_buffer);
        return false;
    }
    
    // Write chunk to file
    size_t written = fwrite(chunk_buffer, 1, transfer_info.chunk_size, file);
    fclose(file);
    free(chunk_buffer);
    
    if(written != transfer_info.chunk_size)
    {
        TRACE_ERROR("Failed to write chunk to file\n");
        return false;
    }
    
    TRACE_VERBOSE("Chunk %u written successfully (%u bytes)\n", 
                  transfer_info.chunk_number, transfer_info.chunk_size);
    
    // Clear the flag to signal firmware that chunk was received
    uint32_t flags = ctx->ring.flags & ~DMLOG_FLAG_FILE_SEND;
    if(!monitor_write_flags(ctx, flags))
    {
        TRACE_ERROR("Failed to clear file send flag\n");
        return false;
    }
    
    return true;
}

/**
 * @brief Handle file receive operation (PC to firmware)
 * 
 * @param ctx Pointer to the monitor context
 * @return true on success, false on failure
 */
bool monitor_handle_file_recv(monitor_ctx_t *ctx)
{
    if(!(ctx->ring.flags & DMLOG_FLAG_FILE_RECV))
    {
        return true; // Nothing to do
    }
    
    TRACE_INFO("File receive operation detected\n");
    
    // Read the file transfer info structure from firmware memory
    dmlog_file_transfer_t transfer_info;
    if(backend_read_memory(ctx->backend_type, ctx->socket, ctx->ring.file_transfer_info,
                          &transfer_info, sizeof(dmlog_file_transfer_t)) < 0)
    {
        TRACE_ERROR("Failed to read file transfer info structure\n");
        return false;
    }
    
    TRACE_INFO("Sending file: %s -> %s (chunk %u, buffer size %u)\n",
               transfer_info.file_path_pc, transfer_info.file_path,
               transfer_info.chunk_number, transfer_info.chunk_size);
    
    // Open the file on PC side
    static FILE* send_file = NULL;
    if(transfer_info.chunk_number == 0)
    {
        if(send_file)
        {
            fclose(send_file);
        }
        send_file = fopen((const char*)transfer_info.file_path_pc, "rb");
        if(!send_file)
        {
            TRACE_ERROR("Failed to open file: %s\n", transfer_info.file_path_pc);
            // Signal EOF by setting chunk_size to 0
            transfer_info.chunk_size = 0;
            if(backend_write_memory(ctx->backend_type, ctx->socket, ctx->ring.file_transfer_info,
                                   &transfer_info, sizeof(dmlog_file_transfer_t)) < 0)
            {
                TRACE_ERROR("Failed to write transfer info\n");
            }
            return false;
        }
    }
    
    if(!send_file)
    {
        TRACE_ERROR("File not open for sending\n");
        return false;
    }
    
    // Read chunk from file
    void* chunk_buffer = malloc(transfer_info.chunk_size);
    if(!chunk_buffer)
    {
        TRACE_ERROR("Failed to allocate chunk buffer\n");
        return false;
    }
    
    size_t bytes_read = fread(chunk_buffer, 1, transfer_info.chunk_size, send_file);
    
    if(bytes_read > 0)
    {
        // Write chunk to firmware memory
        if(backend_write_memory(ctx->backend_type, ctx->socket, transfer_info.chunk_buffer,
                               chunk_buffer, bytes_read) < 0)
        {
            TRACE_ERROR("Failed to write chunk to firmware\n");
            free(chunk_buffer);
            fclose(send_file);
            send_file = NULL;
            return false;
        }
        
        // Update chunk size and number in the transfer info
        transfer_info.chunk_size = (uint32_t)bytes_read;
        transfer_info.chunk_number++;
        
        TRACE_VERBOSE("Chunk %u sent successfully (%zu bytes)\n", 
                      transfer_info.chunk_number - 1, bytes_read);
    }
    
    // Check for EOF
    if(bytes_read < transfer_info.chunk_size || feof(send_file))
    {
        TRACE_INFO("File transfer complete\n");
        fclose(send_file);
        send_file = NULL;
        
        // Signal EOF by setting chunk_size to 0 after writing last chunk
        if(bytes_read > 0)
        {
            // First write the last chunk info
            if(backend_write_memory(ctx->backend_type, ctx->socket, ctx->ring.file_transfer_info,
                                   &transfer_info, sizeof(dmlog_file_transfer_t)) < 0)
            {
                TRACE_ERROR("Failed to write transfer info\n");
                free(chunk_buffer);
                return false;
            }
            
            // Clear flag so firmware processes this chunk
            uint32_t flags = ctx->ring.flags & ~DMLOG_FLAG_FILE_RECV;
            if(!monitor_write_flags(ctx, flags))
            {
                TRACE_ERROR("Failed to clear file recv flag\n");
                free(chunk_buffer);
                return false;
            }
            
            // Wait for firmware to process and set flag again
            int retries = 100;
            while(retries-- > 0)
            {
                usleep(10000);
                if(!monitor_update_ring(ctx))
                {
                    free(chunk_buffer);
                    return false;
                }
                if(ctx->ring.flags & DMLOG_FLAG_FILE_RECV)
                {
                    break;
                }
            }
            
            if(retries <= 0)
            {
                TRACE_ERROR("Timeout waiting for firmware to request next chunk\n");
                free(chunk_buffer);
                return false;
            }
            
            // Now read transfer info again and set EOF marker
            if(backend_read_memory(ctx->backend_type, ctx->socket, ctx->ring.file_transfer_info,
                                  &transfer_info, sizeof(dmlog_file_transfer_t)) < 0)
            {
                TRACE_ERROR("Failed to read transfer info\n");
                free(chunk_buffer);
                return false;
            }
        }
        
        transfer_info.chunk_size = 0; // EOF marker
    }
    
    // Write updated transfer info back to firmware
    if(backend_write_memory(ctx->backend_type, ctx->socket, ctx->ring.file_transfer_info,
                           &transfer_info, sizeof(dmlog_file_transfer_t)) < 0)
    {
        TRACE_ERROR("Failed to write transfer info\n");
        free(chunk_buffer);
        return false;
    }
    
    free(chunk_buffer);
    
    // Clear the flag to signal firmware that chunk is ready
    uint32_t flags = ctx->ring.flags & ~DMLOG_FLAG_FILE_RECV;
    if(!monitor_write_flags(ctx, flags))
    {
        TRACE_ERROR("Failed to clear file recv flag\n");
        return false;
    }
    
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
            
            // Check for file transfer requests
            monitor_handle_file_send(ctx);
            monitor_handle_file_recv(ctx);
            
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
            if(!monitor_handle_input_request(ctx))
            {
                return; // exit on EOF
            }
            
            // Check for file transfer requests
            monitor_handle_file_send(ctx);
            monitor_handle_file_recv(ctx);

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
    bool echo_on = (ctx->ring.flags & DMLOG_FLAG_INPUT_ECHO_OFF) == 0;
    bool line_mode = (ctx->ring.flags & DMLOG_FLAG_INPUT_LINE_MODE) != 0;
    
    configure_input_mode(echo_on, line_mode);
    
    // Read input, potentially switching from init script to stdin
    while(true)
    {
        FILE* input_source = ctx->input_file ? ctx->input_file : stdin;
        if(fgets(input_buffer, sizeof(input_buffer), input_source) != NULL)
        {
            // Successfully read input
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
            // stdin I/O error
            TRACE_ERROR("stdin I/O error, exiting\n");
            configure_input_mode(true, true); // Restore terminal settings
            return false;
        }
        else if(feof(stdin))
        {
            // stdin reached EOF
            TRACE_INFO("stdin reached EOF, exiting\n");
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
