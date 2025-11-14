#ifndef GDBSERVER_H
#define GDBSERVER_H

#include <stddef.h>
#include <stdint.h>
#include "backend.h"

#define GDB_DEFAULT_HOST    "localhost"
#define GDB_DEFAULT_PORT    3333

extern const backend_addr_t gdb_default_addr;

extern int gdb_connect(const backend_addr_t *addr);
extern int gdb_disconnect(int socket);
extern int gdb_continue(int socket);
extern int gdb_resume_briefly(int socket);
extern int gdb_read_memory(int socket, uint32_t address, void *buffer, size_t length);
extern int gdb_write_memory(int socket, uint32_t address, const void *buffer, size_t length);

#endif // GDBSERVER_H