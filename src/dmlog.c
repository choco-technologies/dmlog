#include "dmlog.h"
#include "dmod.h"
#include <string.h>
#include <stdarg.h>

struct dmlog_ctx
{
    dmlog_ring_t ring;
    char write_buffer[DMOD_LOG_MAX_ENTRY_SIZE];
    dmlog_index_t write_entry_offset;
    char read_buffer[DMOD_LOG_MAX_ENTRY_SIZE];
    dmlog_index_t read_entry_offset;
    dmlog_entry_id_t next_id;
    uint32_t lock_recursion;
    uint8_t buffer[4];
};

/* Default DMLoG context */
static dmlog_ctx_t default_ctx = NULL;

/**
 * @brief Lock the DMLoG context for exclusive access.
 * 
 * @param ctx DMLoG context.
 */
static void context_lock(dmlog_ctx_t ctx)
{
    volatile uint32_t timeout = 10000;
    while(ctx->lock_recursion == 0 && (ctx->ring.flags & DMLOG_FLAG_BUSY) && timeout > 0)
    {
        timeout--;
    }
    ctx->ring.flags |= DMLOG_FLAG_BUSY;
    ctx->lock_recursion++;
}

/**
 * @brief Unlock the DMLoG context.
 * 
 * @param ctx DMLoG context.
 */
static void context_unlock(dmlog_ctx_t ctx)
{   
    if(ctx->lock_recursion > 0)
    {
        ctx->lock_recursion--;
    }
    if(ctx->lock_recursion == 0)
    {
        ctx->ring.flags &= ~DMLOG_FLAG_BUSY;
    }
}

/**
 * @brief Wait until the DMLoG context is unlocked.
 * 
 * @param ctx DMLoG context.
 */
static void wait_for_unlock(dmlog_ctx_t ctx)
{
    if(ctx->lock_recursion > 0)
    {
        return; // Already locked by this context
    }
    while(ctx->ring.flags & DMLOG_FLAG_BUSY)
    {
        // Busy wait
    }
}

/**
 * @brief Get the amount of free space in the ring buffer.
 * 
 * @param ctx DMLoG context.
 * @return dmlog_index_t Number of free bytes in the ring buffer.
 */
static dmlog_index_t get_free_space(dmlog_ctx_t ctx)
{
    dmlog_index_t free_space = 0;
    if(ctx->ring.head_offset >= ctx->ring.tail_offset)
    {
        free_space = ctx->ring.buffer_size - (ctx->ring.head_offset - ctx->ring.tail_offset);
    }
    else
    {
        free_space = ctx->ring.tail_offset - ctx->ring.head_offset;
    }
    return free_space;  
}

/**
 * @brief Read a single byte from the tail of the ring buffer.
 * 
 * @param ctx DMLoG context.
 * @param out_byte Pointer to store the read byte.
 * @return true if the buffer is empty, false otherwise.
 */
static bool read_byte_from_tail(dmlog_ctx_t ctx, void* out_byte)
{
    bool empty = (ctx->ring.tail_offset == ctx->ring.head_offset);
    if(!empty)
    {
        *((uint8_t*)out_byte) = ctx->buffer[ctx->ring.tail_offset];
        ctx->ring.tail_offset = (ctx->ring.tail_offset + 1) % ctx->ring.buffer_size;
    }
    return empty;
}

/**
 * @brief Write a single byte to the head of the ring buffer.
 * 
 * @param ctx DMLoG context.
 * @param byte Byte to write.
 * @return true on success, false if the buffer is full.
 */
static bool write_byte_to_tail(dmlog_ctx_t ctx, uint8_t byte)
{
    dmlog_index_t next_head = (ctx->ring.head_offset + 1) % ctx->ring.buffer_size;
    if(next_head == ctx->ring.tail_offset)
    {
        // Buffer full
        return false;
    }
    ctx->buffer[ctx->ring.head_offset] = byte;
    ctx->ring.head_offset = next_head;
    return true;
}

/**
 * @brief Read a log entry from the tail of the ring buffer.
 * 
 * @param ctx DMLoG context.
 * @param out_entry Pointer to store the read log entry header.
 * @param out_data Pointer to store the read log entry data.
 * @param max_data_len Maximum length of the data buffer.
 * @return true on success, false if the buffer is empty or corrupted.
 */
