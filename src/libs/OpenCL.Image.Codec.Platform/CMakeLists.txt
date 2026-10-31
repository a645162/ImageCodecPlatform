# File: src/libs/OpenCL.Image.Codec.Platform/CMakeLists.txt
# Brief: CMake build configuration.
# Author: Haomin Kong
# Email: a645162@163.com
# Created: 2026-08-12

# 平台抽象：可执行目录 / 单调时钟 / 休眠（Windows 与 POSIX 各一份实现）。
add_library(OpenCLImageCodec.Platform STATIC
  src/oic_platform.h
  src/posix_platform.cpp
  src/win_platform.cpp)
target_include_directories(OpenCLImageCodec.Platform PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_library(OpenCLImageCodec::Platform ALIAS OpenCLImageCodec.Platform)
