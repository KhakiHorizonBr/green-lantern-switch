# green_lantern - ReXGlue Recompiled Project
cmake_minimum_required(VERSION 3.25)
project(green_lantern LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The Windows build uses D3D12. macOS has no D3D12 backend; use the SDK's
# Vulkan backend, which stages MoltenVK for Apple Silicon.
if(APPLE)
    set(REXGLUE_USE_D3D12 OFF CACHE BOOL "" FORCE)
    set(REXGLUE_USE_VULKAN ON CACHE BOOL "" FORCE)
endif()


include(generated/rexglue.cmake)

set(GREEN_LANTERN_SOURCES
    src/main.cpp
    src/streamdata_telemetry_stub.cpp
)

if(WIN32)
    add_executable(green_lantern WIN32 ${GREEN_LANTERN_SOURCES})
else()
    add_executable(green_lantern ${GREEN_LANTERN_SOURCES})
endif()

set(GREEN_LANTERN_OUTPUT_NAME "green_lantern_next" CACHE STRING "Host executable name")
set_target_properties(green_lantern PROPERTIES OUTPUT_NAME "${GREEN_LANTERN_OUTPUT_NAME}")

set(GREEN_LANTERN_GAME_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../game" CACHE PATH
    "Path to the extracted Green Lantern game data")
target_compile_definitions(green_lantern PRIVATE
    "GREEN_LANTERN_GAME_DATA_DIR=\"${GREEN_LANTERN_GAME_DATA_DIR}\"")
rexglue_setup_target(green_lantern GPU_PLUGINS xenos)

# Source SDK builds need ImGui's public headers for ReXApp. Compiler-rt supplies
# the unsigned 128-bit division used by the guest clock scale on Windows.
if(WIN32 AND REXSDK_DIR)
    target_include_directories(green_lantern PRIVATE "${REXSDK_DIR}/thirdparty/imgui")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC)
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /clang:-print-resource-dir
            OUTPUT_VARIABLE GREEN_LANTERN_CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(GREEN_LANTERN_COMPILER_BUILTINS
            "${GREEN_LANTERN_CLANG_RESOURCE_DIR}/lib/windows/clang_rt.builtins-x86_64.lib")
        if(EXISTS "${GREEN_LANTERN_COMPILER_BUILTINS}")
            target_link_libraries(rexruntime PRIVATE "${GREEN_LANTERN_COMPILER_BUILTINS}")
        endif()
    endif()
endif()

# Compare the exact native renderer sources used by the Switch on a desktop
# Vulkan driver. Reuse the existing guest object library and preserve the SDK
# Vulkan/D3D12 executable as the independent reference.
option(GREEN_LANTERN_BUILD_NATIVE_PC "Build the Switch native renderer on PC" OFF)
if(WIN32 AND GREEN_LANTERN_BUILD_NATIVE_PC)
    if(NOT REXGLUE_USE_VULKAN)
        message(FATAL_ERROR "Native PC comparison requires REXGLUE_USE_VULKAN=ON")
    endif()
    add_executable(green_lantern_native_pc WIN32
        ${GREEN_LANTERN_SOURCES}
        switch/native_experiment/native/native_system.cpp
        switch/native_experiment/native/native_renderer.cpp
        switch/native_experiment/native/native_geometry_shaders.cpp
        switch/native_experiment/native/native_textures.cpp)
    target_include_directories(green_lantern_native_pc PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${REXGLUE_ENTRYPOINT_INCLUDE_DIR}" "${REXSDK_DIR}/thirdparty/imgui"
        "${REXSDK_DIR}/thirdparty/xxhash"
        "${REXSDK_DIR}/thirdparty/glslang"
        "${REXSDK_DIR}/thirdparty/renderdoc"
        "${REXSDK_DIR}/src/graphics")
    target_compile_definitions(green_lantern_native_pc PRIVATE
        GREEN_LANTERN_NATIVE_PC=1
        "GREEN_LANTERN_GAME_DATA_DIR=\"${GREEN_LANTERN_GAME_DATA_DIR}\"")
    # The desktop Xenos DLL deliberately hides its shader/format helpers.
    # Build a static support archive from those same SDK sources; the linker
    # selects helpers used by native without duplicating runtime/core objects.
    get_target_property(_native_gpu_sources rexgpu-xenos SOURCES)
    set(_native_support_sources "")
    foreach(_source IN LISTS _native_gpu_sources)
        if(_source STREQUAL "plugin_main.cpp" OR _source MATCHES "cmake_pch")
            continue()
        endif()
        if(NOT IS_ABSOLUTE "${_source}")
            set(_source "${REXSDK_DIR}/src/graphics/${_source}")
        endif()
        list(APPEND _native_support_sources "${_source}")
    endforeach()
    add_library(green_lantern_native_gpu_support STATIC ${_native_support_sources})
    target_include_directories(green_lantern_native_gpu_support PRIVATE
        "${REXSDK_DIR}" "${REXSDK_DIR}/thirdparty/renderdoc")
    target_link_libraries(green_lantern_native_gpu_support PUBLIC
        $<TARGET_PROPERTY:rexgpu-xenos,LINK_LIBRARIES>)
    target_link_libraries(green_lantern_native_pc PRIVATE green_lantern_recomp
        rex::runtime green_lantern_native_gpu_support xxHash::xxhash glslang::SPIRV)
    rexglue_configure_target(green_lantern_native_pc GPU_PLUGINS xenos)
    add_executable(green_lantern_texture_checks EXCLUDE_FROM_ALL
        switch/deko_experiment/texture_cpu_checks.cpp)
    target_link_libraries(green_lantern_texture_checks PRIVATE
        rex::runtime green_lantern_native_gpu_support)
    add_executable(green_lantern_deko_draw_checks EXCLUDE_FROM_ALL
        switch/deko_experiment/draw_upload_checks.cpp)
endif()

