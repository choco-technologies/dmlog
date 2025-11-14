#ifndef BACKEND_H
#define BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Backend connection context (opaque pointer)
 */
typedef void* backend_ctx_t;

/**
 * @brief Backend type enumeration
 */
typedef enum {
    BACKEND_OPENOCD,
    BACKEND_GDB
} backend_type_t;

/**
 * @brief Backend address configuration
 */
typedef struct {
    char host[256];
    int port;
} backend_addr_t;

/**
 * @brief Backend operations interface
 */
typedef struct {
    backend_ctx_t (*connect)(backend_addr_t *addr);
    int (*disconnect)(backend_ctx_t ctx);
    int (*read_memory)(backend_ctx_t ctx, uint32_t address, void *buffer, size_t length);
    int (*write_memory)(backend_ctx_t ctx, uint32_t address, const void *buffer, size_t length);
} backend_ops_t;

/**
 * @brief Get backend operations for specified backend type
 * 
 * @param type Backend type
 * @return const backend_ops_t* Pointer to backend operations, or NULL if type is invalid
 */
const backend_ops_t* backend_get_ops(backend_type_t type);

#endif // BACKEND_H
