#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dmlog.h"
#include "monitor.h"
#include "backend.h"
#include "openocd.h"
#include "gdbserver.h"
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
    printf("  --gdb         Use GDB server instead of OpenOCD (default: OpenOCD)\n");
    printf("  --host        Backend IP address (default: localhost)\n");
    printf("  --port        Backend port (default: 4444 for OpenOCD, 1234 for GDB)\n");
    printf("  --addr        Address of the ring buffer\n");
    printf("  --search      Search for the ring buffer in memory\n");
    printf("  --trace-level Set trace level (error, warn, info, verbose)\n");
    printf("  --verbose     Enable verbose output (equivalent to --trace-level verbose)\n");
    printf("  --time        Show timestamps with log entries\n");
    printf("  --blocking    Use blocking mode for reading log entries\n");
    printf("  --snapshot    Enable snapshot mode to reduce target reads\n");
}

int main(int argc, char *argv[])
{
    bool show_timestamps = false;
    bool blocking_mode = false;
    bool snapshot_mode = false;
    bool use_gdb = false;
    uint32_t ring_buffer_address = 0x20010000; // Default address
    backend_addr_t backend_addr;
    strncpy(backend_addr.host, OPENOCD_DEFAULT_HOST, sizeof(backend_addr.host));
    backend_addr.port = OPENOCD_DEFAULT_PORT;
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
        else if(strcmp(argv[i], "--gdb") == 0)
        {
            use_gdb = true;
        }
        else if(strcmp(argv[i], "--host") == 0 && i + 1 < argc)
        {
            strncpy(backend_addr.host, argv[++i], sizeof(backend_addr.host));
        }
        else if(strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            backend_addr.port = atoi(argv[++i]);
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
        else if(strcmp(argv[i], "--time") == 0)
        {
            show_timestamps = true;
        }
        else if(strcmp(argv[i], "--blocking") == 0)
        {
            blocking_mode = true;
        }
        else if(strcmp(argv[i], "--snapshot") == 0)
        {
            snapshot_mode = true;
        }
        else
        {
            TRACE_ERROR("Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    // Set default port based on backend type if not specified
    if(use_gdb && backend_addr.port == OPENOCD_DEFAULT_PORT)
    {
        backend_addr.port = GDB_DEFAULT_PORT;
    }

    backend_type_t backend_type = use_gdb ? BACKEND_GDB : BACKEND_OPENOCD;
    const char *backend_name = use_gdb ? "GDB server" : "OpenOCD";
    
    TRACE_INFO("Connecting to %s at %s:%d\n", backend_name, backend_addr.host, backend_addr.port);
    
    monitor_ctx_t *ctx = monitor_connect(&backend_addr, backend_type, ring_buffer_address, snapshot_mode);
    if(ctx == NULL)
    {
        TRACE_ERROR("Failed to connect to %s\n", backend_name);
        return 1;
    }

    monitor_run(ctx, show_timestamps, blocking_mode);

    // Main monitoring loop would go here
    monitor_disconnect(ctx);

    return 0;
}