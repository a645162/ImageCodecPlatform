# 平台抽象：可执行目录/单调时钟/休眠（Windows 优先，预留 POSIX）。
add_library(OpenCLImageCodec.Platform STATIC
  src/oic_platform.h
  src/win_platform.cpp)
target_include_directories(OpenCLImageCodec.Platform PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_library(OpenCLImageCodec::Platform ALIAS OpenCLImageCodec.Platform)
