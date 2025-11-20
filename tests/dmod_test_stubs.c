/**
 * @file dmod_test_stubs.c
 * @brief Stub definitions for dmod linker symbols required by tests
 * 
 * This file provides weak symbol definitions for the dmod linker symbols
 * that are normally provided by the dmod-common.ld linker script.
 * Using weak symbols allows tests to link without a custom linker script.
 */

#include <stdint.h>

// Weak symbol definitions for dmod input/output sections
// These will be overridden if the linker script provides them
__attribute__((weak)) char __dmod_inputs_start = 0;
__attribute__((weak)) char __dmod_inputs_end = 0;
__attribute__((weak)) char __dmod_inputs_size = 0;

__attribute__((weak)) char __dmod_outputs_start = 0;
__attribute__((weak)) char __dmod_outputs_end = 0;
__attribute__((weak)) char __dmod_outputs_size = 0;
