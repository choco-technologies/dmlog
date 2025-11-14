#ifndef MONITOR_H
#define MONITOR_H

#include "dmlog.h"
#include "backend.h"

typedef struct 
{
    dmlog_ring_t        ring;
    backend_ctx_t       backend_ctx;
    const backend_ops_t *backend_ops;
    uint64_t            ring_address;
    dmlog_index_t       tail_offset;
    char                entry_buffer[DMOD_LOG_MAX_ENTRY_SIZE];
    bool                owns_busy_flag;
    dmlog_ctx_t         dmlog_ctx;
    bool                snapshot_mode;
    size_t              snapshot_size;
    time_t              last_update_time;
} monitor_ctx_t;

monitor_ctx_t* monitor_connect(backend_addr_t *addr, backend_type_t backend_type, uint64_t ring_address, bool snapshot_mode);
void monitor_disconnect(monitor_ctx_t *ctx);
bool monitor_update_ring(monitor_ctx_t *ctx);
bool monitor_wait_until_not_busy(monitor_ctx_t *ctx);
bool monitor_wait_for_new_data(monitor_ctx_t *ctx);
bool monitor_update_entry(monitor_ctx_t *ctx, bool blocking_mode);
const char* monitor_get_entry_buffer(monitor_ctx_t *ctx);
bool monitor_load_snapshot(monitor_ctx_t *ctx, bool blocking_mode);
void monitor_run(monitor_ctx_t *ctx, bool show_timestamps, bool blocking_mode);
bool monitor_write_flags(monitor_ctx_t *ctx, uint32_t flags);
bool monitor_send_clear_command(monitor_ctx_t *ctx);
bool monitor_send_busy_command(monitor_ctx_t *ctx);
bool monitor_send_not_busy_command(monitor_ctx_t *ctx);
bool monitor_synchronize(monitor_ctx_t *ctx);
bool monitor_send_input(monitor_ctx_t *ctx, const char* input, size_t length);
bool monitor_handle_input_request(monitor_ctx_t *ctx);

#endif // MONITOR_H