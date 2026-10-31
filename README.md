# ImageCodecPlatform

平台抽象与**厂商专用**（Intel 媒体引擎）编解码后端。它不是「又一个通用后端」，
而是把「机器上到底有没有硬件编解码单元」这件事问清楚，并把硬件能做的事接进本仓库。

## 这是什么

```
OpenCLImageCodec::Platform      exe_dir / 单调时钟 / 休眠（Windows + POSIX 两套实现）
        ▲
OpenCLImageCodec::Jpeg::oneVPL  硬件单元探测 + baseline JPEG 编解码（oneVPL）
        ▲
OpenCLImageCodec::Tiff          TIFF 6.0 容器：预编码 JPEG strips（Compression=7）/ 未压缩
        ▲
icp_selftest                    自测 CLI（OIC_BUILD_CLI，默认 OFF）
```

三条硬性前提：

- **Linux + Windows 同一个代码路径**：运行库在运行期动态加载（Windows
  `LoadLibraryA("libvpl.dll")`，Linux `dlopen("libvpl.so.2")`），不需要导入库；
  帧只走**系统内存**（`MFX_IOPATTERN_*_SYSTEM_MEMORY`），不建 D3D11 设备、不碰 VA-API 句柄。
- **TIFF 没有硬件单元**：硬件只出 JPEG 码流，TIFF 是容器。所以 TIFF 路线是
  「硬件 JPEG → TIFF JPEG strips（Compression=7）」，容器自己写（libtiff 的 JPEG codec
  只会拿像素重新编码，没有入口接收已经编好的条带）。
- **探测要能解释失败**：把「运行库没装 / 装了但没有实现 / 有实现但不支持 JPEG」
  三种情况分开报告，上层才能给出可操作的提示。

## 硬件单元（oneVPL 实现）探测

```bash
./icp_selftest units
```

```
oneVPL 运行库：libvpl.so.2
实现数量：1
  #0 intel-media [hardware] vendor=Intel device=... accel=VA-API api=2.16 jpeg_encode=yes jpeg_decode=yes enc_in=[NV12] dec_out=[NV12]
  Intel 硬件单元：有（JPEG 编码=yes，解码=yes）
```

没有 `vpl-gpu-rt`（Windows `libmfx-gen.dll`，Linux `libmfx-gen.so.1`）时实现列表为空，
输出会明确指出这一点，而不是在 `Init` 处报一个看不懂的 `MFX_ERR_NOT_FOUND`。

`vpl_probe()` 的实现见 `src/libs/OpenCL.Image.Codec.Jpeg.oneVPL/src/vpl_device.cpp`：
枚举 `mfxImplDescription`（厂商 / ImplName / API 版本 / 加速栈 / 编解码能力），
优先选 Intel(0x8086) + hardware，并且**绝不**把软件实现当成硬件来跑。

## 硬件 JPEG 编解码

- 编码：`MFXMemory_GetSurfaceForEncode` 取运行库内部系统内存 surface，按 pitch 拷入
  NV12，`MFXVideoENCODE_EncodeFrameAsync` + 同步点等待，输出完整 baseline 码流。
- 解码：`DecodeFrameAsync(session, &bs, nullptr, &surface, &sync)` 由解码器自行分配输出
  surface，按 pitch 拷出 NV12，再转 BGR24 / BGRA32。
- 宽高**不要求偶数**：oneVPL 的 JPEG 能力表步长是 1，运行库内部按 MCU 补边，SOF 里
  仍写真实尺寸（TIFF 侧依赖这一点，见下）。色度按 `ceil(h/2)` 行处理。

## TIFF（Compression=7 / 1）

`OpenCLImageCodec::Tiff` 自写 TIFF 6.0 容器：

- 每条 strip 调用一次编码器（硬件或软件参考实现，接口统一为 `JpegStripEncoder`），
  高度就是该条实际覆盖的行数；
