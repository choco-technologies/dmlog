#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include "gdb.h"
#include "trace.h"
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>

const backend_addr_t gdb_default_addr = {
    .host = GDB_DEFAULT_HOST,
    .port = GDB_DEFAULT_PORT,
    .type = BACKEND_TYPE_GDB
};

/**
 * @brief Calculate checksum for GDB packet
 * 
 * @param data Packet data (without $ and #)
 * @param length Length of data
 * @return uint8_t Checksum value
 */
static uint8_t gdb_calculate_checksum(const char *data, size_t length)
{
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum += (uint8_t)data[i];
    }
    return checksum;
}

/**
 * @brief Send a GDB packet
 * 
 * @param socket Socket file descriptor
 * @param data Packet data (without $ and #checksum)
 * @return int 0 on success, -1 on failure
 */
static int gdb_send_packet(int socket, const char *data)
{
    size_t data_len = strlen(data);
    uint8_t checksum = gdb_calculate_checksum(data, data_len);
    
    // Format: $<data>#<checksum>
    char packet[4096];
    int packet_len = snprintf(packet, sizeof(packet), "$%s#%02x", data, checksum);
    
    TRACE_VERBOSE("GDB SEND: %s\n", packet);
    
    ssize_t sent = send(socket, packet, packet_len, 0);
    if (sent < 0) {
        TRACE_ERROR("Failed to send GDB packet\n");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Receive a GDB packet
 * 
 * @param socket Socket file descriptor
 * @param buffer Buffer to store received packet data (without $ and #checksum)
 * @param buffer_size Size of the buffer
 * @return int Length of received data on success, -1 on failure
 */
static int gdb_receive_packet(int socket, char *buffer, size_t buffer_size)
{
    char c;
    ssize_t n;
    
    // Wait for start of packet '$'
    do {
        n = recv(socket, &c, 1, 0);
        if (n <= 0) {
            TRACE_ERROR("Failed to receive GDB packet start\n");
            return -1;
        }
    } while (c != '$');
    
    // Read packet data until '#'
    size_t offset = 0;
    while (offset < buffer_size - 1) {
        n = recv(socket, &c, 1, 0);
        if (n <= 0) {
            TRACE_ERROR("Failed to receive GDB packet data\n");
            return -1;
        }
        
        if (c == '#') {
            break;
        }
        
        buffer[offset++] = c;
    }
    buffer[offset] = '\0';
    
    // Read checksum (2 hex digits)
    char checksum_str[3];
    n = recv(socket, checksum_str, 2, MSG_WAITALL);
    if (n != 2) {
        TRACE_ERROR("Failed to receive GDB packet checksum\n");
        return -1;
    }
    checksum_str[2] = '\0';
    
    // Verify checksum
    uint8_t received_checksum = (uint8_t)strtoul(checksum_str, NULL, 16);
    uint8_t calculated_checksum = gdb_calculate_checksum(buffer, offset);
    
    if (received_checksum != calculated_checksum) {
        TRACE_ERROR("GDB packet checksum mismatch: received %02x, calculated %02x\n", 
                    received_checksum, calculated_checksum);
        // Send NAK
        send(socket, "-", 1, 0);
        return -1;
    }
    
    // Send ACK
    send(socket, "+", 1, 0);
    
    TRACE_VERBOSE("GDB RECV: $%s#%02x\n", buffer, received_checksum);
    
    return (int)offset;
}

/**
 * @brief Send ACK and wait for acknowledgment
 * 
 * @param socket Socket file descriptor
 * @return int 0 on success, -1 on failure
 */
static int gdb_wait_for_ack(int socket)
{
    char ack;
    ssize_t n = recv(socket, &ack, 1, 0);
    if (n <= 0) {
        TRACE_ERROR("Failed to receive GDB acknowledgment\n");
        return -1;
    }
    
    if (ack == '+') {
        return 0;
    } else if (ack == '-') {
        TRACE_ERROR("GDB server sent NAK\n");
        return -1;
    }
    
    TRACE_WARN("Unexpected GDB response: %c\n", ack);
    return -1;
}

/**
 * @brief Check if a packet is a stop reply (S or T packet)
 * 
 * Stop replies indicate that the target has stopped execution.
 * Format: "S<signal>" or "T<signal>..." where signal is a 2-digit hex number
 * Examples: "S02", "S05", "T05", etc.
 * 
 * @param packet Packet data
 * @return bool True if packet is a stop reply
 */
static bool is_stop_reply(const char *packet)
{
    if (packet == NULL || packet[0] == '\0') {
        return false;
    }
    
    // Check for S or T packet (stop replies)
    if (packet[0] == 'S' || packet[0] == 'T') {
        // Must have at least 2 more characters (signal number)
        if (strlen(packet) >= 3 && isxdigit(packet[1]) && isxdigit(packet[2])) {
            return true;
        }
    }
    
    return false;
}

/**
 * @brief Drain any pending packets from socket with timeout
 * 
 * This function reads and discards any pending data on the socket
 * to clear out unexpected packets (like unsolicited stop replies).
 * Uses non-blocking I/O with a short timeout to avoid hanging.
 * 
 * @param socket Socket file descriptor
 * @return int Number of packets drained, or -1 on error
 */
static int gdb_drain_pending_packets(int socket)
{
    int count = 0;
    struct timeval tv;
    fd_set readfds;
    
    // Try to read pending packets for up to 100ms
    for (int attempt = 0; attempt < 5; attempt++) {
        FD_ZERO(&readfds);
        FD_SET(socket, &readfds);
        
        tv.tv_sec = 0;
        tv.tv_usec = 20000; // 20ms timeout
        
        int ret = select(socket + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            TRACE_ERROR("select() failed while draining packets\n");
            return -1;
        } else if (ret == 0) {
            // No more data available
            break;
        }
        
        // Data available, try to read it
        char buffer[256];
        int len = gdb_receive_packet(socket, buffer, sizeof(buffer));
        if (len < 0) {
            // Error or no complete packet, stop draining
            break;
        }
        
        TRACE_VERBOSE("Drained pending packet: %s\n", buffer);
        count++;
    }
    
    if (count > 0) {
        TRACE_INFO("Drained %d pending packet(s)\n", count);
    }
    
    return count;
}

/**
 * @brief Connect to GDB server
 * 
 * @param addr Pointer to gdb_addr_t structure with host and port
 * @return int Socket file descriptor on success, -1 on failure
 */
int gdb_connect(const backend_addr_t *addr)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP
    };
    struct addrinfo *res = NULL;
    struct addrinfo *rp = NULL;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", addr->port);

    if (getaddrinfo(addr->host, port_str, &hints, &res) != 0) {
        TRACE_ERROR("getaddrinfo failed for %s:%s\n", addr->host, port_str);
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        int sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0)
            continue;

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            freeaddrinfo(res);
            
            // Note: NoAck mode is disabled because it requires tracking state
            // and would complicate the implementation. ACK mode is reliable enough.
            
            TRACE_INFO("Connected to GDB server at %s:%d\n", addr->host, addr->port);
            
            // Drain any pending packets (like unsolicited stop replies)
            // This handles cases where GDB sends S02 (SIGINT) at connection time
            gdb_drain_pending_packets(sock);
            
            // Send continue command to start/resume the target
            // When gdbserver starts a process, it stops at entry point
            // We need to continue it so the program can run and initialize
            // gdb_continue() will run the target then interrupt it after a delay
            if (gdb_continue(sock) < 0) {
                TRACE_ERROR("Failed to continue target execution\n");
                close(sock);
                return -1;
            }
            
            return sock;
        }

        close(sock);
    }

    freeaddrinfo(res);
    TRACE_ERROR("Failed to connect to GDB server at %s:%s\n", addr->host, port_str);
    return -1;
}

