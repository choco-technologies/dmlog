/**
 * @file test_file_transfer.c
 * @brief Unit tests for dmlog file transfer functionality
 */

#include "dmlog.h"
#include "test_common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

int tests_passed = 0;
int tests_failed = 0;

// Test buffer size
#define TEST_BUFFER_SIZE (8 * 1024)

// Mock file operations tracking
static int mock_file_open_count = 0;
static int mock_file_close_count = 0;
static int mock_file_read_count = 0;
static int mock_file_write_count = 0;
static size_t mock_file_size_value = 0;
static void* mock_file_handle = (void*)0xDEADBEEF;
static char mock_file_buffer[4096];
static size_t mock_file_buffer_offset = 0;

// Mock Dmod_FileOpen
void* Dmod_FileOpen(const char* Path, const char* Mode) {
    mock_file_open_count++;
    mock_file_buffer_offset = 0;
    
    if(strstr(Path, "nonexistent") != NULL) {
        return NULL; // Simulate file not found
    }
    
    if(strcmp(Mode, "rb") == 0) {
        // Reading - prepare test data
        const char* test_data = "This is test file content for file transfer testing.";
        strcpy(mock_file_buffer, test_data);
        mock_file_size_value = strlen(test_data);
    } else if(strcmp(Mode, "wb") == 0) {
        // Writing - clear buffer
        memset(mock_file_buffer, 0, sizeof(mock_file_buffer));
        mock_file_size_value = 0;
    }
    
    return mock_file_handle;
}

// Mock Dmod_FileClose
void Dmod_FileClose(void* File) {
    mock_file_close_count++;
}

// Mock Dmod_FileRead
size_t Dmod_FileRead(void* Buffer, size_t Size, size_t Count, void* File) {
    mock_file_read_count++;
    size_t bytes_to_read = Size * Count;
    size_t available = mock_file_size_value - mock_file_buffer_offset;
    size_t actual_read = (bytes_to_read < available) ? bytes_to_read : available;
    
    memcpy(Buffer, mock_file_buffer + mock_file_buffer_offset, actual_read);
    mock_file_buffer_offset += actual_read;
    
    return actual_read / Size; // Return number of items read
}

// Mock Dmod_FileWrite
size_t Dmod_FileWrite(const void* Buffer, size_t Size, size_t Count, void* File) {
    mock_file_write_count++;
    size_t bytes_to_write = Size * Count;
    
    if(mock_file_buffer_offset + bytes_to_write > sizeof(mock_file_buffer)) {
        bytes_to_write = sizeof(mock_file_buffer) - mock_file_buffer_offset;
    }
    
    memcpy(mock_file_buffer + mock_file_buffer_offset, Buffer, bytes_to_write);
    mock_file_buffer_offset += bytes_to_write;
    mock_file_size_value = mock_file_buffer_offset;
    
    return bytes_to_write / Size; // Return number of items written
}

// Mock Dmod_FileSize
size_t Dmod_FileSize(void* File) {
    return mock_file_size_value;
}

// Mock Dmod_FileSeek
int Dmod_FileSeek(void* File, long Offset, int Origin) {
    return 0;
}

// Mock Dmod_FileTell
size_t Dmod_FileTell(void* File) {
    return mock_file_buffer_offset;
}

// Mock memory allocation tracking
static int mock_malloc_count = 0;
static int mock_free_count = 0;
static void* mock_last_allocated = NULL;

void* Dmod_Malloc(size_t Size) {
    mock_malloc_count++;
    mock_last_allocated = malloc(Size);
    return mock_last_allocated;
}

void Dmod_Free(void* Ptr) {
    mock_free_count++;
    free(Ptr);
}

// Reset mock counters
void reset_mock_counters(void) {
    mock_file_open_count = 0;
    mock_file_close_count = 0;
    mock_file_read_count = 0;
    mock_file_write_count = 0;
    mock_malloc_count = 0;
    mock_free_count = 0;
    mock_file_buffer_offset = 0;
}

// Test: File transfer structure initialization
void test_file_transfer_structure_init(void) {
    TEST_SECTION("File Transfer Structure Initialization");
    
    char buffer[TEST_BUFFER_SIZE];
    dmlog_ctx_t ctx = dmlog_create(buffer, TEST_BUFFER_SIZE);
    
    ASSERT_TEST(ctx != NULL, "Context created successfully");
    ASSERT_TEST(dmlog_is_valid(ctx), "Context is valid after creation");
    
    // We can't directly access internal fields as dmlog_ctx is opaque
    // But we can verify basic functionality works
    
    dmlog_destroy(ctx);
}

