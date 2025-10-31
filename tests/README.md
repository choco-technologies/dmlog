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

## Coverage Goal

The tests aim to achieve >80% line coverage of the dmlog library code.
