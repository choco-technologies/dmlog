# DMLoG Monitor Tool

A command-line tool for monitoring DMLoG ring buffer via OpenOCD. This tool connects to OpenOCD's telnet interface and continuously reads the log ring buffer from the target microcontroller's memory.

## Features

- Real-time log monitoring from embedded devices
- Connects via OpenOCD telnet interface
- Configurable buffer address, size, and polling interval
- Debug mode for troubleshooting
- Displays existing log entries on startup
- Graceful shutdown with Ctrl+C

## Prerequisites

- OpenOCD running with telnet server enabled (default port 4444)
- Target device with DMLoG library integrated
- Network connectivity to OpenOCD host

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

### Basic Usage

```bash
./dmlog_monitor
```

This connects to OpenOCD at `localhost:4444` and monitors the ring buffer at the default address `0x20000000`.

### Custom Configuration

```bash
./dmlog_monitor --host 192.168.1.10 --port 4444 --addr 0x20001000 --size 8192
```

### Command-Line Options

- `--host HOST` - OpenOCD host (default: localhost)
- `--port PORT` - OpenOCD telnet port (default: 4444)
- `--addr ADDRESS` - Ring buffer address in hex (default: 0x20000000)
- `--size SIZE` - Total buffer size in bytes (default: 4096)
- `--max-entry SIZE` - Maximum entry size in bytes (default: 512)
- `--interval SECONDS` - Polling interval in seconds (default: 0.1)
- `--max-startup N` - Maximum old entries to show on startup (default: 100)
- `--debug` - Enable debug logging
- `--help` - Show help message

## Example

Start OpenOCD with your target configuration:

```bash
openocd -f interface/stlink.cfg -f target/stm32f7x.cfg
```

In another terminal, start the monitor:

```bash
./dmlog_monitor --addr 0x20000000 --size 4096
```

The tool will:
1. Connect to OpenOCD
2. Display any existing log entries (up to 100 most recent)
3. Continuously monitor for new log entries
4. Display new entries in real-time
5. Exit gracefully on Ctrl+C

## Implementation Details

This tool is implemented in C and uses the same type definitions as the DMLoG library (`dmlog.h`). It communicates with OpenOCD via the telnet interface and uses the `mdw` (memory display word) command to read memory from the target device.

The implementation follows the same logic as the Python reference implementation from dmod-boot, but is written in C for better integration with the DMLoG type system.

## Troubleshooting

### Connection Refused

```
Failed to connect to OpenOCD at localhost:4444: Connection refused
```

**Solution**: Make sure OpenOCD is running and the telnet server is enabled on the specified port.

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
