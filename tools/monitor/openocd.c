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

        if(*offset + 4 > max_length)
        {
            TRACE_ERROR("Buffer overflow while parsing memory line. Offset: %lu max: %lu\n", *offset, max_length);
            TRACE_ERROR("current line: %s\n", ptr);
            return false;
        }

        // Store word in buffer in little-endian format
        uint32_t* dst = (uint32_t*)&buffer[*offset];
        *dst = word;
        *offset += 4;

        while(*ptr && !isspace((unsigned char)*ptr) && *ptr != '\n')
            ptr++;
    }

    return true;
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
    sprintf(line, "%s\n", cmd);
    print_command(cmd);
    ssize_t sent = send(socket, line, strlen(line), 0);
    if(sent < 0)
    {
        TRACE_ERROR("Failed to send command to OpenOCD\n");
        return -1;
    }
    
    char chunk[512] = {0};
    int total_received = 0;
    while(!contains_prompt(chunk, total_received))
    {
        memset(chunk, 0, sizeof(chunk));
        ssize_t received = recv(socket, chunk, sizeof(chunk) - 1, MSG_WAITFORONE);
        if(received < 0)
        {
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
    print_response(response, total_received);

    return 0;
}

int openocd_read_memory(int socket, uint32_t address, uint8_t *buffer, size_t length)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "mdw 0x%08X %zu", address, length / 4);
    char response[1024] = {0};
    if(openocd_send_command(socket, cmd, response, sizeof(response)) < 0)
    {
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

    return 0;
}
