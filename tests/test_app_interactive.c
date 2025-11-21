/**
 * @file test_app_interactive.c
 * @brief Configurable test application for automated dmlog testing
 * 
 * This application reads a test scenario file and executes it,
 * allowing automated testing of dmlog with gdbserver integration.
 * 
 * Usage: test_app_interactive <input_file> [buffer_size]
 * 
 * Input file format:
 * - Regular lines are logged to dmlog (one line = one log entry)
 * - Special marker "<user_input>" triggers reading from dmlog input
 * - Special marker "<send_file:fw_path:pc_path>" sends file from FW to PC
 * - Special marker "<recv_file:fw_path:pc_path>" receives file from PC to FW
 * - Lines starting with "#" are comments and ignored
 * 
 * Arguments:
 * - input_file: Path to test scenario file
 * - buffer_size: Optional buffer size in bytes (default: 4096)
 */

#include "dmlog.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>

#define DEFAULT_BUFFER_SIZE (4 * 1024)
#define MAX_BUFFER_SIZE (16 * 1024)
#define MAX_LINE_LENGTH 512

static volatile bool keep_running = true;
static dmlog_ctx_t g_dmlog_ctx = NULL;
// Use static buffer so it can be found with nm for gdbserver testing
static char g_log_buffer[MAX_BUFFER_SIZE];

void signal_handler(int signum) {
    (void)signum;
    keep_running = false;
}

void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s <input_file> [buffer_size]\n", progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "Arguments:\n");
    fprintf(stderr, "  input_file   : Path to test scenario file\n");
    fprintf(stderr, "  buffer_size  : Buffer size in bytes (default: %d)\n", DEFAULT_BUFFER_SIZE);
    fprintf(stderr, "\n");
    fprintf(stderr, "Input file format:\n");
    fprintf(stderr, "  - Regular lines are logged to dmlog\n");
    fprintf(stderr, "  - '<user_input>' marker triggers reading from dmlog input\n");
    fprintf(stderr, "  - '<send_file:fw_path:pc_path>' sends file from FW to PC\n");
    fprintf(stderr, "  - '<recv_file:fw_path:pc_path>' receives file from PC to FW\n");
    fprintf(stderr, "  - Lines starting with '#' are comments\n");
}

