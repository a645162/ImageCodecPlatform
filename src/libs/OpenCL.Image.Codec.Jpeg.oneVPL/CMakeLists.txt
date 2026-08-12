# oneVPL (libvpl.dll 动态加载) JPEG 后端：仅 WIN32 编译。
#
# 可移植性说明：
#  - oneVPL API 头经 git submodule 引入（仓库根 3rdparty/libvpl，
#    https://github.com/intel/libvpl），头文件位于其 api/vpl/，未做任何复制。
#    另挂 3rdparty/vpl-gpu-rt（https://github.com/intel/vpl-gpu-rt）
#    submodule 供参考运行时实现。Windows 上无需导入库，
#    MFX 入口经 vpl_dyn 动态绑定。
#  - 首次克隆需 `git submodule update --init --recursive`。
#  - d3d11/dxgi 由 Windows SDK 提供，仅 WIN32 需要。
#  - 非 Windows 平台产出空静态库，保证 src/CMakeLists.txt 的聚合 target 仍可链接。
if(WIN32)
  add_library(OpenCLImageCodec.Jpeg.oneVPL STATIC
    src/vpl_dyn.cpp
    src/rgb_nv12.cpp
    src/vpl_jpeg_encoder.cpp
    src/vpl_jpeg_decoder.cpp
    src/onevpl_test.cpp)
  target_include_directories(OpenCLImageCodec.Jpeg.oneVPL PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/3rdparty/libvpl/api/vpl")
  target_link_libraries(OpenCLImageCodec.Jpeg.oneVPL PUBLIC
    OpenCLImageCodec.Platform
    d3d11
    dxgi)
  # mfx 头与 windows.h 共存时避免 min/max 宏冲突。
  target_compile_definitions(OpenCLImageCodec.Jpeg.oneVPL PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN)
else()
  add_library(OpenCLImageCodec.Jpeg.oneVPL STATIC)
endif()

# 命名空间别名：target_link_libraries(app PRIVATE OpenCLImageCodec::Jpeg::oneVPL)
add_library(OpenCLImageCodec::Jpeg::oneVPL ALIAS OpenCLImageCodec.Jpeg.oneVPL)
