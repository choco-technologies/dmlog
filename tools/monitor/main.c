/*
 * dmlog_monitor - Monitor dmlog ring buffer via OpenOCD
 * 
 * This tool connects to OpenOCD and reads the memory ring buffer to display
 * log messages from the target microcontroller.
 */

#include "dmlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>

/* Default configuration */
#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT 4444
#define DEFAULT_INTERVAL 100000  /* 0.1 seconds in microseconds */
#define DEFAULT_RING_ADDR 0x20000000
#define DEFAULT_TOTAL_SIZE 4096
#define DEFAULT_MAX_ENTRY_SIZE DMOD_LOG_MAX_ENTRY_SIZE
#define DEFAULT_MAX_STARTUP_ENTRIES 100

/* Validation constants */
#define DMLOG_INVALID_ID_THRESHOLD 0xFFFFFF00  /* IDs above this are considered invalid/uninitialized */

/* Buffer sizes */
#define RESPONSE_BUFFER_SIZE 32768
#define RECV_CHUNK_SIZE 1024

/* OpenOCD client structure */
typedef struct {
    int sockfd;
    char host[256];
    int port;
} openocd_client_t;

/* Ring buffer monitor structure */
typedef struct {
    openocd_client_t* client;
    uint32_t ring_addr;
    uint32_t ring_control_size;
    uint32_t buffer_addr;  /* Address where log entries are stored */
    uint32_t total_size;
    uint32_t max_entry_size;
    uint32_t expected_magic;
    uint32_t expected_entry_magic;
    uint32_t max_startup_entries;
    dmlog_entry_id_t last_id;
    bool debug;
} dmlog_monitor_t;

/* Ring control structure (matching dmlog_ring_t memory layout for reading)
 * We need to read:
 * - magic(4) + latest_id(4) + flags(4) + head_offset(4) + tail_offset(4) = 20 bytes
 * - buffer_size(4) = 4 bytes
 * - buffer pointer(4 or 8 bytes depending on architecture)
 * 
 * The buffer pointer tells us where the actual log entries are stored in memory.
 */
typedef struct {
    uint32_t magic;
    dmlog_entry_id_t latest_id;
    uint32_t flags;
    dmlog_index_t head_offset;
    dmlog_index_t tail_offset;
    dmlog_index_t buffer_size;
    uint32_t buffer_addr;  /* Address of the buffer (assuming 32-bit pointers) */
} ring_control_t;

/* Size of the control structure to read from memory (in bytes) */
#define RING_CONTROL_READ_SIZE 28  /* magic(4) + latest_id(4) + flags(4) + head(4) + tail(4) + buffer_size(4) + buffer_addr(4) */

/* Log entry structure */
typedef struct {
    dmlog_entry_id_t id;
    uint16_t length;
    char message[DMOD_LOG_MAX_ENTRY_SIZE];
    dmlog_index_t next_offset;
} log_entry_t;

/* Global flag for signal handling */
static volatile bool g_running = true;

/* Signal handler for graceful shutdown */
static void signal_handler(int signum) {
    (void)signum;
    g_running = false;
}

