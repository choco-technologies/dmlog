#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dmlog.h"
#include "openocd.h"
#include "trace.h"

#ifndef DMLOG_VERSION
#   define DMLOG_VERSION "unknown"
#endif

void usage(const char *progname)
{
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  --help        Show this help message\n");
    printf("  --version     Show version information\n");
    printf("  --host        OpenOCD IP address (default: localhost)\n");
    printf("  --port        OpenOCD port (default: 4444)\n");
    printf("  --addr        Address of the ring buffer\n");
}

int main(int argc, char *argv[])
{
    opencd_addr_t openocd_addr;
    strncpy(openocd_addr.host, OPENOCD_DEFAULT_HOST, sizeof(openocd_addr.host));
    openocd_addr.port = OPENOCD_DEFAULT_PORT;
    for(int i = 1; i < argc; i++)
    {
        if(strcmp(argv[i], "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else if(strcmp(argv[i], "--version") == 0)
        {
            printf("dmlog monitor version %s\n", DMLOG_VERSION);
            return 0;
        }
        else if(strcmp(argv[i], "--host") == 0 && i + 1 < argc)
        {
            strncpy(openocd_addr.host, argv[++i], sizeof(openocd_addr.host));
        }
        else if(strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            openocd_addr.port = atoi(argv[++i]);
        }
        else
        {
            TRACE_ERROR("Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    int socket = openocd_connect(&openocd_addr);
    if(socket < 0)
    {
        TRACE_ERROR("Failed to connect to OpenOCD at %s:%d\n", openocd_addr.host, openocd_addr.port);
        return 1;
    }
    TRACE_INFO("Connected to OpenOCD at %s:%d\n", openocd_addr.host, openocd_addr.port);

    uint8_t buffer[256];
    openocd_read_memory(socket, 0x20010000, buffer, sizeof(buffer)); 

    uint32_t* words = (uint32_t*)buffer;
    for(int i = 0; i < (sizeof(buffer) / sizeof(uint32_t)); i++)
    {
        printf("%08X ", words[i]);
        if((i + 1) % 8 == 0)
        {
            printf("\n");
        }
    }
    printf("\n");

    openocd_disconnect(socket);

    return 0;
}