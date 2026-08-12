# OpenCL-Image-Codec

通过 vcpkg 获取 OpenCL，CMake 子项目组织，独立构建；对外提供稳定 C API（另附 C++ 包装）与测试 CLI。

## 特性

- **JPEG**（baseline，SOF0，8-bit）
  - GPU（OpenCL）：**混合策略**——Huffman 熵编解码在 CPU，DCT/IDCT、量化/反量化、YCbCr↔RGB、上下采样在 GPU。支持 4:4:4 / 4:2:0 / 4:2:2 / 灰度（解码）。
- **TIFF**（6.0，Compression=1 none / 5 LZW，strip/tile）
  - 编码：LZW 在 GPU（strip/tile 并行 kernel，block 自适应）。
  - 解码：LZW 在 CPU（对齐 libtiff 语义）。GPU 并行 LZW 解码已探索并评估为不优于 CPU（见 `src/tiff/README.md`）。

## 构建

依赖：vcpkg（`opencl` 端口）、CMake ≥3.16、MSVC（Ninja 生成器）。

```bash
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

- OpenCL 由自带 `vcpkg.json`（manifest 模式）从 vcpkg 获取。
- 选项：`OIC_BUILD_CLI`（默认 ON）、`OIC_BUILD_EXAMPLES`（默认 ON）、`OIC_BUILD_TESTS`（默认 OFF）。

## CLI 用法（`build/cli/oic.exe`）

```
oic list-devices
oic info <in>
oic decode <in> <out> [--backend opencl|onevpl|cpu]
oic encode <in> <out> [--backend ...] [--jpeg-quality N]
            [--tiff-compression 1|5] [--rows-per-strip N]
oic bench <in> [--iterations N] [--backend ...]
```

示例：
```bash
oic encode in.bmp out.tiff --backend opencl --tiff-compression 5   # TIFF LZW GPU 编码
oic decode out.tiff back.bmp --backend cpu                         # TIFF 解码
oic encode in.bmp out.jpg --backend opencl --jpeg-quality 90       # JPEG OpenCL 编码
oic decode out.jpg back.bmp --backend opencl                       # JPEG 解码
```

## C API（`include/OpenCLImageCodec/oic.h`）

```c
oic_codec *c; oic_codec_create(&c, OIC_BACKEND_OPENCL);
oic_image img; oic_codec_decode(c, OIC_FORMAT_JPEG, buf, len, &img);  // img.data RGB
oic_codec_encode(c, OIC_FORMAT_TIFF, &img, &params, out, &out_len, cap);
oic_image_free(&img); oic_codec_destroy(c);
```

C++ 包装（`oic.hpp`）：`oic::Codec`（RAII，抛 `oic::Error`）、`oic::Image`、`oic::DeviceList`。
示例：`examples/c_example.c`（纯 C）、`examples/cpp_example.cpp`。

## 目录结构

```
include/OpenCLImageCodec/  对外 C/C++ API 头（oic.h / oic.hpp）
src/core/                  OpenCLImageCodec.Core —— 平台抽象 + OpenCL 设备/上下文/kernel 构建
src/tiff/                  OpenCLImageCodec.Tiff —— TIFF 容器 + LZW 编码(GPU)/解码(CPU)
src/jpeg/                  OpenCLImageCodec.Jpeg —— baseline JPEG 混合编解码（OpenCL）
src/api/                   OpenCLImageCodec.Api —— C API 实现（buffer ↔ 临时文件桥接）
cli/                       oic 可执行（子命令 info/decode/encode/bench）
examples/                  纯 C / C++ 示例
```

## 已知限制

- **JPEG 彩色解码**：与 libjpeg 最大差 ≤3 灰阶（0.5% 像素，浮点 IDCT vs libjpeg 整数 IDCT）；灰度/8×8 像素级一致。
- **JPEG 编码**：仅 3 分量彩色（灰度编码未实现）；渐进式/非 8-bit/16-bit 量化表拒绝。
- **TIFF 解码**：仅 8-bit RGB chunky（LZW/none）；tiled TIFF 需 libtiff 复核（本机 ffmpeg 的 tiled 解码有缺陷）。
- **Intel NEO**：单 work-item 输入 >~200KB 可能 `CL_OUT_OF_RESOURCES`（编码已按 `block_cap` 自适应）。

## 许可证