/* Debug print macro */
#define DEBUG_PRINT(monitor, fmt, ...) \
    do { if ((monitor)->debug) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while(0)

/*
 * OpenOCD Client Functions
 */

/* Connect to OpenOCD telnet server */
static bool openocd_connect(openocd_client_t* client) {
    struct sockaddr_in serv_addr;
    char initial_response[4096];
    struct addrinfo hints, *result, *rp;
    int addr_result;
    
    /* Setup hints for getaddrinfo */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    /* Resolve hostname */
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", client->port);
    addr_result = getaddrinfo(client->host, port_str, &hints, &result);
    
    if (addr_result != 0) {
        fprintf(stderr, "Failed to resolve hostname %s: %s\n", 
                client->host, gai_strerror(addr_result));
        return false;
    }
    
    /* Try each address until we successfully connect */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        client->sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (client->sockfd < 0) {
            continue;
        }
        
        if (connect(client->sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break; /* Success */
        }
        
        close(client->sockfd);
        client->sockfd = -1;
    }
    
    freeaddrinfo(result);
    
    if (client->sockfd < 0) {
        fprintf(stderr, "Failed to connect to OpenOCD at %s:%d: %s\n", 
                client->host, client->port, strerror(errno));
        return false;
    }
    
    /* Read and discard initial prompt */
    ssize_t received = recv(client->sockfd, initial_response, sizeof(initial_response), 0);
    if (received < 0) {
        fprintf(stderr, "Error receiving initial response: %s\n", strerror(errno));
        close(client->sockfd);
        client->sockfd = -1;
        return false;
    }
    
    return true;
}

/* Send command to OpenOCD and get response */
static char* openocd_send_command(openocd_client_t* client, const char* cmd) {
    static char response[RESPONSE_BUFFER_SIZE];
    char chunk[RECV_CHUNK_SIZE];
    int total_received = 0;
    char cmd_with_newline[1024];
    
    /* Send command */
    snprintf(cmd_with_newline, sizeof(cmd_with_newline), "%s\n", cmd);
    if (send(client->sockfd, cmd_with_newline, strlen(cmd_with_newline), 0) < 0) {
        fprintf(stderr, "Error sending command: %s\n", strerror(errno));
        return NULL;
    }
    
    /* Read response until we get the prompt */
    memset(response, 0, sizeof(response));
    while (total_received < RESPONSE_BUFFER_SIZE - 1) {
        ssize_t received = recv(client->sockfd, chunk, RECV_CHUNK_SIZE - 1, 0);
        if (received < 0) {
            fprintf(stderr, "Error receiving response: %s\n", strerror(errno));
            return NULL;
        } else if (received == 0) {
            /* Connection closed */
            fprintf(stderr, "Connection closed by OpenOCD\n");
            return NULL;
        }
        
        chunk[received] = '\0';
        
        /* Copy to response buffer */
        int space_left = RESPONSE_BUFFER_SIZE - total_received - 1;
        int to_copy = (received < space_left) ? (int)received : space_left;
        memcpy(response + total_received, chunk, to_copy);
        total_received += to_copy;
        response[total_received] = '\0';
        
        /* Check if we got the prompt */
        if (strstr(response, "> ") != NULL) {
            /* Remove prompt */
            char* prompt_pos = strstr(response, "> ");
            if (prompt_pos) *prompt_pos = '\0';
            break;
        }
        
        /* Safety check */
        if (total_received >= RESPONSE_BUFFER_SIZE - 1024) break;
    }
    
    return response;
}

/* Read memory from target via OpenOCD */
static uint8_t* openocd_read_memory(openocd_client_t* client, uint32_t address, 
                                     uint32_t size, bool debug) {
    static uint8_t data[DMOD_LOG_MAX_ENTRY_SIZE * 2];
    char cmd[256];
    char* response;
    /* Allocate words array with proper size: need enough words to cover size plus alignment */
    uint32_t words[(DMOD_LOG_MAX_ENTRY_SIZE + 3) / 4 + 1];
    size_t word_count = 0;
    
    /* Calculate alignment */
    uint32_t alignment_offset = address % 4;
    uint32_t aligned_address = address - alignment_offset;
    uint32_t total_bytes_needed = alignment_offset + size;
    uint32_t words_needed = (total_bytes_needed + 3) / 4;
    
    /* Read memory words */
    snprintf(cmd, sizeof(cmd), "mdw 0x%08x %u", aligned_address, words_needed);
    response = openocd_send_command(client, cmd);
    
    if (!response || strlen(response) == 0) {
        if (debug) {
            fprintf(stderr, "No response from OpenOCD for command: %s\n", cmd);
        }
        return NULL;
    }
    
    /* Parse response - format: "0xADDRESS: VALUE VALUE ..." */
    char* line = strtok(response, "\n\r");
    while (line != NULL) {
        /* Skip empty lines and command echo */
        if (strlen(line) > 0 && strchr(line, ':') != NULL && 
            strstr(line, "0x") != NULL && strstr(line, "mdw") == NULL) {
            
            char* colon = strchr(line, ':');
            if (colon) {
                colon++; /* Skip the colon */
                
                /* Parse hex values */
                char* token = strtok(colon, " \t");
                while (token != NULL && word_count < sizeof(words) / sizeof(words[0])) {
                    if (strlen(token) == 8) {
                        /* Parse hex value without 0x prefix */
                        words[word_count++] = (uint32_t)strtoul(token, NULL, 16);
                    }
                    token = strtok(NULL, " \t");
                }
            }
        }
        line = strtok(NULL, "\n\r");
    }
    
    /* Convert words to bytes (little endian) */
    size_t byte_count = 0;
    for (size_t i = 0; i < word_count && byte_count < sizeof(data) - 4; i++) {
        data[byte_count++] = (words[i] >> 0) & 0xFF;
        data[byte_count++] = (words[i] >> 8) & 0xFF;
        data[byte_count++] = (words[i] >> 16) & 0xFF;
        data[byte_count++] = (words[i] >> 24) & 0xFF;
    }
    
    /* Check if we have enough data */
    if (byte_count < alignment_offset + size) {
        if (debug) {
            fprintf(stderr, "Not enough data: got %zu bytes, need %u bytes\n", 
                    byte_count, alignment_offset + size);
        }
        return NULL;
    }
    
    /* Return data with alignment offset applied */
    memmove(data, data + alignment_offset, size);
    return data;
}

/* Close OpenOCD connection */
static void openocd_close(openocd_client_t* client) {
    if (client->sockfd >= 0) {
        close(client->sockfd);
        client->sockfd = -1;
    }
}

/*
 * DMLog Monitor Functions
 */

/* Read ring buffer control structure */
static bool read_ring_control(dmlog_monitor_t* monitor, ring_control_t* control) {
    const int max_retries = 3;
    
    for (int attempt = 0; attempt < max_retries; attempt++) {
        uint8_t* data = openocd_read_memory(monitor->client, monitor->ring_addr, 
                                            monitor->ring_control_size, monitor->debug);
        
        if (!data) {
            DEBUG_PRINT(monitor, "Control read attempt %d/%d: no data", 
                       attempt + 1, max_retries);
            if (attempt < max_retries - 1) usleep(10000); /* 10ms */
            continue;
        }
        
        /* Parse control structure */
        memcpy(&control->magic, data + 0, 4);
        memcpy(&control->latest_id, data + 4, 4);
        memcpy(&control->flags, data + 8, 4);
        memcpy(&control->head_offset, data + 12, 4);
        memcpy(&control->tail_offset, data + 16, 4);
        memcpy(&control->buffer_size, data + 20, 4);
        memcpy(&control->buffer_addr, data + 24, 4);
        
        /* Check magic number */
        if (control->magic != monitor->expected_magic) {
            DEBUG_PRINT(monitor, "Control read attempt %d/%d: magic mismatch (got 0x%08X, expected 0x%08X)",
                       attempt + 1, max_retries, control->magic, monitor->expected_magic);
            if (attempt < max_retries - 1) usleep(10000);
            continue;
        }
        
        /* Update monitor with buffer address from control structure */
        if (control->buffer_addr != 0) {
            monitor->buffer_addr = control->buffer_addr;
            DEBUG_PRINT(monitor, "Buffer address read from control: 0x%08X", control->buffer_addr);
        }
        
        /* Validate values */
        if (control->latest_id < DMLOG_INVALID_ID_THRESHOLD && 
            control->head_offset < control->buffer_size && 
            control->tail_offset < control->buffer_size) {
            return true;
        }
        
        DEBUG_PRINT(monitor, "Control read attempt %d/%d: invalid values", 
                   attempt + 1, max_retries);
        if (attempt < max_retries - 1) usleep(10000);
    }
    
    return false;
}

/* Read a single log entry at given offset */
static bool read_entry_at_offset(dmlog_monitor_t* monitor, dmlog_index_t offset, 
                                 log_entry_t* entry) {
    const int max_retries = 3;
    const int header_size = 10; /* magic(4) + id(4) + length(2) */
    uint32_t buffer_start = monitor->buffer_addr;  /* Use the buffer address read from control */
    uint32_t entry_addr = buffer_start + offset;
    
    for (int attempt = 0; attempt < max_retries; attempt++) {
        /* Read entry header */
        uint8_t* header_data = openocd_read_memory(monitor->client, entry_addr, 
                                                   header_size, monitor->debug);
        
        if (!header_data) {
            DEBUG_PRINT(monitor, "Entry at offset %u, attempt %d/%d: insufficient header data",
                       offset, attempt + 1, max_retries);
            if (attempt < max_retries - 1) usleep(10000);
            continue;
        }
        
        /* Parse header */
        uint32_t entry_magic;
        memcpy(&entry_magic, header_data + 0, 4);
        memcpy(&entry->id, header_data + 4, 4);
        memcpy(&entry->length, header_data + 8, 2);
        
        /* Validate entry magic */
        if (entry_magic != monitor->expected_entry_magic) {
            DEBUG_PRINT(monitor, "Entry at offset %u, attempt %d/%d: magic mismatch (got 0x%08X, expected 0x%08X)",
                       offset, attempt + 1, max_retries, entry_magic, monitor->expected_entry_magic);
            if (attempt < max_retries - 1) usleep(10000);
            continue;
        }
        
        DEBUG_PRINT(monitor, "Reading entry at offset %u: magic=0x%08X, id=%u, length=%u",
                   offset, entry_magic, entry->id, entry->length);
        
        if (entry->length > monitor->max_entry_size) {
            DEBUG_PRINT(monitor, "Entry at offset %u: length %u too big (max %u)",
                       offset, entry->length, monitor->max_entry_size);
            if (attempt < max_retries - 1) usleep(10000);
            continue;
        }
        
        /* Read message data */
        dmlog_index_t data_offset = (offset + header_size) % monitor->total_size;
        uint32_t data_addr = buffer_start + data_offset;
        
        /* Check if data wraps around */
        if (data_offset + entry->length <= monitor->total_size) {
            /* No wraparound */
            uint8_t* message_data = openocd_read_memory(monitor->client, data_addr, 
                                                       entry->length, monitor->debug);
            if (!message_data) {
                DEBUG_PRINT(monitor, "Entry at offset %u, attempt %d/%d: failed to read message data",
                           offset, attempt + 1, max_retries);
                if (attempt < max_retries - 1) usleep(10000);
                continue;
            }
            memcpy(entry->message, message_data, entry->length);
        } else {
            /* Wraparound: read in two parts */
            uint32_t first_part_len = monitor->total_size - data_offset;
            uint32_t second_part_len = entry->length - first_part_len;
            
            uint8_t* first_part = openocd_read_memory(monitor->client, data_addr, 
                                                     first_part_len, monitor->debug);
            uint8_t* second_part = openocd_read_memory(monitor->client, buffer_start, 
                                                      second_part_len, monitor->debug);
            
            if (!first_part || !second_part) {
                DEBUG_PRINT(monitor, "Entry at offset %u, attempt %d/%d: failed to read wraparound message data",
                           offset, attempt + 1, max_retries);
                if (attempt < max_retries - 1) usleep(10000);
                continue;
            }
            
            memcpy(entry->message, first_part, first_part_len);
            memcpy(entry->message + first_part_len, second_part, second_part_len);
        }
        
        entry->message[entry->length] = '\0';
        entry->next_offset = (offset + header_size + entry->length) % monitor->total_size;
        
        return true;
    }
    
    return false;
}

/* Read entries from tail to head, filtering by start_id */
static int read_entries_from_tail(dmlog_monitor_t* monitor, dmlog_index_t tail_offset,
                                  dmlog_index_t head_offset, dmlog_entry_id_t start_id,
                                  log_entry_t* entries, int max_entries) {
    int count = 0;
    dmlog_index_t current_offset = tail_offset;
    int iterations = 0;
    const int max_iterations = 1000;
    
    while (current_offset != head_offset && iterations < max_iterations && count < max_entries) {
        iterations++;
        
        log_entry_t entry;
        if (!read_entry_at_offset(monitor, current_offset, &entry)) {
            DEBUG_PRINT(monitor, "Failed to read entry at offset %u", current_offset);
            break;
        }
        
        if (entry.id >= start_id) {
            memcpy(&entries[count++], &entry, sizeof(log_entry_t));
        }
        
        current_offset = entry.next_offset;
        
        /* Safety check: if we've wrapped around past head, stop */
        if (iterations > 1 && current_offset == tail_offset) {
            break;
        }
    }
    
    return count;
}

/* Monitor ring buffer continuously */
static void monitor_loop(dmlog_monitor_t* monitor, int interval_us) {
    ring_control_t control;
    log_entry_t entries[DEFAULT_MAX_STARTUP_ENTRIES];
    
    printf("Monitoring dmlog ring buffer at 0x%08X\n", monitor->ring_addr);
    printf("Buffer size: %u bytes, max entry: %u bytes\n", 
           monitor->total_size, monitor->max_entry_size);
    printf("Press Ctrl+C to stop\n\n");
    
    /* Initialize - start from current position */
    if (!read_ring_control(monitor, &control)) {
        fprintf(stderr, "Failed to read ring buffer control\n");
        return;
    }
    
    /* Update total_size from control structure */
    monitor->total_size = control.buffer_size;
    
    printf("Buffer address: 0x%08X, size: %u bytes\n", 
           monitor->buffer_addr, monitor->total_size);
    
    monitor->last_id = control.latest_id;
    printf("Starting from log ID: %u, head: %u, tail: %u\n", 
           monitor->last_id, control.head_offset, control.tail_offset);
    
    /* Read and display existing entries on startup */
    if (monitor->last_id > 0 && control.tail_offset != control.head_offset) {
        uint32_t max_startup = monitor->max_startup_entries;
        dmlog_entry_id_t start_id = (monitor->last_id > max_startup) ? 
                                     (monitor->last_id - max_startup + 1) : 1;
        
        if (start_id > 1) {
            printf("Reading existing log entries (showing last %u)...\n\n", max_startup);
        } else {
            printf("Reading existing log entries...\n\n");
        }
        
        DEBUG_PRINT(monitor, "Reading entries from tail, filtering for id >= %u", start_id);
        
        int entry_count = read_entries_from_tail(monitor, control.tail_offset, 
                                                control.head_offset, start_id,
                                                entries, DEFAULT_MAX_STARTUP_ENTRIES);
        
        DEBUG_PRINT(monitor, "Found %d entries to display", entry_count);
        
        for (int i = 0; i < entry_count; i++) {
            printf("%s", entries[i].message);
            fflush(stdout);
        }
        
        if (entry_count > 0) {
            printf("\n");
        }
    }
    
    /* Main monitoring loop */
    while (g_running) {
        if (!read_ring_control(monitor, &control)) {
            /* Sleep in smaller chunks to be more responsive to Ctrl+C */
            for (int i = 0; i < interval_us / 10000 && g_running; i++) {
                usleep(10000); /* 10ms chunks */
            }
            usleep(interval_us % 10000);
            continue;
        }
        
        dmlog_entry_id_t latest_id = control.latest_id;
        
        if (latest_id != monitor->last_id) {
            DEBUG_PRINT(monitor, "latest_id changed from %u to %u", 
                       monitor->last_id, latest_id);
        }
        
        /* Check if there are new entries */
        if (latest_id > monitor->last_id) {
            DEBUG_PRINT(monitor, "Found new entries, reading from tail %u to head %u",
                       control.tail_offset, control.head_offset);
            
            dmlog_entry_id_t start_id = monitor->last_id + 1;
            int entry_count = read_entries_from_tail(monitor, control.tail_offset,
                                                    control.head_offset, start_id,
                                                    entries, DEFAULT_MAX_STARTUP_ENTRIES);
            
            /* Print entries in order */
            for (int i = 0; i < entry_count; i++) {
                printf("%s", entries[i].message);
                fflush(stdout);
            }
            
            monitor->last_id = latest_id;
        }
        
        /* Sleep in smaller chunks to be more responsive to Ctrl+C */
        for (int i = 0; i < interval_us / 10000 && g_running; i++) {
            usleep(10000); /* 10ms chunks */
        }
        if (g_running) {
            usleep(interval_us % 10000);
        }
    }
    
    printf("\n\nMonitoring stopped by user\n");
}

/* Print usage information */
static void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n\n", program_name);
    printf("Monitor dmlog ring buffer via OpenOCD\n\n");
    printf("Options:\n");
    printf("  --host HOST         OpenOCD host (default: %s)\n", DEFAULT_HOST);
    printf("  --port PORT         OpenOCD telnet port (default: %d)\n", DEFAULT_PORT);
    printf("  --addr ADDRESS      Ring buffer address in hex (default: 0x%08X)\n", DEFAULT_RING_ADDR);
    printf("  --size SIZE         Total buffer size in bytes (default: %u)\n", DEFAULT_TOTAL_SIZE);
    printf("  --max-entry SIZE    Maximum entry size in bytes (default: %u)\n", DEFAULT_MAX_ENTRY_SIZE);
    printf("  --interval SECONDS  Polling interval in seconds (default: 0.1)\n");
    printf("  --max-startup N     Maximum old entries to show on startup (default: %u)\n", DEFAULT_MAX_STARTUP_ENTRIES);
    printf("  --debug             Enable debug logging\n");
    printf("  --help              Show this help message\n");
}

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