static bool read_entry_from_tail(dmlog_ctx_t ctx, dmlog_entry_t* out_entry, char* out_data, size_t max_data_len)
{
    if(ctx->ring.tail_offset == ctx->ring.head_offset)
    {
        return false; // Buffer empty
    }

    uint8_t* entry_ptr = (uint8_t*)out_entry;
    size_t header_size = sizeof(dmlog_entry_t); 
    for(size_t i = 0; i < header_size; i++)
    {
        if(read_byte_from_tail(ctx, &entry_ptr[i]))
        {
            dmlog_clear(ctx); // Corrupted entry, clear buffer
            return false; // Buffer empty
        }
    }

    if(out_entry->magic != DMLOG_ENTRY_MAGIC_NUMBER)
    {
        dmlog_clear(ctx); // Corrupted entry, clear buffer
        return false;
    }

    size_t data_len = out_entry->length;
    size_t len_to_read = (data_len < max_data_len - 1) ? data_len : (max_data_len - 1);
    for(size_t i = 0; i < len_to_read; i++)
    {
        uint8_t dummy;
        uint8_t* dst = out_data != NULL ? (uint8_t*)&out_data[i] : &dummy;
        if(read_byte_from_tail(ctx, dst))
        {
            dmlog_clear(ctx); // Corrupted entry, clear buffer
            return false; // Buffer empty
        }
    }
    if(out_data != NULL)
    {
        out_data[len_to_read] = '\0'; // Null-terminate
    }
    // Skip remaining data if any
    for(size_t i = len_to_read; i < data_len; i++)
    {
        uint8_t dummy;
        if(read_byte_from_tail(ctx, &dummy))
        {
            dmlog_clear(ctx); // Corrupted entry, clear buffer
            return false; // Buffer empty   
        }
    }
    return true;
}

/**
 * @brief Write a log entry to the head of the ring buffer.
 * 
 * @param ctx DMLoG context.
 * @param entry Pointer to the log entry header to write.
 * @param data Pointer to the log entry data to write.
 * @return true on success, false if the buffer is full.
 */
static bool write_entry_to_head(dmlog_ctx_t ctx, dmlog_entry_t* entry, const char* data)
{
    size_t total_size = sizeof(dmlog_entry_t) + entry->length;
    dmlog_index_t free_space = dmlog_get_free_space(ctx);
    if(free_space < total_size)
    {
        return false; // Not enough space
    }

    uint8_t* entry_ptr = (uint8_t*)entry;
    for(size_t i = 0; i < sizeof(dmlog_entry_t); i++)
    {
        if(!write_byte_to_tail(ctx, entry_ptr[i]))
        {
            return false; // Should not happen
        }
    }

    for(size_t i = 0; i < entry->length; i++)
    {
        if(!write_byte_to_tail(ctx, (uint8_t)data[i]))
        {
            return false; // Should not happen
        }
    }
    return true;
}

/**
 * @brief Calculate the required size for a DMLoG context with the given buffer size.
 * 
 * @param buffer_size Size of the log buffer in bytes.
 * @return size_t Required size for the DMLoG context.
 */
size_t dmlog_get_required_size(dmlog_index_t buffer_size)
{
    return sizeof(struct dmlog_ctx) + buffer_size - sizeof(((dmlog_ctx_t)0)->buffer);
}

/**
 * @brief Set the provided DMLoG context as the default context.
 * 
 * @param ctx DMLoG context to set as default.
 */
void dmlog_set_as_default(dmlog_ctx_t ctx)
{
    Dmod_EnterCritical();
    default_ctx = ctx;
    Dmod_ExitCritical();
}

/**
 * @brief Get the current default DMLoG context.
 * 
 * @return dmlog_ctx_t Current default DMLoG context.
 */
dmlog_ctx_t dmlog_get_default(void)
{
    Dmod_EnterCritical();
    dmlog_ctx_t ctx = default_ctx;
    Dmod_ExitCritical();
    return ctx;
}

/**
 * @brief Create and initialize a DMLoG context with the provided buffer.
 * 
 * @param buffer Pointer to the memory buffer to use for the log ring.
 * @param buffer_size Size of the provided buffer in bytes.
 * @return dmlog_ctx_t Initialized DMLoG context, or NULL on failure.
 */
dmlog_ctx_t dmlog_create(void *buffer, dmlog_index_t buffer_size)
{
    if (buffer_size < sizeof(dmlog_ring_t))
    {
        DMOD_ASSERT_MSG(false, "Buffer size too small for dmlog_ring_t");
        return NULL;
    }
    Dmod_EnterCritical();
    dmlog_ctx_t ctx = buffer;
    if(dmlog_is_valid(ctx))
    {
        DMOD_ASSERT_MSG(false, "DMLoG context already initialized");
        return NULL;
    }
    memset(buffer, 0, buffer_size);
    dmlog_index_t control_size  = (dmlog_index_t)((uintptr_t)ctx->buffer - (uintptr_t)ctx);
    DMOD_ASSERT_MSG(buffer_size > control_size, "Buffer size too small for control structure");
    ctx->ring.magic             = DMLOG_MAGIC_NUMBER;
    ctx->ring.buffer_size       = buffer_size - control_size;
    ctx->ring.buffer            = (uint64_t)((uintptr_t)ctx->buffer);
    ctx->ring.head_offset       = 0;
    ctx->ring.tail_offset       = 0;
    ctx->ring.latest_id         = 0;
    ctx->ring.flags             = 0;
    ctx->write_entry_offset     = 0;
    ctx->read_entry_offset      = 0;
    ctx->next_id                = 0;
    ctx->lock_recursion         = 0;
    Dmod_ExitCritical();

    return ctx;
}