/**
 * @brief Disconnect from GDB server
 * 
 * @param socket Socket file descriptor
 * @return int 0 on success
 */
int gdb_disconnect(int socket)
{
    close(socket);
    TRACE_INFO("Disconnected from GDB server\n");
    return 0;
}

/**
 * @brief Send continue command to GDB server
 * 
 * Sends 'c' command to resume execution of the target.
 * This is needed when gdbserver starts a process - it stops at entry point.
 * 
 * Note: After sending 'c', we use Ctrl-C (0x03) to interrupt the target
 * so it stops and we can read memory. This is the standard GDB protocol.
 * 
 * @param socket Socket file descriptor
 * @return int 0 on success, -1 on failure
 */
int gdb_continue(int socket)
{
    // Send continue command
    if (gdb_send_packet(socket, "c") < 0) {
        return -1;
    }
    
    // Note: GDB protocol does NOT send an ACK after 'c' command
    // The server will only respond when the target stops
    
    // The 'c' command causes gdbserver to run the target and wait for it to stop.
    // We need to let the program run for a bit, then interrupt it so we can
    // read memory. Send Ctrl-C (0x03) to interrupt.
    usleep(1000000); // Let it run for 1 second to initialize and write data
    
    char interrupt = 0x03;
    if (send(socket, &interrupt, 1, 0) != 1) {
        TRACE_ERROR("Failed to send interrupt to GDB server\n");
        return -1;
    }
    
    // Wait for the stop reply (e.g., "T05" or "S05")
    char stop_reply[256];
    int len = gdb_receive_packet(socket, stop_reply, sizeof(stop_reply));
    if (len < 0) {
        TRACE_ERROR("Failed to receive stop reply from GDB server\n");
        return -1;
    }
    
    TRACE_INFO("Target started and stopped, ready for memory access\n");
    return 0;
}

