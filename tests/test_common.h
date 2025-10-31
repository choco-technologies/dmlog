#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>

// VT100 color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31;1m"
#define COLOR_GREEN   "\033[32;1m"
#define COLOR_YELLOW  "\033[33;1m"
#define COLOR_BLUE    "\033[34;1m"
#define COLOR_MAGENTA "\033[35;1m"
#define COLOR_CYAN    "\033[36;1m"

// Test counters - declare as extern, must be defined in test files
extern int tests_passed;
extern int tests_failed;

// Macro for test assertions with color
#define ASSERT_TEST(condition, message) \
    do { \
        if (condition) { \
            tests_passed++; \
            printf("[" COLOR_GREEN "PASS" COLOR_RESET "] %s\n", message); \
        } else { \
            tests_failed++; \
            printf("[" COLOR_RED "FAIL" COLOR_RESET "] %s (line %d)\n", message, __LINE__); \
        } \
    } while(0)

// Helper macro for printing info messages
#define TEST_INFO(message, ...) \
    printf("[" COLOR_CYAN "INFO" COLOR_RESET "] " message "\n", ##__VA_ARGS__)

// Helper macro for printing bench results
#define TEST_BENCH(message, ...) \
    printf("[" COLOR_YELLOW "BENCH" COLOR_RESET "] " message "\n", ##__VA_ARGS__)

// Helper macro for test section headers
#define TEST_SECTION(name) \
    printf("\n" COLOR_MAGENTA "===" COLOR_RESET " %s " COLOR_MAGENTA "===" COLOR_RESET "\n", name)

#endif // TEST_COMMON_H
