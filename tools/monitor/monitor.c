#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include "monitor.h"
#include "openocd.h"
#include "trace.h"

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
        if(openocd_read_memory(ctx->socket, address, dst, length) < 0)
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
        if(openocd_read_memory(ctx->socket, address, dst, left_size) < 0)
        {
            TRACE_ERROR("Failed to read %u bytes from buffer at offset %u\n", left_size, ctx->tail_offset);
            return false;
        }
        size_t remaining = length - left_size;
        address = (uint32_t)((uintptr_t)ctx->ring.buffer);
        if(openocd_read_memory(ctx->socket, address, (uint8_t*)dst + left_size, remaining) < 0)
        {
            TRACE_ERROR("Failed to read %zu bytes from buffer at offset 0\n", remaining);
            return false;
        }
        ctx->tail_offset = remaining;
    }
    return true;
}

/**
 * @brief Connect to the monitor via OpenOCD and initialize context
 * 
 * @param addr Pointer to OpenOCD address structure
 * @param ring_address Address of the dmlog ring buffer in target memory
 * @param snapshot_mode Whether to use snapshot mode to reduce target reads
 * @return monitor_ctx_t* Pointer to initialized monitor context, or NULL on failure
 */
monitor_ctx_t *monitor_connect(opencd_addr_t *addr, uint32_t ring_address, bool snapshot_mode)
{
    monitor_ctx_t *ctx = malloc(sizeof(monitor_ctx_t));
    if(ctx == NULL)
    {
        TRACE_ERROR("Failed to allocate memory for monitor context\n");
        return NULL;
    }
    memset(ctx, 0, sizeof(monitor_ctx_t));

    ctx->socket = openocd_connect(addr);
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
        openocd_disconnect(ctx->socket);
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
    if(openocd_read_memory(ctx->socket, ctx->ring_address, &ctx->ring, sizeof(dmlog_ring_t)) < 0)
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
            return false;
        }
        empty = is_buffer_empty(ctx);
    }
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

    if(openocd_read_memory(ctx->socket, ctx->ring_address, ctx->dmlog_ctx, ctx->snapshot_size) < 0)
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
            usleep(100000); // Sleep briefly to allow data to accumulate
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
            }
            
            // Check for input request from firmware (after printing all output)
            monitor_handle_input_request(ctx);
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

    if(openocd_write_memory(ctx->socket, ctx->ring_address + offsetof(dmlog_ring_t, flags), &flags, sizeof(uint32_t)) < 0)
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

    if(!monitor_wait_until_not_busy(ctx))
    {
        TRACE_ERROR("Dmlog ring buffer is busy, cannot send input\n");
        return false;
    }

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
    for(size_t i = 0; i < length; i++)
    {
        uint32_t write_addr = input_buffer_addr + input_head;
        if(openocd_write_memory(ctx->socket, write_addr, &input[i], 1) < 0)
        {
            TRACE_ERROR("Failed to write input byte at offset %u\n", input_head);
            return false;
        }
        input_head = (input_head + 1) % input_size;
    }

    // Update input_head_offset in the ring structure
    uint32_t head_offset_addr = ctx->ring_address + offsetof(dmlog_ring_t, input_head_offset);
    if(openocd_write_memory(ctx->socket, head_offset_addr, &input_head, sizeof(dmlog_index_t)) < 0)
    {
        TRACE_ERROR("Failed to update input_head_offset\n");
        return false;
    }

    // Set INPUT_AVAILABLE flag
    uint32_t new_flags = ctx->ring.flags | DMLOG_FLAG_INPUT_AVAILABLE;
    if(!monitor_write_flags(ctx, new_flags))
    {
        TRACE_ERROR("Failed to set INPUT_AVAILABLE flag\n");
        return false;
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

    // Read input from user (no prompt, firmware should print its own prompt)
    char input_buffer[512];
    if(fgets(input_buffer, sizeof(input_buffer), stdin) == NULL)
    {
        TRACE_ERROR("Failed to read input from user\n");
        return false;
    }

    // Send input to firmware
    size_t input_len = strlen(input_buffer);
    if(!monitor_send_input(ctx, input_buffer, input_len))
    {
        TRACE_ERROR("Failed to send input to firmware\n");
        return false;
    }

    // Clear the INPUT_REQUESTED flag
    uint32_t new_flags = ctx->ring.flags & ~DMLOG_FLAG_INPUT_REQUESTED;
    if(!monitor_write_flags(ctx, new_flags))
    {
        TRACE_ERROR("Failed to clear INPUT_REQUESTED flag\n");
        return false;
    }

    return true;
}
