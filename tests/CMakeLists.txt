# =====================================================================
#               DMLOG Tests
# =====================================================================
cmake_minimum_required(VERSION 3.10)

# Enable testing
enable_testing()

# =====================================================================
#               Test: Unit Tests
# =====================================================================
add_executable(test_dmlog_unit test_dmlog_unit.c)
target_link_libraries(test_dmlog_unit 
    PRIVATE 
        dmlog
        dmod_system
        dmod_common
        dmod_fastlz
        dmod_inc
)
target_include_directories(test_dmlog_unit
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# =====================================================================
#               Test: Simple Test
# =====================================================================
add_executable(test_simple test_simple.c)
target_link_libraries(test_simple 
    PRIVATE 
        dmlog
        dmod_system
        dmod_common
        dmod_fastlz
        dmod_inc
)
target_include_directories(test_simple
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# =====================================================================
#               Test: Benchmark Test
# =====================================================================
add_executable(test_benchmark test_benchmark.c)
target_link_libraries(test_benchmark 
    PRIVATE 
        dmlog
        dmod_system
        dmod_common
        dmod_fastlz
        dmod_inc
)
target_include_directories(test_benchmark
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

add_test(NAME dmlog_unit   COMMAND test_dmlog_unit)
add_test(NAME simple_test  COMMAND test_simple)
add_test(NAME benchmark    COMMAND test_benchmark)

# =====================================================================
#               Coverage Support (optional)
# =====================================================================
option(ENABLE_COVERAGE "Enable code coverage" OFF)

if(ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "Enabling code coverage")
        
        # Add coverage flags to the dmlog library
        target_compile_options(dmlog PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(dmlog PRIVATE --coverage)
        
        # Add coverage flags to test executables
        target_compile_options(test_dmlog_unit PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(test_dmlog_unit PRIVATE --coverage)
        
        target_compile_options(test_simple PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(test_simple PRIVATE --coverage)
        
        target_compile_options(test_benchmark PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(test_benchmark PRIVATE --coverage)
        
        # Add custom target for generating coverage report
        find_program(GCOV gcov)
        find_program(LCOV lcov)
        find_program(GENHTML genhtml)
        
        if(LCOV AND GENHTML)
            add_custom_target(coverage
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
                COMMAND ${LCOV} --directory . --zerocounters
                COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
                COMMAND ${LCOV} --directory . --capture --output-file ${CMAKE_BINARY_DIR}/coverage/coverage.info
                COMMAND ${LCOV} --remove ${CMAKE_BINARY_DIR}/coverage/coverage.info '/usr/*' '*/build/_deps/*' --output-file ${CMAKE_BINARY_DIR}/coverage/coverage_filtered.info
                COMMAND ${GENHTML} ${CMAKE_BINARY_DIR}/coverage/coverage_filtered.info --output-directory ${CMAKE_BINARY_DIR}/coverage/html
                COMMAND ${LCOV} --list ${CMAKE_BINARY_DIR}/coverage/coverage_filtered.info
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Generating code coverage report"
            )
            message(STATUS "Coverage target available. Run 'make coverage' to generate report.")
        else()
            message(WARNING "lcov/genhtml not found. Coverage report generation will not be available.")
        endif()
    else()
        message(WARNING "Code coverage is only supported with GCC or Clang")
    endif()
endif()
