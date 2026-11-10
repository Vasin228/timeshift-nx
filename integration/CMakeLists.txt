cmake_minimum_required(VERSION 3.25)
project(timeshift_renut_core_probe LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(TS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(TS_UP "${TS_ROOT}/upstream/sa2-nx/app")
set(TS_GPU "${TS_UP}/src/renut_engine/gpu")
set(TS_SDK "${TS_ROOT}/private/sdk")
set(TS_SDK_THIRDPARTY "/root/lab/rexglue-sdk/thirdparty" CACHE PATH "Read-only dependencies of the frozen SDK")
set(TS_TRANSLATOR "${TS_UP}/thirdparty/reblue-XenosRecomp")
set(PLUME_PLATFORM_SWITCH ON CACHE BOOL "" FORCE)
set(PLUME_SWITCH_DEVKITPRO "/opt/devkitpro" CACHE PATH "" FORCE)
set(PLUME_SWITCH_NO_NVK ON CACHE BOOL "" FORCE)
set(PLUME_DEKO3D ON CACHE BOOL "" FORCE)
set(PLUME_SWITCH_UAM_DIR "${TS_UP}/thirdparty/uam" CACHE PATH "" FORCE)
add_subdirectory("${TS_UP}/thirdparty/plume" "${CMAKE_BINARY_DIR}/plume")

# Decoder copies must match XenosRecomp's cache ABI.
file(GLOB ts_zstd_sources "${TS_TRANSLATOR}/thirdparty/zstd/lib/common/*.c"
                          "${TS_TRANSLATOR}/thirdparty/zstd/lib/decompress/*.c")
add_library(ts_renut_zstd STATIC ${ts_zstd_sources})
target_compile_definitions(ts_renut_zstd PRIVATE ZSTD_DISABLE_ASM)
target_include_directories(ts_renut_zstd PUBLIC "${TS_TRANSLATOR}/thirdparty/zstd/lib")
add_library(ts_renut_smolv STATIC "${TS_TRANSLATOR}/thirdparty/smol-v/source/smolv.cpp")
target_include_directories(ts_renut_smolv PUBLIC "${TS_TRANSLATOR}/thirdparty/smol-v/source")
add_library(ts_renut_xxhash STATIC "${TS_TRANSLATOR}/thirdparty/xxHash/xxhash.c")
target_include_directories(ts_renut_xxhash PUBLIC "${TS_TRANSLATOR}/thirdparty/xxHash")

# Compile the real renderer and its SDK interfaces, without title hooks. This
# intentionally produces objects, not a game executable or an enabled backend.
# No SA2/N&B address can override a generated TimeShift function in this target.
set(ts_core_sources
    backend/backend.cpp backend/gpu_profile.cpp backend/memory_report.cpp
    backend/native_window.cpp backend/present.cpp backend/queued.cpp
    backend/render_thread.cpp backend/video.cpp
    guest/callbacks.cpp guest/command_buffers.cpp guest/guest_records.cpp
    overlay/app_integration.cpp overlay/overlay_drawer.cpp overlay/overlay_ui.cpp
    render/draw.cpp render/format.cpp render/graphics_options.cpp render/pipeline.cpp
    render/shader_library.cpp render/surfaces.cpp render/texture_layout.cpp
    render/textures.cpp render/upload_ring.cpp render/vertex_format.cpp
    banjo_renut_deko3d.cpp)
list(TRANSFORM ts_core_sources PREPEND "${TS_GPU}/")
add_library(timeshift_renut_core OBJECT ${ts_core_sources}
    "${TS_ROOT}/build/renut-switch/shader_cache_switch.cpp"
    "${TS_ROOT}/build/renut-switch/deko3d_glsl_switch.cpp")
target_compile_definitions(timeshift_renut_core PRIVATE
    RENUT_RECOMP TIMESHIFT_RECOMP SA2_SWITCH_NO_NVK=1
    REXGLUE_PLATFORM_SWITCH=1 FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL)
target_include_directories(timeshift_renut_core PRIVATE
    "${TS_ROOT}/adapter" "${TS_UP}/src" "${TS_GPU}/shaders"
    "${TS_ROOT}/build/renut-switch" "${TS_SDK}/include"
    "${TS_GPU}/../sa2_compat" "${TS_SDK}/src/core"
    "${TS_SDK_THIRDPARTY}/simde" "${TS_SDK_THIRDPARTY}/fmt/include"
    "${TS_SDK_THIRDPARTY}/spdlog/include" "${TS_SDK_THIRDPARTY}/imgui"
    "${TS_SDK_THIRDPARTY}/o1heap/o1heap"
    "${TS_TRANSLATOR}/XenosRecomp")
target_link_libraries(timeshift_renut_core PRIVATE plume ts_renut_zstd ts_renut_smolv ts_renut_xxhash)
# This SDK has its own xxHash; the shader cache's version must win for ReNut.
target_include_directories(timeshift_renut_core BEFORE PRIVATE "${TS_TRANSLATOR}/thirdparty/xxHash")

# The first title-specific slice. Production hooks remain dormant; original
# guest calls are always retained. Do not add SA2 resource/device/draw sources.
add_library(timeshift_renut_resource_hooks OBJECT
    "${TS_GPU}/d3d/hooks/shader.cpp"
    "${TS_ROOT}/adapter/shader_capture.cpp"
    "${TS_ROOT}/adapter/shader_resource_hooks.cpp"
    "${TS_ROOT}/adapter/shader_runtime_bridge.cpp")
get_target_property(ts_core_includes timeshift_renut_core INCLUDE_DIRECTORIES)
get_target_property(ts_core_defines timeshift_renut_core COMPILE_DEFINITIONS)
target_include_directories(timeshift_renut_resource_hooks PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_resource_hooks PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_resource_hooks PRIVATE plume ts_renut_zstd ts_renut_smolv ts_renut_xxhash)

add_library(timeshift_renut_device_calls OBJECT
    "${TS_ROOT}/adapter/device_bootstrap.cpp"
    "${TS_ROOT}/adapter/device_guest_calls.cpp")
target_include_directories(timeshift_renut_device_calls PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_device_calls PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_device_calls PRIVATE plume)

add_library(timeshift_renut_draw_hooks OBJECT
    "${TS_ROOT}/adapter/draw_hooks.cpp"
    "${TS_ROOT}/adapter/draw_runtime_bridge.cpp")
target_include_directories(timeshift_renut_draw_hooks PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_draw_hooks PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_draw_hooks PRIVATE plume)

# Native TimeShift tiling CPU protocol and SDK argument bridge. Concrete SDK
# services and dormant Begin/End/predication entries are a separate target below.
add_library(timeshift_renut_tiling OBJECT
    "${TS_ROOT}/adapter/tiling.cpp"
    "${TS_ROOT}/adapter/tiling_guest_calls.cpp")
target_include_directories(timeshift_renut_tiling PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_tiling PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_tiling PRIVATE plume)

add_library(timeshift_renut_sources OBJECT
    "${TS_ROOT}/adapter/source_memory.cpp"
    "${TS_ROOT}/adapter/source_runtime.cpp")
target_include_directories(timeshift_renut_sources PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_sources PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_sources PRIVATE plume)

add_library(timeshift_renut_lifecycle OBJECT
    "${TS_ROOT}/adapter/device_registry.cpp"
    "${TS_ROOT}/adapter/device_lifecycle_hooks.cpp"
    "${TS_ROOT}/adapter/device_lifecycle_runtime.cpp")
target_include_directories(timeshift_renut_lifecycle PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_lifecycle PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_lifecycle PRIVATE plume)

add_library(timeshift_renut_sdk_tiling OBJECT
    "${TS_ROOT}/adapter/sdk_tiling_host.cpp"
    "${TS_ROOT}/adapter/tiling_hooks.cpp")
target_include_directories(timeshift_renut_sdk_tiling PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_sdk_tiling PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_sdk_tiling PRIVATE plume)

add_library(timeshift_renut_fence_query OBJECT "${TS_ROOT}/adapter/fence_query_hooks.cpp")
target_include_directories(timeshift_renut_fence_query PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_fence_query PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_fence_query PRIVATE plume)

add_library(timeshift_renut_fence_wait OBJECT
    "${TS_ROOT}/adapter/fence_read.cpp" "${TS_ROOT}/adapter/fence_wait_hooks.cpp")
target_include_directories(timeshift_renut_fence_wait PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_fence_wait PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_fence_wait PRIVATE plume)

# Compile the actual three private generated chunks affected by the six-load
# interposition. The Windows port tool verifies owned frozen source beforehand.
# These original guest symbols remain separate from the native-hook audit.
add_library(timeshift_renut_guest_wait OBJECT
    "${TS_ROOT}/private/app/generated/default/timeshift_recomp.2.cpp"
    "${TS_ROOT}/private/app/generated/default/timeshift_recomp.78.cpp"
    "${TS_ROOT}/private/app/generated/default/timeshift_recomp.153.cpp")
target_include_directories(timeshift_renut_guest_wait PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_guest_wait PRIVATE ${ts_core_defines})
target_compile_options(timeshift_renut_guest_wait PRIVATE -O2 -fno-strict-aliasing)

add_library(timeshift_renut_device_cleanup OBJECT
    "${TS_ROOT}/adapter/device_cleanup.cpp" "${TS_ROOT}/adapter/device_cleanup_runtime.cpp")
target_include_directories(timeshift_renut_device_cleanup PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_device_cleanup PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_device_cleanup PRIVATE plume)

add_library(timeshift_renut_callback_hooks OBJECT
    "${TS_ROOT}/adapter/callback_hooks.cpp")
target_include_directories(timeshift_renut_callback_hooks PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_callback_hooks PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_callback_hooks PRIVATE plume)

add_library(timeshift_renut_presentation_hooks OBJECT
    "${TS_ROOT}/adapter/presentation_hooks.cpp")
target_include_directories(timeshift_renut_presentation_hooks PRIVATE ${ts_core_includes})
target_compile_definitions(timeshift_renut_presentation_hooks PRIVATE ${ts_core_defines})
target_link_libraries(timeshift_renut_presentation_hooks PRIVATE plume)

