#include "dmlog.h"
#include "dmod.h"
#include <string.h>
#include <stdarg.h>

#ifndef DMLOG_VERSION_STRING
#   define DMLOG_VERSION_STRING "== dmlog ver. unknown ==\n"
#endif

struct dmlog_ctx
{
    dmlog_ring_t ring;
    char write_buffer[DMOD_LOG_MAX_ENTRY_SIZE];
    dmlog_index_t write_entry_offset;
    char read_buffer[DMOD_LOG_MAX_ENTRY_SIZE];
    dmlog_index_t read_entry_offset;
    char input_read_buffer[DMOD_LOG_MAX_ENTRY_SIZE];
    dmlog_index_t input_read_entry_offset;
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
    return free_space > 0 ? free_space - 1 : 0; // Leave one byte empty to distinguish full/empty
}

/**
 * @brief Read a single byte from the tail of the ring buffer.
 * 
 * @param ctx DMLoG context.
 * @param out_byte Pointer to store the read byte.
 * @param out_offset Pointer to store the updated tail offset.
 * @return true if the buffer is empty, false otherwise.
 */
static bool read_byte_from_tail(dmlog_ctx_t ctx, void* out_byte)
{
    bool empty = (ctx->ring.tail_offset == ctx->ring.head_offset);
    if(!empty)
    {
        if(out_byte != NULL)
        {
            *((uint8_t*)out_byte) = ctx->buffer[ctx->ring.tail_offset];
        }
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
    
    // Split buffer: use configurable input buffer size
    dmlog_index_t total_buffer_size = buffer_size - control_size;
#ifndef DMLOG_INPUT_BUFFER_SIZE
#define DMLOG_INPUT_BUFFER_SIZE 512
#endif
    dmlog_index_t input_buffer_size = DMLOG_INPUT_BUFFER_SIZE;
    if (input_buffer_size >= total_buffer_size) {
        // Ensure we have at least some space for output
        input_buffer_size = total_buffer_size / 5;  // Fallback to 20% if configured size is too large
    }
    dmlog_index_t output_buffer_size = total_buffer_size - input_buffer_size;
    
    ctx->ring.magic             = DMLOG_MAGIC_NUMBER;
    ctx->ring.buffer_size       = output_buffer_size;
    ctx->ring.buffer            = (uint64_t)((uintptr_t)ctx->buffer);
    ctx->ring.head_offset       = 0;
    ctx->ring.tail_offset       = 0;
    ctx->ring.input_buffer_size = input_buffer_size;
    ctx->ring.input_buffer      = (uint64_t)((uintptr_t)ctx->buffer + output_buffer_size);
    ctx->ring.input_head_offset = 0;
    ctx->ring.input_tail_offset = 0;
    ctx->ring.flags             = 0;
    ctx->ring.file_chunk_buffer = 0;
    ctx->ring.file_chunk_size   = 0;
    ctx->ring.file_chunk_number = 0;
    ctx->ring.file_total_size   = 0;
    memset((void*)ctx->ring.file_path, 0, DMLOG_MAX_FILE_PATH);
    memset((void*)ctx->ring.file_path_pc, 0, DMLOG_MAX_FILE_PATH);
    ctx->write_entry_offset     = 0;
    ctx->read_entry_offset      = 0;
    ctx->input_read_entry_offset = 0;
    ctx->lock_recursion         = 0;
    Dmod_ExitCritical();

    // Log dmlog version string (prepared at compile time)
    dmlog_puts(ctx, DMLOG_VERSION_STRING);

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
        if(c == '\n')
        {
            result = dmlog_flush(ctx);
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
        result = true; // Initialize as success (empty string is valid)
        for(size_t i = 0; i < len; i++)
        {
            if(!dmlog_putc(ctx, s[i]))
            {
                result = false; // Failed
                break;
            }
        }
        if(result && len > 0 && s[len - 1] != '\n')
        {
            result = dmlog_flush(ctx);
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
        context_lock(ctx);
        result = true; // Initialize as success
        for(dmlog_index_t i = 0; i < ctx->write_entry_offset; i++)
        {
            if(get_free_space(ctx) == 0)
            {
                read_byte_from_tail(ctx, NULL); // Discard oldest byte
            }
            if(!write_byte_to_tail(ctx, (uint8_t)ctx->write_buffer[i]))
            {
                result = false; // Buffer full
                break;
            }
        }
        memset(ctx->write_buffer, 0, DMOD_LOG_MAX_ENTRY_SIZE);
        ctx->write_entry_offset = 0;

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
        
        // Clear read buffer
        memset(ctx->read_buffer, 0, DMOD_LOG_MAX_ENTRY_SIZE);
        
        dmlog_index_t i = 0;
        for(i = 0; i < DMOD_LOG_MAX_ENTRY_SIZE - 1; i++)
        {
            uint8_t byte;
            if(read_byte_from_tail(ctx, &byte))
            {
                // Buffer empty
                break;
            }
            ctx->read_buffer[i] = (char)byte;
            result = true; // Successfully read at least one byte
            
            // Stop reading at newline (end of entry)
            if(byte == '\n')
            {
                i++; // Include the newline in the entry
                break;
            }
        }
        
        // Null-terminate the read buffer
        if(i < DMOD_LOG_MAX_ENTRY_SIZE)
        {
            ctx->read_buffer[i] = '\0';
        }
        
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
    bool result = false;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        context_lock(ctx);
        size_t i = 0;
        size_t read_len = strlen(ctx->read_buffer);
        
        // Copy from read_buffer starting from current offset
        while(i < max_len - 1 && ctx->read_entry_offset < read_len)
        {
            char c = ctx->read_buffer[ctx->read_entry_offset++];
            s[i++] = c;
            if(c == '\n')
            {
                break; // End of line
            }
        }
        s[i] = '\0'; // Null-terminate
        result = (i > 0);
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
    return result;
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
        ctx->ring.input_head_offset = 0;
        ctx->ring.input_tail_offset = 0;
        ctx->write_entry_offset = 0;
        ctx->read_entry_offset = 0;
        ctx->input_read_entry_offset = 0;
        memset(ctx->write_buffer, 0, DMOD_LOG_MAX_ENTRY_SIZE);
        memset(ctx->read_buffer, 0, DMOD_LOG_MAX_ENTRY_SIZE);
        memset(ctx->input_read_buffer, 0, DMOD_LOG_MAX_ENTRY_SIZE);
        memset(ctx->buffer, 0, ctx->ring.buffer_size + ctx->ring.input_buffer_size);
        ctx->ring.flags &= ~(DMLOG_FLAG_CLEAR_BUFFER | DMLOG_FLAG_INPUT_AVAILABLE | DMLOG_FLAG_INPUT_REQUESTED | DMLOG_FLAG_FILE_SEND | DMLOG_FLAG_FILE_RECV);
        ctx->ring.file_chunk_buffer = 0;
        ctx->ring.file_chunk_size = 0;
        ctx->ring.file_chunk_number = 0;
        ctx->ring.file_total_size = 0;
        memset((void*)ctx->ring.file_path, 0, DMLOG_MAX_FILE_PATH);
        memset((void*)ctx->ring.file_path_pc, 0, DMLOG_MAX_FILE_PATH);
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
}

/**
 * @brief Get the amount of free space in the input ring buffer.
 * 
 * @param ctx DMLoG context.
 * @return dmlog_index_t Number of free bytes in the input ring buffer.
 */
static dmlog_index_t get_input_free_space(dmlog_ctx_t ctx)
{
    dmlog_index_t free_space = 0;
    if(ctx->ring.input_head_offset >= ctx->ring.input_tail_offset)
    {
        free_space = ctx->ring.input_buffer_size - (ctx->ring.input_head_offset - ctx->ring.input_tail_offset);
    }
    else
    {
        free_space = ctx->ring.input_tail_offset - ctx->ring.input_head_offset;
    }
    return free_space > 0 ? free_space - 1 : 0; // Leave one byte empty to distinguish full/empty
}

/**
 * @brief Read a single byte from the input buffer tail.
 * 
 * @param ctx DMLoG context.
 * @param out_byte Pointer to store the read byte.
 * @return true if the buffer is empty, false otherwise.
 */
static bool read_byte_from_input_tail(dmlog_ctx_t ctx, void* out_byte)
{
    bool empty = (ctx->ring.input_tail_offset == ctx->ring.input_head_offset);
    if(!empty)
    {
        if(out_byte != NULL)
        {
            uint8_t* input_buffer = (uint8_t*)((uintptr_t)ctx->ring.input_buffer);
            *((uint8_t*)out_byte) = input_buffer[ctx->ring.input_tail_offset];
        }
        ctx->ring.input_tail_offset = (ctx->ring.input_tail_offset + 1) % ctx->ring.input_buffer_size;
    }
    return empty;
}

/**
 * @brief Write a single byte to the input buffer head.
 * 
 * @param ctx DMLoG context.
 * @param byte Byte to write.
 * @return true on success, false if the buffer is full.
 */
static bool write_byte_to_input_head(dmlog_ctx_t ctx, uint8_t byte)
{
    dmlog_index_t next_head = (ctx->ring.input_head_offset + 1) % ctx->ring.input_buffer_size;
    if(next_head == ctx->ring.input_tail_offset)
    {
        // Buffer full
        return false;
    }
    uint8_t* input_buffer = (uint8_t*)((uintptr_t)ctx->ring.input_buffer);
    input_buffer[ctx->ring.input_head_offset] = byte;
    ctx->ring.input_head_offset = next_head;
    return true;
}

/**
 * @brief Check if input data is available in the buffer.
 * 
 * @param ctx DMLoG context.
 * @return true if input data is available, false otherwise.
 */
bool dmlog_input_available(dmlog_ctx_t ctx)
{
    bool result = false;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        result = (ctx->ring.input_tail_offset != ctx->ring.input_head_offset);
    }
    Dmod_ExitCritical();
    return result;
}

/**
 * @brief Get the amount of free space in the input buffer.
 * 
 * @param ctx DMLoG context.
 * @return dmlog_index_t Number of free bytes in the input buffer.
 */
dmlog_index_t dmlog_input_get_free_space(dmlog_ctx_t ctx)
{
    dmlog_index_t free_space = 0;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        free_space = get_input_free_space(ctx);
    }
    Dmod_ExitCritical();
    return free_space;
}

/**
 * @brief Read a single character from the input buffer.
 * 
 * @param ctx DMLoG context.
 * @return char The next character from input, or '\0' if none available.
 */
char dmlog_input_getc(dmlog_ctx_t ctx)
{
    char c = '\0';
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        context_lock(ctx);
        
        // Check if we need to read the next entry
        if(ctx->input_read_entry_offset >= DMOD_LOG_MAX_ENTRY_SIZE || 
           ctx->input_read_buffer[ctx->input_read_entry_offset] == '\0')
        {
            // Clear input read buffer
            memset(ctx->input_read_buffer, 0, DMOD_LOG_MAX_ENTRY_SIZE);
            
            // Read next entry from input buffer
            dmlog_index_t i = 0;
            for(i = 0; i < DMOD_LOG_MAX_ENTRY_SIZE - 1; i++)
            {
                uint8_t byte;
                if(read_byte_from_input_tail(ctx, &byte))
                {
                    // Buffer empty
                    break;
                }
                ctx->input_read_buffer[i] = (char)byte;
                
                // Stop reading at newline (end of entry)
                if(byte == '\n')
                {
                    i++; // Include the newline in the entry
                    break;
                }
            }
            
            // Null-terminate the read buffer
            if(i < DMOD_LOG_MAX_ENTRY_SIZE)
            {
                ctx->input_read_buffer[i] = '\0';
            }
            
            ctx->input_read_entry_offset = 0;
            
            // Update flag if buffer is now empty
            if(ctx->ring.input_tail_offset == ctx->ring.input_head_offset)
            {
                ctx->ring.flags &= ~DMLOG_FLAG_INPUT_AVAILABLE;
            }
        }
        
        if(ctx->input_read_buffer[ctx->input_read_entry_offset] != '\0')
        {
            c = ctx->input_read_buffer[ctx->input_read_entry_offset++];
        }
        
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
    return c;
}

/**
 * @brief Read a line from the input buffer.
 * 
 * @param ctx DMLoG context.
 * @param s Buffer to store the string.
 * @param max_len Maximum length of the buffer.
 * @return true on success, false on failure.
 */
bool dmlog_input_gets(dmlog_ctx_t ctx, char *s, size_t max_len)
{
    bool result = false;
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        context_lock(ctx);
        size_t i = 0;
        
        while(i < max_len - 1)
        {
            char c = dmlog_input_getc(ctx);
            if(c == '\0')
            {
                break; // No more data
            }
            s[i++] = c;
            if(c == '\n')
            {
                break; // End of line
            }
        }
        
        s[i] = '\0'; // Null-terminate
        result = (i > 0);
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
    return result;
}

/**
 * @brief Request input from the user (sets INPUT_REQUESTED flag).
 * 
 * This function sets a flag that the monitor can detect to prompt the user for input.
 * 
 * @param ctx DMLoG context.
 * @param flags Input request flags (e.g., echo off, line mode).
 */
void dmlog_input_request(dmlog_ctx_t ctx, dmlog_input_request_flags_t flags)
{
    Dmod_EnterCritical();
    if(dmlog_is_valid(ctx))
    {
        context_lock(ctx);
        ctx->ring.flags &= ~DMLOG_INPUT_REQUEST_MASK;
        ctx->ring.flags |= DMLOG_FLAG_INPUT_REQUESTED | (flags & DMLOG_INPUT_REQUEST_MASK);
        context_unlock(ctx);
    }
    Dmod_ExitCritical();
}

/**
 * @brief Helper function to copy file paths to ring buffer.
 * 
 * @param dest Destination buffer in ring
 * @param src Source path string
 */
static void copy_file_path(volatile char* dest, const char* src)
{
    size_t path_len = strlen(src);
    if(path_len >= DMLOG_MAX_FILE_PATH)
    {
        path_len = DMLOG_MAX_FILE_PATH - 1;
    }
    memcpy((void*)dest, src, path_len);
    ((char*)dest)[path_len] = '\0';
}

/**
 * @brief Send a file from firmware to PC in chunks.
 * 
 * This function initiates a file transfer from the firmware to the host PC.
 * The file is read from the firmware filesystem and transferred in chunks.
 * The monitor tool on the PC side detects the transfer flag and saves the 
 * received chunks to disk at the specified PC path.
 * 
 * NOTE: This function uses dynamic memory allocation via Dmod_Malloc() and 
 * file I/O operations via Dmod_FileOpen(). It assumes the system is fully 
 * initialized and these interfaces are available.
 * 
 * @param ctx DMLoG context.
 * @param file_path_fw Path to the file in the firmware filesystem (source).
 * @param file_path_pc Path where the file should be saved on PC (destination).
 * @param chunk_size Size of each chunk to send (0 = use default).
 * @return true on success (all chunks sent), false on failure.
 */
bool dmlog_sendf(dmlog_ctx_t ctx, const char* file_path_fw, const char* file_path_pc, uint32_t chunk_size)
{
    bool result = false;
    void* file = NULL;
    void* chunk_buffer = NULL;
    
    Dmod_EnterCritical();
    if(!dmlog_is_valid(ctx) || file_path_fw == NULL || file_path_pc == NULL)
    {
        Dmod_ExitCritical();
        return false;
    }
    
    // Use default chunk size if not specified
    if(chunk_size == 0)
    {
        chunk_size = DMLOG_DEFAULT_CHUNK_SIZE;
    }
    
    context_lock(ctx);
    
    // Open the file for reading
    file = Dmod_FileOpen(file_path_fw, "rb");
    if(file == NULL)
    {
        context_unlock(ctx);
        Dmod_ExitCritical();
        return false;
    }
    
    // Get file size and check if it fits in uint32_t
    size_t file_size = Dmod_FileSize(file);
    if(file_size > DMLOG_MAX_FILE_SIZE)
    {
        Dmod_FileClose(file);
        context_unlock(ctx);
        Dmod_ExitCritical();
        return false;
    }
    
    // Allocate buffer for chunks (using Dmod_Malloc as system is initialized)
    chunk_buffer = Dmod_Malloc(chunk_size);
    if(chunk_buffer == NULL)
    {
        Dmod_FileClose(file);
        context_unlock(ctx);
        Dmod_ExitCritical();
        return false;
    }
    
    // Copy file paths to ring buffer using helper function
    copy_file_path(ctx->ring.file_path, file_path_fw);
    copy_file_path(ctx->ring.file_path_pc, file_path_pc);
    
    // Store file metadata
    ctx->ring.file_total_size = (uint32_t)file_size;
    ctx->ring.file_chunk_buffer = (uint64_t)((uintptr_t)chunk_buffer);
    
    // Send file in chunks
    uint32_t chunk_number = 0;
    size_t bytes_remaining = file_size;
    result = true;
    
    while(bytes_remaining > 0)
    {
        // Calculate chunk size for this iteration
        size_t current_chunk_size = (bytes_remaining > chunk_size) ? chunk_size : bytes_remaining;
        
        // Read chunk from file
        size_t bytes_read = Dmod_FileRead(chunk_buffer, 1, current_chunk_size, file);
        if(bytes_read != current_chunk_size)
        {
            result = false;
            break;
        }
        
        // Update ring buffer with chunk info
        ctx->ring.file_chunk_number = chunk_number;
        ctx->ring.file_chunk_size = (uint32_t)current_chunk_size;
        
        // Set file send flag to signal monitor
        ctx->ring.flags |= DMLOG_FLAG_FILE_SEND;
        
        // Wait for monitor to clear the flag (indicating chunk was received)
        volatile uint32_t timeout = DMLOG_FILE_SEND_TIMEOUT;
        while((ctx->ring.flags & DMLOG_FLAG_FILE_SEND) && timeout > 0)
        {
            timeout--;
        }
        
        if(timeout == 0)
        {
            result = false;
            break;
        }
        
        bytes_remaining -= current_chunk_size;
        chunk_number++;
    }
    
    // Clean up
    Dmod_FileClose(file);
    Dmod_Free(chunk_buffer);
    
    // Clear file transfer metadata
    ctx->ring.file_chunk_buffer = 0;
    ctx->ring.file_chunk_size = 0;
    ctx->ring.file_chunk_number = 0;
    ctx->ring.file_total_size = 0;
    memset((void*)ctx->ring.file_path, 0, DMLOG_MAX_FILE_PATH);
    memset((void*)ctx->ring.file_path_pc, 0, DMLOG_MAX_FILE_PATH);
    
    context_unlock(ctx);
    Dmod_ExitCritical();
    
    return result;
}

/**
 * @brief Receive a file from PC to firmware in chunks.
 * 
 * This function initiates a file transfer from the host PC to the firmware.
 * The firmware requests a file from a PC path, and the monitor tool on the PC side 
 * reads the file and sends it in chunks. The firmware receives and writes the chunks 
 * to its filesystem at the specified firmware path.
 * 
 * NOTE: This function uses dynamic memory allocation via Dmod_Malloc() and 
 * file I/O operations via Dmod_FileOpen(). It assumes the system is fully 
 * initialized and these interfaces are available.
 * 
 * @param ctx DMLoG context.
 * @param file_path_fw Path where the file should be saved in the firmware filesystem (destination).
 * @param file_path_pc Path to the file on PC to read from (source).
 * @param chunk_size Size of each chunk to receive (0 = use default).
 * @return true on success (all chunks received), false on failure.
 */
bool dmlog_recvf(dmlog_ctx_t ctx, const char* file_path_fw, const char* file_path_pc, uint32_t chunk_size)
{
    bool result = false;
    void* file = NULL;
    void* chunk_buffer = NULL;
    
    Dmod_EnterCritical();
    if(!dmlog_is_valid(ctx) || file_path_fw == NULL || file_path_pc == NULL)
    {
        Dmod_ExitCritical();
        return false;
    }
    
    // Use default chunk size if not specified
    if(chunk_size == 0)
    {
        chunk_size = DMLOG_DEFAULT_CHUNK_SIZE;
    }
    
    context_lock(ctx);
    
    // Allocate buffer for chunks (using Dmod_Malloc as system is initialized)
    chunk_buffer = Dmod_Malloc(chunk_size);
    if(chunk_buffer == NULL)
    {
        context_unlock(ctx);
        Dmod_ExitCritical();
        return false;
    }
    
    // Open the file for writing
    file = Dmod_FileOpen(file_path_fw, "wb");
    if(file == NULL)
    {
        Dmod_Free(chunk_buffer);
        context_unlock(ctx);
        Dmod_ExitCritical();
        return false;
    }
    
    // Copy file paths to ring buffer using helper function
    copy_file_path(ctx->ring.file_path, file_path_fw);
    copy_file_path(ctx->ring.file_path_pc, file_path_pc);
    
    // Store buffer address and size in ring
    ctx->ring.file_chunk_buffer = (uint64_t)((uintptr_t)chunk_buffer);
    ctx->ring.file_chunk_size = chunk_size;
    ctx->ring.file_chunk_number = 0;
    ctx->ring.file_total_size = 0; // Unknown at start
    
    // Set file receive flag to signal monitor
    ctx->ring.flags |= DMLOG_FLAG_FILE_RECV;
    
    // Receive file in chunks
    uint32_t expected_chunk = 0;
    result = true;
    
    while(true)
    {
        // Wait for monitor to provide a chunk (clear the flag when chunk is ready)
        volatile uint32_t timeout = DMLOG_FILE_RECV_TIMEOUT;
        while((ctx->ring.flags & DMLOG_FLAG_FILE_RECV) && timeout > 0)
        {
            timeout--;
        }
        
        if(timeout == 0)
        {
            result = false;
            break;
        }
        
        // Check if this is the last chunk (chunk_size will be 0 to signal end)
        if(ctx->ring.file_chunk_size == 0)
        {
            // End of file transfer
            break;
        }
        
        // Verify chunk number
        if(ctx->ring.file_chunk_number != expected_chunk)
        {
            result = false;
            break;
        }
        
        // Write chunk to file
        size_t bytes_written = Dmod_FileWrite(chunk_buffer, 1, ctx->ring.file_chunk_size, file);
        if(bytes_written != ctx->ring.file_chunk_size)
        {
            result = false;
            break;
        }
        
        expected_chunk++;
        
        // Signal that we're ready for next chunk
        ctx->ring.flags |= DMLOG_FLAG_FILE_RECV;
    }
    
    // Clean up
    Dmod_FileClose(file);
    Dmod_Free(chunk_buffer);
    
    // Clear file transfer metadata
    ctx->ring.file_chunk_buffer = 0;
    ctx->ring.file_chunk_size = 0;
    ctx->ring.file_chunk_number = 0;
    ctx->ring.file_total_size = 0;
    memset((void*)ctx->ring.file_path, 0, DMLOG_MAX_FILE_PATH);
    memset((void*)ctx->ring.file_path_pc, 0, DMLOG_MAX_FILE_PATH);
    
    context_unlock(ctx);
    Dmod_ExitCritical();
    
    return result;
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