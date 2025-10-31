# =====================================================================
#               DMOD Virtual File System
# =====================================================================
cmake_minimum_required(VERSION 3.10)

# ======================================================================
#               DMOD Log
# ======================================================================
project(dmlog
    VERSION 1.0
    DESCRIPTION "DMOD Log Library"
    LANGUAGES C CXX)

set(DMLOG_DONT_IMPLEMENT_DMOD_API OFF CACHE BOOL "Do not implement DMOD API in dmheap library")

# ======================================================================
#               Fetch DMOD repository
# ======================================================================
include(FetchContent)
FetchContent_Declare(
    dmod
    GIT_REPOSITORY https://github.com/choco-technologies/dmod.git
    GIT_TAG        develop
)

# ======================================================================
#               DMOD Configuration
# ======================================================================
set(DMOD_MODE "DMOD_SYSTEM" CACHE STRING "DMOD build mode")
set(DMOD_BUILD_TESTS OFF CACHE BOOL "Build tests")
set(DMOD_BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
set(DMOD_BUILD_TOOLS OFF CACHE BOOL "Build tools")
set(DMOD_BUILD_TEMPLATES OFF CACHE BOOL "Build templates")

FetchContent_MakeAvailable(dmod)
set(DMOD_DIR ${dmod_SOURCE_DIR} CACHE PATH "DMOD source directory" FORCE)

# ======================================================================
#               DMOD Heap Library
# ======================================================================
set(MODULE_NAME dmlog)
add_library(${MODULE_NAME} STATIC
    src/dmlog.c
)

target_compile_definitions(${MODULE_NAME} 
    PRIVATE 
        $<$<BOOL:${DMLOG_DONT_IMPLEMENT_DMOD_API}>:DMLOG_DONT_IMPLEMENT_DMOD_API>
)

target_include_directories(${MODULE_NAME} 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)   

target_link_libraries(${MODULE_NAME} 
    PRIVATE    
        dmod_inc
    )

create_library_makefile(${MODULE_NAME})

# ======================================================================
#               Tools
# ======================================================================    
add_subdirectory(tools)