// Static flag to track if target is running
static bool target_is_running = false;

/**
 * @brief Interrupt the running target
 * 
 * @param socket Socket file descriptor
 * @return int 0 on success, -1 on failure
 */
static int gdb_interrupt(int socket)
{
    if (!target_is_running) {
        return 0; // Already stopped
    }
    
    // Send interrupt (Ctrl-C)
    char interrupt = 0x03;
    if (send(socket, &interrupt, 1, 0) != 1) {
        TRACE_ERROR("Failed to send interrupt to GDB server\n");
        return -1;
    }
    
    // Wait for the stop reply (e.g., "T05" or "S05")
    char stop_reply[256];
    int len = gdb_receive_packet(socket, stop_reply, sizeof(stop_reply));
    if (len < 0) {
        TRACE_ERROR("Failed to receive stop reply from GDB server\n");
        return -1;
    }
    
    target_is_running = false;
    TRACE_VERBOSE("Target interrupted\n");
    return 0;
}

/**
 * @brief Resume target execution
 * 
 * Sends 'c' (continue) command to resume target execution. The target will
 * run continuously until interrupted. This allows the firmware to process
 * input, generate output, and handle the interactive shell.
 * 
 * @param socket Socket file descriptor
 * @return int 0 on success, -1 on failure
 */
static int gdb_resume(int socket)
{
    if (target_is_running) {
        return 0; // Already running
    }
    
    // Send continue command
    if (gdb_send_packet(socket, "c") < 0) {
        return -1;
    }
    
    // Note: GDB protocol does NOT send an ACK after 'c' command
    // The server will only respond when the target stops (with a stop reply packet)
    // We should NOT wait for an ACK here - it will block indefinitely
    
    target_is_running = true;
    TRACE_VERBOSE("Target resumed\n");
    return 0;
}

/**
 * @brief Resume target after writing input to allow firmware to process it
 * 
 * This function resumes target execution after input data has been written.
 * The target will continue running, allowing the firmware to process the input,
 * print output, and wait for the next input request.
 * 
 * @param socket Socket file descriptor
 * @return int 0 on success, -1 on failure
 */
