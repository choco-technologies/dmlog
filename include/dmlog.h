#ifndef DMLOG_H
#define DMLOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dmod.h"

#ifndef DMLOG_MAGIC_NUMBER
#   define DMLOG_MAGIC_NUMBER      0x444D4C4F 
#endif

#ifndef DMLOG_ENTRY_MAGIC_NUMBER
#   define DMLOG_ENTRY_MAGIC_NUMBER 0x454E5452 
#endif

/* Maximum size of a single log message */
#ifndef DMOD_LOG_MAX_ENTRY_SIZE
#define DMOD_LOG_MAX_ENTRY_SIZE    500
#endif

#ifndef DMLOG_PACKED
#   define DMLOG_PACKED __attribute__((packed))
#endif

/* Flag bits for commands/status */
#define DMLOG_FLAG_CLEAR_BUFFER  0x00000001  /* Set to clear buffer, cleared after execution */
#define DMLOG_FLAG_BUSY          0x00000002  /* Buffer busy flag - set during write operations */

/* Type definition for log entry IDs */
typedef uint32_t dmlog_entry_id_t;

/* Type definition for log entry indices */
typedef uint32_t dmlog_index_t;

/**
 * @brief Log entry header structure
 * 
 * Each entry in the buffer has this header followed by the message data:
 * [magic(4)] [entry_id(4)] [length(2)] [data(length)]
 * 
 * - magic: Magic number for validation (0x454E5452 = "ENTR")
 * - id: Unique incrementing ID to detect new entries
 * - length: Actual length of the message data (max 65535 bytes)
 */
typedef struct 
{
    volatile uint32_t           magic;
    volatile dmlog_entry_id_t   id;
    volatile uint32_t           length;
} DMLOG_PACKED dmlog_entry_t;


/**
 * @brief Ring buffer control structure
 * 
 * Contains:
 * - magic: Magic number for validation (0x444D4F44 = "DMOD")
 * - latest_id: Most recent log entry ID (for easy monitoring)
 * - flags: Command/status flags (bit 0: clear buffer)
 * - head_offset: Offset to the newest entry in the buffer
 * - tail_offset: Offset to the oldest entry in the buffer
 * - buffer_size: Total size of the buffer in bytes
 * - buffer: Variable-length entries stored here
 * 
 * Buffer layout: Entries are stored sequentially with their headers.
 * When the buffer wraps around, the oldest entries are overwritten.
 */
typedef struct 
{
    volatile uint32_t           magic;
    volatile dmlog_entry_id_t   latest_id;
    volatile uint32_t           flags;
    volatile dmlog_index_t      head_offset;
    volatile dmlog_index_t      tail_offset;
    volatile dmlog_index_t      buffer_size;
    volatile uint64_t           buffer;
} DMLOG_PACKED dmlog_ring_t;

typedef struct dmlog_ctx* dmlog_ctx_t;

DMOD_BUILTIN_API(dmlog, 1.0, size_t,           _get_required_size, (dmlog_index_t buffer_size) );
DMOD_BUILTIN_API(dmlog, 1.0, void,             _set_as_default,    (dmlog_ctx_t ctx) );
DMOD_BUILTIN_API(dmlog, 1.0, dmlog_ctx_t,      _get_default,       (void) );
DMOD_BUILTIN_API(dmlog, 1.0, dmlog_ctx_t,      _create,            (void* buffer, dmlog_index_t buffer_size) );
DMOD_BUILTIN_API(dmlog, 1.0, void,             _destroy,           (dmlog_ctx_t ctx) );
DMOD_BUILTIN_API(dmlog, 1.0, bool,             _is_valid,          (dmlog_ctx_t ctx) );
DMOD_BUILTIN_API(dmlog, 1.0, dmlog_index_t,    _left_entry_space,  (dmlog_ctx_t ctx) );
DMOD_BUILTIN_API(dmlog, 1.0, bool,             _putc,              (dmlog_ctx_t ctx, char c) );
DMOD_BUILTIN_API(dmlog, 1.0, bool,             _puts,              (dmlog_ctx_t ctx, const char* s) );
DMOD_BUILTIN_API(dmlog, 1.0, bool,             _putsn,             (dmlog_ctx_t ctx, const char* s, size_t n) );
DMOD_BUILTIN_API(dmlog, 1.0, dmlog_index_t,    _get_free_space,    (dmlog_ctx_t ctx) );
DMOD_BUILTIN_API(dmlog, 1.0, bool,             _flush,             (dmlog_ctx_t ctx) );
DMOD_BUILTIN_API(dmlog, 1.0, bool,             _read_next,         (dmlog_ctx_t ctx) );
DMOD_BUILTIN_API(dmlog, 1.0, const char*,      _get_ref_buffer,    (dmlog_ctx_t ctx) );
DMOD_BUILTIN_API(dmlog, 1.0, char,             _getc,              (dmlog_ctx_t ctx) );
DMOD_BUILTIN_API(dmlog, 1.0, bool,             _gets,              (dmlog_ctx_t ctx, char* s, size_t max_len) );
DMOD_BUILTIN_API(dmlog, 1.0, void,             _clear,             (dmlog_ctx_t ctx) );

#endif // DMLOG_H