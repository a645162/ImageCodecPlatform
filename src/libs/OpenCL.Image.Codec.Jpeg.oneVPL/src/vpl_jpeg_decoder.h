// vpl_jpeg_decoder.h - oneVPL (libvpl.dll) 硬件 JPEG 解码器。
//
// JPEG -> 硬件解码(D3D11) -> NV12 系统内存 -> BGRA32。
// 基于 oneVPL loader API 动态加载，无导入库。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vpl_dyn.h"

namespace oic {
namespace jpeg {
namespace onevpl {

// 系统内存 NV12 帧分配器：硬件解码器的输出纹理经 Lock 拷贝到 CPU 内存。
// 此处实现为纯系统内存分配（Alloc 直接给 NV12 缓冲），满足
// MFX_IOPATTERN_OUT_SYSTEM_MEMORY + D3D11 设备句柄的组合路径。
struct SimpleAllocator {
    mfxFrameAllocator alloc{};
    void* device = nullptr;          // ID3D11Device*（仅持有，不用于系统内存路径）
    uint32_t w = 0, h = 0;
    std::vector<std::vector<uint8_t>> bufs;   // NV12 系统内存缓冲
    std::vector<mfxMemId> mids;               // 持续存在的 mid 数组

    static mfxStatus MFX_CDECL alloc_cb(mfxHDL pthis, mfxFrameAllocRequest* req, mfxFrameAllocResponse* resp);
    static mfxStatus MFX_CDECL lock_cb(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr);
    static mfxStatus MFX_CDECL unlock_cb(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr);
    static mfxStatus MFX_CDECL getHDL_cb(mfxHDL pthis, mfxMemId mid, mfxHDL* handle);
    static mfxStatus MFX_CDECL free_cb(mfxHDL pthis, mfxFrameAllocResponse* resp);

    SimpleAllocator(void* d3d11_device);
};

// oneVPL 硬件 JPEG 解码器：整图 -> GPU -> NV12 系统内存 -> BGRA32。
struct VplJpegDecoder {
    VplDyn dyn;                       // 动态绑定的 libvpl.dll 入口
    mfxLoader loader = nullptr;
    mfxSession session = nullptr;
    mfxVideoParam param{};
    SimpleAllocator allocator{nullptr};
    void* d3d11 = nullptr;            // ID3D11Device*，保持存活
    std::string impl_name;

    // 建立会话 + D3D11 设备。失败返回 false（不崩溃）。
    bool init();

    // 解码一帧 JPEG -> BGRA32（bgra 需 w*h*4 字节）。成功返回 true。
    bool decode(const uint8_t* jpeg, size_t size,
                std::vector<uint8_t>& bgra, uint32_t& w, uint32_t& h);

    // 释放会话、D3D11 设备与动态库。
    void close();
};

}  // namespace onevpl
}  // namespace jpeg
}  // namespace oic
