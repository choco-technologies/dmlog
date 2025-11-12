#ifndef DMLOG_H
#define DMLOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dmod.h"

#ifndef DMLOG_MAGIC_NUMBER
#   define DMLOG_MAGIC_NUMBER      0x444D4C4F 
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
#define DMLOG_FLAG_INPUT_AVAILABLE 0x00000004  /* Input data available flag */

/* Type definition for log entry indices */
typedef uint32_t dmlog_index_t;

/**
 * @brief Ring buffer control structure
 * 
 * Contains:
 * - magic: Magic number for validation (0x444D4C4F = "DMLO")
 * - flags: Command/status flags (bit 0: clear buffer, bit 1: busy flag, bit 2: input available)
 * - head_offset: Offset to the write position in the output buffer
 * - tail_offset: Offset to the read position in the output buffer
 * - buffer_size: Total size of the output buffer in bytes
 * - buffer: Raw log data stored here
 * - input_head_offset: Offset to the write position in the input buffer (written by PC)
 * - input_tail_offset: Offset to the read position in the input buffer (read by firmware)
 * - input_buffer_size: Total size of the input buffer in bytes
 * - input_buffer: Raw input data from PC stored here
 * 
 * Buffer layout: Raw bytes are stored directly without entry headers.
 * Entries are delimited by newline characters ('\n').
 * When the buffer wraps around, the oldest data is overwritten.
 */
typedef struct 
{
    volatile uint32_t           magic;
    volatile uint32_t           flags;
    volatile dmlog_index_t      head_offset;
    volatile dmlog_index_t      tail_offset;
    volatile dmlog_index_t      buffer_size;
    volatile uint64_t           buffer;
    volatile dmlog_index_t      input_head_offset;
    volatile dmlog_index_t      input_tail_offset;
    volatile dmlog_index_t      input_buffer_size;
    volatile uint64_t           input_buffer;
} DMLOG_PACKED dmlog_ring_t;

typedef struct dmlog_ctx* dmlog_ctx_t;

/* Output (firmware to PC) API */
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

/* Input (PC to firmware) API */
DMOD_BUILTIN_API(dmlog, 1.0, bool,             _input_available,   (dmlog_ctx_t ctx) );
DMOD_BUILTIN_API(dmlog, 1.0, char,             _input_getc,        (dmlog_ctx_t ctx) );
DMOD_BUILTIN_API(dmlog, 1.0, bool,             _input_gets,        (dmlog_ctx_t ctx, char* s, size_t max_len) );
DMOD_BUILTIN_API(dmlog, 1.0, dmlog_index_t,    _input_get_free_space, (dmlog_ctx_t ctx) );

#endif // DMLOG_H