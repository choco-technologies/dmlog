#ifndef OPENOCD_H
#define OPENOCD_H

#include <stddef.h>
#include <stdint.h>
#include "backend.h"

#define OPENOCD_DEFAULT_HOST    "localhost"
#define OPENOCD_DEFAULT_PORT    4444

extern int openocd_connect(const backend_addr_t *addr);
extern int openocd_read_welcome(int socket);
extern int openocd_disconnect(int socket);
extern int openocd_set_debug_level(int socket, int level);
extern int openocd_send_command(int socket, const char *cmd, char *response, size_t response_size);
extern int openocd_read_memory(int socket, uint32_t address, void *buffer, size_t length);
extern int openocd_write_memory(int socket, uint32_t address, const void *buffer, size_t length);

#endif // OPENOCD_H