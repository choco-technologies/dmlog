#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dmlog.h"
#include "monitor.h"
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
    printf("  --search      Search for the ring buffer in memory\n");
    printf("  --trace-level Set trace level (error, warn, info, verbose)\n");
    printf("  --verbose     Enable verbose output (equivalent to --trace-level verbose)\n");
}

int main(int argc, char *argv[])
{
    uint32_t ring_buffer_address = 0x20010000; // Default address
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
        else if(strcmp(argv[i], "--addr") == 0 && i + 1 < argc)
        {
            ring_buffer_address = (uint32_t)strtoul(argv[++i], NULL, 0);
        }
        else if(strcmp(argv[i], "--trace-level") == 0 && i + 1 < argc)
        {
            const char *level_str = argv[++i];
            if(strcmp(level_str, "error") == 0)
            {
                current_trace_level = TRACE_LEVEL_ERROR;
            }
            else if(strcmp(level_str, "warn") == 0)
            {
                current_trace_level = TRACE_LEVEL_WARN;
            }
            else if(strcmp(level_str, "info") == 0)
            {
                current_trace_level = TRACE_LEVEL_INFO;
            }
            else if(strcmp(level_str, "verbose") == 0)
            {
                current_trace_level = TRACE_LEVEL_VERBOSE;
            }
            else
            {
                TRACE_ERROR("Unknown trace level: %s\n", level_str);
                usage(argv[0]);
                return 1;
            }
        }
        else if(strcmp(argv[i], "--verbose") == 0)
        {
            current_trace_level = TRACE_LEVEL_VERBOSE;
        }
        else
        {
            TRACE_ERROR("Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    monitor_ctx_t *ctx = monitor_connect(&openocd_addr, ring_buffer_address);
    if(ctx == NULL)
    {
        TRACE_ERROR("Failed to connect to monitor\n");
        return 1;
    }

    monitor_update_entry(ctx);

    // Main monitoring loop would go here
    monitor_disconnect(ctx);

    return 0;
}