cmake_minimum_required(VERSION 3.20)
project(green_lantern_deko_preflight LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(DEKO_REFERENCE "" CACHE PATH "Pinned SDK source snapshot")
set(SPIRV_CROSS_REFERENCE "" CACHE PATH "SPIRV-Cross source snapshot")
add_executable(deko_preflight preflight.cpp
  ${DEKO_REFERENCE}/src/ui/deko3d/glsl.cpp
  ${SPIRV_CROSS_REFERENCE}/spirv_cross.cpp
  ${SPIRV_CROSS_REFERENCE}/spirv_parser.cpp
  ${SPIRV_CROSS_REFERENCE}/spirv_cross_parsed_ir.cpp
  ${SPIRV_CROSS_REFERENCE}/spirv_cfg.cpp
  ${SPIRV_CROSS_REFERENCE}/spirv_glsl.cpp)
target_include_directories(deko_preflight PRIVATE
  ${DEKO_REFERENCE}/include ${SPIRV_CROSS_REFERENCE})
