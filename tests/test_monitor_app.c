/**
 * @file test_monitor_app.c
 * @brief Test application for dmlog_monitor GDB server integration
 * 
 * This application creates a dmlog buffer, writes test messages,
 * and can be run under gdbserver for testing monitor connectivity.
 */

#include "dmlog.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>

#define TEST_BUFFER_SIZE (8 * 1024)
static char test_buffer[TEST_BUFFER_SIZE];

// Global dmlog context for GDB to access
static dmlog_ctx_t g_dmlog_ctx = NULL;
static volatile bool keep_running = true;

void signal_handler(int signum) {
    keep_running = false;
}

int main(void) {
    // Set up signal handler for clean exit
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    
    printf("=== dmlog_monitor GDB Integration Test ===\n");
    
    // Create dmlog context
    g_dmlog_ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    if (!g_dmlog_ctx) {
        fprintf(stderr, "Failed to create dmlog context\n");
        return 1;
    }
    
    printf("dmlog context created at: %p\n", (void*)test_buffer);
    printf("Buffer size: %d bytes\n", TEST_BUFFER_SIZE);
    
    // Write test messages
    dmlog_puts(g_dmlog_ctx, "Test message 1: Hello from dmlog!\n");
    dmlog_puts(g_dmlog_ctx, "Test message 2: GDB server integration test\n");
    dmlog_puts(g_dmlog_ctx, "Test message 3: This is line three\n");
    
    // Flush to ensure data is written
    dmlog_flush(g_dmlog_ctx);
    
    printf("Test messages written to dmlog buffer\n");
    printf("Waiting for monitor connection...\n");
    printf("Buffer address: %p\n", (void*)test_buffer);
    
    // Keep running to allow monitor to connect and read
    // Monitor should see the 3 test messages above
    int i = 0;
    while (keep_running && i < 30) {
        sleep(1);
        i++;
        
        // Write periodic updates every 5 seconds
        if (i % 5 == 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Periodic update %d\n", i/5);
            dmlog_puts(g_dmlog_ctx, msg);
        }
    }
    
    printf("Test application exiting\n");
    dmlog_destroy(g_dmlog_ctx);
    
    return 0;
}
