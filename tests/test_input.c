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

// Helper function to create context and clear initial version message for tests
static dmlog_ctx_t create_test_context(void) {
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    if (ctx) {
        // Clear the initial version message for clean testing
        dmlog_clear(ctx);
    }
    return ctx;
}

// Helper function to simulate PC writing to input buffer
// This simulates what monitor_send_input would do
static bool write_to_input_buffer(dmlog_ctx_t ctx, const char* data, size_t length) {
    // Access the ring structure directly (simulating what monitor does via OpenOCD)
    typedef struct {
        volatile uint32_t           magic;
        volatile uint32_t           flags;
        volatile uint32_t           head_offset;
        volatile uint32_t           tail_offset;
        volatile uint32_t           buffer_size;
        volatile uint64_t           buffer;
        volatile uint32_t           input_head_offset;
        volatile uint32_t           input_tail_offset;
        volatile uint32_t           input_buffer_size;
        volatile uint64_t           input_buffer;
    } __attribute__((packed)) test_ring_t;
    
    test_ring_t* ring = (test_ring_t*)ctx;
    
    // Check available space
    uint32_t input_head = ring->input_head_offset;
    uint32_t input_tail = ring->input_tail_offset;
    uint32_t input_size = ring->input_buffer_size;
    
    uint32_t free_space;
    if(input_head >= input_tail) {
        free_space = input_size - (input_head - input_tail);
    } else {
        free_space = input_tail - input_head;
    }
    free_space = free_space > 0 ? free_space - 1 : 0;
    
    if(length > free_space) {
        return false;
    }
    
    // Write data to input buffer
    uint8_t* input_buffer = (uint8_t*)((uintptr_t)ring->input_buffer);
    for(size_t i = 0; i < length; i++) {
        input_buffer[input_head] = (uint8_t)data[i];
        input_head = (input_head + 1) % input_size;
    }
    
    // Update head offset and set flag
    ring->input_head_offset = input_head;
    ring->flags |= DMLOG_FLAG_INPUT_AVAILABLE;
    
    return true;
}

// Test: Input buffer initialization
static void test_input_buffer_initialization(void) {
    TEST_SECTION("Input Buffer Initialization");
    
    reset_buffer();
    dmlog_ctx_t ctx = create_test_context();
    ASSERT_TEST(ctx != NULL, "Create context with input buffer");
    
    // Check that input buffer is initially empty
    ASSERT_TEST(dmlog_input_available(ctx) == false, "Input buffer is initially empty");
    ASSERT_TEST(dmlog_input_get_free_space(ctx) > 0, "Input buffer has free space");
    
    // Try to read from empty input buffer
    char c = dmlog_input_getc(ctx);
    ASSERT_TEST(c == '\0', "Reading from empty input buffer returns null char");
}

// Test: Writing and reading single character
static void test_input_single_char(void) {
    TEST_SECTION("Input Single Character");
    
    reset_buffer();
    dmlog_ctx_t ctx = create_test_context();
    ASSERT_TEST(ctx != NULL, "Create context");
    
    // Write a single character to input buffer
    const char test_char = 'A';
    ASSERT_TEST(write_to_input_buffer(ctx, &test_char, 1) == true, "Write single char to input buffer");
    
    // Check that input is available
    ASSERT_TEST(dmlog_input_available(ctx) == true, "Input is available after write");
    
    // Read the character
    char c = dmlog_input_getc(ctx);
    ASSERT_TEST(c == 'A', "Read correct character from input buffer");
    
    // After reading, buffer should be empty again
    ASSERT_TEST(dmlog_input_available(ctx) == false, "Input buffer is empty after reading");
}

// Test: Writing and reading a line
static void test_input_line(void) {
    TEST_SECTION("Input Line");
    
    reset_buffer();
    dmlog_ctx_t ctx = create_test_context();
    ASSERT_TEST(ctx != NULL, "Create context");
    
    // Write a line to input buffer
    const char* test_line = "Hello from PC\n";
    size_t len = strlen(test_line);
    ASSERT_TEST(write_to_input_buffer(ctx, test_line, len) == true, "Write line to input buffer");
    
    // Check that input is available
    ASSERT_TEST(dmlog_input_available(ctx) == true, "Input is available after write");
    
    // Read the line using input_gets
    char read_buf[256];
    bool result = dmlog_input_gets(ctx, read_buf, sizeof(read_buf));
    ASSERT_TEST(result == true, "Read line from input buffer");
    ASSERT_TEST(strcmp(read_buf, test_line) == 0, "Read line matches written line");
}

