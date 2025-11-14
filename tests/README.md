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

### Integration Tests
- **test_monitor_app.c**: Test application for dmlog_monitor integration testing
  - Creates a dmlog buffer with test messages
  - Can be run under gdbserver for testing GDB backend
  - Useful for manual testing of monitor connectivity
  
- **test_monitor_gdb_basic.sh**: Basic integration test for GDB backend
  - Validates --gdb flag recognition
  - Tests GDB backend connection attempts
  - Verifies OpenOCD backend as default
  - Runs in CI automatically
  
- **test_monitor_gdb.sh**: Full GDB server integration test (advanced)
  - Tests complete end-to-end GDB server connectivity
  - Validates memory read operations via GDB Remote Serial Protocol
  - Requires gdbserver and more complex setup
  - Can be run manually for comprehensive validation

## Coverage Goal

The tests aim to achieve >80% line coverage of the dmlog library code.
