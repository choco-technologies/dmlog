#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include "openocd.h"
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

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
    fprintf(stderr, "Failed to connect to %s:%s\n", addr->host, port_str);

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
        fprintf(stderr, "Failed to read welcome message from OpenOCD\n");
        return -1;
    }
    printf("OpenOCD Welcome Message: \n");
    for(ssize_t i = 0; i < n; i++)
    {
        printf("%02x ", buffer[i]);
    }
    putchar('\n');
    return 0;
}

int openocd_disconnect(int socket)
{
    return 0;
}
