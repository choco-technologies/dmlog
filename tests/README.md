# DMLOG Unit Tests

This directory contains unit tests for the DMLOG library.

## Building and Running Tests

### Build tests
```bash
mkdir build
cd build
cmake ..
make
```

### Run all tests
```bash
ctest --output-on-failure
```

### Run specific test
```bash
./tests/test_dmlog_unit
./tests/test_simple
./tests/test_benchmark
```

### Run benchmark test only
```bash
./tests/test_benchmark
```

This will display performance metrics including:
- Time to write 3000 log messages
- Throughput in logs/second
- Average time per log entry
- Performance with different message sizes
- Read performance metrics
- Buffer wraparound performance

## Code Coverage

To generate code coverage reports:

### Build with coverage enabled
```bash
mkdir build
cd build
cmake -DENABLE_COVERAGE=ON ..
make
```

### Generate coverage report
```bash
make coverage
```

The coverage report will be generated in `build/coverage/html/index.html`.

### View coverage summary
The `make coverage` command will also print a summary to the console showing line coverage percentages for each file.

## Test Files

### Unit Tests

- **test_common.h**: Common test utilities and macros
- **test_simple.c**: Basic smoke tests
- **test_dmlog_unit.c**: Comprehensive unit tests covering:
  - Context creation and validation
  - Write operations (putc, puts, putsn)
  - Read operations (read_next, getc, gets)
  - Buffer management and space tracking
  - Buffer clear operations
  - Multiple entry handling
  - Auto-flush on newline
  - Buffer wraparound scenarios
  - Edge cases and error handling
  - Stress testing
  - Maximum entry size handling
  - Invalid context operations
- **test_benchmark.c**: Performance benchmarks including:
  - 3000 log messages write performance test
  - Varying message size benchmarks (small, medium, large)
  - Read performance measurements
  - Buffer wraparound performance under heavy load
- **test_input.c**: Tests for bidirectional communication (PC → firmware input)
  - Input buffer initialization
  - Single and multiple character input
  - Line-based input reading
  - Buffer wraparound handling
  - Input request functionality

### Integration Tests

- **test_app_interactive.c**: Configurable test application for automated testing
  - Reads test scenarios from input files
  - Logs messages line-by-line to dmlog
  - Supports `<user_input>` marker for input testing
  - Configurable buffer size
  - Designed for gdbserver integration testing

- **test_automated_gdb.sh**: Automated integration test script
  - Tests complete end-to-end GDB server integration
  - Validates output path (firmware → PC via dmlog_monitor)
  - Multiple test scenarios with different buffer sizes
  - Runs automatically in CI on every build
  - **Requires gdbserver** (install with: `apt-get install gdbserver`)

### Test Scenarios

Located in `scenarios/` directory:

- **test_output_only.txt**: Simple output test without user input (✓ Working)
- **test_input_single.txt**: Test with one user input request (future)
- **test_input_multiple.txt**: Test with multiple input requests (future)
- **test_mixed_complex.txt**: Complex mixed output/input scenario (future)

## Running Integration Tests

### Prerequisites

```bash
sudo apt-get install gdbserver
```

### Run automated GDB integration tests

```bash
cd tests
./test_automated_gdb.sh
```

This script will:
1. Build and start the test application under gdbserver
2. Connect dmlog_monitor using GDB backend
3. Validate that expected log messages are received
4. Test different scenarios and buffer sizes
5. Report pass/fail for each test

### Manual testing with test_app_interactive

```bash
# Build the project first
cd build
make test_app_interactive

# Run with a test scenario
./tests/test_app_interactive ../tests/scenarios/test_output_only.txt 4096

# In another terminal, connect dmlog_monitor
./tools/monitor/dmlog_monitor --gdb --addr <buffer_address>
```

## Coverage Goal

The tests aim to achieve >80% line coverage of the dmlog library code.
