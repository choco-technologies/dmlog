#include "backend.h"
#include "openocd.h"
#include "gdb.h"

backend_if_t backend_openocd = 
{
    .name = "OpenOCD",
    .connect = openocd_connect,
    .disconnect = openocd_disconnect,
    .read_memory = openocd_read_memory,
    .write_memory = openocd_write_memory
};
backend_if_t backend_gdb = 
{
    .name = "GDB",
    .connect = gdb_connect,
    .disconnect = gdb_disconnect,
    .read_memory = gdb_read_memory,
    .write_memory = gdb_write_memory
};
backend_if_t *backends[BACKEND_TYPE__COUNT] = 
{
    &backend_openocd,
    &backend_gdb
};

const backend_addr_t* backend_default_addrs[BACKEND_TYPE__COUNT] = 
{
    &openocd_default_addr,
    &gdb_default_addr
};

int backend_connect(backend_type_t type, const backend_addr_t *addr)
{
    if(type >= BACKEND_TYPE__COUNT || backends[type] == NULL)
    {
        return -1;
    }
    return backends[type]->connect(addr);
}

int backend_disconnect(backend_type_t type, int socket)
{
    if(type >= BACKEND_TYPE__COUNT || backends[type] == NULL)
    {
        return -1;
    }
    return backends[type]->disconnect(socket);
}

int backend_read_memory(backend_type_t type, int socket, uint64_t address, void *buffer, size_t length)
{
    if(type >= BACKEND_TYPE__COUNT || backends[type] == NULL)
    {
        return -1;
    }
    return backends[type]->read_memory(socket, address, buffer, length);
}

int backend_write_memory(backend_type_t type, int socket, uint64_t address, const void *buffer, size_t length)
{
    if(type >= BACKEND_TYPE__COUNT || backends[type] == NULL)
    {
        return -1;
    }
    return backends[type]->write_memory(socket, address, buffer, length);
}

const char* backend_type_to_string(backend_type_t type)
{
    if(type < BACKEND_TYPE__COUNT && backends[type] != NULL)
    {
        return backends[type]->name;
    }
    return "Unknown";
}