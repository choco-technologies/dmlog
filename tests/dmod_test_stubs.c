/**
 * @file dmod_test_stubs.c
 * @brief Stub definitions for dmod linker symbols required by tests
 * 
 * This file provides weak symbol definitions for the dmod linker symbols
 * that are normally provided by the dmod-common.ld linker script.
 * Using weak symbols allows tests to link without a custom linker script.
 */

#include <stdint.h>
#include "dmod.h"

// Weak symbol definitions for dmod input/output sections
// These will be overridden if the linker script provides them
__attribute__((weak)) char __dmod_inputs_start = 0;
__attribute__((weak)) char __dmod_inputs_end = 0;
__attribute__((weak)) char __dmod_inputs_size = 0;

__attribute__((weak)) char __dmod_outputs_start = 0;
__attribute__((weak)) char __dmod_outputs_end = 0;
__attribute__((weak)) char __dmod_outputs_size = 0;

// Strong override of dmod's weak Dmod_LockStdio(): on a host build with
// DMOD_USE_STDIO=ON, the weak default resolves DMOD_STDIN/DMOD_STDOUT to the
// real process stdin/stdout instead of reporting them unbound, so
// Dmod_Getc/Dmod_Gets/Dmod_Printf never fall through to dmlog's own
// Dmod_ReadKernel/Dmod_WriteKernel ring-buffer path. Tests need that raw
// kernel-I/O path (it's what they poke via the ring buffer directly), so
// report every stream as unbound here, matching the embedded
// (DMOD_USE_STDIO=OFF) target behavior these tests are meant to exercise.
void* Dmod_LockStdio(void* StdHandle)
{
    (void)StdHandle;
    return NULL;
}
