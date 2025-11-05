#include "dmlog.h"
#include <stdio.h>
#include <string.h>

#define TEST_BUFFER_SIZE (4 * 1024)
static char test_buffer[TEST_BUFFER_SIZE];

int main(void) {
    printf("=== Simple DMLOG Test ===\n");
    
    // Test 1: Create context
    dmlog_ctx_t ctx = dmlog_create(test_buffer, TEST_BUFFER_SIZE);
    printf("Create context: %s\n", ctx != NULL ? "PASS" : "FAIL");
    
    // Test 2: Is valid
    bool is_valid = dmlog_is_valid(ctx);
    printf("Is valid: %s\n", is_valid ? "PASS" : "FAIL");
    
    // Test 3: Write a character
    bool result = dmlog_putc(ctx, 'A');
    printf("Put char: %s\n", result ? "PASS" : "FAIL");
    
    // Test 4: Write a string
    result = dmlog_puts(ctx, "Hello, World!\n");
    printf("Put string: %s\n", result ? "PASS" : "FAIL");
    
    // Test 5: Flush the data
    result = dmlog_flush(ctx);
    printf("Flush: %s\n", result ? "PASS" : "FAIL");
    
    // Test 6: Get free space
    dmlog_index_t free_space = dmlog_get_free_space(ctx);
    printf("Get free space: %s (free: %u bytes)\n", 
           free_space > 0 ? "PASS" : "FAIL", free_space);
    
    // Test 7: Read entry
    result = dmlog_read_next(ctx);
    printf("Read next: %s\n", result ? "PASS" : "FAIL");
    
    if (result) {
        char read_buf[256];
        result = dmlog_gets(ctx, read_buf, sizeof(read_buf));
        printf("Get string: %s\n", result ? "PASS" : "FAIL");
        if (result) {
            printf("Read data: '%s'\n", read_buf);
        }
    }
    
    // Test 8: Clear buffer
    dmlog_clear(ctx);
    printf("Clear: PASS\n");
    
    // Test 9: Destroy context
    dmlog_destroy(ctx);
    printf("Destroy: PASS\n");
    
    printf("\nAll simple tests completed!\n");
    return 0;
}
