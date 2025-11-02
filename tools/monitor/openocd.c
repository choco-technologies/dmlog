#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include "openocd.h"
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
 * @brief Check if character is a hexadecimal digit
 * 
 * @param c Character to check
 * @return true if c is a hex digit, false otherwise
 */
static bool ishexchar(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/**
 * @brief Print response buffer in a readable format
 * 
 * @param buffer Pointer to the response buffer
 * @param n Number of bytes in the buffer
 */
static void print_response(const uint8_t *buffer, ssize_t n)
{
    TRACE_VERBOSE("\tRECV < ");
    for(ssize_t i = 0; i < n; i++)
    {
        if(buffer[i] == '\r')
        {
            continue;
        }
        else if(buffer[i] == '\n')
        {
            TRACE_VERBOSE("%c", buffer[i]);
            TRACE_VERBOSE("\tRECV < ");
            continue;
        }
        else if(isprint(buffer[i]))
        {
            TRACE_VERBOSE("%c", buffer[i]);
        }
        else
        {
            TRACE_VERBOSE("%02x ", buffer[i]);
        }
    }
    TRACE_VERBOSE("\n");
}

/**
 * @brief Print command being sent to OpenOCD
 * 
 * @param cmd Command string
 */
static void print_command(const char *cmd)
{
    TRACE_VERBOSE("\tSEND > %s\n", cmd);
}

/**
 * @brief Check if buffer contains OpenOCD prompt '>'
 * 
 * @param buffer Pointer to the response buffer
 * @param n Number of bytes in the buffer
 * @return true if prompt is found, false otherwise
 */
static bool contains_prompt(const char *buffer, ssize_t n)
{
    for(ssize_t i = 0; i < n; i++)
    {
        if(buffer[i] == '>')
            return true;
    }
    return false;
}

/**
 * @brief Parse a line of memory dump and store it into buffer
 * 
 * @param line Line of memory dump from OpenOCD
 * @param buffer Buffer to store parsed memory
 * @param offset Current offset in the buffer
 * @param max_length Maximum length of the buffer
 * @return true on success, false on failure
 */
static bool parse_memory_line(const char *line, uint8_t *buffer, size_t *offset, size_t max_length)
{
    // Example line: 0x20000000: 12345678 9abcdef0 11223344 55667788
    const char *ptr = line;
    while(*ptr && *ptr != ':')
    {
        ptr++;
    }
    if(*ptr != ':')
    {
        return false;
    }
    ptr++; // Skip ':'

    while(*ptr && *ptr != '\n')
    {
        while(*ptr && isspace((unsigned char)*ptr))
        {
            if(*ptr == '\n')
                return true;
            ptr++;
        }
        char c = *ptr;
        if(!c || ishexchar(c) == 0)
            break;

        uint32_t word = 0;
        if(sscanf(ptr, "%8x", &word) != 1)
        {
            TRACE_ERROR("Failed to parse word in memory line\n");
            return false;
        }

        if(*offset + 4 <= max_length)
        {
            // Store word in buffer in little-endian format
            uint32_t* dst = (uint32_t*)&buffer[*offset];
            *dst = word;
            *offset += 4;
        }
        else if(*offset + 3 <= max_length)
        {
            // Store lower 3 bytes
            uint8_t* dst = (uint8_t*)&buffer[*offset];
            dst[0] = (uint8_t)(word & 0xFF);
            dst[1] = (uint8_t)((word >> 8) & 0xFF);
            dst[2] = (uint8_t)((word >> 16) & 0xFF);
            *offset += 3;
        }
        else if(*offset + 2 <= max_length)
        {
            // Store lower 2 bytes
            uint16_t* dst = (uint16_t*)&buffer[*offset];
            *dst = (uint16_t)(word & 0xFFFF);
            *offset += 2;
        }
        else if(*offset + 1 <= max_length)
        {
            // Store lower byte
            buffer[*offset] = (uint8_t)(word & 0xFF);
            (*offset)++;
        }
        else
        {
            TRACE_ERROR("Buffer overflow while parsing memory line\n");
            return false;
        }


        while(*ptr && !isspace((unsigned char)*ptr) && *ptr != '\n')
            ptr++;
    }

    return true;
}

/**
 * @brief Read a line from the OpenOCD socket
 * 
 * @param socket Socket file descriptor
 * @param buffer Buffer to store the line
 * @param max_length Maximum length of the buffer
 * @return number of bytes read on success, -1 on failure
 */
static size_t read_line(int socket, char *buffer, size_t max_length)
{
    size_t offset = 0;
    while(offset < max_length - 1)
    {
        ssize_t n = recv(socket, &buffer[offset], 1, MSG_WAITFORONE);
        if(n <= 0)
        {
            TRACE_ERROR("Failed to read line from OpenOCD\n");
            return -1;
        }
        if(buffer[offset] == '\n' || buffer[offset] == '>')
        {
            offset++;
            break;
        }
        offset++;
    }
    print_response((uint8_t *)buffer, offset);
    return offset;
}

/**
 * @brief Connect to OpenOCD server
 * 
 * @param addr Pointer to opencd_addr_t structure with host and port
 * @return int Socket file descriptor on success, -1 on failure
 */
int openocd_connect(opencd_addr_t *addr)
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

    if(getaddrinfo(addr->host, port_str, &hints, &res) != 0)
    {
        fprintf(stderr, "getaddrinfo failed for %s:%s\n", addr->host, port_str);
        return -1;
    }

    for(rp = res; rp != NULL; rp = rp->ai_next)
    {
        int sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(sock < 0)
            continue;

        if(connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
        {
            freeaddrinfo(res);
            if(openocd_read_welcome(sock) < 0)
            {
                close(sock);
                return -1;
            }
            TRACE_INFO("Connected to OpenOCD at %s:%d\n", addr->host, addr->port);
            return sock;
        }

        close(sock);
    }

    freeaddrinfo(res);
    TRACE_ERROR("Failed to connect to %s:%s\n", addr->host, port_str);

    return -1;
}

/**
 * @brief Read welcome message from OpenOCD server
 * 
 * @param socket Socket file descriptor
 * @return int 0 on success, -1 on failure
 */
int openocd_read_welcome(int socket)
{
    uint8_t buffer[256];
    ssize_t n = recv(socket, buffer, sizeof(buffer) - 1, 0);
    if(n < 0)
    {
        TRACE_ERROR("Failed to read welcome message from OpenOCD\n");
        return -1;
    }
    TRACE_VERBOSE("OpenOCD Welcome Message: \n");
    print_response(buffer, n);

    while(!contains_prompt((char *)buffer, n))
    {
        memset(buffer, 0, sizeof(buffer));
        // Read until no more data
        n = recv(socket, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
        if(n > 0)
        {
            print_response(buffer, n);
        }
    };

    return 0;
}

/**
 * @brief Disconnect from OpenOCD server
 * 
 * @param socket Socket file descriptor
 * @return int 0 on success
 */
int openocd_disconnect(int socket)
{
    close(socket);
    return 0;
}

/**
 * @brief Send command to OpenOCD server and receive response
 * 
 * @param socket Socket file descriptor
 * @param cmd Command string to send
 * @param response Buffer to store the response
 * @param response_size Size of the response buffer
 * @return int 0 on success, -1 on failure
 */
int openocd_send_command(int socket, const char *cmd, char *response, size_t response_size)
{
    char line[512];
    sprintf(line, "%s\r\n", cmd);
    print_command(cmd);
    size_t cmd_len = strlen(line);
    ssize_t sent = send(socket, line, cmd_len, 0);
    if(sent < 0)
    {
        TRACE_ERROR("Failed to send command to OpenOCD\n");
        return -1;
    }
    
    bool echo_received = false;
    char chunk[512] = {0};
    int total_received = 0;
    while(!contains_prompt(chunk, total_received))
    {
        memset(chunk, 0, sizeof(chunk));
        ssize_t received = read_line(socket, chunk, sizeof(chunk));
        if(received < 0)
        {
            continue;
        }
        if(!echo_received && strstr(chunk, cmd))
        {
            // Skip echo line
            echo_received = true;
            continue;
        }
        if(!echo_received)
        {
            // Still waiting for echo
            continue;
        }
        if(total_received + received < response_size)
        {
            memcpy(response + total_received, chunk, received);
            total_received += received;
        }
        else
        {
            TRACE_ERROR("Response buffer overflow\n");
            return -1;
        }
    }

    return 0;
}

/**
 * @brief Read memory from target via OpenOCD
 * 
 * @param socket Socket file descriptor
 * @param address Memory address to read from
 * @param buffer Buffer to store read data
 * @param length Number of bytes to read
 * @return int 0 on success, -1 on failure
 */
int openocd_read_memory(int socket, uint32_t address, void *buffer, size_t length)
{
    char cmd[64];
    size_t word_count = (length + 3) / 4; // Number of 32-bit words to read
    snprintf(cmd, sizeof(cmd), "mdw 0x%08X %zu", address, word_count);
    size_t response_size = length * 5 + 128;
    char* response = malloc(response_size); // Allocate enough space for response
    if(response == NULL)
    {
        TRACE_ERROR("Failed to allocate memory for response\n");
        return -1;
    }
    memset(response, 0, response_size);

    if(openocd_send_command(socket, cmd, response, response_size) < 0)
    {
        free(response);
        return -1;
    }

    char* data = response;
    while(*data)
    {
        data++;
    }
    while(!isdigit((unsigned char)*data))
    {
        data++;
    }
    size_t offset = 0;
    while(parse_memory_line(data, buffer, &offset, length))
    {
        while(*data && *data != '\n')
        {
            data++;
        }
        if(*data == '\n')
        {
            data++;
        }
    }

    free(response);
    return 0;
}

/**
 * @brief Write memory to target via OpenOCD
 * 
 * @param socket Socket file descriptor
 * @param address Memory address to write to
 * @param buffer Buffer containing data to write
 * @param length Number of bytes to write
 * @return int 0 on success, -1 on failure
 */
int openocd_write_memory(int socket, uint32_t address, const void *buffer, size_t length)
{
    size_t word_count = (length + 3) / 4; // Number of 32-bit words to write
    char* cmd = malloc(64 + word_count * 9); // "mww " + address + count + words
    if(cmd == NULL)
    {
        TRACE_ERROR("Failed to allocate memory for write command\n");
        return -1;
    }

    size_t offset = 0;
    size_t cmd_offset = snprintf(cmd, 64, "mww 0x%08X %zu", address, word_count);
    while(offset < length)
    {
        uint32_t word = 0;
        size_t bytes_to_copy = (length - offset >= 4) ? 4 : (length - offset);
        memcpy(&word, (const uint8_t*)buffer + offset, bytes_to_copy);
        cmd_offset += snprintf(cmd + cmd_offset, 10, " %08X", word);
        offset += bytes_to_copy;
    }

    char response[256] = {0};
    int result = 0;
    if(openocd_send_command(socket, cmd, response, sizeof(response)) < 0)
    {
        TRACE_ERROR("Failed to send write memory command\n");
        result = -1;
    }

    free(cmd);
    return result;
}
