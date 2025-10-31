#include "dmlog.h"
#include "test_common.h"
#include <string.h>
#include <stdlib.h>

// Test counters
int tests_passed = 0;
int tests_failed = 0;

#define TEST_BUFFER_SIZE (8 * 1024)  // 8KB for tests
static char test_buffer[TEST_BUFFER_SIZE];

// Helper function to reset buffer for each test
static void reset_buffer(void) {
    memset(test_buffer, 0, TEST_BUFFER_SIZE);
}

// Test: Context creation and validation
static void test_context_creation(void) {
    TEST_SECTION("Context Creation and Validation");
    
    // Test with valid parameters
    reset_buffer();
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context with valid parameters");
    ASSERT_TEST(dmlog_is_valid(ctx) == true, "Context is valid after creation");
    
    // Note: Cannot test with too small buffer as DMOD_ASSERT_MSG will terminate
    // the program. The API expects callers to provide valid buffers.
    
    // Test destroying valid context
    dmlog_destroy(ctx);
    ASSERT_TEST(dmlog_is_valid(ctx) == false, "Context is invalid after destroy");
}

// Test: Basic write operations
static void test_basic_write(void) {
    TEST_SECTION("Basic Write Operations");
    reset_buffer();
    
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for write tests");
    
    // Test writing a single character
    bool result = dmlog_putc(ctx, 'A');
    ASSERT_TEST(result == true, "Write single character");
    
    // Test writing multiple characters
    result = dmlog_putc(ctx, 'B');
    ASSERT_TEST(result == true, "Write second character");
    result = dmlog_putc(ctx, 'C');
    ASSERT_TEST(result == true, "Write third character");
    
    // Flush to complete the entry
    result = dmlog_flush(ctx);
    ASSERT_TEST(result == true, "Flush write buffer");
    
    dmlog_destroy(ctx);
}

// Test: String write operations
static void test_string_write(void) {
    TEST_SECTION("String Write Operations");
    reset_buffer();
    
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for string tests");
    
    // Test writing a simple string
    bool result = dmlog_puts(ctx, "Hello, World!\n");
    ASSERT_TEST(result == true, "Write simple string");
    
    // Test writing an empty string
    result = dmlog_puts(ctx, "");
    ASSERT_TEST(result == true, "Write empty string");
    
    // Test writing a string with specific length
    const char* test_str = "Test string";
    result = dmlog_putsn(ctx, test_str, 4);
    ASSERT_TEST(result == true, "Write string with length limit");
    
    // Test writing longer string
    char long_str[256];
    memset(long_str, 'X', sizeof(long_str) - 1);
    long_str[sizeof(long_str) - 1] = '\0';
    result = dmlog_puts(ctx, long_str);
    ASSERT_TEST(result == true, "Write long string");
    
    dmlog_destroy(ctx);
}

// Test: Read operations
static void test_read_operations(void) {
    TEST_SECTION("Read Operations");
    reset_buffer();
    
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for read tests");
    
    // Write some test data
    const char* test_msg = "Test message\n";
    bool result = dmlog_puts(ctx, test_msg);
    ASSERT_TEST(result == true, "Write test message");
    
    // Read the entry
    result = dmlog_read_next(ctx);
    ASSERT_TEST(result == true, "Read next entry");
    
    // Get string from entry
    char read_buf[256];
    result = dmlog_gets(ctx, read_buf, sizeof(read_buf));
    ASSERT_TEST(result == true, "Get string from entry");
    ASSERT_TEST(strcmp(read_buf, test_msg) == 0, "Read data matches written data");
    
    // Try to read when buffer is empty (after consuming the entry)
    // Since dmlog_gets consumed the entry, trying to read another should fail
    result = dmlog_read_next(ctx);
    // Note: result might be true or false depending on internal state
    TEST_INFO("Read from buffer after consuming entry: %s", result ? "success" : "empty");
    
    dmlog_destroy(ctx);
}

// Test: Character-by-character read
static void test_getc(void) {
    TEST_SECTION("Character Read Operations");
    reset_buffer();
    
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for getc tests");
    
    // Write test data
    const char* test_str = "ABC";
    dmlog_puts(ctx, test_str);
    
    // Read entry first
    bool result = dmlog_read_next(ctx);
    ASSERT_TEST(result == true, "Read entry for getc test");
    
    // Read characters one by one
    char c1 = dmlog_getc(ctx);
    ASSERT_TEST(c1 == 'A', "Read first character");
    
    char c2 = dmlog_getc(ctx);
    ASSERT_TEST(c2 == 'B', "Read second character");
    
    char c3 = dmlog_getc(ctx);
    ASSERT_TEST(c3 == 'C', "Read third character");
    
    dmlog_destroy(ctx);
}

