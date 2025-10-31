add_executable(dmlog_monitor
    main.c
)

target_link_libraries(dmlog_monitor
    PRIVATE
        dmlog
)