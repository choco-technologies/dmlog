#ifndef GDBSERVER_H
#define GDBSERVER_H

#include <stddef.h>
#include <stdint.h>

#define GDB_DEFAULT_HOST    "localhost"
#define GDB_DEFAULT_PORT    1234

typedef struct 
{
    char host[256];
    int port;
} gdb_addr_t;

extern int gdb_connect(gdb_addr_t *addr);
extern int gdb_disconnect(int socket);
extern int gdb_read_memory(int socket, uint32_t address, void *buffer, size_t length);
extern int gdb_write_memory(int socket, uint32_t address, const void *buffer, size_t length);

#endif // GDBSERVER_H
