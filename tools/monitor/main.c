#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include "dmlog.h"
#include "monitor.h"
#include "trace.h"
#include "backend.h"

#ifndef DMLOG_VERSION
#   define DMLOG_VERSION "unknown"
#endif

/**
 * @brief Signal handler for graceful shutdown
 */
static void signal_handler(int signum)
{
    (void)signum;
    // Restore terminal settings immediately
    monitor_restore_terminal();
    exit(0);
}

void usage(const char *progname)
{
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  --help        Show this help message\n");
    printf("  --version     Show version information\n");
    printf("  --host        Backend IP address (default: localhost)\n");
    printf("  --port        Backend port (default: 4444)\n");
    printf("  --addr        Address of the ring buffer\n");
    printf("  --search      Search for the ring buffer in memory\n");
    printf("  --trace-level Set trace level (error, warn, info, verbose)\n");
    printf("  --verbose     Enable verbose output (equivalent to --trace-level verbose)\n");
    printf("  --time        Show timestamps with log entries\n");
    printf("  --blocking    Use blocking mode for reading log entries\n");
    printf("  --snapshot    Enable snapshot mode to reduce target reads\n");
    printf("  --gdb         Use GDB backend instead of OpenOCD\n");
    printf("  --input-file  File to read input from for automated testing\n");
    printf("  --init-script File to read as initialization script, then switch to stdin\n");
}

int main(int argc, char *argv[])
{
    bool show_timestamps = false;
    bool blocking_mode = false;
    bool snapshot_mode = false;
    const char *input_file_path = NULL;
    bool init_script_mode = false;
    uint32_t ring_buffer_address = 0x20010000; // Default address
    backend_addr_t backend_addr;
    const backend_addr_t* default_addr = backend_default_addrs[BACKEND_TYPE_OPENOCD];
    memcpy(&backend_addr, default_addr, sizeof(backend_addr_t));

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
        else if(strcmp(argv[i], "--input-file") == 0 && i + 1 < argc)
        {
            input_file_path = argv[++i];
        }
        else if(strcmp(argv[i], "--init-script") == 0 && i + 1 < argc)
        {
            input_file_path = argv[++i];
            init_script_mode = true;
        }
        else if(strcmp(argv[i], "--gdb") == 0)
        {
            const backend_addr_t* gdb_default = backend_default_addrs[BACKEND_TYPE_GDB];
            if(gdb_default == NULL)
            {
                TRACE_ERROR("GDB backend not implemented\n");
                return 1;
            }
            if(backend_addr.port == default_addr->port)
            {
                backend_addr.port = gdb_default->port;
            }
            if(strcmp(backend_addr.host, default_addr->host) == 0)
            {
                strncpy(backend_addr.host, gdb_default->host, sizeof(backend_addr.host));
            }
            backend_addr.type = BACKEND_TYPE_GDB;
            default_addr = gdb_default;
        }
        else
        {
            TRACE_ERROR("Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    TRACE_INFO("dmlog monitor version %s\n", DMLOG_VERSION);
    TRACE_INFO("Using backend: %s (%s:%d)\n", 
        backend_type_to_string(backend_addr.type),
        backend_addr.host,
        backend_addr.port);

    monitor_ctx_t *ctx = monitor_connect(&backend_addr, ring_buffer_address, snapshot_mode);
    if(ctx == NULL)
    {
        TRACE_ERROR("Failed to connect to monitor\n");
        return 1;
    }

    // Open input file if specified
    if(input_file_path != NULL)
    {
        ctx->input_file = fopen(input_file_path, "r");
        if(ctx->input_file == NULL)
        {
            TRACE_ERROR("Failed to open input file: %s\n", input_file_path);
            monitor_disconnect(ctx);
            return 1;
        }
        ctx->init_script_mode = init_script_mode;
        if(init_script_mode)
        {
            TRACE_INFO("Using init script: %s (will switch to stdin after completion)\n", input_file_path);
        }
        else
        {
            TRACE_INFO("Using input file: %s\n", input_file_path);
        }
    }

    // Register signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    monitor_run(ctx, show_timestamps, blocking_mode);

    TRACE_INFO("Exiting monitor\n");

    // Restore terminal settings before exit
    monitor_restore_terminal();
    
    // Main monitoring loop would go here
    monitor_disconnect(ctx);

    return 0;
}