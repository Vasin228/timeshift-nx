# timeshift - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(timeshift LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Cross builds (Nintendo Switch) compile the SDK from source with devkitA64, and
# the SDK skips its host codegen CLI in that configuration
# (rexglue-sdk/CMakeLists.txt: `if(NOT REX_PLATFORM_SWITCH) add_subdirectory(src/rexglue)`),
# yet codegen always runs on the host. Provide the CLI as an imported target so
# $<TARGET_FILE:rex::rexglue> in generated/rexglue.cmake resolves.
if(NOT TARGET rex::rexglue)
    set(REXGLUE_HOST_CLI "/root/lab/rexglue-sdk/out/linux-amd64/rexgluerd"
        CACHE FILEPATH "Host rexglue CLI used for code generation in cross builds")
    if(EXISTS "${REXGLUE_HOST_CLI}")
        add_executable(rex::rexglue IMPORTED)
        set_target_properties(rex::rexglue PROPERTIES
            IMPORTED_LOCATION "${REXGLUE_HOST_CLI}")
    endif()
endif()

# Sources
set(TIMESHIFT_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(timeshift WIN32 ${TIMESHIFT_SOURCES})
else()
    add_executable(timeshift ${TIMESHIFT_SOURCES})
endif()

rexglue_setup_target(timeshift)