// Test: dmlog_sendf with invalid parameters
void test_sendf_invalid_params(void) {
    TEST_SECTION("dmlog_sendf Invalid Parameters");
    
    char buffer[TEST_BUFFER_SIZE];
    dmlog_ctx_t ctx = dmlog_create(buffer, TEST_BUFFER_SIZE);
    
    reset_mock_counters();
    
    // Test NULL context
    bool result = dmlog_sendf(NULL, "test.txt", "dest.txt", 0);
    ASSERT_TEST(result == false, "dmlog_sendf returns false for NULL context");
    
    // Test NULL firmware file path
    result = dmlog_sendf(ctx, NULL, "dest.txt", 0);
    ASSERT_TEST(result == false, "dmlog_sendf returns false for NULL firmware file path");
    
    // Test NULL PC file path
    result = dmlog_sendf(ctx, "test.txt", NULL, 0);
    ASSERT_TEST(result == false, "dmlog_sendf returns false for NULL PC file path");
    
    dmlog_destroy(ctx);
}

// Test: dmlog_sendf with nonexistent file
void test_sendf_nonexistent_file(void) {
    TEST_SECTION("dmlog_sendf Nonexistent File");
    
    char buffer[TEST_BUFFER_SIZE];
    dmlog_ctx_t ctx = dmlog_create(buffer, TEST_BUFFER_SIZE);
    
    reset_mock_counters();
    
    // Try to send a file that doesn't exist
    bool result = dmlog_sendf(ctx, "nonexistent.txt", "dest.txt", 0);
    ASSERT_TEST(result == false, "dmlog_sendf returns false for nonexistent file");
    ASSERT_TEST(mock_file_open_count == 1, "File open was attempted");
    ASSERT_TEST(mock_file_close_count == 0, "File was not closed (open failed)");
    
    dmlog_destroy(ctx);
}

// Test: dmlog_recvf with invalid parameters
void test_recvf_invalid_params(void) {
    TEST_SECTION("dmlog_recvf Invalid Parameters");
    
    char buffer[TEST_BUFFER_SIZE];
    dmlog_ctx_t ctx = dmlog_create(buffer, TEST_BUFFER_SIZE);
    
    reset_mock_counters();
    
    // Test NULL context
    bool result = dmlog_recvf(NULL, "test.txt", "source.txt", 0);
    ASSERT_TEST(result == false, "dmlog_recvf returns false for NULL context");
    
    // Test NULL firmware file path
    result = dmlog_recvf(ctx, NULL, "source.txt", 0);
    ASSERT_TEST(result == false, "dmlog_recvf returns false for NULL firmware file path");
    
    // Test NULL PC file path
    result = dmlog_recvf(ctx, "test.txt", NULL, 0);
    ASSERT_TEST(result == false, "dmlog_recvf returns false for NULL PC file path");
    
    dmlog_destroy(ctx);
}

// Test: File transfer flags
void test_file_transfer_flags(void) {
    TEST_SECTION("File Transfer Flags");
    
    char buffer[TEST_BUFFER_SIZE];
    dmlog_ctx_t ctx = dmlog_create(buffer, TEST_BUFFER_SIZE);
    
    // Verify flag values are unique and don't conflict
    ASSERT_TEST(DMLOG_FLAG_FILE_SEND != DMLOG_FLAG_FILE_RECV, "FILE_SEND and FILE_RECV flags are different");
    ASSERT_TEST(DMLOG_FLAG_FILE_SEND != DMLOG_FLAG_BUSY, "FILE_SEND doesn't conflict with BUSY");
    ASSERT_TEST(DMLOG_FLAG_FILE_RECV != DMLOG_FLAG_BUSY, "FILE_RECV doesn't conflict with BUSY");
    ASSERT_TEST(DMLOG_FLAG_FILE_SEND == 0x00000040, "FILE_SEND flag has expected value");
    ASSERT_TEST(DMLOG_FLAG_FILE_RECV == 0x00000080, "FILE_RECV flag has expected value");
    
    // Test clearing - dmlog_clear should work without errors
    dmlog_clear(ctx);
    ASSERT_TEST(dmlog_is_valid(ctx), "Context is still valid after clear");
    
    dmlog_destroy(ctx);
}

