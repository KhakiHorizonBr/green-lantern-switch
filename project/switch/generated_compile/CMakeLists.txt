cmake_minimum_required(VERSION 3.25)
project(green_lantern_switch_generated LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compile only the statically recompiled game code for Horizon/AArch64. This
# target intentionally does not create an NRO or launch guest/game code yet.
set(REXGLUE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_PERF_COUNTERS OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_FIDELITYFX OFF CACHE BOOL "" FORCE)
set(REXGLUE_HEADLESS ON CACHE BOOL "" FORCE)

set(GREEN_LANTERN_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../" )
set(REXGLUE_SWITCH_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "" FORCE)
set(REXGLUE_SWITCH_OVERLAY_DIR "${CMAKE_CURRENT_LIST_DIR}/../include" CACHE PATH "" FORCE)
set(REXGLUE_SWITCH_BOOT_PROBE_SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/../runtime_boot_probe/runtime_boot_probe.cpp"
    CACHE FILEPATH "" FORCE)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../rexglue-sdk" rexglue-sdk)

include("${GREEN_LANTERN_PROJECT_ROOT}/generated/hinted/sources.cmake")

add_library(green_lantern_recomp OBJECT ${GENERATED_SOURCES})
target_include_directories(green_lantern_recomp PRIVATE
    "${GREEN_LANTERN_PROJECT_ROOT}"
    "${GREEN_LANTERN_PROJECT_ROOT}/src"
    "${GREEN_LANTERN_PROJECT_ROOT}/generated/hinted"
)
target_link_libraries(green_lantern_recomp PRIVATE
    rexruntime rexcore rexfilesystem)
target_compile_definitions(green_lantern_recomp PRIVATE _GNU_SOURCE)
target_compile_options(green_lantern_recomp PRIVATE -g0 -w -include cstdarg)
target_precompile_headers(green_lantern_recomp PRIVATE
    "${GREEN_LANTERN_PROJECT_ROOT}/generated/hinted/green_lantern_pch.h")

# Turn the already-tested loader probe into a link probe containing every
# generated ARM64 game function. Its source only loads the XEX; it never starts
# a guest thread or calls the recompiled game code.
target_sources(rexglue_switch_runtime_boot_probe PRIVATE
    $<TARGET_OBJECTS:green_lantern_recomp>)
target_include_directories(rexglue_switch_runtime_boot_probe PRIVATE
    "${GREEN_LANTERN_PROJECT_ROOT}/generated/hinted")

