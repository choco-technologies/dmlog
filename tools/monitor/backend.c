#include "backend.h"
#include "openocd.h"
#include "gdbserver.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/**
 * @brief OpenOCD backend wrapper functions
 */
static backend_ctx_t openocd_backend_connect(backend_addr_t *addr)
{
    opencd_addr_t openocd_addr;
    strncpy(openocd_addr.host, addr->host, sizeof(openocd_addr.host));
    openocd_addr.port = addr->port;
    
    int socket = openocd_connect(&openocd_addr);
    if (socket < 0) {
        return NULL;
    }
    
    // Return socket as void* (backend_ctx_t)
    return (backend_ctx_t)(intptr_t)socket;
}

static int openocd_backend_disconnect(backend_ctx_t ctx)
{
    int socket = (int)(intptr_t)ctx;
    return openocd_disconnect(socket);
}

static int openocd_backend_read_memory(backend_ctx_t ctx, uint64_t address, void *buffer, size_t length)
{
    int socket = (int)(intptr_t)ctx;
    // OpenOCD uses 32-bit addresses, so truncate for now
    // This is acceptable as OpenOCD is typically used with 32-bit microcontrollers
    return openocd_read_memory(socket, (uint32_t)address, buffer, length);
}

static int openocd_backend_write_memory(backend_ctx_t ctx, uint64_t address, const void *buffer, size_t length)
{
    int socket = (int)(intptr_t)ctx;
    // OpenOCD uses 32-bit addresses, so truncate for now
    return openocd_write_memory(socket, (uint32_t)address, buffer, length);
}

/**
 * @brief GDB backend wrapper functions
 */
static backend_ctx_t gdb_backend_connect(backend_addr_t *addr)
{
    gdb_addr_t gdb_addr;
    strncpy(gdb_addr.host, addr->host, sizeof(gdb_addr.host));
    gdb_addr.port = addr->port;
    
    int socket = gdb_connect(&gdb_addr);
    if (socket < 0) {
        return NULL;
    }
    
    // Send continue command to start/resume the target
    // When gdbserver starts a process, it stops at entry point
    // We need to continue it so the program can run and initialize
    // gdb_continue() will run the target then interrupt it after a delay
    if (gdb_continue(socket) < 0) {
        gdb_disconnect(socket);
        return NULL;
    }
    
    // Return socket as void* (backend_ctx_t)
    return (backend_ctx_t)(intptr_t)socket;
}

static int gdb_backend_disconnect(backend_ctx_t ctx)
{
    int socket = (int)(intptr_t)ctx;
    return gdb_disconnect(socket);
}

static int gdb_backend_read_memory(backend_ctx_t ctx, uint64_t address, void *buffer, size_t length)
{
    int socket = (int)(intptr_t)ctx;
    return gdb_read_memory(socket, address, buffer, length);
}

static int gdb_backend_write_memory(backend_ctx_t ctx, uint64_t address, const void *buffer, size_t length)
{
    int socket = (int)(intptr_t)ctx;
    return gdb_write_memory(socket, address, buffer, length);
}

/**
 * @brief Backend operations tables
 */
static const backend_ops_t openocd_ops = {
    .connect = openocd_backend_connect,
    .disconnect = openocd_backend_disconnect,
    .read_memory = openocd_backend_read_memory,
    .write_memory = openocd_backend_write_memory
};

static const backend_ops_t gdb_ops = {
    .connect = gdb_backend_connect,
    .disconnect = gdb_backend_disconnect,
    .read_memory = gdb_backend_read_memory,
    .write_memory = gdb_backend_write_memory
};

/**
 * @brief Get backend operations for specified backend type
 */
const backend_ops_t* backend_get_ops(backend_type_t type)
{
    switch (type) {
        case BACKEND_OPENOCD:
            return &openocd_ops;
        case BACKEND_GDB:
            return &gdb_ops;
        default:
            return NULL;
    }
}
