#ifndef TRACE_H
#define TRACE_H

#include <stdio.h>

#define TRACE_LOG(...) \
        fprintf(stdout, __VA_ARGS__)
#define TRACE_INFO(fmt, ...) \
        TRACE_LOG("[\033[34;1mINFO\033[0m] " fmt, ##__VA_ARGS__)
#define TRACE_WARN(fmt, ...) \
        fprintf(stderr, "[\033[33;1mWARN\033[0m] " fmt, ##__VA_ARGS__)
#define TRACE_ERROR(fmt, ...) \
        fprintf(stderr, "[\033[31;1mERROR\033[0m] " fmt, ##__VA_ARGS__)

#ifdef ENABLE_VERBOSE_TRACE
#   define TRACE_VERBOSE(fmt, ...) \
            TRACE_LOG(fmt, ##__VA_ARGS__)
#else
#   define TRACE_VERBOSE(fmt, ...)
#endif
#endif // TRACE_H