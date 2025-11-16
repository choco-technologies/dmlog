# DMLoG - DMOD Log Library

A lightweight, thread-safe ring buffer logging library for embedded systems, designed to work with the DMOD framework. DMLoG provides efficient circular buffer logging with support for real-time monitoring via OpenOCD.

[![CI](https://github.com/choco-technologies/dmlog/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmlog/actions/workflows/ci.yml)

## 📋 Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Usage Examples](#usage-examples)
- [API Reference](#api-reference)
- [Building](#building)
- [Testing](#testing)
- [Tools](#tools)
- [Architecture](#architecture)
- [Contributing](#contributing)
- [License](#license)

## ✨ Features

- **Bidirectional Communication**: Support for both output (firmware → PC) and input (PC → firmware) data transfer
- **Ring Buffer Architecture**: Circular buffer automatically overwrites oldest entries when full
- **Thread-Safe**: Built-in locking mechanism for multi-threaded environments
- **Zero-Copy Reads**: Direct buffer access for efficient log reading
- **Auto-Flush**: Automatic flushing on newline characters
- **Real-Time Monitoring**: OpenOCD integration for live log monitoring from embedded devices
- **User Input Support**: Read data from PC/monitor into firmware for interactive applications
- **Configurable Buffer Size**: Flexible buffer sizing to fit your memory constraints
- **Minimal Dependencies**: Only depends on the DMOD framework
- **Well-Tested**: Comprehensive unit tests with >80% code coverage

## 📦 Requirements

### Build Requirements
- CMake 3.10 or higher
- C compiler with C11 support
- C++ compiler (for tests)
- [DMOD framework](https://github.com/choco-technologies/dmod) (automatically fetched)

### Optional Requirements
- lcov (for code coverage reports)
- OpenOCD (for real-time monitoring)

## 🚀 Installation

### Using CMake FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    dmlog
    GIT_REPOSITORY https://github.com/choco-technologies/dmlog.git
    GIT_TAG        develop
)

FetchContent_MakeAvailable(dmlog)

# Link against your target
target_link_libraries(your_target PRIVATE dmlog)
```

### Manual Build

```bash
git clone https://github.com/choco-technologies/dmlog.git
cd dmlog
mkdir build && cd build
cmake ..
make
```

## 🏁 Quick Start

Here's a minimal example to get you started:

```c
#include "dmlog.h"
#include <stdio.h>

#define BUFFER_SIZE 4096
static char log_buffer[BUFFER_SIZE];

int main(void) {
    // Create a log context
    dmlog_ctx_t ctx = dmlog_create(log_buffer, BUFFER_SIZE);
    
    if (!ctx) {
        printf("Failed to create log context\n");
        return 1;
    }
    
    // Write some log messages
    dmlog_puts(ctx, "Hello from DMLoG!\n");
    dmlog_puts(ctx, "This is a test message\n");
    
    // Read back the logs
    while (dmlog_read_next(ctx)) {
        char buffer[256];
        if (dmlog_gets(ctx, buffer, sizeof(buffer))) {
            printf("Log: %s", buffer);
        }
    }
    
    // Clean up
    dmlog_destroy(ctx);
    return 0;
}
```

## 📚 Usage Examples

### Basic Logging

```c
#include "dmlog.h"

// Create a buffer for logs
#define LOG_BUFFER_SIZE (8 * 1024)
static char my_log_buffer[LOG_BUFFER_SIZE];

void setup_logging(void) {
    // Create and initialize the log context
    dmlog_ctx_t ctx = dmlog_create(my_log_buffer, LOG_BUFFER_SIZE);
    
    // Optionally set it as the default context
    dmlog_set_as_default(ctx);
}

void log_messages(void) {
    dmlog_ctx_t ctx = dmlog_get_default();
    
    // Write individual characters
    dmlog_putc(ctx, 'X');
    dmlog_putc(ctx, '\n');  // Auto-flushes
    
    // Write strings
    dmlog_puts(ctx, "System initialized\n");
    
    // Write string with length limit
    dmlog_putsn(ctx, "Partial message", 7);  // Writes "Partial"
    dmlog_putc(ctx, '\n');
    
    // Manual flush if needed (usually not required)
    dmlog_flush(ctx);
}
```

### Reading Logs

```c
void read_all_logs(dmlog_ctx_t ctx) {
    char buffer[512];
    
    // Read all available log entries
    while (dmlog_read_next(ctx)) {
        if (dmlog_gets(ctx, buffer, sizeof(buffer))) {
            printf("%s", buffer);
        }
    }
}

void read_logs_char_by_char(dmlog_ctx_t ctx) {
    // Position at next entry
    if (dmlog_read_next(ctx)) {
        char c;
        while ((c = dmlog_getc(ctx)) != '\0') {
            putchar(c);
        }
    }
}
```

### Buffer Management

```c
void manage_buffer(dmlog_ctx_t ctx) {
    // Check available space
    dmlog_index_t free_space = dmlog_get_free_space(ctx);
    printf("Free space: %u bytes\n", free_space);
    
    // Check space for entries (accounting for headers)
    dmlog_index_t entry_space = dmlog_left_entry_space(ctx);
    printf("Space for entry data: %u bytes\n", entry_space);
    
    // Clear all logs if needed
    dmlog_clear(ctx);
    
    // Verify context is valid
    if (dmlog_is_valid(ctx)) {
        printf("Context is valid\n");
    }
}
```

### Reading User Input (PC to Firmware)

DMLoG supports bidirectional communication, allowing firmware to read data sent from the PC/monitor:

```c
void interactive_console(dmlog_ctx_t ctx) {
    // Request input from user - monitor will detect this and prompt
    dmlog_input_request(ctx);
    
    // Wait for input to become available
    while (!dmlog_input_available(ctx)) {
        // Could do other work here or yield to other tasks
    }
    
    // Read the input
    char input_buffer[256];
    if (dmlog_input_gets(ctx, input_buffer, sizeof(input_buffer))) {
        printf("Received command: %s", input_buffer);
        // Process the command
    }
}

void read_user_input(dmlog_ctx_t ctx) {
    // Check if input data is available
    if (dmlog_input_available(ctx)) {
        char input_buffer[256];
        
        // Read a line of input from PC
        if (dmlog_input_gets(ctx, input_buffer, sizeof(input_buffer))) {
            printf("Received from PC: %s", input_buffer);
        }
    }
}

void read_input_char_by_char(dmlog_ctx_t ctx) {
    // Read input character by character
    char c;
    while ((c = dmlog_input_getc(ctx)) != '\0') {
        // Process each character
        if (c == '\n') {
            printf("End of line received\n");
            break;
        }
        printf("Char: %c\n", c);
    }
}

void check_input_space(dmlog_ctx_t ctx) {
    // Check available space in input buffer
    dmlog_index_t free_space = dmlog_input_get_free_space(ctx);
    printf("Input buffer free space: %u bytes\n", free_space);
}
```

**Note**: The input buffer size is configurable via CMake (`DMLOG_INPUT_BUFFER_SIZE`, default: 512 bytes). When firmware calls `dmlog_input_request()`, it sets a flag that the monitor detects, prompting the user for input which is then sent to the firmware.

#### Input Request Flags: ECHO_OFF and LINE_MODE

DMLoG provides two important flags to control how the monitor tool handles user input:

- **`DMLOG_INPUT_REQUEST_FLAG_ECHO_OFF`**: Disables echoing of input characters to the terminal. Useful for password input or sensitive data.
- **`DMLOG_INPUT_REQUEST_FLAG_LINE_MODE`**: Enables line-buffered input mode where input is sent only after pressing Enter (canonical mode). Without this flag, the monitor operates in character mode where each character is sent immediately as typed.

##### Basic Usage

```c
void request_password(dmlog_ctx_t ctx) {
    // Request password input without echo (characters won't be displayed)
    dmlog_input_request(ctx, DMLOG_INPUT_REQUEST_FLAG_ECHO_OFF | 
                             DMLOG_INPUT_REQUEST_FLAG_LINE_MODE);
    
    // Wait for input
    while (!dmlog_input_available(ctx)) {
        // Wait...
    }
    
    // Read the password
    char password[256];
    if (dmlog_input_gets(ctx, password, sizeof(password))) {
        // Process password securely
        // ...
        // Clear password from memory after use
        memset(password, 0, sizeof(password));
    }
}

void request_single_key(dmlog_ctx_t ctx) {
    // Request single character input in character mode (no echo)
    dmlog_input_request(ctx, DMLOG_INPUT_REQUEST_FLAG_ECHO_OFF);
    
    // Wait for input
    while (!dmlog_input_available(ctx)) {
        // Wait...
    }
    
    // Read single character
    char c = dmlog_input_getc(ctx);
    printf("You pressed: %c\n", c);
}

void request_visible_line(dmlog_ctx_t ctx) {
    // Request line input with echo (default behavior)
    dmlog_input_request(ctx, DMLOG_INPUT_REQUEST_FLAG_LINE_MODE);
    
    // Wait and read
    while (!dmlog_input_available(ctx)) {
        // Wait...
    }
    
    char line[256];
    dmlog_input_gets(ctx, line, sizeof(line));
}

void request_default_input(dmlog_ctx_t ctx) {
    // Request input with default settings (echo enabled, line mode disabled)
    dmlog_input_request(ctx, DMLOG_INPUT_REQUEST_FLAG_DEFAULT);
    
    // This is equivalent to:
    // dmlog_input_request(ctx, 0);
}
```

##### Flag Combinations

The flags can be combined using bitwise OR to achieve different input behaviors:

| Flags | Behavior | Use Case |
|-------|----------|----------|
| `DMLOG_INPUT_REQUEST_FLAG_DEFAULT` (0) | Echo ON, Character mode | Real-time character input with visual feedback |
| `DMLOG_INPUT_REQUEST_FLAG_ECHO_OFF` | Echo OFF, Character mode | Single key press without displaying the character |
| `DMLOG_INPUT_REQUEST_FLAG_LINE_MODE` | Echo ON, Line mode | Normal command-line input |
| `DMLOG_INPUT_REQUEST_FLAG_ECHO_OFF \| DMLOG_INPUT_REQUEST_FLAG_LINE_MODE` | Echo OFF, Line mode | Password or sensitive data input |

##### Technical Details

1. **Terminal Configuration**: The monitor tool (`dmlog_monitor`) reads these flags from the dmlog ring buffer and configures the terminal accordingly using `termios`:
   - Echo control: `ECHO` flag in `c_lflag`
   - Line mode: `ICANON` flag in `c_lflag`

2. **Flag Persistence**: The flags remain set in the ring buffer until:
   - A new `dmlog_input_request()` call is made with different flags
   - `dmlog_clear()` is called, which clears all flags

3. **Monitor Behavior**: When the monitor detects `DMLOG_FLAG_INPUT_REQUESTED`, it:
   - Configures the terminal according to ECHO_OFF and LINE_MODE flags
   - Reads input from stdin
   - Sends the input to the firmware via the input buffer
   - Restores terminal settings to normal (echo on, line mode on)

##### Example: Interactive Menu

```c
void show_menu(dmlog_ctx_t ctx) {
    dmlog_puts(ctx, "\nMain Menu:\n");
    dmlog_puts(ctx, "1. Option A\n");
    dmlog_puts(ctx, "2. Option B\n");
    dmlog_puts(ctx, "3. Exit\n");
    dmlog_puts(ctx, "Select (1-3): ");
    
    // Request single character without echo in character mode
    dmlog_input_request(ctx, DMLOG_INPUT_REQUEST_FLAG_ECHO_OFF);
    
    while (!dmlog_input_available(ctx)) {
        // Wait for user input
    }
    
    char choice = dmlog_input_getc(ctx);
    dmlog_putc(ctx, choice);  // Echo the choice ourselves
    dmlog_putc(ctx, '\n');
    
    switch (choice) {
        case '1':
            dmlog_puts(ctx, "You selected Option A\n");
            break;
        case '2':
            dmlog_puts(ctx, "You selected Option B\n");
            break;
        case '3':
            dmlog_puts(ctx, "Exiting...\n");
            break;
        default:
            dmlog_puts(ctx, "Invalid choice\n");
            break;
    }
}
```

### Calculating Required Buffer Size

```c
#include "dmlog.h"
#include <stdlib.h>

void allocate_dynamic_buffer(void) {
    dmlog_index_t desired_buffer_size = 4096;
    
    // Calculate total memory needed
    size_t required_size = dmlog_get_required_size(desired_buffer_size);
    
    // Allocate memory
    void* buffer = malloc(required_size);
    if (buffer) {
        dmlog_ctx_t ctx = dmlog_create(buffer, required_size);
        // Use ctx...
        dmlog_destroy(ctx);
        free(buffer);
    }
}
```

## 📖 API Reference

### Context Management

| Function | Description |
|----------|-------------|
| `dmlog_ctx_t dmlog_create(void* buffer, dmlog_index_t buffer_size)` | Create and initialize a log context |
| `void dmlog_destroy(dmlog_ctx_t ctx)` | Destroy a log context |
| `bool dmlog_is_valid(dmlog_ctx_t ctx)` | Check if context is valid |
| `void dmlog_set_as_default(dmlog_ctx_t ctx)` | Set context as default |
| `dmlog_ctx_t dmlog_get_default(void)` | Get the default context |
| `size_t dmlog_get_required_size(dmlog_index_t buffer_size)` | Calculate required memory for context |

### Writing Operations

| Function | Description |
|----------|-------------|
| `bool dmlog_putc(dmlog_ctx_t ctx, char c)` | Write a single character |
| `bool dmlog_puts(dmlog_ctx_t ctx, const char* s)` | Write a null-terminated string |
| `bool dmlog_putsn(dmlog_ctx_t ctx, const char* s, size_t n)` | Write up to n characters |
| `bool dmlog_flush(dmlog_ctx_t ctx)` | Flush current entry to buffer |

### Reading Operations

| Function | Description |
|----------|-------------|
| `bool dmlog_read_next(dmlog_ctx_t ctx)` | Position at next log entry |
| `char dmlog_getc(dmlog_ctx_t ctx)` | Read next character from current entry |
| `bool dmlog_gets(dmlog_ctx_t ctx, char* s, size_t max_len)` | Read current entry into buffer |
| `const char* dmlog_get_ref_buffer(dmlog_ctx_t ctx)` | Get direct pointer to current entry |

### Buffer Management

| Function | Description |
|----------|-------------|
| `dmlog_index_t dmlog_get_free_space(dmlog_ctx_t ctx)` | Get available buffer space |
| `dmlog_index_t dmlog_left_entry_space(dmlog_ctx_t ctx)` | Get available space for entry data |
| `void dmlog_clear(dmlog_ctx_t ctx)` | Clear all log entries |

### Input Operations (PC to Firmware)

| Function | Description |
|----------|-------------|
| `void dmlog_input_request(dmlog_ctx_t ctx, dmlog_input_request_flags_t flags)` | Request input from user with specified flags (ECHO_OFF, LINE_MODE). Monitor detects this and prompts for input. |
| `bool dmlog_input_available(dmlog_ctx_t ctx)` | Check if input data is available |
| `char dmlog_input_getc(dmlog_ctx_t ctx)` | Read next character from input buffer |
| `bool dmlog_input_gets(dmlog_ctx_t ctx, char* s, size_t max_len)` | Read line from input buffer |
| `dmlog_index_t dmlog_input_get_free_space(dmlog_ctx_t ctx)` | Get available space in input buffer |

#### Input Request Flags

| Flag | Value | Description |
|------|-------|-------------|
| `DMLOG_INPUT_REQUEST_FLAG_DEFAULT` | 0x0 | Default mode: echo enabled, character mode |
| `DMLOG_INPUT_REQUEST_FLAG_ECHO_OFF` | 0x10 | Disable echoing of input characters (useful for passwords) |
| `DMLOG_INPUT_REQUEST_FLAG_LINE_MODE` | 0x20 | Enable line-buffered input mode (input sent after Enter) |

Flags can be combined using bitwise OR: `DMLOG_INPUT_REQUEST_FLAG_ECHO_OFF | DMLOG_INPUT_REQUEST_FLAG_LINE_MODE`

## 🔨 Building

### Basic Build

```bash
mkdir build
cd build
cmake ..
make
```

### Build with Tests

```bash
mkdir build
cd build
cmake .. -DDMLOG_BUILD_TESTS=ON
make
```

### Build with Tools

```bash
mkdir build
cd build
cmake .. -DDMLOG_BUILD_TOOLS=ON
make
```

### Build Everything with Coverage

```bash
mkdir build
cd build
cmake .. -DDMLOG_BUILD_TESTS=ON -DDMLOG_BUILD_TOOLS=ON -DENABLE_COVERAGE=ON
make
```

### CMake Options

| Option | Description | Default |
|--------|-------------|---------|
| `DMLOG_BUILD_TESTS` | Build unit tests | OFF |
| `DMLOG_BUILD_TOOLS` | Build monitoring tools | OFF |
| `ENABLE_COVERAGE` | Enable code coverage | OFF |
| `DMLOG_DONT_IMPLEMENT_DMOD_API` | Don't implement DMOD API | OFF |
| `DMLOG_INPUT_BUFFER_SIZE` | Input buffer size in bytes | 512 |

## 🧪 Testing

### Run All Tests

```bash
cd build
ctest --output-on-failure
```

### Run Specific Tests

```bash
# Simple smoke test
./tests/test_simple

# Comprehensive unit tests
./tests/test_dmlog_unit

# Input/output tests
./tests/test_input

# Performance benchmarks
./tests/test_benchmark
```

### Integration Tests with GDB

The project includes automated integration tests that validate the complete dmlog system using gdbserver and dmlog_monitor:

```bash
# Install gdbserver (required)
sudo apt-get install gdbserver

# Run automated integration tests
cd tests
./test_automated_gdb.sh
```

These tests verify:
- **Output path**: Firmware → PC communication via dmlog_monitor
- **GDB backend integration**: Connection through GDB Remote Serial Protocol
- **Multiple scenarios**: Different buffer sizes and usage patterns
- **Real-world flow**: Complete end-to-end testing with actual gdbserver

The integration tests run automatically in CI on every build.

### Code Coverage

```bash
# Build with coverage
cmake .. -DENABLE_COVERAGE=ON -DDMLOG_BUILD_TESTS=ON
make

# Run tests
ctest

# Generate coverage report
lcov --directory . --capture --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/build/_deps/*' --output-file coverage_filtered.info
genhtml coverage_filtered.info --output-directory coverage_html

# Open coverage_html/index.html in a browser
```

See [tests/README.md](tests/README.md) for more details.

## 🛠️ Tools

### DMLoG Monitor

A command-line tool for real-time log monitoring from embedded devices via OpenOCD.

```bash
# Basic usage (connects to localhost:4444)
./build/tools/monitor/dmlog_monitor

# With custom configuration
./build/tools/monitor/dmlog_monitor \
    --host 192.168.1.10 \
    --port 4444 \
    --addr 0x20000000 \
    --size 4096 \
    --interval 0.1
```

#### Monitor Features
- Real-time log streaming from target device
- Shows existing logs on startup
- Configurable polling interval
- Debug mode for troubleshooting
- Graceful shutdown with Ctrl+C

See [tools/monitor/README.md](tools/monitor/README.md) for complete documentation.

## 🏗️ Architecture

### Bidirectional Ring Buffer Structure

DMLoG uses a split circular buffer supporting bidirectional communication:

```
+------------------------+
|  Control Header        |  (dmlog_ring_t)
|  - magic               |  Magic number (0x444D4C4F = "DMLO")
|  - flags               |  Status/command flags:
|                        |    • BUSY: Buffer locked
|                        |    • CLEAR_BUFFER: Clear requested
|                        |    • INPUT_AVAILABLE: Input data ready
|                        |    • INPUT_REQUESTED: FW requests input
|  - head_offset         |  Output write position (firmware)
|  - tail_offset         |  Output read position (PC)
|  - buffer_size         |  Output buffer capacity
|  - input_head_offset   |  Input write position (PC)
|  - input_tail_offset   |  Input read position (firmware)
|  - input_buffer_size   |  Input buffer capacity (configurable)
+------------------------+
|                        |
|   Output Ring Buffer   |  Firmware → PC
|   (Log Data)           |  Entries delimited by newlines
|                        |
+------------------------+
|                        |
|   Input Ring Buffer    |  PC → Firmware (configurable size)
|   (User Input)         |  Commands/data from user
|                        |
+------------------------+
```

### Data Storage

**Output Buffer (Firmware → PC)**:
- Raw bytes written sequentially by firmware
- Entries delimited by newline characters (`\n`)
- Automatic flush on newline or manual flush
- Oldest data automatically overwritten when buffer is full
- 80% of total buffer space

**Input Buffer (PC → Firmware)**:
- Data written by monitor tool via OpenOCD when firmware requests input
- Read by firmware using `dmlog_input_*` functions
- Newline-delimited entries
- Configurable size via `DMLOG_INPUT_BUFFER_SIZE` CMake option (default: 512 bytes)
- Firmware uses `dmlog_input_request()` to request input, monitor detects flag and prompts user

### Thread Safety

- Built-in busy flag prevents concurrent access
- Recursive locking support for nested calls
- Timeout mechanism prevents deadlocks
- Compatible with DMOD critical sections

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

### Development Workflow

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Run tests (`ctest --output-on-failure`)
5. Check code coverage (aim for >80%)
6. Commit your changes (`git commit -m 'Add amazing feature'`)
7. Push to the branch (`git push origin feature/amazing-feature`)
8. Open a Pull Request

### Code Style

- Follow existing code style
- Add unit tests for new features
- Update documentation as needed
- Ensure all tests pass

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🔗 Related Projects

- [DMOD](https://github.com/choco-technologies/dmod) - The DMOD framework
- [dmod-boot](https://github.com/choco-technologies/dmod-boot) - DMOD bootloader with Python monitor implementation

## 📧 Support

For issues and questions:
- Open an issue on [GitHub](https://github.com/choco-technologies/dmlog/issues)
- Check existing documentation in the repository

---

**Note**: This library is part of the Choco Technologies DMOD ecosystem for embedded systems development.