- 从编码器返回的完整码流里拆出 DQT/DHT 写进 `JPEGTables`(347)，strip 内写**缩写码流**
  （SOI + SOF + SOS + 熵数据 + EOI）；`YCbCrSubSampling`(530) 从 SOF0 的采样因子读回，
  `ReferenceBlackWhite`(532) 按 TIFF 6.0 §21 显式写出；
- 各 strip 的量化/霍夫曼表必须一致，否则直接报错——共用一份 `JPEGTables` 是不容商量
  的前提，写出一个「别的软件打不开」的文件比失败更糟；
- 另有 `Compression=1`（未压缩 RGB）基线，用作像素级对照。

两个容易踩的坑（都有实测依据）：

| 坑 | 后果 |
| --- | --- |
| strip 里不带 SOI | libtiff 解码每条 strip 前会 `jpeg_abort`，再从头读标记；没有 SOI 会被 libjpeg 判成 `Not a JPEG file` |
| 把宽度补到偶数再编码 | libtiff 拿 SOF 宽度与 `ImageWidth` 比对，JPEG 比图像宽就报 `JPEG strip/tile size exceeds expected dimensions` 并**拒绝解码** |

## 构建与自测

```bash
# 只构建库
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# 连自测 CLI 一起构建（需要 libjpeg / libtiff 才能跑容器自测）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DOIC_BUILD_CLI=ON
cmake --build build -j
./build/src/cli/icp_selftest          # 探测硬件 + 容器自测 + 硬件往返
```

首次克隆需要 `git submodule update --init --recursive`（`3rdparty/libvpl` 提供 API 头，
`3rdparty/vpl-gpu-rt` 作为运行时实现参考）。

```
icp_selftest                                   # = selftest
icp_selftest units                             # 只打印硬件单元
icp_selftest encode <in.bmp> <out.jpg> [q]     # 硬件 JPEG 编码
icp_selftest decode <in.jpg> <out.bmp>         # 硬件 JPEG 解码
icp_selftest tiff <in.bmp> <out.tiff> [--rows N] [--quality Q] [--none] [--software]
icp_selftest selftest [--require-hardware]
```

自测的三种结果，不要混为一谈：

- **PASS**：容器三种形态（缩写+JPEGTables / 完整码流 / 未压缩）都被 libtiff 解开，
  未压缩形态逐字节一致，JPEG 形态 PSNR 达标；
- **SKIP**：没有 Intel 媒体引擎（缺 `vpl-gpu-rt`）或构建时没找到 libjpeg/libtiff；
  这是正常结果，不是失败；
- **FAIL**：`--require-hardware` 明确要求硬件路径可跑，而它跑不了。

`--software` 用 libjpeg 参考编码器产出 strip，因此**没有 Intel 硬件的机器也能完整验证
TIFF 容器**——容器代码与硬件路径不互相掩盖。

## 与父仓库的关系

父仓库 `ImageCodecOffload` 用 `IMAGECODEC_OFFLOAD_WITH_PLATFORM=ON` 挂载本子项目。
当前只提供库与自测 CLI，**尚未接入路由后端表**：`ico backends` / `ico encode --backend`
里没有它，主项目的 `ImageCodecOffload.Router` 也还没有 Platform 适配层。
这是刻意的分步交付——先把「硬件能不能用、容器写得对不对」验证清楚，再接路由。

## 目录

```
src/libs/OpenCL.Image.Codec.Platform/      oic_platform.h + win/posix 实现
src/libs/OpenCL.Image.Codec.Jpeg.oneVPL/   vpl_dyn（动态加载）/ vpl_device（单元探测与会话）
                                           vpl_jpeg_encoder / vpl_jpeg_decoder / rgb_nv12 / bmp24
                                            onevpl_test（C 入口）
src/libs/OpenCL.Image.Codec.Tiff/          jpeg_stream（SOF0 解析、表拆分）
                                           tiff_jpeg_strip_writer（IFD 写出）
src/cli/                                   icp_selftest + selftest_support
```

## 许可

见仓库根 `LICENSE`。
