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
    if(get_left_data_in_buffer(ctx) < length)
    {
        TRACE_ERROR("Not enough data in buffer to read %zu bytes\n", length);
        return false;
    }
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
 * @return monitor_ctx_t* Pointer to initialized monitor context, or NULL on failure
 */
monitor_ctx_t *monitor_connect(opencd_addr_t *addr, uint32_t ring_address)
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

    ctx->ring_address = ring_address;

    if(!monitor_update_ring(ctx))
    {
        monitor_disconnect(ctx);
        return NULL;
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
    TRACE_VERBOSE("Dmlog Ring Buffer: head_offset=%u, tail_offset=%u, buffer_size=%x, buffer_address=%lx\n",
        ctx->ring.head_offset,
        ctx->ring.tail_offset,
        ctx->ring.buffer_size,
        ctx->ring.buffer);

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
    while(is_buffer_empty(ctx))
    {
        if(!monitor_update_ring(ctx))
        {
            return false;
        }
        usleep(10000); // Sleep briefly to avoid busy-waiting
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

    uint32_t entry_address = (uint32_t)((uintptr_t)ctx->ring.buffer) + ctx->tail_offset;
    if(!read_from_buffer(ctx, &ctx->current_entry, sizeof(dmlog_entry_t)))
    {
        TRACE_ERROR("Failed to read dmlog entry from target\n");
        return false;
    }
    if(ctx->current_entry.magic != DMLOG_ENTRY_MAGIC_NUMBER)
    {
        TRACE_ERROR("Invalid dmlog entry magic number: 0x%08X\n", ctx->current_entry.magic);
        return false;
    }
    if(ctx->current_entry.length > DMOD_LOG_MAX_ENTRY_SIZE)
    {
        TRACE_ERROR("Dmlog entry length %u exceeds maximum %u\n", ctx->current_entry.length, DMOD_LOG_MAX_ENTRY_SIZE);
        return false;
    }
    if(!read_from_buffer(ctx, ctx->entry_buffer, ctx->current_entry.length) )
    {
        TRACE_ERROR("Failed to read dmlog entry data from target\n");
        return false;
    }
    ctx->last_entry_id = ctx->current_entry.id;
    ctx->entry_buffer[ctx->current_entry.length] = '\0'; // Null-terminate
    TRACE_VERBOSE("Dmlog Entry ID: %u, Length: %u\n", ctx->current_entry.id, ctx->current_entry.length);

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
 * @brief Run the monitor loop (not implemented)
 * 
 * @param ctx Pointer to the monitor context
 * @param show_timestamps Whether to show timestamps with log entries
 * @param blocking_mode Whether to use blocking mode for reading log entries
 */
void monitor_run(monitor_ctx_t *ctx, bool show_timestamps, bool blocking_mode)
{
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
                    return;
                }
            }
            if(ctx->current_entry.id < ctx->last_entry_id)
            {
                continue; // No new entry
            }
            if(strlen(entry_data) == 0)
            {
                continue;
            }
            if(show_timestamps)
            {
                time_t now = time(NULL);
                struct tm *local_time = localtime(&now);
                printf("[%02d:%02d:%02d] <%u> %s", 
                       local_time->tm_hour, 
                       local_time->tm_min, 
                       local_time->tm_sec, 
                       ctx->current_entry.id,
                       entry_data);
            }
            else
            {
                printf("<%u> %s", ctx->current_entry.id, entry_data);
            }
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
        usleep(10000); // Sleep briefly to avoid busy-waiting
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
    if(!monitor_send_clear_command(ctx))
    {
        TRACE_ERROR("Failed to send clear command to dmlog ring buffer\n");
        return false;
    }
    ctx->tail_offset = ctx->ring.tail_offset;
    ctx->last_entry_id = ctx->ring.latest_id;

    return true;
}
