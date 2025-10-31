#include "dmlog.h"

struct dmlog_ctx
{
    dmlog_ring_t ring;
    char current_entry[DMOD_LOG_MAX_ENTRY_SIZE];
    uint8_t buffer[4];
};

dmlog_ctx_t dmlog_create(void *buffer, size_t buffer_size)
{
    dmlog_ctx_t ctx = buffer;

    if (buffer_size < sizeof(dmlog_ring_t))
        return NULL;

    ctx->ring.magic         = DMLOG_MAGIC_NUMBER;
    ctx->ring.latest_id     = 0;
    ctx->ring.flags         = 0;
    ctx->ring.head_offset   = 0;
    ctx->ring.tail_offset   = 0;
    ctx->ring.buffer_size   = (uint32_t)(buffer_size - sizeof(dmlog_ring_t));
    ctx->ring.buffer        = (uint8_t*)buffer + sizeof(dmlog_ring_t);

    return ctx;
}

void dmlog_destroy(dmlog_ctx_t ctx)
{
}

bool dmlog_putc(dmlog_ctx_t ctx, char c)
{

    return false;
}

bool dmlog_puts(dmlog_ctx_t ctx, const char *s)
{
    return false;
}

char dmlog_getc(dmlog_ctx_t ctx)
{
    return 0;
}

size_t dmlog_gets(dmlog_ctx_t ctx, char *s, size_t max_len)
{
    return 0;
}

size_t dmlog_available(dmlog_ctx_t ctx)
{
    return 0;
}

size_t dmlog_used(dmlog_ctx_t ctx)
{
    return 0;
}

void dmlog_clear(dmlog_ctx_t ctx)
{
}
