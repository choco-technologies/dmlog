# DMLoG Monitor Tool

A command-line tool for monitoring DMLoG ring buffer via OpenOCD. This tool connects to OpenOCD's telnet interface and continuously reads the log ring buffer from the target microcontroller's memory.

## Features

- Real-time log monitoring from embedded devices
- Bidirectional communication support (read logs, send input to firmware)
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

- `--help` - Show help message
- `--version` - Show version information
- `--host HOST` - Backend IP address (default: localhost)
- `--port PORT` - Backend port (default: 4444)
- `--addr ADDRESS` - Ring buffer address in hex (default: 0x20010000)
- `--search` - Search for the ring buffer in memory
- `--trace-level LEVEL` - Set trace level (error, warn, info, verbose)
- `--verbose` - Enable verbose output (equivalent to --trace-level verbose)
- `--time` - Show timestamps with log entries
- `--blocking` - Use blocking mode for reading log entries
- `--snapshot` - Enable snapshot mode to reduce target reads
- `--gdb` - Use GDB backend instead of OpenOCD
- `--input-file FILE` - File to read input from for automated testing (exits when file ends)
- `--init-script FILE` - File to read as initialization script, then switch to stdin for interactive use

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

### Using Init Scripts

You can provide an initialization script that will be executed when the firmware requests input, and then the monitor will switch to reading from stdin for interactive use:

```bash
# Create an init script
cat > init.txt << EOF
setup command 1
setup command 2
configure option A
EOF

# Start monitor with init script
./dmlog_monitor --init-script init.txt --addr 0x20000000
```

This is useful for:
- Automatically configuring the firmware on startup
- Running setup commands before interactive use
- Pre-loading data or settings into the firmware

The monitor will:
1. Read commands from the init script file when firmware requests input
2. After the init script completes (EOF), switch to reading from stdin
3. Continue running interactively, allowing manual input

## Implementation Details

This tool is implemented in C and uses the same type definitions as the DMLoG library (`dmlog.h`). It communicates with OpenOCD via the telnet interface and uses the `mdw` (memory display word) and `mww` (memory write word) commands to read from and write to the target device.

The implementation follows the same logic as the Python reference implementation from dmod-boot, but is written in C for better integration with the DMLoG type system.

### Bidirectional Communication

The monitor supports sending input data to the firmware via the `monitor_send_input()` function. This writes data to the input buffer portion of the ring buffer, which can then be read by the firmware using `dmlog_input_*` functions. This enables interactive console applications and remote command execution on the target device.

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