// Test: Buffer space management
static void test_space_management(void) {
    TEST_SECTION("Buffer Space Management");
    reset_buffer();
    
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for space tests");
    
    // Get initial free space
    dmlog_index_t initial_space = dmlog_get_free_space(ctx);
    ASSERT_TEST(initial_space > 0, "Initial free space is positive");
    TEST_INFO("Initial free space: %u bytes", initial_space);
    
    // Write some data
    dmlog_puts(ctx, "Some test data\n");
    
    // Check free space decreased
    dmlog_index_t after_write = dmlog_get_free_space(ctx);
    ASSERT_TEST(after_write < initial_space, "Free space decreased after write");
    TEST_INFO("Free space after write: %u bytes", after_write);
    
    // Get left space in current entry
    dmlog_index_t left_space = dmlog_left_entry_space(ctx);
    TEST_INFO("Left space in current entry: %u bytes", left_space);
    ASSERT_TEST(left_space > 0, "Left entry space is positive");
    
    dmlog_destroy(ctx);
}

// Test: Buffer clear operation
static void test_clear_buffer(void) {
    TEST_SECTION("Buffer Clear Operations");
    reset_buffer();
    
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for clear tests");
    
    // Write some data
    dmlog_puts(ctx, "Data to be cleared\n");
    dmlog_puts(ctx, "More data\n");
    
    // Get free space before clear
    dmlog_index_t space_before = dmlog_get_free_space(ctx);
    
    // Clear the buffer
    dmlog_clear(ctx);
    
    // Get free space after clear
    dmlog_index_t space_after = dmlog_get_free_space(ctx);
    ASSERT_TEST(space_after > space_before, "Free space increased after clear");
    
    // Try to read from cleared buffer
    bool result = dmlog_read_next(ctx);
    ASSERT_TEST(result == false, "Read from cleared buffer should fail");
    
    dmlog_destroy(ctx);
}

// Test: Multiple entries handling
static void test_multiple_entries(void) {
    TEST_SECTION("Multiple Entries Handling");
    reset_buffer();
    
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for multiple entries test");
    
    // Write multiple entries
    const char* msg1 = "First message\n";
    const char* msg2 = "Second message\n";
    const char* msg3 = "Third message\n";
    
    dmlog_puts(ctx, msg1);
    dmlog_puts(ctx, msg2);
    dmlog_puts(ctx, msg3);
    
    // Read them back in order
    char read_buf[256];
    
    bool result = dmlog_read_next(ctx);
    ASSERT_TEST(result == true, "Read first entry");
    dmlog_gets(ctx, read_buf, sizeof(read_buf));
    ASSERT_TEST(strcmp(read_buf, msg1) == 0, "First entry matches");
    
    result = dmlog_read_next(ctx);
    ASSERT_TEST(result == true, "Read second entry");
    dmlog_gets(ctx, read_buf, sizeof(read_buf));
    ASSERT_TEST(strcmp(read_buf, msg2) == 0, "Second entry matches");
    
    result = dmlog_read_next(ctx);
    ASSERT_TEST(result == true, "Read third entry");
    dmlog_gets(ctx, read_buf, sizeof(read_buf));
    ASSERT_TEST(strcmp(read_buf, msg3) == 0, "Third entry matches");
    
    dmlog_destroy(ctx);
}

// Test: Auto-flush on newline
static void test_auto_flush(void) {
    TEST_SECTION("Auto-flush on Newline");
    reset_buffer();
    
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for auto-flush test");
    
    // Write characters including newline (should auto-flush)
    dmlog_putc(ctx, 'T');
    dmlog_putc(ctx, 'e');
    dmlog_putc(ctx, 's');
    dmlog_putc(ctx, 't');
    dmlog_putc(ctx, '\n');  // This should trigger flush
    
    // Should be able to read immediately
    bool result = dmlog_read_next(ctx);
    ASSERT_TEST(result == true, "Auto-flush allows immediate read");
    
    char read_buf[256];
    dmlog_gets(ctx, read_buf, sizeof(read_buf));
    ASSERT_TEST(strcmp(read_buf, "Test\n") == 0, "Auto-flushed data is correct");
    
    dmlog_destroy(ctx);
}

// Test: Buffer wraparound
static void test_buffer_wraparound(void) {
    TEST_SECTION("Buffer Wraparound");
    
    // Use a smaller buffer for this test (but large enough for context + some entries)
    char small_buffer[2048];
    memset(small_buffer, 0, sizeof(small_buffer));
    
    dmlog_ctx_t ctx = dmlog_create(small_buffer, sizeof(small_buffer));
    ASSERT_TEST(ctx != NULL, "Create context with small buffer");
    
    // Fill the buffer with entries
    char msg[64];
    int entries_written = 0;
    for (int i = 0; i < 50; i++) {
        snprintf(msg, sizeof(msg), "Entry %d\n", i);
        if (dmlog_puts(ctx, msg)) {
            entries_written++;
        }
    }
    
    ASSERT_TEST(entries_written > 0, "Successfully wrote entries to buffer");
    TEST_INFO("Wrote %d entries", entries_written);
    
    // Try to read some entries (oldest might be overwritten)
    int entries_read = 0;
    while (dmlog_read_next(ctx)) {
        char read_buf[256];
        if (dmlog_gets(ctx, read_buf, sizeof(read_buf))) {
            entries_read++;
        }
    }
    
    ASSERT_TEST(entries_read > 0, "Successfully read entries from buffer");
    TEST_INFO("Read %d entries", entries_read);
    
    dmlog_destroy(ctx);
}