int gdb_resume_briefly(int socket)
{
    return gdb_resume(socket);
}

/**
 * @brief Decode run-length encoding in GDB response
 * 
 * GDB uses run-length encoding: '*' followed by a character indicates
 * repetition. The repeat count is the character minus 29.
 * For example: "0*3" means "0000" (repeat '0' three times)
 * 
 * @param input Input string with run-length encoding
 * @param output Output buffer for decoded string
 * @param output_size Size of output buffer
 * @return int Length of decoded string, or -1 on error
 */
static int gdb_decode_rle(const char *input, char *output, size_t output_size)
{
    size_t in_idx = 0;
    size_t out_idx = 0;
    
    while (input[in_idx] != '\0' && out_idx < output_size - 1) {
        if (input[in_idx] == '*') {
            // Run-length encoding: next char - 29 = repeat count
            in_idx++;
            if (input[in_idx] == '\0') {
                TRACE_ERROR("Invalid RLE encoding: unexpected end of input\n");
                return -1; // Invalid RLE
            }
            int repeat_count = (unsigned char)input[in_idx] - 29;
            if (repeat_count <= 0 || out_idx == 0) {
                TRACE_ERROR("Invalid RLE encoding: invalid repeat count\n");
                return -1; // Invalid repeat count
            }
            
            // Repeat the previous character
            char prev_char = output[out_idx - 1];
            for (int i = 0; i < repeat_count && out_idx < output_size - 1; i++) {
                output[out_idx++] = prev_char;
            }
            in_idx++;
        } else {
            // Regular character
            output[out_idx++] = input[in_idx++];
        }
    }
    
    output[out_idx] = '\0';
    return out_idx;
}

/**
 * @brief Read memory from target via GDB server
 * 
 * Uses GDB Remote Serial Protocol 'm' command: m addr,length
 * Returns data as hex string
 * 
 * @param socket Socket file descriptor
 * @param address Memory address to read from
 * @param buffer Buffer to store read data
 * @param length Number of bytes to read
 * @return int 0 on success, -1 on failure
 */
int gdb_read_memory(int socket, uint64_t address, void *buffer, size_t length)
{
    // Interrupt target if it's running to allow memory access
    bool was_running = target_is_running;
    if (was_running) {
        if (gdb_interrupt(socket) < 0) {
            return -1;
        }
    }
    
    // Format: m<addr>,<length>
    char command[64];
    snprintf(command, sizeof(command), "m%lx,%zx", address, length);
    
    if (gdb_send_packet(socket, command) < 0) {
        // Resume if we interrupted
        if (was_running) gdb_resume(socket);
        return -1;
    }
    
    if (gdb_wait_for_ack(socket) < 0) {
        // Resume if we interrupted
        if (was_running) gdb_resume(socket);
        return -1;
    }
    
    // Receive response
    char response[8192];
    int response_len = gdb_receive_packet(socket, response, sizeof(response));
    if (response_len < 0) {
        // Resume if we interrupted
        if (was_running) gdb_resume(socket);
        return -1;
    }
    
    // Check if we received a stop reply instead of memory data
    // This can happen if the target sends an asynchronous stop signal (like S02 for SIGINT)
    if (is_stop_reply(response)) {
        TRACE_WARN("Received stop reply '%s' instead of memory data, retrying...\n", response);
        
        // Drain any other pending packets
        gdb_drain_pending_packets(socket);
        
        // Retry the memory read command
        if (gdb_send_packet(socket, command) < 0) {
            if (was_running) gdb_resume(socket);
            return -1;
        }
        
        if (gdb_wait_for_ack(socket) < 0) {
            if (was_running) gdb_resume(socket);
            return -1;
        }
        
        // Receive the actual response
        response_len = gdb_receive_packet(socket, response, sizeof(response));
        if (response_len < 0) {
            if (was_running) gdb_resume(socket);
            return -1;
        }
    }
    
    // Check for error response
    if (response[0] == 'E') {
        TRACE_ERROR("GDB read memory error: %s\n", response);
        // Resume if we interrupted
        if (was_running) gdb_resume(socket);
        return -1;
    }
    
    // Decode run-length encoding
    char decoded[8192];
    int decoded_len = gdb_decode_rle(response, decoded, sizeof(decoded));
    if (decoded_len < 0) {
        TRACE_ERROR("Failed to decode RLE in GDB response\n");
        // Resume if we interrupted
        if (was_running) gdb_resume(socket);
        return -1;
    }
    
    // Parse hex data
    if ((size_t)decoded_len < length * 2) {
        TRACE_ERROR("GDB response too short after decode: expected %zu hex chars, got %d. Response len: %d Response: %s\n", length * 2, decoded_len, response_len, response);
        // Resume if we interrupted
        if (was_running) gdb_resume(socket);
        return -1;
    }
    
    uint8_t *buf = (uint8_t *)buffer;
    for (size_t i = 0; i < length; i++) {
        char hex_byte[3];
        hex_byte[0] = decoded[i * 2];
        hex_byte[1] = decoded[i * 2 + 1];
        hex_byte[2] = '\0';
        buf[i] = (uint8_t)strtoul(hex_byte, NULL, 16);
    }
    
    // Resume target if we interrupted it
    if (was_running) {
        gdb_resume(socket);
    }
    
    return 0;
}

