# DMLoG File Transfer Implementation

## Overview

DMLoG supports bidirectional file transfer between firmware and host through the monitor tool. This document describes the implementation, protocol, and known limitations.

## Protocol

### File Send (Firmware → Host)

1. Firmware calls `dmlog_file_send(ctx, src_path, dst_path)`
2. Firmware opens source file and allocates chunk buffer
3. For each chunk:
   - Firmware reads chunk from file into buffer
   - Firmware sets `file_transfer` pointer to transfer structure
   - Firmware sets `DMLOG_FLAG_FILE_SEND_REQ` flag
   - Firmware waits for flag to be cleared (with timeout)
   - Monitor detects flag, reads transfer structure and chunk data
   - Monitor writes chunk to host file
   - Monitor clears `DMLOG_FLAG_FILE_SEND_REQ` to acknowledge
4. Firmware continues with next chunk until complete

### File Receive (Host → Firmware)

1. Firmware calls `dmlog_file_receive(ctx, src_path, dst_path)`
2. Firmware allocates chunk buffer
3. For each chunk:
   - Firmware sets `file_transfer` pointer to transfer structure
   - Firmware sets `DMLOG_FLAG_FILE_RECV_REQ` flag
   - Firmware waits for flag to be cleared (with timeout)
   - Monitor detects flag, reads transfer structure
   - Monitor reads chunk from host file
   - Monitor writes chunk to firmware buffer
   - Monitor updates transfer structure with chunk info
   - Monitor clears `DMLOG_FLAG_FILE_RECV_REQ` to acknowledge
   - Firmware writes chunk to destination file
4. Firmware continues until all chunks received

## Monitor Implementation

The monitor (`dmlog_monitor`) implements two handlers:

- `monitor_handle_file_send_request()`: Handles FW→Host transfers
- `monitor_handle_file_recv_request()`: Handles Host→FW transfers

These handlers are called in the main monitoring loop after checking for new log data.

## Testing

### Test Application

`test_app_interactive` supports file transfer commands in test scenarios:

- `<file_send:src_path:dst_path>` - Send file from firmware to host
- `<file_recv:src_path:dst_path>` - Receive file from host to firmware

### Test Scenarios

Located in `tests/scenarios/`:

- `test_file_send.txt` - Simple FW→Host transfer
- `test_file_recv.txt` - Simple Host→FW transfer
- `test_file_bidirectional.txt` - Both directions

### Running Tests

```bash
# Run all tests
cd tests
./test_automated_gdb.sh

# Run specific file transfer test
./test_automated_gdb.sh 5  # File send test
./test_automated_gdb.sh 6  # File receive test
./test_automated_gdb.sh 7  # Bidirectional test
```

## Backend Compatibility

### OpenOCD Backend ✓

The file transfer protocol is designed for the OpenOCD backend where:
- Target runs continuously
- Monitor can read/write memory while target executes
- Firmware's busy-wait loops work as expected

### GDB Backend ⚠️

**Known Limitation**: File transfers have timing challenges with the GDB backend due to its stop-and-go execution model:

1. **Architecture**: GDB halts the target to read memory, then resumes it
2. **Timing Issue**: Firmware's busy-wait loops don't progress while halted
3. **Timeout Problem**: When resumed, timeouts expire before monitor can respond

#### Why This Happens

The firmware uses busy-wait loops like:
```c
volatile uint64_t timeout = 10000;
while(((ctx->ring.flags & DMLOG_FLAG_FILE_SEND_REQ) != 0) && timeout > 0) {
    timeout--;
}
```

With GDB:
- Target is halted most of the time
- Loop doesn't execute → timeout doesn't decrement
- When resumed, loop runs at full speed → timeout expires instantly
- Monitor hasn't had a chance to check/clear the flag yet

#### Workaround Attempts

The implementation includes several mitigations:
- Keep target running between memory accesses
- Automatically resume after reads/writes
- Increased resume duration
- Check for file transfers immediately after resume

However, these don't fully solve the architectural mismatch.

#### Recommended Solutions

For GDB backend users:

1. **Use for development only**: File transfers work fine for testing the monitor code itself
2. **Switch to OpenOCD**: For production file transfers, use OpenOCD backend
3. **Direct hardware**: Best results with real hardware where target runs continuously

## Future Improvements

Potential protocol modifications to better support GDB backend:

1. **Longer timeouts**: Increase firmware timeout values
2. **Different signaling**: Use interrupt-based signaling instead of busy-wait
3. **Chunked acknowledgments**: Allow monitor to process transfers asynchronously
4. **Backend-specific tuning**: Adjust behavior based on backend type

## Usage Examples

### Firmware Side

```c
// Send file from firmware to host
if (dmlog_file_send(ctx, "/flash/data.bin", "/tmp/received_data.bin")) {
    dmlog_puts(ctx, "File sent successfully\n");
} else {
    dmlog_puts(ctx, "File send failed\n");
}

// Receive file from host to firmware
if (dmlog_file_receive(ctx, "/tmp/config.txt", "/flash/config.txt")) {
    dmlog_puts(ctx, "File received successfully\n");
} else {
    dmlog_puts(ctx, "File receive failed\n");
}
```

### Monitor Side

The monitor handles file transfers automatically. No special configuration needed:

```bash
# With OpenOCD (recommended for file transfers)
./dmlog_monitor --addr 0x20000000

# With GDB (limited file transfer support)
./dmlog_monitor --gdb --port 1234 --addr 0x20000000
```

## Implementation Files

- **Firmware API**: `include/dmlog.h`, `src/dmlog.c`
- **Monitor**: `tools/monitor/monitor.c`, `tools/monitor/monitor.h`
- **Tests**: `tests/test_app_interactive.c`, `tests/scenarios/test_file_*.txt`
- **Test Script**: `tests/test_automated_gdb.sh`

## Conclusion

The file transfer implementation is complete and functional for continuous execution environments (OpenOCD, real hardware). The GDB backend has known timing limitations due to architectural differences, but the implementation is correct and will work once the target can run continuously.