/**
 * @brief Destroy the DMLoG context, invalidating the ring buffer.
 * 
 * @param ctx DMLoG context to destroy.
 */
void dmlog_destroy(dmlog_ctx_t ctx)
{
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        context_lock(ctx);
        dmlog_clear(ctx);
        ctx->ring.magic = 0;
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
}

/**
 * @brief Check if the DMLoG context is valid.
 * 
 * @param ctx DMLoG context to check.
 * @return true if valid, false otherwise.
 */
bool dmlog_is_valid(dmlog_ctx_t ctx)
{
    Dmod_EnterCritical();
    bool result = ctx != NULL && ctx->ring.magic == DMLOG_MAGIC_NUMBER;
    Dmod_ExitCritical();
    return result;
}

/**
 * @brief Get the amount of space left in the current log entry.
 * 
 * @param ctx DMLoG context.
 * @return dmlog_index_t Number of bytes left in the current entry.
 */
dmlog_index_t dmlog_left_entry_space(dmlog_ctx_t ctx)
{
    dmlog_index_t left_space = 0;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        left_space = DMOD_LOG_MAX_ENTRY_SIZE - ctx->write_entry_offset;
    }
    Dmod_ExitCritical();
    return left_space;
}

/**
 * @brief Add a single character to the log.
 * 
 * @param ctx DMLoG context.
 * @param c Character to add.
 * @return true on success, false on failure.
 */
bool dmlog_putc(dmlog_ctx_t ctx, char c)
{
    bool result = false;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        context_lock(ctx);
        if(ctx->ring.flags & DMLOG_FLAG_CLEAR_BUFFER)
        {
            dmlog_clear(ctx);
            ctx->ring.flags &= ~DMLOG_FLAG_CLEAR_BUFFER;
        }
        if(dmlog_left_entry_space(ctx) == 0)
        {
            dmlog_flush(ctx);
        }
        if(dmlog_left_entry_space(ctx) > 0)
        {
            ctx->write_buffer[ctx->write_entry_offset++] = c;
            result = true;
        }
        if(c == '\n' || dmlog_left_entry_space(ctx) == 0)
        {
            dmlog_flush(ctx);
        }
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
    return result;
}

/**
 * @brief Add a string to the log.
 * 
 * @param ctx DMLoG context.
 * @param s String to add.
 * @return true on success, false on failure.
 */
bool dmlog_puts(dmlog_ctx_t ctx, const char *s)
{
    bool result = false;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        context_lock(ctx);
        size_t len = strlen(s);
        for(size_t i = 0; i < len; i++)
        {
            if(!dmlog_putc(ctx, s[i]))
            {
                break;
            }
        }
        if(s[len - 1] != '\n')
        {
            result = dmlog_flush(ctx);
        }
        else 
        {
            result = true;
        }
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
    return result;
}

/**
 * @brief Add a string with specified length to the log.
 * 
 * @param ctx DMLoG context.
 * @param s String to add.
 * @param n Maximum number of characters to add.
 * @return true on success, false on failure.
 */
bool dmlog_putsn(dmlog_ctx_t ctx, const char *s, size_t n)
{
    bool result = false;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        for(size_t i = 0; i < n; i++)
        {
            if(s[i] == '\0')
            {
                break;
            }
            if(!dmlog_putc(ctx, s[i]))
            {
                break;
            }
        }
        result = dmlog_flush(ctx);
    }
    Dmod_ExitCritical();
    return result;
}

/**
 * @brief Get the amount of free space in the log buffer.
 * 
 * @param ctx DMLoG context.
 * @return dmlog_index_t Number of free bytes in the log buffer.
 */
dmlog_index_t dmlog_get_free_space(dmlog_ctx_t ctx)
{
    dmlog_index_t free_space = 0;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        free_space = get_free_space(ctx);
    }
    Dmod_ExitCritical();
    return free_space;  
}

/**
 * @brief Flush the current log entry to the ring buffer.
 * 
 * @param ctx DMLoG context.
 * @return true on success, false on failure.
 */
