#include <stdlib.h>
#include <stdbool.h>
#include "monitor.h"
#include "openocd.h"
#include "trace.h"

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
        TRACE_ERROR("Invalid dmlog ring buffer magic number: 0x%08X\n", ctx->ring.magic);
        return false;
    }
    TRACE_VERBOSE("Dmlog Ring Buffer: head_offset=%u, tail_offset=%u, buffer_size=%x, buffer_address=%lx\n",
        ctx->ring.head_offset,
        ctx->ring.tail_offset,
        ctx->ring.buffer_size,
        ctx->ring.buffer);
    
    ctx->tail_offset = ctx->ring.tail_offset;

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
    bool success = false;

    while(true)
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
 * @brief Update the current dmlog entry from the target
 * 
 * @param ctx Pointer to the monitor context
 * @return true on success, false on failure
 */
bool monitor_update_entry(monitor_ctx_t *ctx)
{
    monitor_wait_until_not_busy(ctx);

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
 */
void monitor_run(monitor_ctx_t *ctx)
{
    const char* entry_data = monitor_get_entry_buffer(ctx);
    while(monitor_update_entry(ctx))
    {
        printf("%s", entry_data);
    }
}
