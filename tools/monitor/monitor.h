#ifndef MONITOR_H
#define MONITOR_H

#include "dmlog.h"
#include "openocd.h"

typedef struct 
{
    dmlog_ring_t        ring;
    int                 socket;
    uint32_t            ring_address;
    dmlog_entry_t       current_entry;
    dmlog_index_t       tail_offset;
    dmlog_entry_id_t    last_entry_id;
    char                entry_buffer[DMOD_LOG_MAX_ENTRY_SIZE];
    bool                owns_busy_flag;
    void*               local_buffer;       // Local copy of the entire buffer
    size_t              local_buffer_size;  // Size of the local buffer
} monitor_ctx_t;

monitor_ctx_t* monitor_connect(opencd_addr_t *addr, uint32_t ring_address);
void monitor_disconnect(monitor_ctx_t *ctx);
bool monitor_update_ring(monitor_ctx_t *ctx);
bool monitor_wait_until_not_busy(monitor_ctx_t *ctx);
bool monitor_wait_for_new_data(monitor_ctx_t *ctx);
bool monitor_update_entry(monitor_ctx_t *ctx, bool blocking_mode);
const char* monitor_get_entry_buffer(monitor_ctx_t *ctx);
void monitor_run(monitor_ctx_t *ctx, bool show_timestamps, bool blocking_mode);
bool monitor_write_flags(monitor_ctx_t *ctx, uint32_t flags);
bool monitor_send_clear_command(monitor_ctx_t *ctx);
bool monitor_send_busy_command(monitor_ctx_t *ctx);
bool monitor_send_not_busy_command(monitor_ctx_t *ctx);
bool monitor_synchronize(monitor_ctx_t *ctx);

#endif // MONITOR_H