bool dmlog_flush(dmlog_ctx_t ctx)
{
    bool result = false;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        dmlog_index_t required_size = sizeof(dmlog_entry_t) + ctx->write_entry_offset;
        while(get_free_space(ctx) < required_size)
        {
            // Not enough space, remove oldest entry
            if(!dmlog_read_next(ctx))
            {
                // Failed to read old entry, clear buffer
                dmlog_clear(ctx);
                break;
            }
        }
        context_lock(ctx);
        dmlog_entry_t entry;
        entry.magic = DMLOG_ENTRY_MAGIC_NUMBER;
        entry.id = ++ctx->next_id;
        entry.length = (uint32_t)ctx->write_entry_offset;
        result = write_entry_to_head(ctx, &entry, ctx->write_buffer);
        ctx->write_entry_offset = 0;
        ctx->ring.latest_id = entry.id;
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
    return result;
}

/**
 * @brief Read the next log entry from the ring buffer.
 * 
 * @param ctx DMLoG context.
 * @return true on success, false on failure.
 */
bool dmlog_read_next(dmlog_ctx_t ctx)
{
    bool result = false;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        wait_for_unlock(ctx);
        context_lock(ctx);
        dmlog_entry_t old_entry;
        result = read_entry_from_tail(ctx, &old_entry, ctx->read_buffer, DMOD_LOG_MAX_ENTRY_SIZE);
        ctx->read_entry_offset = 0;
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
    return result;
}

/**
 * @brief Get a reference to the current log entry buffer.
 * 
 * @param ctx DMLoG context.
 * @return const char* Pointer to the current log entry buffer.
 */
const char *dmlog_get_ref_buffer(dmlog_ctx_t ctx)
{
    const char* result = NULL;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        result = ctx->read_buffer;
    }
    Dmod_ExitCritical();
    return result;
}

/**
 * @brief Get a single character from the current log entry.
 * 
 * @param ctx DMLoG context.
 * @return char The next character, or '\0' if none available.
 */
char dmlog_getc(dmlog_ctx_t ctx)
{
    char c = '\0';
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        context_lock(ctx);  
        if(ctx->read_entry_offset >= DMOD_LOG_MAX_ENTRY_SIZE || ctx->read_buffer[ctx->read_entry_offset] == '\0')
        {
            // Need to read next entry
            if(!dmlog_read_next(ctx))
            {
                Dmod_ExitCritical();
                return '\0'; // No more entries
            }
            ctx->read_entry_offset = 0;
        }
        c = ctx->read_buffer[ctx->read_entry_offset++];
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
    return c;
}

/**
 * @brief Get a string from the current log entry.
 * 
 * @param ctx DMLoG context.
 * @param s Buffer to store the string.
 * @param max_len Maximum length of the buffer.
 * @return true on success, false on failure.
 */
bool dmlog_gets(dmlog_ctx_t ctx, char *s, size_t max_len)
{
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        context_lock(ctx);
        size_t i = 0;
        while(i < max_len - 1)
        {
            char c = dmlog_getc(ctx);
            if(c == '\0')
            {
                break; // No more characters
            }
            s[i++] = c;
            if(c == '\n')
            {
                break; // End of line
            }
        }
        s[i] = '\0'; // Null-terminate
        Dmod_ExitCritical();
        context_unlock(ctx);
        return i > 0;
    }
    Dmod_ExitCritical();
    return false;
}

/**
 * @brief Clear the entire log buffer.
 * 
 * @param ctx DMLoG context.
 */
void dmlog_clear(dmlog_ctx_t ctx)
{
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        context_lock(ctx);
        ctx->ring.head_offset = 0;
        ctx->ring.tail_offset = 0;
        ctx->ring.buffer = (uint64_t)((uintptr_t)ctx->buffer);
        ctx->write_entry_offset = 0;
        ctx->read_entry_offset = 0; 
        ctx->next_id = 0;
        memset(ctx->write_buffer, 0, DMOD_LOG_MAX_ENTRY_SIZE);
        memset(ctx->read_buffer, 0, DMOD_LOG_MAX_ENTRY_SIZE);
        memset(ctx->buffer, 0, ctx->ring.buffer_size);
        ctx->ring.flags &= ~DMLOG_FLAG_CLEAR_BUFFER;
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
}

#ifndef DMLOG_DONT_IMPLEMENT_DMOD_API
/**
 * @brief Built-in printf function for DMLoG.
 * 
 * @param Format Format string.
 * @param ... Additional arguments.
 * @return int Number of characters written, or -1 on failure.
 */
DMOD_INPUT_API_DECLARATION( Dmod, 1.0, int  ,_Printf, ( const char* Format, ... ) )
{
    dmlog_ctx_t ctx = dmlog_get_default();
    if(ctx == NULL)
    {
        return -1;
    }
    va_list args;
    char temp_buffer[DMOD_LOG_MAX_ENTRY_SIZE];
    va_start(args, Format);
    int written = Dmod_VSnPrintf(temp_buffer, sizeof(temp_buffer), Format, args);
    va_end(args);

    if(written > 0)
    {
        dmlog_puts(ctx, temp_buffer);
    }

    return written;
}

#endif // DMLOG_DONT_IMPLEMENT_DMOD_API