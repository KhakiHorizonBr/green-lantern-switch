cmake_minimum_required(VERSION 3.25)
project(green_lantern_switch_full_runtime LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Separate full-runtime build. The already-tested headless diagnostic project
# stays intact while this one integrates the Horizon input/audio/UI adapters.
set(REXGLUE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_PERF_COUNTERS ON CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_FIDELITYFX OFF CACHE BOOL "" FORCE)
set(REXGLUE_PLATFORM_SWITCH ON CACHE BOOL "" FORCE)
set(REXGLUE_USE_D3D12 OFF CACHE BOOL "" FORCE)
set(REXGLUE_USE_VULKAN ON CACHE BOOL "" FORCE)

set(GREEN_LANTERN_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../")
set(REXGLUE_SWITCH_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(REXGLUE_SWITCH_OVERLAY_DIR "${CMAKE_CURRENT_LIST_DIR}/../include")
set(REXGLUE_SDK_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../rexglue-sdk/work/rexglue-sdk-horizon-reference")

add_subdirectory("${REXGLUE_SDK_ROOT}" rexglue-sdk)
# Only the Green Lantern Switch app reserves a startup guest-backing arena.
# Keep it inside the existing 1,280 MiB libnx heap; this changes where guest
# slabs come from, not the total heap or other ReXGlue applications.
set(GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB 384 CACHE STRING
    "Guest backing arena reserved before NVK initialization")
option(GREEN_LANTERN_NATIVE_RENDERER "Build the experimental native Vulkan renderer" OFF)
if (NOT GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB MATCHES "^(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB must be a nonnegative integer")
endif()
# The Switch helper computes this availability flag inside the SDK subdirectory.
# Publish it in this directory too so its exported target helper sees the NVK
# SDK during game-target configuration.
set(REXGLUE_SWITCH_NVK_AVAILABLE ON)

include("${GREEN_LANTERN_PROJECT_ROOT}/generated/hinted/sources.cmake")
add_library(green_lantern_recomp OBJECT ${GENERATED_SOURCES})
set(GREEN_LANTERN_SWITCH_STREAMDATA_TEST_CAP_MIB 48 CACHE STRING
    "Temporary Switch-only STREAMDATA pool cap for reversible bring-up testing")
target_compile_definitions(green_lantern_recomp PRIVATE
    GREEN_LANTERN_SWITCH_STREAMDATA_TEST_CAP_MIB=${GREEN_LANTERN_SWITCH_STREAMDATA_TEST_CAP_MIB})
target_include_directories(green_lantern_recomp PRIVATE
    "${GREEN_LANTERN_PROJECT_ROOT}"
    "${GREEN_LANTERN_PROJECT_ROOT}/src"
    "${GREEN_LANTERN_PROJECT_ROOT}/generated/hinted")
target_link_libraries(green_lantern_recomp PRIVATE rexruntime rexcore rexfilesystem)
target_compile_definitions(green_lantern_recomp PRIVATE _GNU_SOURCE)
# O código PPC acessa memória guest e registradores por tipos diferentes,
# inclusive vetores SIMD. Use a mesma regra de aliasing do runtime do SDK.
target_compile_options(green_lantern_recomp PRIVATE -g0 -w -fno-strict-aliasing
    -ffp-contract=off -include cstdarg)
target_precompile_headers(green_lantern_recomp PRIVATE
    "${GREEN_LANTERN_PROJECT_ROOT}/generated/hinted/green_lantern_pch.h")

add_executable(green_lantern_switch_full_runtime_probe
    "${CMAKE_CURRENT_LIST_DIR}/../runtime_boot_probe/runtime_boot_probe.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/horizon_guest_memory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/horizon_shared_memory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/sparse_guest_pages.cpp")
target_include_directories(green_lantern_switch_full_runtime_probe PRIVATE
    "${GREEN_LANTERN_PROJECT_ROOT}/generated/hinted"
    "${REXGLUE_SWITCH_OVERLAY_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../include"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime")
target_sources(green_lantern_switch_full_runtime_probe PRIVATE
    $<TARGET_OBJECTS:green_lantern_recomp>)
target_link_libraries(green_lantern_switch_full_runtime_probe PRIVATE
    rexruntime rexcore rexfilesystem rexui rexaudio rexinput rexgpu-xenos rexglue_switch_nvk
    rexglue_switch_startup nx)
target_link_directories(green_lantern_switch_full_runtime_probe PRIVATE
    "${LIBNX_ROOT}/lib")
target_link_options(green_lantern_switch_full_runtime_probe PRIVATE
    "-specs=${LIBNX_ROOT}/switch.specs")
set_target_properties(green_lantern_switch_full_runtime_probe PROPERTIES
    OUTPUT_NAME "green_lantern_switch_full_runtime_probe")

find_program(REXGLUE_ELF2NRO elf2nro
    HINTS "${DEVKITPRO}/tools/bin" NO_DEFAULT_PATH REQUIRED)
add_custom_command(TARGET green_lantern_switch_full_runtime_probe POST_BUILD
    COMMAND "${REXGLUE_ELF2NRO}"
        "$<TARGET_FILE:green_lantern_switch_full_runtime_probe>"
        "$<TARGET_FILE_DIR:green_lantern_switch_full_runtime_probe>/green_lantern_switch_full_runtime_probe.nro"
        "--nacp=${CMAKE_CURRENT_LIST_DIR}/../green_lantern_switch.nacp"
    COMMENT "Packaging Green Lantern NVK runtime probe as NRO"
    VERBATIM)

# Isolated Horizon SVC experiment: test whether shared-memory pages can back
# the existing ReXGlue code-memory + guest-view mapping sequence.
add_executable(green_lantern_code_memory_shm_probe
    "${CMAKE_CURRENT_LIST_DIR}/../runtime_shm_probe/main.cpp")
target_link_directories(green_lantern_code_memory_shm_probe PRIVATE "${LIBNX_ROOT}/lib")
target_link_libraries(green_lantern_code_memory_shm_probe PRIVATE nx)
target_link_options(green_lantern_code_memory_shm_probe PRIVATE
    "-specs=${LIBNX_ROOT}/switch.specs")
set_target_properties(green_lantern_code_memory_shm_probe PROPERTIES
    OUTPUT_NAME "green_lantern_code_memory_shm_probe")
add_custom_command(TARGET green_lantern_code_memory_shm_probe POST_BUILD
    COMMAND "${REXGLUE_ELF2NRO}"
        "$<TARGET_FILE:green_lantern_code_memory_shm_probe>"
        "$<TARGET_FILE_DIR:green_lantern_code_memory_shm_probe>/green_lantern_code_memory_shm_probe.nro"
        "--nacp=${CMAKE_CURRENT_LIST_DIR}/../green_lantern_switch.nacp"
    COMMENT "Packaging shared-memory code-map probe as NRO"
    VERBATIM)

# Real windowed ReXApp target. Keep the proven diagnostic NRO above available
# while this target advances from headless setup to surface/swapchain/game boot.
add_executable(green_lantern_switch_game
    "${CMAKE_CURRENT_LIST_DIR}/../source/green_lantern_switch_main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/horizon_guest_memory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/horizon_shared_memory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/sparse_guest_pages.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/memory_telemetry.cpp")
if(GREEN_LANTERN_NATIVE_RENDERER)
    set(GREEN_LANTERN_NATIVE_DIR "${CMAKE_CURRENT_LIST_DIR}/../native_experiment/native")
    target_sources(green_lantern_switch_game PRIVATE
        "${GREEN_LANTERN_NATIVE_DIR}/native_system.cpp"
        "${GREEN_LANTERN_NATIVE_DIR}/native_renderer.cpp"
        "${GREEN_LANTERN_NATIVE_DIR}/native_textures.cpp"
        "${GREEN_LANTERN_NATIVE_DIR}/native_geometry_shaders.cpp")
    target_include_directories(green_lantern_switch_game PRIVATE
        "${GREEN_LANTERN_NATIVE_DIR}"
        "${REXGLUE_SDK_ROOT}/src/graphics"
        "${REXGLUE_SDK_ROOT}/thirdparty/glslang"
        "${REXGLUE_SDK_ROOT}/thirdparty/xxHash"
        "${REXGLUE_SDK_ROOT}/thirdparty/renderdoc")
    target_compile_definitions(green_lantern_switch_game PRIVATE GREEN_LANTERN_NATIVE_RENDERER=1)
endif()
if (GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB GREATER 0)
    target_compile_definitions(green_lantern_switch_game PRIVATE
        GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB=${GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB})
endif()
target_include_directories(green_lantern_switch_game PRIVATE
    "${GREEN_LANTERN_PROJECT_ROOT}"
    "${GREEN_LANTERN_PROJECT_ROOT}/src"
    "${GREEN_LANTERN_PROJECT_ROOT}/generated/hinted"
    "${REXGLUE_SWITCH_OVERLAY_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../include"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime")
target_sources(green_lantern_switch_game PRIVATE
    $<TARGET_OBJECTS:green_lantern_recomp>)
rexglue_switch_configure_target(green_lantern_switch_game GPU_PLUGINS xenos)
target_compile_definitions(green_lantern_switch_game PRIVATE REX_PLATFORM_SWITCH=1)
target_link_directories(green_lantern_switch_game PRIVATE "${LIBNX_ROOT}/lib")
target_link_libraries(green_lantern_switch_game PRIVATE nx)
target_link_options(green_lantern_switch_game PRIVATE
    "-specs=${LIBNX_ROOT}/switch.specs")
rexglue_switch_add_nro(green_lantern_switch_game
    NAME "Green Lantern Rise of the Manhunters"
    AUTHOR "Green Lantern Recomp"
    VERSION "0.1.0")

# Isolated startup budget; no PPC recompilation required when varying heap size.
set(GREEN_LANTERN_SWITCH_HEAP_MIB 1280 CACHE STRING "libnx malloc heap in MiB")
if (GREEN_LANTERN_SWITCH_HEAP_MIB LESS 1024 OR GREEN_LANTERN_SWITCH_HEAP_MIB GREATER 2304)
  message(FATAL_ERROR "Heap must be between1024 and2304MiB")
endif()
target_compile_definitions(rexglue_switch_startup PRIVATE
    REX_SWITCH_HEAP_MIB=${GREEN_LANTERN_SWITCH_HEAP_MIB})
option(GREEN_LANTERN_SWITCH_PROFILE_STACKS "Enable invasive stack sampling in diagnostic NRO" OFF)
target_compile_definitions(rexglue_switch_startup PRIVATE
    REX_SWITCH_PROFILE_STACKS=$<BOOL:${GREEN_LANTERN_SWITCH_PROFILE_STACKS}>)

option(GREEN_LANTERN_BUILD_FSI_VARIANT "Build a separate stock Xenos/Vulkan FSI benchmark NRO" OFF)
if(GREEN_LANTERN_BUILD_FSI_VARIANT)
    add_executable(green_lantern_switch_xenos_fsi
        "${CMAKE_CURRENT_LIST_DIR}/../source/green_lantern_switch_main.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../runtime/horizon_guest_memory.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../runtime/horizon_shared_memory.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../runtime/sparse_guest_pages.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../runtime/memory_telemetry.cpp"
        $<TARGET_OBJECTS:green_lantern_recomp>)
    target_include_directories(green_lantern_switch_xenos_fsi PRIVATE
        "${GREEN_LANTERN_PROJECT_ROOT}"
        "${GREEN_LANTERN_PROJECT_ROOT}/src"
        "${GREEN_LANTERN_PROJECT_ROOT}/generated/hinted"
        "${REXGLUE_SWITCH_OVERLAY_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/../include"
        "${CMAKE_CURRENT_LIST_DIR}/../runtime")
    target_compile_definitions(green_lantern_switch_xenos_fsi PRIVATE
        GREEN_LANTERN_XENOS_FSI=1 REX_PLATFORM_SWITCH=1
        GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB=${GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB})
    rexglue_switch_configure_target(green_lantern_switch_xenos_fsi GPU_PLUGINS xenos)
    target_link_directories(green_lantern_switch_xenos_fsi PRIVATE "${LIBNX_ROOT}/lib")
    target_link_libraries(green_lantern_switch_xenos_fsi PRIVATE nx)
    target_link_options(green_lantern_switch_xenos_fsi PRIVATE "-specs=${LIBNX_ROOT}/switch.specs")
    rexglue_switch_add_nro(green_lantern_switch_xenos_fsi
        NAME "Green Lantern - Xenos FSI"
        AUTHOR "Green Lantern Recomp"
        VERSION "0.1.0")
endif()