// Test: Multiple lines in input buffer
static void test_input_multiple_lines(void) {
    TEST_SECTION("Input Multiple Lines");
    
    reset_buffer();
    dmlog_ctx_t ctx = create_test_context();
    ASSERT_TEST(ctx != NULL, "Create context");
    
    // Write multiple lines
    const char* line1 = "First line\n";
    const char* line2 = "Second line\n";
    const char* line3 = "Third line\n";
    
    ASSERT_TEST(write_to_input_buffer(ctx, line1, strlen(line1)) == true, "Write first line");
    ASSERT_TEST(write_to_input_buffer(ctx, line2, strlen(line2)) == true, "Write second line");
    ASSERT_TEST(write_to_input_buffer(ctx, line3, strlen(line3)) == true, "Write third line");
    
    // Read all three lines
    char read_buf[256];
    
    ASSERT_TEST(dmlog_input_gets(ctx, read_buf, sizeof(read_buf)) == true, "Read first line");
    ASSERT_TEST(strcmp(read_buf, line1) == 0, "First line matches");
    
    ASSERT_TEST(dmlog_input_gets(ctx, read_buf, sizeof(read_buf)) == true, "Read second line");
    ASSERT_TEST(strcmp(read_buf, line2) == 0, "Second line matches");
    
    ASSERT_TEST(dmlog_input_gets(ctx, read_buf, sizeof(read_buf)) == true, "Read third line");
    ASSERT_TEST(strcmp(read_buf, line3) == 0, "Third line matches");
    
    // After reading all lines, buffer should be empty
    ASSERT_TEST(dmlog_input_available(ctx) == false, "Input buffer is empty after reading all lines");
}

// Test: Input buffer wrap-around
static void test_input_buffer_wraparound(void) {
    TEST_SECTION("Input Buffer Wrap-around");
    
    reset_buffer();
    dmlog_ctx_t ctx = create_test_context();
    ASSERT_TEST(ctx != NULL, "Create context");
    
    // Get initial free space
    uint32_t initial_free_space = dmlog_input_get_free_space(ctx);
    ASSERT_TEST(initial_free_space > 0, "Input buffer has free space");
    
    // Fill buffer almost to capacity
    char fill_data[256];
    memset(fill_data, 'X', sizeof(fill_data));
    fill_data[255] = '\n';
    
    uint32_t written = 0;
    while(dmlog_input_get_free_space(ctx) > 256) {
        if(!write_to_input_buffer(ctx, fill_data, 256)) {
            break;
        }
        written += 256;
    }
    
    ASSERT_TEST(written > 0, "Filled input buffer partially");
    
    // Read some data to free space
    char read_buf[512];
    for(int i = 0; i < 3 && dmlog_input_available(ctx); i++) {
        dmlog_input_gets(ctx, read_buf, sizeof(read_buf));
    }
    
    // Write more data to cause wrap-around
    const char* test_line = "Wrap-around test\n";
    ASSERT_TEST(write_to_input_buffer(ctx, test_line, strlen(test_line)) == true, 
                "Write data after wrap-around");
    
    // Skip to the last line we wrote
    while(dmlog_input_available(ctx)) {
        dmlog_input_gets(ctx, read_buf, sizeof(read_buf));
        if(strcmp(read_buf, test_line) == 0) {
            ASSERT_TEST(true, "Successfully read wrap-around data");
            break;
        }
    }
}

// Test: Input buffer with character-by-character reading
static void test_input_char_by_char(void) {
    TEST_SECTION("Input Character-by-Character Reading");
    
    reset_buffer();
    dmlog_ctx_t ctx = create_test_context();
    ASSERT_TEST(ctx != NULL, "Create context");
    
    // Write a line
    const char* test_line = "Test\n";
    ASSERT_TEST(write_to_input_buffer(ctx, test_line, strlen(test_line)) == true, 
                "Write line to input buffer");
    
    // Read character by character
    char result[256];
    int i = 0;
    char c;
    while((c = dmlog_input_getc(ctx)) != '\0' && i < 255) {
        result[i++] = c;
        if(c == '\n') break;
    }
    result[i] = '\0';
    
    ASSERT_TEST(strcmp(result, test_line) == 0, "Character-by-character reading matches");
}

