#include "dmlog.h"
#include "test_common.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

// Test counters
int tests_passed = 0;
int tests_failed = 0;

#define TEST_BUFFER_SIZE (256 * 1024)  // 256KB buffer
static char test_buffer[TEST_BUFFER_SIZE];

// Get current time in microseconds
static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
}

// Test: Benchmark 3000 log messages
static void test_benchmark_3000_logs(void) {
    TEST_SECTION("Benchmark: 3000 Log Messages");
    
    memset(test_buffer, 0, TEST_BUFFER_SIZE);
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for benchmark");
    
    const int NUM_LOGS = 3000;
    char log_message[128];
    
    // Start timing
    double start_time = get_time_us();
    
    // Write 3000 log messages
    for (int i = 0; i < NUM_LOGS; i++) {
        snprintf(log_message, sizeof(log_message), 
                 "Log message #%d: This is a test log entry with some data\n", i);
        dmlog_puts(ctx, log_message);
        dmlog_flush(ctx);
    }
    
    // End timing
    double end_time = get_time_us();
    double elapsed_us = end_time - start_time;
    double elapsed_ms = elapsed_us / 1000.0;
    double elapsed_s = elapsed_us / 1000000.0;
    
    // Calculate metrics
    double logs_per_second = NUM_LOGS / elapsed_s;
    double avg_time_per_log_us = elapsed_us / NUM_LOGS;
    
    ASSERT_TEST(elapsed_us > 0, "Elapsed time is positive");
    
    // Print benchmark results
    TEST_BENCH("Total logs written: %d", NUM_LOGS);
    TEST_BENCH("Total time: %.3f ms (%.6f seconds)", elapsed_ms, elapsed_s);
    TEST_BENCH("Average time per log: %.3f μs", avg_time_per_log_us);
    TEST_BENCH("Throughput: %.0f logs/second", logs_per_second);
    
    // Verify we can read back some entries
    int read_count = 0;
    while (dmlog_read_next(ctx) && read_count < 10) {
        char read_buf[256];
        if (dmlog_gets(ctx, read_buf, sizeof(read_buf))) {
            read_count++;
        }
    }
    
    TEST_INFO("Successfully read back %d entries", read_count);
    ASSERT_TEST(read_count > 0, "Can read back logged entries");
    
    dmlog_destroy(ctx);
}

// Test: Benchmark with different message sizes
static void test_benchmark_varying_sizes(void) {
    TEST_SECTION("Benchmark: Varying Message Sizes");
    
    memset(test_buffer, 0, TEST_BUFFER_SIZE);
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for size benchmark");
    
    const int NUM_LOGS = 1000;
    char log_message[256];
    
    // Test with small messages (10-20 chars)
    double start_time = get_time_us();
    for (int i = 0; i < NUM_LOGS; i++) {
        snprintf(log_message, sizeof(log_message), "Short %d\n", i);
        dmlog_puts(ctx, log_message);
        dmlog_flush(ctx);
    }
    double small_time = (get_time_us() - start_time) / 1000.0;
    
    dmlog_clear(ctx);
    
    // Test with medium messages (50-70 chars)
    start_time = get_time_us();
    for (int i = 0; i < NUM_LOGS; i++) {
        snprintf(log_message, sizeof(log_message), 
                 "Medium message %d with some additional content here\n", i);
        dmlog_puts(ctx, log_message);
        dmlog_flush(ctx);
    }
    double medium_time = (get_time_us() - start_time) / 1000.0;
    
    dmlog_clear(ctx);
    
    // Test with large messages (200-250 chars)
    start_time = get_time_us();
    for (int i = 0; i < NUM_LOGS; i++) {
        snprintf(log_message, sizeof(log_message), 
                 "Large message %d: This is a much longer log entry with significantly "
                 "more content to test the performance characteristics of the logging "
                 "system when dealing with larger message payloads that approach the "
                 "maximum size\n", i);
        dmlog_puts(ctx, log_message);
        dmlog_flush(ctx);
    }
    double large_time = (get_time_us() - start_time) / 1000.0;
    
    TEST_BENCH("Small messages (10-20 chars): %.3f ms for %d logs", small_time, NUM_LOGS);
    TEST_BENCH("Medium messages (50-70 chars): %.3f ms for %d logs", medium_time, NUM_LOGS);
    TEST_BENCH("Large messages (200-250 chars): %.3f ms for %d logs", large_time, NUM_LOGS);
    
    ASSERT_TEST(small_time > 0 && medium_time > 0 && large_time > 0, 
                "All benchmark times are positive");
    
    dmlog_destroy(ctx);
}

