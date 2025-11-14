#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include "gdbserver.h"
#include "trace.h"
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>

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
    n = recv(socket, checksum_str, 2, 0);
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
 * @brief Connect to GDB server
 * 
 * @param addr Pointer to gdb_addr_t structure with host and port
 * @return int Socket file descriptor on success, -1 on failure
 */
int gdb_connect(gdb_addr_t *addr)
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
            
            // Disable ACK mode for faster communication (optional)
            // Send QStartNoAckMode packet
            char response[256];
            if (gdb_send_packet(sock, "QStartNoAckMode") == 0) {
                if (gdb_receive_packet(sock, response, sizeof(response)) > 0) {
                    if (strcmp(response, "OK") == 0) {
                        TRACE_INFO("GDB NoAck mode enabled\n");
                    }
                }
            }
            
            TRACE_INFO("Connected to GDB server at %s:%d\n", addr->host, addr->port);
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
int gdb_read_memory(int socket, uint32_t address, void *buffer, size_t length)
{
    // Format: m<addr>,<length>
    char command[64];
    snprintf(command, sizeof(command), "m%x,%zx", address, length);
    
    if (gdb_send_packet(socket, command) < 0) {
        return -1;
    }
    
    if (gdb_wait_for_ack(socket) < 0) {
        return -1;
    }
    
    // Receive response
    char response[8192];
    int response_len = gdb_receive_packet(socket, response, sizeof(response));
    if (response_len < 0) {
        return -1;
    }
    
    // Check for error response
    if (response[0] == 'E') {
        TRACE_ERROR("GDB read memory error: %s\n", response);
        return -1;
    }
    
    // Parse hex data
    if ((size_t)response_len < length * 2) {
        TRACE_ERROR("GDB response too short: expected %zu bytes, got %d\n", length * 2, response_len);
        return -1;
    }
    
    uint8_t *buf = (uint8_t *)buffer;
    for (size_t i = 0; i < length; i++) {
        char hex_byte[3];
        hex_byte[0] = response[i * 2];
        hex_byte[1] = response[i * 2 + 1];
        hex_byte[2] = '\0';
        buf[i] = (uint8_t)strtoul(hex_byte, NULL, 16);
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
int gdb_write_memory(int socket, uint32_t address, const void *buffer, size_t length)
{
    // Format: M<addr>,<length>:<hex data>
    // Maximum packet size is typically 16KB, so we may need to split large writes
    const size_t MAX_WRITE_SIZE = 1024; // Conservative limit
    
    const uint8_t *buf = (const uint8_t *)buffer;
    size_t offset = 0;
    
    while (offset < length) {
        size_t chunk_size = (length - offset > MAX_WRITE_SIZE) ? MAX_WRITE_SIZE : (length - offset);
        
        char command[4096];
        int cmd_len = snprintf(command, sizeof(command), "M%x,%lx:", address + (uint32_t)offset, (unsigned long)chunk_size);
        
        // Append hex data
        for (size_t i = 0; i < chunk_size; i++) {
            cmd_len += snprintf(command + cmd_len, sizeof(command) - cmd_len, "%02x", buf[offset + i]);
        }
        
        if (gdb_send_packet(socket, command) < 0) {
            return -1;
        }
        
        if (gdb_wait_for_ack(socket) < 0) {
            return -1;
        }
        
        // Receive response (should be "OK")
        char response[256];
        int response_len = gdb_receive_packet(socket, response, sizeof(response));
        if (response_len < 0) {
            return -1;
        }
        
        if (strcmp(response, "OK") != 0) {
            TRACE_ERROR("GDB write memory failed: %s\n", response);
            return -1;
        }
        
        offset += chunk_size;
    }
    
    return 0;
}