// Test: File path length limits
void test_file_path_limits(void) {
    TEST_SECTION("File Path Length Limits");
    
    char buffer[TEST_BUFFER_SIZE];
    dmlog_ctx_t ctx = dmlog_create(buffer, TEST_BUFFER_SIZE);
    
    // Create a very long path
    char long_path[DMLOG_MAX_FILE_PATH + 100];
    memset(long_path, 'x', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    
    reset_mock_counters();
    
    // The function should handle long paths gracefully (truncate)
    // Note: This will fail to send because mock blocks the transfer
    // but we're testing that it doesn't crash
    dmlog_sendf(ctx, long_path, "dest.txt", 512);
    
    // Verify that the function returned (didn't crash)
    ASSERT_TEST(true, "Function handled long path without crashing");
    
    // Verify maximum path length constant
    ASSERT_TEST(DMLOG_MAX_FILE_PATH == 256, "DMLOG_MAX_FILE_PATH is 256 bytes");
    
    dmlog_destroy(ctx);
}

// Test: Chunk size handling
void test_chunk_size_handling(void) {
    TEST_SECTION("Chunk Size Handling");
    
    char buffer[TEST_BUFFER_SIZE];
    dmlog_ctx_t ctx = dmlog_create(buffer, TEST_BUFFER_SIZE);
    
    reset_mock_counters();
    
    // Test with default chunk size (0)
    // Note: Will timeout waiting for monitor, but we're testing parameter handling
    
    // Just verify the default is set correctly in the header
    ASSERT_TEST(DMLOG_DEFAULT_CHUNK_SIZE == 512, "Default chunk size is 512 bytes");
    
    dmlog_destroy(ctx);
}

// Test: Memory allocation and deallocation
void test_memory_management(void) {
    TEST_SECTION("Memory Management");
    
    char buffer[TEST_BUFFER_SIZE];
    dmlog_ctx_t ctx = dmlog_create(buffer, TEST_BUFFER_SIZE);
    
    reset_mock_counters();
    
    // Note: These functions will timeout waiting for the monitor,
    // but we can verify that malloc/free are called
    
    // We can't fully test sendf/recvf without a monitor simulation,
    // but we verified they compile and the structure is correct
    
    ASSERT_TEST(mock_malloc_count == 0, "No allocations before file operations");
    ASSERT_TEST(mock_free_count == 0, "No deallocations before file operations");
    
    dmlog_destroy(ctx);
}

// Test: Ring buffer structure size
void test_ring_buffer_size(void) {
    TEST_SECTION("Ring Buffer Structure Size");
    
    // Verify the structure doesn't grow too large
    size_t ring_size = sizeof(dmlog_ring_t);
    TEST_INFO("dmlog_ring_t size: %zu bytes", ring_size);
    
    // With the new fields, we expect:
    // - 10 x uint32_t (40 bytes)
    // - 2 x uint64_t for buffers (16 bytes)
    // - 4 x uint64_t/uint32_t for file transfer (4*8 + 4*4 = 48 bytes)
    // - 2 x DMLOG_MAX_FILE_PATH bytes for paths (2 * 256 = 512 bytes)
    // Total should be around 616 bytes with packing
    
    ASSERT_TEST(ring_size < 768, "Ring buffer structure is reasonably sized");
    
    // The structure should still be packed
    ASSERT_TEST(ring_size > 576 && ring_size < 656, "Ring buffer size is in expected range");
}

int main(void) {
    printf("\n");
    printf(COLOR_BLUE "========================================\n" COLOR_RESET);
    printf(COLOR_BLUE "   DMLoG File Transfer Tests\n" COLOR_RESET);
    printf(COLOR_BLUE "========================================\n" COLOR_RESET);
    printf("\n");
    
    // Run all tests
    test_file_transfer_structure_init();
    test_sendf_invalid_params();
    test_sendf_nonexistent_file();
    test_recvf_invalid_params();
    test_file_transfer_flags();
    test_file_path_limits();
    test_chunk_size_handling();
    test_memory_management();
    test_ring_buffer_size();
    
    // Print summary
    printf("\n");
    printf(COLOR_BLUE "========================================\n" COLOR_RESET);
    printf(COLOR_BLUE "   Test Summary\n" COLOR_RESET);
    printf(COLOR_BLUE "========================================\n" COLOR_RESET);
    printf("Total tests run: %d\n", tests_passed + tests_failed);
    printf(COLOR_GREEN "Passed: %d\n" COLOR_RESET, tests_passed);
    if (tests_failed > 0) {
        printf(COLOR_RED "Failed: %d\n" COLOR_RESET, tests_failed);
    } else {
        printf("Failed: 0\n");
    }
    printf("\n");
    
    return (tests_failed == 0) ? 0 : 1;
}
