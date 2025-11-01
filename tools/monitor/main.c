/*
 * dmlog_monitor - Monitor dmlog ring buffer via OpenOCD
 * 
 * This tool connects to OpenOCD and reads the memory ring buffer to display
 * log messages from the target microcontroller.
 */

#include "dmlog.h"

int main(int argc, char *argv[]) {
    openocd_client_t client = {
        .sockfd = -1,
        .port = DEFAULT_PORT
    };
    strncpy(client.host, DEFAULT_HOST, sizeof(client.host) - 1);
    
    dmlog_monitor_t monitor = {
        .client = &client,
        .ring_addr = DEFAULT_RING_ADDR,
        .ring_control_size = RING_CONTROL_READ_SIZE,  /* Use defined constant */
        .buffer_addr = 0,  /* Will be read from control structure */
        .total_size = DEFAULT_TOTAL_SIZE,
        .max_entry_size = DEFAULT_MAX_ENTRY_SIZE,
        .expected_magic = DMLOG_MAGIC_NUMBER,
        .expected_entry_magic = DMLOG_ENTRY_MAGIC_NUMBER,
        .max_startup_entries = DEFAULT_MAX_STARTUP_ENTRIES,
        .last_id = 0,
        .debug = false
    };
    
    double interval_sec = 0.1;  /* Default interval in seconds */
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            strncpy(client.host, argv[++i], sizeof(client.host) - 1);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            client.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--addr") == 0 && i + 1 < argc) {
            monitor.ring_addr = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            monitor.total_size = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-entry") == 0 && i + 1 < argc) {
            monitor.max_entry_size = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            interval_sec = atof(argv[++i]);
            if (interval_sec < 0.001) {
                fprintf(stderr, "Warning: Interval too small, setting to 0.001 seconds\n");
                interval_sec = 0.001;
            }
        } else if (strcmp(argv[i], "--max-startup") == 0 && i + 1 < argc) {
            monitor.max_startup_entries = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--debug") == 0) {
            monitor.debug = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Convert interval to microseconds */
    int interval_us = (int)(interval_sec * 1000000.0);
    
    /* Connect to OpenOCD */
    printf("Connecting to OpenOCD at %s:%d...\n", client.host, client.port);
    fflush(stdout);
    if (!openocd_connect(&client)) {
        fprintf(stderr, "Make sure OpenOCD is running with telnet server enabled\n");
        fprintf(stderr, "Example: openocd -f interface/stlink.cfg -f target/stm32f7x.cfg\n");
        return 1;
    }
    printf("Connected to OpenOCD\n\n");
    
    /* Start monitoring */
    monitor_loop(&monitor, interval_us);
    
    /* Cleanup */
    openocd_close(&client);
    
    return 0;
}