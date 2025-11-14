# DMLoG Monitor Tool

A command-line tool for monitoring DMLoG ring buffer via OpenOCD or GDB server. This tool connects to the backend's interface and continuously reads the log ring buffer from the target microcontroller's memory.

## Features

- Real-time log monitoring from embedded devices
- Bidirectional communication support (read logs, send input to firmware)
- Dual backend support: OpenOCD or GDB server
- Configurable buffer address, size, and polling interval
- Debug mode for troubleshooting
- Displays existing log entries on startup
- Graceful shutdown with Ctrl+C

## Prerequisites

### For OpenOCD Backend
- OpenOCD running with telnet server enabled (default port 4444)
- Target device with DMLoG library integrated
- Network connectivity to OpenOCD host

### For GDB Server Backend
- GDB server running (e.g., QEMU with `-s -S`, or hardware debugger)
- Default port: 1234
- Target device with DMLoG library integrated
- Network connectivity to GDB server host

## Building

The tool is built automatically as part of the DMLoG project:

```bash
mkdir build
cd build
cmake ..
make
```

The executable will be located at `build/tools/monitor/dmlog_monitor`.

## Usage

### Basic Usage (OpenOCD)

```bash
./dmlog_monitor
```

This connects to OpenOCD at `localhost:4444` and monitors the ring buffer at the default address `0x20010000`.

### Using GDB Server Backend

```bash
./dmlog_monitor --gdb
```

This connects to GDB server at `localhost:1234` and monitors the ring buffer.

### Custom Configuration

```bash
# OpenOCD with custom host and port
./dmlog_monitor --host 192.168.1.10 --port 4444 --addr 0x20001000

# GDB server with custom configuration
./dmlog_monitor --gdb --host localhost --port 1234 --addr 0x20010000
```

### Command-Line Options

- `--help` - Show help message
- `--version` - Show version information
- `--gdb` - Use GDB server instead of OpenOCD (default: OpenOCD)
- `--host HOST` - Backend IP address (default: localhost)
- `--port PORT` - Backend port (default: 4444 for OpenOCD, 1234 for GDB)
- `--addr ADDRESS` - Ring buffer address in hex (default: 0x20010000)
- `--trace-level LEVEL` - Set trace level (error, warn, info, verbose)
- `--verbose` - Enable verbose output (equivalent to --trace-level verbose)
- `--time` - Show timestamps with log entries
- `--blocking` - Use blocking mode for reading log entries
- `--snapshot` - Enable snapshot mode to reduce target reads

## Examples

### Example 1: OpenOCD Backend

Start OpenOCD with your target configuration:

```bash
openocd -f interface/stlink.cfg -f target/stm32f7x.cfg
```

In another terminal, start the monitor:

```bash
./dmlog_monitor --addr 0x20010000
```

### Example 2: GDB Server Backend with QEMU

Start QEMU with GDB server enabled:

```bash
qemu-system-arm -M <machine> -kernel <firmware.elf> -s -S
```

In another terminal, start the monitor:

```bash
./dmlog_monitor --gdb --addr 0x20010000
```

### Example 3: Remote GDB Server

```bash
./dmlog_monitor --gdb --host 192.168.1.100 --port 1234 --addr 0x20010000 --verbose
```

The tool will:
1. Connect to the specified backend (OpenOCD or GDB server)
2. Display any existing log entries from the ring buffer
3. Continuously monitor for new log entries
4. Display new entries in real-time
5. Support bidirectional communication (firmware can request input)
6. Exit gracefully on Ctrl+C

## Implementation Details

This tool is implemented in C and uses the same type definitions as the DMLoG library (`dmlog.h`). It supports two backend implementations:

### Backend Abstraction

The tool uses a pluggable backend architecture that allows switching between different debug interfaces:

- **OpenOCD Backend**: Communicates via OpenOCD's telnet interface using `mdw` and `mww` commands
- **GDB Server Backend**: Implements GDB Remote Serial Protocol for memory operations (`m` for read, `M` for write)

### GDB Remote Serial Protocol

The GDB server backend implements the following protocol features:
- Packet format: `$<data>#<checksum>`
- Memory read: `m<addr>,<length>` - returns hex-encoded data
- Memory write: `M<addr>,<length>:<hex-data>` - writes data to memory
- Acknowledgment mode (with optional NoAck mode for improved performance)
- Checksum verification for data integrity

### Bidirectional Communication

The monitor supports sending input data to the firmware via the `monitor_send_input()` function. This writes data to the input buffer portion of the ring buffer, which can then be read by the firmware using `dmlog_input_*` functions. This enables interactive console applications and remote command execution on the target device.

## Troubleshooting

### Connection Refused (OpenOCD)

```
Failed to connect to OpenOCD at localhost:4444: Connection refused
```

**Solution**: Make sure OpenOCD is running and the telnet server is enabled on the specified port.

### Connection Refused (GDB Server)

```
Failed to connect to GDB server at localhost:1234: Connection refused
```

**Solution**: 
- For QEMU: Make sure you started QEMU with `-s` flag (or `-gdb tcp::1234`)
- For hardware debuggers: Verify the GDB server is running on the specified port
- Check firewall settings if connecting remotely

### Invalid Magic Number

If you see repeated debug messages about magic number mismatches (with `--debug` flag), verify:
- The ring buffer address is correct
- The target device has initialized the DMLoG buffer
- The target device is running

### No New Entries

If the monitor connects successfully but shows no entries:
- Verify that the target application is writing logs to the DMLoG buffer
- Check the buffer size configuration matches the target
- Use `--debug` to see detailed information about control structure reads

## Related

- Python version: [dmod_log_monitor.py](https://github.com/choco-technologies/dmod-boot/blob/master/scripts/dmod_log_monitor.py)
- DMLoG library: [dmlog.h](../../include/dmlog.h)