// Test: Clear function clears input buffer
static void test_input_clear(void) {
    TEST_SECTION("Clear Input Buffer");
    
    reset_buffer();
    dmlog_ctx_t ctx = create_test_context();
    ASSERT_TEST(ctx != NULL, "Create context");
    
    // Write some data to input buffer
    const char* test_line = "Data to be cleared\n";
    ASSERT_TEST(write_to_input_buffer(ctx, test_line, strlen(test_line)) == true, 
                "Write data to input buffer");
    ASSERT_TEST(dmlog_input_available(ctx) == true, "Input is available");
    
    // Clear the context
    dmlog_clear(ctx);
    
    // Check that input buffer is cleared
    ASSERT_TEST(dmlog_input_available(ctx) == false, "Input buffer is cleared");
    char c = dmlog_input_getc(ctx);
    ASSERT_TEST(c == '\0', "Reading from cleared input buffer returns null char");
}

// Test: Input buffer overflow protection
static void test_input_buffer_overflow(void) {
    TEST_SECTION("Input Buffer Overflow Protection");
    
    reset_buffer();
    dmlog_ctx_t ctx = create_test_context();
    ASSERT_TEST(ctx != NULL, "Create context");
    
    // Get buffer capacity
    uint32_t free_space = dmlog_input_get_free_space(ctx);
    ASSERT_TEST(free_space > 0, "Input buffer has free space");
    
    // Try to fill buffer completely
    char* large_data = malloc(free_space + 100);
    ASSERT_TEST(large_data != NULL, "Allocate test data");
    memset(large_data, 'A', free_space + 100);
    
    // Writing more than available should fail
    bool result = write_to_input_buffer(ctx, large_data, free_space + 100);
    ASSERT_TEST(result == false, "Writing beyond capacity fails");
    
    // Writing exactly the available amount should succeed
    result = write_to_input_buffer(ctx, large_data, free_space);
    ASSERT_TEST(result == true, "Writing within capacity succeeds");
    
    free(large_data);
}

// Test: Input request functionality
static void test_input_request(void) {
    TEST_SECTION("Input Request Functionality");
    
    reset_buffer();
    dmlog_ctx_t ctx = create_test_context();
    ASSERT_TEST(ctx != NULL, "Create context");
    
    // Access the ring structure directly to check flags
    typedef struct {
        volatile uint32_t           magic;
        volatile uint32_t           flags;
        volatile uint32_t           head_offset;
        volatile uint32_t           tail_offset;
        volatile uint32_t           buffer_size;
        volatile uint64_t           buffer;
        volatile uint32_t           input_head_offset;
        volatile uint32_t           input_tail_offset;
        volatile uint32_t           input_buffer_size;
        volatile uint64_t           input_buffer;
    } __attribute__((packed)) test_ring_t;
    
    test_ring_t* ring = (test_ring_t*)ctx;
    
    // Initially INPUT_REQUESTED flag should not be set
    ASSERT_TEST((ring->flags & DMLOG_FLAG_INPUT_REQUESTED) == 0, "INPUT_REQUESTED flag initially not set");
    
    // Request input from firmware
    dmlog_input_request(ctx);
    
    // Check that INPUT_REQUESTED flag is now set
    ASSERT_TEST((ring->flags & DMLOG_FLAG_INPUT_REQUESTED) != 0, "INPUT_REQUESTED flag is set after request");
    
    // Clear the context - should clear the flag
    dmlog_clear(ctx);
    ASSERT_TEST((ring->flags & DMLOG_FLAG_INPUT_REQUESTED) == 0, "INPUT_REQUESTED flag cleared after dmlog_clear");
}

int main(void) {
    printf("Running dmlog input tests...\n\n");
    
    test_input_buffer_initialization();
    test_input_single_char();
    test_input_line();
    test_input_multiple_lines();
    test_input_buffer_wraparound();
    test_input_char_by_char();
    test_input_clear();
    test_input_buffer_overflow();
    test_input_request();
    
    // Print summary
    printf("\n");
    printf("=====================================\n");
    printf("Test Summary:\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Total:  %d\n", tests_passed + tests_failed);
    printf("=====================================\n");
    
    return tests_failed > 0 ? 1 : 0;
}
