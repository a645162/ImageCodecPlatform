// vpl_jpeg_encoder.h - oneVPL (libvpl.dll) 硬件 JPEG 编码器。
//
// 输入 NV12（系统内存，Y 平面 + 交错 UV，4:2:0，pitch=width），
// 输出 JPEG 码流（baseline）。基于 oneVPL loader API 动态加载，无导入库。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vpl_dyn.h"

namespace oic {
namespace jpeg {
namespace onevpl {

// oneVPL 硬件 JPEG 编码器（libvpl.dll 动态加载）。
struct VplJpegEncoder {
    VplDyn dyn;                       // 动态绑定的 libvpl.dll 入口
    mfxLoader loader = nullptr;
    mfxSession session = nullptr;
    mfxVideoParam param{};
    mfxFrameSurface1 surface{};
    mfxU32 width = 0, height = 0;
    std::string impl_name;

    // 建立会话并初始化编码器。quality 0..100。失败返回 false（不崩溃）。
    bool init(uint32_t w, uint32_t h, uint32_t quality);

    // 编码一帧 NV12（w*h*3/2 字节，Y 后接 UV）-> JPEG 码流。
    // 成功返回 true 且 jpeg 非空；失败返回 false。
    bool encode(const uint8_t* nv12, size_t nv12_bytes, std::vector<uint8_t>& jpeg);

    // RGB24(r,g,b) 便捷入口：内部转 NV12 后编码。
    bool encode_rgb24(const uint8_t* rgb, size_t rgb_bytes, std::vector<uint8_t>& jpeg);

    // BGR24(b,g,r) 便捷入口：内部转 NV12 后编码。
    bool encode_bgr24(const uint8_t* bgr, size_t bgr_bytes, std::vector<uint8_t>& jpeg);

    // 释放会话与动态库。
    void close();
};

}  // namespace onevpl
}  // namespace jpeg
}  // namespace oic
