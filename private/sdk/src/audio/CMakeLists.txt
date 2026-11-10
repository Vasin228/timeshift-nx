# rexaudio - Audio subsystem library
# Audio processing and XMA decoding for Xbox 360 emulation

add_library(rexaudio OBJECT
    
    audio_driver.cpp
    audio_system.cpp
    downmix.cpp
    xma_register_file.cpp
    # NOP backend (always available)
    nop/nop_audio_system.cpp
)

# SDL3 has no Nintendo Switch platform; the Horizon audio backend (libnx audren)
# is a separate work item, so Switch builds with the NOP backend for now.
if(NOT REX_PLATFORM_SWITCH)
    target_sources(rexaudio PRIVATE
        xma_context.cpp
        xma_decoder.cpp
        sdl/sdl_audio_system.cpp
        sdl/sdl_audio_driver.cpp
    )
else()
    target_sources(rexaudio PRIVATE
        xma_context.cpp
        xma_decoder.cpp
    )
    message(STATUS "Switch build: Real XMA decoding (xma_context.cpp, xma_decoder.cpp) enabled")
endif()
add_library(rex::audio ALIAS rexaudio)

rex_target_pch(rexaudio
    <mutex>
    <rex/math.h>
    <rex/string.h>
    <rex/memory.h>
    <rex/filesystem.h>
    <rex/thread.h>
    <rex/thread/mutex.h>
    <rex/ppc/context.h>
    <rex/system/kernel_state.h>
    <rex/system/xthread.h>
)

target_include_directories(rexaudio PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(REX_PLATFORM_SWITCH)
    # Horizon build: devkitPro portlib libavcodec and libavutil provide real XMA audio decoding
    target_include_directories(rexaudio PRIVATE /opt/devkitpro/portlibs/switch/include)
    find_library(AVCODEC_LIBRARY avcodec HINTS /opt/devkitpro/portlibs/switch/lib)
    find_library(AVUTIL_LIBRARY avutil HINTS /opt/devkitpro/portlibs/switch/lib)
    target_link_libraries(rexaudio PUBLIC rexcore PRIVATE ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY})
else()
    target_link_libraries(rexaudio PUBLIC rexcore SDL3::SDL3 PRIVATE libavcodec libavutil)
endif()
