#ifndef OPENOCD_H
#define OPENOCD_H

#define OPENOCD_DEFAULT_HOST    "localhost"
#define OPENOCD_DEFAULT_PORT    4444

typedef struct 
{
    char host[256];
    int port;
} opencd_addr_t;

extern int openocd_connect(opencd_addr_t *addr);
extern int openocd_read_welcome(int socket);
extern int openocd_disconnect(int socket);

#endif // OPENOCD_H