add_executable(dmlog_monitor
    main.c
    openocd.c
    monitor.c
)

target_link_libraries(dmlog_monitor
    PRIVATE
        dmlog
)

# Add project version to the compiler definitions
target_compile_definitions(dmlog_monitor
    PRIVATE
        DMLOG_VERSION="${PROJECT_VERSION}"
)