// Test: Benchmark read performance
static void test_benchmark_read_performance(void) {
    TEST_SECTION("Benchmark: Read Performance");
    
    memset(test_buffer, 0, TEST_BUFFER_SIZE);
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for read benchmark");
    
    const int NUM_LOGS = 1000;
    char log_message[128];
    
    // Write logs
    for (int i = 0; i < NUM_LOGS; i++) {
        snprintf(log_message, sizeof(log_message), 
                 "Test log entry number %d for read performance testing\n", i);
        dmlog_puts(ctx, log_message);
        dmlog_flush(ctx);
    }
    
    // Benchmark reading
    double start_time = get_time_us();
    int read_count = 0;
    char read_buf[256];
    
    while (dmlog_read_next(ctx)) {
        if (dmlog_gets(ctx, read_buf, sizeof(read_buf))) {
            read_count++;
        }
    }
    
    double elapsed_ms = (get_time_us() - start_time) / 1000.0;
    double avg_read_time_us = (elapsed_ms * 1000.0) / read_count;
    
    TEST_BENCH("Read %d entries in %.3f ms", read_count, elapsed_ms);
    TEST_BENCH("Average read time: %.3f μs per entry", avg_read_time_us);
    
    ASSERT_TEST(read_count > 0, "Successfully read entries");
    
    dmlog_destroy(ctx);
}

// Test: Buffer wraparound performance
static void test_benchmark_wraparound(void) {
    TEST_SECTION("Benchmark: Buffer Wraparound Performance");
    
    // Use smaller buffer to force wraparound
    char small_buffer[16 * 1024];
    memset(small_buffer, 0, sizeof(small_buffer));
    
    dmlog_ctx_t ctx = dmlog_create(small_buffer, sizeof(small_buffer));
    ASSERT_TEST(ctx != NULL, "Create context for wraparound benchmark");
    
    const int NUM_LOGS = 2000;
    char log_message[128];
    
    double start_time = get_time_us();
    
    // Write many logs to force buffer wraparound
    for (int i = 0; i < NUM_LOGS; i++) {
        snprintf(log_message, sizeof(log_message), 
                 "Wraparound test message %d with content\n", i);
        dmlog_puts(ctx, log_message);
        dmlog_flush(ctx);
    }
    
    double elapsed_ms = (get_time_us() - start_time) / 1000.0;
    double avg_time_us = (elapsed_ms * 1000.0) / NUM_LOGS;
    
    TEST_BENCH("Wrote %d logs with wraparound in %.3f ms", NUM_LOGS, elapsed_ms);
    TEST_BENCH("Average time per log (with wraparound): %.3f μs", avg_time_us);
    
    ASSERT_TEST(elapsed_ms > 0, "Wraparound benchmark completed");
    
    dmlog_destroy(ctx);
}

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("     DMLOG Performance Benchmarks\n");
    printf("========================================\n");
    
    // Run all benchmark tests
    test_benchmark_3000_logs();
    test_benchmark_varying_sizes();
    test_benchmark_read_performance();
    test_benchmark_wraparound();
    
    // Print summary
    printf("\n");
    printf("========================================\n");
    printf("          Benchmark Summary\n");
    printf("========================================\n");
    printf("Tests Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed);
    printf("Tests Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed);
    printf("Total Tests:  %d\n", tests_passed + tests_failed);
    
    if (tests_failed == 0) {
        printf("\n" COLOR_GREEN "All benchmarks completed!" COLOR_RESET "\n\n");
        return 0;
    } else {
        printf("\n" COLOR_RED "Some benchmarks failed!" COLOR_RESET "\n\n");
        return 1;
    }
}
