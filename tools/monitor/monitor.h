#ifndef MONITOR_H
#define MONITOR_H

#include "dmlog.h"
#include "openocd.h"

typedef struct 
{
    dmlog_ring_t    ring;
    int             socket;
    uint32_t        ring_address;
} monitor_ctx_t;

monitor_ctx_t* monitor_connect(opencd_addr_t *addr, uint32_t ring_address);
void monitor_disconnect(monitor_ctx_t *ctx);
bool monitor_update_ring(monitor_ctx_t *ctx);
bool monitor_wait_until_busy(monitor_ctx_t *ctx);

#endif // MONITOR_H