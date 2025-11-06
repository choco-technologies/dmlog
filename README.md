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

- **Ring Buffer Architecture**: Circular buffer automatically overwrites oldest entries when full
- **Thread-Safe**: Built-in locking mechanism for multi-threaded environments
- **Zero-Copy Reads**: Direct buffer access for efficient log reading
- **Auto-Flush**: Automatic flushing on newline characters
- **Real-Time Monitoring**: OpenOCD integration for live log monitoring from embedded devices
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

### Calculating Required Buffer Size

```c
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

# Performance benchmarks
./tests/test_benchmark
```

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

### Ring Buffer Structure

DMLoG uses a circular buffer with the following layout:

```
+------------------+
|  Control Header  |  (dmlog_ring_t)
|  - magic         |  Magic number (0x444D4C4F)
|  - flags         |  Status/command flags
|  - head_offset   |  Write position
|  - tail_offset   |  Read position
|  - buffer_size   |  Buffer capacity
+------------------+
|                  |
|   Log Entries    |  Variable-length entries
|   (Ring Buffer)  |  Each with header + data
|                  |
+------------------+
```

### Entry Format

Each log entry contains:
- Magic number (4 bytes): 0x454E5452 ("ENTR")
- Entry size (4 bytes): Total size including header
- Data: Null-terminated log message

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
