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

#ifndef DMLOG_FILE_CHUNK_SIZE
#   define DMLOG_FILE_TRANSFER_CHUNK_SIZE  512
#endif

#ifndef DMLOG_MAX_FILE_PATH_LENGTH
#   define DMLOG_MAX_FILE_PATH_LENGTH 255
#endif

#ifndef DMLOG_PACKED
#   define DMLOG_PACKED __attribute__((packed))
#endif

/* Flag bits for commands/status */
#define DMLOG_FLAG_CLEAR_BUFFER     0x00000001  /* Set to clear buffer, cleared after execution */
#define DMLOG_FLAG_BUSY             0x00000002  /* Buffer busy flag - set during write operations */
#define DMLOG_FLAG_INPUT_AVAILABLE  0x00000004  /* Input data available flag */
#define DMLOG_FLAG_INPUT_REQUESTED  0x00000008  /* Firmware requests input from user */
#define DMLOG_FLAG_INPUT_ECHO_OFF   0x00000010  /* Disable echoing of input characters */
#define DMLOG_FLAG_INPUT_LINE_MODE  0x00000020  /* Input line mode (vs. character mode) */
#define DMLOG_FLAG_FILE_SEND_REQ    0x00000040  /* FW requests sending a file to the host */
#define DMLOG_FLAG_FILE_RECV_REQ    0x00000080  /* FW requests receiving a file from the host */
#define DMLOG_FLAG_FILE_CHUNK_ACK   0x00000100  /* ACK flag set by host to acknowledge processing of the chunk */

/**
 * @brief Input request flags
 * Used when requesting input from the user via dmlog_input_request().
 */
typedef enum 
{
    DMLOG_INPUT_REQUEST_FLAG_ECHO_OFF    = DMLOG_FLAG_INPUT_ECHO_OFF,   //!< Disable echoing of input characters
    DMLOG_INPUT_REQUEST_FLAG_LINE_MODE   = DMLOG_FLAG_INPUT_LINE_MODE,  //!< Use line mode (vs. character mode)
    DMLOG_INPUT_REQUEST_FLAG_DEFAULT     = 0x0,
    DMLOG_INPUT_REQUEST_MASK             = DMLOG_FLAG_INPUT_ECHO_OFF | DMLOG_FLAG_INPUT_LINE_MODE
} dmlog_input_request_flags_t;

/* Type definition for log entry indices */
typedef uint32_t dmlog_index_t;

/**
 * @brief File transfer information structure
 * 
 * Used for file transfer requests between firmware and host.
 */
typedef struct 
{
    volatile uint64_t buffer_address;                               //!< Pointer to file data buffer
    volatile uint32_t chunk_size;                                   //!< Size of each file chunk
    volatile uint32_t total_size;                                   //!< Total size of the file
    volatile uint32_t offset;                                       //!< Current offset in the file
    char host_file_name[DMLOG_MAX_FILE_PATH_LENGTH];   //!< File name on the host (source or destination)
} dmlog_file_transfer_t;

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
    volatile uint64_t           file_transfer; /* dmlog_file_transfer_t structure address */
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
DMOD_BUILTIN_API(dmlog, 1.0, void,             _input_request,     (dmlog_ctx_t ctx, dmlog_input_request_flags_t flags) );

/* File transfer API */
DMOD_BUILTIN_API(dmlog, 1.0, bool,             _file_send,         (dmlog_ctx_t ctx, const char* src_file_path, const char* dst_file_path) );
DMOD_BUILTIN_API(dmlog, 1.0, bool,             _file_receive,      (dmlog_ctx_t ctx, const char* src_file_path, const char* dst_file_path) );

#endif // DMLOG_H