/**
 * @brief Write memory to target via GDB server
 * 
 * Uses GDB Remote Serial Protocol 'M' command: M addr,length:data
 * Data is sent as hex string
 * 
 * @param socket Socket file descriptor
 * @param address Memory address to write to
 * @param buffer Buffer containing data to write
 * @param length Number of bytes to write
 * @return int 0 on success, -1 on failure
 */
int gdb_write_memory(int socket, uint64_t address, const void *buffer, size_t length)
{
    // Interrupt target if it's running to allow memory access
    bool was_running = target_is_running;
    if (was_running) {
        if (gdb_interrupt(socket) < 0) {
            return -1;
        }
    }
    
    // Format: M<addr>,<length>:<hex data>
    // Maximum packet size is typically 16KB, so we may need to split large writes
    const size_t MAX_WRITE_SIZE = 1024; // Conservative limit
    
    const uint8_t *buf = (const uint8_t *)buffer;
    size_t offset = 0;
    
    while (offset < length) {
        size_t chunk_size = (length - offset > MAX_WRITE_SIZE) ? MAX_WRITE_SIZE : (length - offset);
        
        char command[4096];
        int cmd_len = snprintf(command, sizeof(command), "M%lx,%lx:", address + offset, (unsigned long)chunk_size);
        
        // Append hex data
        for (size_t i = 0; i < chunk_size; i++) {
            cmd_len += snprintf(command + cmd_len, sizeof(command) - cmd_len, "%02x", buf[offset + i]);
        }
        
        if (gdb_send_packet(socket, command) < 0) {
            // Resume if we interrupted
            if (was_running) gdb_resume(socket);
            return -1;
        }
        
        if (gdb_wait_for_ack(socket) < 0) {
            // Resume if we interrupted
            if (was_running) gdb_resume(socket);
            return -1;
        }
        
        // Receive response (should be "OK")
        char response[256];
        int response_len = gdb_receive_packet(socket, response, sizeof(response));
        if (response_len < 0) {
            // Resume if we interrupted
            if (was_running) gdb_resume(socket);
            return -1;
        }
        
        if (strcmp(response, "OK") != 0) {
            TRACE_ERROR("GDB write memory failed: %s\n", response);
            // Resume if we interrupted
            if (was_running) gdb_resume(socket);
            return -1;
        }
        
        offset += chunk_size;
    }
    
    // Resume target if we interrupted it
    if (was_running) {
        gdb_resume(socket);
    }
    
    return 0;
}