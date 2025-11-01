#include <stdlib.h>
#include <stdbool.h>
#include "monitor.h"
#include "openocd.h"
#include "trace.h"

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
bool monitor_wait_until_busy(monitor_ctx_t *ctx)
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
