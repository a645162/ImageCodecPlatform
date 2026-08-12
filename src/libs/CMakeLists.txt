# 平台与 Intel 专用库（参照 ImageCodecOffload 组织，可按需链接）。
add_subdirectory(OpenCL.Image.Codec.Platform)     # OpenCLImageCodec::Platform        平台抽象（exe_dir/计时/休眠）
add_subdirectory(OpenCL.Image.Codec.Jpeg.oneVPL)  # OpenCLImageCodec::Jpeg::oneVPL    oneVPL(libvpl.dll) JPEG 后端（依赖 Platform）