int main(int argc, char *argv[]) {
    // Parse arguments
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    size_t buffer_size = DEFAULT_BUFFER_SIZE;

    if (argc >= 3) {
        buffer_size = (size_t)atoi(argv[2]);
        if (buffer_size < 512) {
            fprintf(stderr, "Error: Buffer size too small (minimum: 512)\n");
            return 1;
        }
    }

    // Set up signal handler
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    printf("=== dmlog Interactive Test Application ===\n");
    printf("Input file: %s\n", input_file);
    printf("Buffer size: %zu bytes\n", buffer_size);

    // Validate buffer size
    if (buffer_size > MAX_BUFFER_SIZE) {
        fprintf(stderr, "Error: Buffer size exceeds maximum (%d bytes)\n", MAX_BUFFER_SIZE);
        return 1;
    }

    // Create dmlog buffer using static global buffer
    g_dmlog_ctx = dmlog_create(g_log_buffer, buffer_size);
    if (!g_dmlog_ctx) {
        fprintf(stderr, "Error: Failed to create dmlog context\n");
        return 1;
    }

    printf("dmlog context created at: %p\n", (void*)g_log_buffer);

    // Open input file
    FILE *f = fopen(input_file, "r");
    if (!f) {
        fprintf(stderr, "Error: Failed to open input file: %s\n", input_file);
        dmlog_destroy(g_dmlog_ctx);
        return 1;
    }

    printf("Processing test scenario...\n");

    char line[MAX_LINE_LENGTH];
    int line_num = 0;

    while (keep_running && fgets(line, sizeof(line), f)) {
        line_num++;

        // Remove trailing newline if present
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
            len--;
        }

        // Skip empty lines
        if (len == 0) {
            continue;
        }

        // Skip comments
        if (line[0] == '#') {
            continue;
        }

        // Check for special markers
        if (strcmp(line, "<user_input>") == 0) {
            printf("[Line %d] Requesting user input...\n", line_num);
            
            // Request input from dmlog
            dmlog_input_request(g_dmlog_ctx, DMLOG_INPUT_REQUEST_FLAG_LINE_MODE);
            
            // Wait for input to become available (with timeout)
            // Long timeout (3 minutes) as fallback - app should exit via "exit" command
            int timeout = 1800; // 3 minutes
            while (!dmlog_input_available(g_dmlog_ctx) && timeout > 0 && keep_running) {
                usleep(100000); // 100ms
                timeout--;
            }

            if (!dmlog_input_available(g_dmlog_ctx)) {
                printf("[Line %d] Warning: No input received (timeout)\n", line_num);
                dmlog_puts(g_dmlog_ctx, "ERROR: No input received\n");
            } else {
                // Read the input
                char input_buffer[256];
                if (dmlog_input_gets(g_dmlog_ctx, input_buffer, sizeof(input_buffer))) {
                    printf("[Line %d] Received input: %s", line_num, input_buffer);
                    
                    // Echo the input to dmlog output
                    dmlog_puts(g_dmlog_ctx, "Received: ");
                    dmlog_puts(g_dmlog_ctx, input_buffer);
                    
                    // Check for exit command
                    if (strncmp(input_buffer, "exit", 4) == 0) {
                        printf("[Line %d] Exit command received, stopping...\n", line_num);
                        keep_running = false;
                        break;
                    }
                } else {
                    printf("[Line %d] Warning: Failed to read input\n", line_num);
                    dmlog_puts(g_dmlog_ctx, "ERROR: Failed to read input\n");
                }
            }
        } else if (strncmp(line, "<send_file:", 11) == 0) {
            // Parse: <send_file:fw_path:pc_path>
            char fw_path[256], pc_path[256];
            if (sscanf(line, "<send_file:%255[^:]:%255[^>]>", fw_path, pc_path) == 2) {
                printf("[Line %d] Sending file: %s -> %s\n", line_num, fw_path, pc_path);
                dmlog_puts(g_dmlog_ctx, "Sending file: ");
                dmlog_puts(g_dmlog_ctx, fw_path);
                dmlog_puts(g_dmlog_ctx, " -> ");
                dmlog_puts(g_dmlog_ctx, pc_path);
                dmlog_puts(g_dmlog_ctx, "\n");
                dmlog_flush(g_dmlog_ctx);
                
                bool result = dmlog_sendf(g_dmlog_ctx, fw_path, pc_path, 0);
                
                if (result) {
                    printf("[Line %d] File sent successfully\n", line_num);
                    dmlog_puts(g_dmlog_ctx, "File sent successfully\n");
                } else {
                    printf("[Line %d] File send failed\n", line_num);
                    dmlog_puts(g_dmlog_ctx, "File send FAILED\n");
                }
            } else {
                printf("[Line %d] Error: Invalid send_file format\n", line_num);
                dmlog_puts(g_dmlog_ctx, "ERROR: Invalid send_file format\n");
            }
        } else if (strncmp(line, "<recv_file:", 11) == 0) {
            // Parse: <recv_file:fw_path:pc_path>
            char fw_path[256], pc_path[256];
            if (sscanf(line, "<recv_file:%255[^:]:%255[^>]>", fw_path, pc_path) == 2) {
                printf("[Line %d] Receiving file: %s <- %s\n", line_num, fw_path, pc_path);
                dmlog_puts(g_dmlog_ctx, "Receiving file: ");
                dmlog_puts(g_dmlog_ctx, fw_path);
                dmlog_puts(g_dmlog_ctx, " <- ");
                dmlog_puts(g_dmlog_ctx, pc_path);
                dmlog_puts(g_dmlog_ctx, "\n");
                dmlog_flush(g_dmlog_ctx);
                
                bool result = dmlog_recvf(g_dmlog_ctx, fw_path, pc_path, 0);
                
                if (result) {
                    printf("[Line %d] File received successfully\n", line_num);
                    dmlog_puts(g_dmlog_ctx, "File received successfully\n");
                } else {
                    printf("[Line %d] File receive failed\n", line_num);
                    dmlog_puts(g_dmlog_ctx, "File receive FAILED\n");
                }
            } else {
                printf("[Line %d] Error: Invalid recv_file format\n", line_num);
                dmlog_puts(g_dmlog_ctx, "ERROR: Invalid recv_file format\n");
            }
        } else {
            // Regular log line - add newline back
            printf("[Line %d] Logging: %s\n", line_num, line);
            dmlog_puts(g_dmlog_ctx, line);
            dmlog_puts(g_dmlog_ctx, "\n");
        }

        dmlog_flush(g_dmlog_ctx);
    }

    fclose(f);

    printf("Test scenario completed. Flushing final logs...\n");
    dmlog_flush(g_dmlog_ctx);
    
    // Give monitor time to read final logs and process any pending inputs
    // For tests with multiple inputs, we need a bit more time
    sleep(3);

    printf("Exiting gracefully...\n");
    dmlog_destroy(g_dmlog_ctx);

    return 0;
}
