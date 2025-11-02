#ifndef TRACE_H
#define TRACE_H

#include <stdio.h>

typedef enum 
{
    TRACE_LEVEL_ERROR = 0,
    TRACE_LEVEL_WARN,
    TRACE_LEVEL_INFO,
    TRACE_LEVEL_VERBOSE
} trace_level_t;

extern trace_level_t current_trace_level;

#define TRACE_LOG(...) \
        fprintf(stdout, __VA_ARGS__)
#define TRACE_INFO(fmt, ...) \
    do { \
        if(current_trace_level >= TRACE_LEVEL_INFO) { \
            TRACE_LOG("[\033[34;1mINFO\033[0m] " fmt, ##__VA_ARGS__);\
        } \
    } while(0)
#define TRACE_WARN(fmt, ...) \
    do { \
        if(current_trace_level >= TRACE_LEVEL_WARN) { \
            fprintf(stderr, "[\033[33;1mWARN\033[0m] " fmt, ##__VA_ARGS__);\
        } \
    } while(0)
#define TRACE_ERROR(fmt, ...) \
    do { \
        if(current_trace_level >= TRACE_LEVEL_ERROR) { \
            fprintf(stderr, "[\033[31;1mERROR\033[0m] " fmt, ##__VA_ARGS__);\
        } \
    } while(0)

#define TRACE_VERBOSE(fmt, ...) \
    do { \
        if(current_trace_level >= TRACE_LEVEL_VERBOSE) { \
            TRACE_LOG(fmt, ##__VA_ARGS__);\
        } \
    } while(0)

#endif // TRACE_H