// Test: Edge cases
static void test_edge_cases(void) {
    TEST_SECTION("Edge Cases");
    reset_buffer();
    
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for edge case tests");
    
    // Test with empty string
    bool result = dmlog_puts(ctx, "");
    ASSERT_TEST(result == true, "Put empty string");
    
    // Test with zero-length putsn
    result = dmlog_putsn(ctx, "test", 0);
    ASSERT_TEST(result == true, "Write zero-length string");
    
    // Clear buffer before testing small buffer read
    dmlog_clear(ctx);
    
    // Test gets with small buffer
    dmlog_puts(ctx, "Long test message\n");
    dmlog_read_next(ctx);
    char small_buf[8];
    result = dmlog_gets(ctx, small_buf, sizeof(small_buf));
    // dmlog_gets returns false if no characters were read
    // With a small buffer, we should still get some characters
    TEST_INFO("Read into small buffer: '%s'", small_buf);
    ASSERT_TEST(strlen(small_buf) > 0, "Got some characters with small buffer");
    
    dmlog_destroy(ctx);
}

// Test: Stress test
static void test_stress(void) {
    TEST_SECTION("Stress Test");
    reset_buffer();
    
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for stress test");
    
    // Write many entries rapidly
    int write_count = 0;
    for (int i = 0; i < 100; i++) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Stress test message number %d\n", i);
        if (dmlog_puts(ctx, msg)) {
            write_count++;
        }
    }
    
    ASSERT_TEST(write_count > 0, "Wrote entries in stress test");
    TEST_INFO("Successfully wrote %d entries", write_count);
    
    // Read back as many as possible
    int read_count = 0;
    char read_buf[256];
    while (dmlog_read_next(ctx)) {
        if (dmlog_gets(ctx, read_buf, sizeof(read_buf))) {
            read_count++;
        }
    }
    
    ASSERT_TEST(read_count > 0, "Read entries in stress test");
    TEST_INFO("Successfully read %d entries", read_count);
    
    dmlog_destroy(ctx);
}

// Test: Maximum entry size
static void test_max_entry_size(void) {
    TEST_SECTION("Maximum Entry Size");
    reset_buffer();
    
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    ASSERT_TEST(ctx != NULL, "Create context for max size test");
    
    // Create a message close to max size
    char large_msg[DMOD_LOG_MAX_ENTRY_SIZE];
    memset(large_msg, 'X', DMOD_LOG_MAX_ENTRY_SIZE - 2);
    large_msg[DMOD_LOG_MAX_ENTRY_SIZE - 2] = '\n';
    large_msg[DMOD_LOG_MAX_ENTRY_SIZE - 1] = '\0';
    
    bool result = dmlog_puts(ctx, large_msg);
    ASSERT_TEST(result == true, "Write maximum size entry");
    
    // Read it back
    result = dmlog_read_next(ctx);
    ASSERT_TEST(result == true, "Read maximum size entry");
    
    dmlog_destroy(ctx);
}

// Test: Invalid context operations
static void test_invalid_context(void) {
    TEST_SECTION("Invalid Context Operations");
    
    // Test operations on NULL context
    bool result = dmlog_is_valid(NULL);
    ASSERT_TEST(result == false, "NULL context is invalid");
    
    result = dmlog_putc(NULL, 'A');
    ASSERT_TEST(result == false, "Write to NULL context fails");
    
    result = dmlog_puts(NULL, "test");
    ASSERT_TEST(result == false, "Put string to NULL context fails");
    
    dmlog_index_t space = dmlog_get_free_space(NULL);
    ASSERT_TEST(space == 0, "Free space on NULL context is zero");
    
    // Operations should not crash
    dmlog_clear(NULL);
    dmlog_destroy(NULL);
    TEST_INFO("NULL context operations handled gracefully");
}

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("     DMLOG Unit Tests\n");
    printf("========================================\n");
    
    // Run all tests
    test_context_creation();
    test_basic_write();
    test_string_write();
    test_read_operations();
    test_getc();
    test_space_management();
    test_clear_buffer();
    test_multiple_entries();
    test_auto_flush();
    test_buffer_wraparound();
    test_edge_cases();
    test_stress();
    test_max_entry_size();
    test_invalid_context();
    
    // Print summary
    printf("\n");
    printf("========================================\n");
    printf("          Test Summary\n");
    printf("========================================\n");
    printf("Tests Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed);
    printf("Tests Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed);
    printf("Total Tests:  %d\n", tests_passed + tests_failed);
    
    if (tests_failed == 0) {
        printf("\n" COLOR_GREEN "All tests passed!" COLOR_RESET "\n\n");
        return 0;
    } else {
        printf("\n" COLOR_RED "Some tests failed!" COLOR_RESET "\n\n");
        return 1;
    }
}
