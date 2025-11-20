dmod_add_tool(dmlog_monitor
    main.c
    openocd.c
    monitor.c
    trace.c
    backend.c
    gdb.c
)

target_link_libraries(dmlog_monitor
    dmlog
)

# Add project version to the compiler definitions
target_compile_definitions(dmlog_monitor
    PRIVATE
        DMLOG_VERSION="${PROJECT_VERSION}"
)