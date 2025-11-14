#ifndef BACKEND_H
#define BACKEND_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Enumeration of supported backend types for dmlog monitor.
 */
typedef enum 
{
    BACKEND_TYPE_OPENOCD,   //!< OpenOCD backend
    BACKEND_TYPE_GDB,       //!< GDB backend (not implemented)

    BACKEND_TYPE__COUNT     //!< Number of backend types
} backend_type_t;

/**
 * @file backend.h
 * @brief Backend interface definitions for dmlog monitor.
 */
typedef struct 
{
    char host[256];         //!< Backend host address
    int port;               //!< Backend port number
    backend_type_t type;    //!< Backend type
} backend_addr_t;

/**
 * @brief Interface structure for backend operations.
 */
typedef struct 
{
    /**
     * @brief Connect to the backend.
     * 
     * @param addr Pointer to opencd_addr_t structure with host and port
     * @return int Socket file descriptor on success, -1 on failure
     */
    int (*connect)(const backend_addr_t *addr);    

    /**
     * @brief Disconnect from the backend.
     * 
     * @param socket Socket file descriptor
     * @return int 0 on success
     */
    int (*disconnect)(int socket);

    /**
     * @brief Read memory from target via the backend.
     * 
     * @param socket Socket file descriptor
     * @param address Memory address to read from
     * @param buffer Buffer to store read data
     * @param length Number of bytes to read
     * @return int 0 on success, -1 on failure
     */
    int (*read_memory)(int socket, uint32_t address, void *buffer, size_t length);

    /**
     * @brief Write memory to target via the backend.
     * 
     * @param socket Socket file descriptor
     * @param address Memory address to write to
     * @param buffer Buffer containing data to write
     * @param length Number of bytes to write
     * @return int 0 on success, -1 on failure
     */
    int (*write_memory)(int socket, uint32_t address, const void *buffer, size_t length); 
} backend_if_t;

extern backend_if_t backend_openocd;
extern backend_if_t *backends[BACKEND_TYPE__COUNT];

/**
 * @brief Connect to the specified backend.
 * 
 * @param type Backend type.
 * @param addr Pointer to backend_addr_t structure with host and port.
 * @return int Socket file descriptor on success, -1 on failure.
 */
extern int backend_connect(backend_type_t type, const backend_addr_t *addr);

/**
 * @brief Disconnect from the specified backend.
 * 
 * @param type Backend type.
 * @param socket Socket file descriptor.
 * @return int 0 on success.
 */
extern int backend_disconnect(backend_type_t type, int socket);

/**
 * @brief Read memory from target via the specified backend.
 * 
 * @param type Backend type.
 * @param socket Socket file descriptor.
 * @param address Memory address to read from.
 * @param buffer Buffer to store read data.
 * @param length Number of bytes to read.
 * @return int 0 on success, -1 on failure.
 */
extern int backend_read_memory(backend_type_t type, int socket, uint32_t address, void *buffer, size_t length);

/**
 * @brief Write memory to target via the specified backend.
 * 
 * @param type Backend type.
 * @param socket Socket file descriptor.
 * @param address Memory address to write to.
 * @param buffer Buffer containing data to write.
 * @param length Number of bytes to write.
 * @return int 0 on success, -1 on failure.
 */
extern int backend_write_memory(backend_type_t type, int socket, uint32_t address, const void *buffer, size_t length);

#endif // BACKEND_H