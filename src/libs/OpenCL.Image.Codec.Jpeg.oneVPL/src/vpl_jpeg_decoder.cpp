// vpl_jpeg_decoder.cpp - oneVPL 硬件 JPEG 解码器实现（D3D11 + NV12 系统内存）。
#include "vpl_jpeg_decoder.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <d3d11.h>
#include <dxgi.h>
#endif

#include "rgb_nv12.h"

namespace oic {
namespace jpeg {
namespace onevpl {

namespace {

#if defined(_WIN32)
// 在 Intel(0x8086) 适配器上创建 D3D11 设备（解码器需要 VideoSupport）。
ID3D11Device* CreateD3D11Intel()
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        return nullptr;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; i++) {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(adapter->GetDesc1(&d)) && d.VendorId == 0x8086)
            break;                       // 保留该 Intel 适配器
        adapter->Release();
        adapter = nullptr;
    }
    factory->Release();
    if (!adapter)
        return nullptr;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device* device = nullptr;
    UINT flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                                   &level, 1, D3D11_SDK_VERSION,
                                   &device, nullptr, nullptr);
    adapter->Release();
    return SUCCEEDED(hr) ? device : nullptr;
}
#endif

}  // namespace

// ---- 系统内存 NV12 帧分配器 ----
SimpleAllocator::SimpleAllocator(void* d3d11_device) : device(d3d11_device)
{
    alloc.pthis = this;
    alloc.Alloc  = alloc_cb;
    alloc.Lock   = lock_cb;
    alloc.Unlock = unlock_cb;
    alloc.GetHDL = getHDL_cb;
    alloc.Free   = free_cb;
}

mfxStatus MFX_CDECL SimpleAllocator::alloc_cb(mfxHDL pthis, mfxFrameAllocRequest* req, mfxFrameAllocResponse* resp)
{
    auto* a = (SimpleAllocator*)pthis;
    if (!(req->Type & MFX_MEMTYPE_SYSTEM_MEMORY))
        return MFX_ERR_UNSUPPORTED;
    a->w = req->Info.Width;
    a->h = req->Info.Height;
    a->mids.resize(req->NumFrameSuggested);
    resp->mids = a->mids.data();
    resp->NumFrameActual = req->NumFrameSuggested;
    size_t sz = (size_t)a->w * a->h + (size_t)a->w * (a->h / 2);   // NV12
    for (mfxU16 i = 0; i < req->NumFrameSuggested; i++) {
        a->bufs.push_back(std::vector<uint8_t>(sz, 0));
        resp->mids[i] = (mfxMemId)a->bufs.back().data();
    }
    return MFX_ERR_NONE;
}

mfxStatus MFX_CDECL SimpleAllocator::lock_cb(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr)
{
    auto* a = (SimpleAllocator*)pthis;
    uint8_t* buf = (uint8_t*)mid;
    ptr->Y  = buf;
    ptr->UV = buf + (size_t)a->w * a->h;
    ptr->Pitch = (mfxU16)a->w;
    return MFX_ERR_NONE;
}

mfxStatus MFX_CDECL SimpleAllocator::unlock_cb(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr)
{
    (void)pthis; (void)mid; (void)ptr;
    return MFX_ERR_NONE;   // 系统内存无需解锁
}

mfxStatus MFX_CDECL SimpleAllocator::getHDL_cb(mfxHDL pthis, mfxMemId mid, mfxHDL* handle)
{
    (void)pthis;
    static thread_local mfxHDLPair pair;
    pair.first = mid;
    pair.second = 0;
    *handle = &pair;
    return MFX_ERR_NONE;
}

mfxStatus MFX_CDECL SimpleAllocator::free_cb(mfxHDL pthis, mfxFrameAllocResponse* resp)
{
    (void)resp;
    auto* a = (SimpleAllocator*)pthis;
    a->bufs.clear();
    a->mids.clear();
    return MFX_ERR_NONE;
}

bool VplJpegDecoder::init()
{
    if (!dyn.load()) {
        std::fprintf(stderr, "[vpl] decoder: libvpl.dll 加载失败\n");
        return false;
    }
    loader = dyn.MFXLoad();
    if (!loader) {
        std::fprintf(stderr, "[vpl] decoder: MFXLoad 失败\n");
        dyn.unload();
        return false;
    }

    mfxConfig cfg = dyn.MFXCreateConfig(loader);
    if (cfg) {
        mfxVariant v{};
        v.Type = MFX_VARIANT_TYPE_U32;
        v.Data.U32 = MFX_IMPL_TYPE_HARDWARE;
        dyn.MFXSetConfigFilterProperty(cfg, (const mfxU8*)"mfxImplDescription.Impl", v);
    }

    int chosen = -1;
    for (mfxU32 i = 0;; i++) {
        mfxHDL hdl = nullptr;
        if (dyn.MFXEnumImplementations(loader, i, MFX_IMPLCAPS_IMPLDESCSTRUCTURE, &hdl) != MFX_ERR_NONE)
            break;
        auto* d = (mfxImplDescription*)hdl;
        if (d->VendorID == 0x8086) {
            chosen = (int)i;
            impl_name = d->ImplName;
        }
    }
    if (chosen < 0) {
        std::fprintf(stderr, "[vpl] decoder: 未找到 Intel 硬件实现\n");
        dyn.MFXUnload(loader);
        loader = nullptr;
        dyn.unload();
        return false;
    }
    if (dyn.MFXCreateSession(loader, chosen, &session) != MFX_ERR_NONE) {
        std::fprintf(stderr, "[vpl] decoder: MFXCreateSession 失败\n");
        dyn.MFXUnload(loader);
        loader = nullptr;
        dyn.unload();
        return false;
    }

    // Windows 硬件解码需要 D3D11 设备句柄。
#if defined(_WIN32)
    ID3D11Device* dev = CreateD3D11Intel();
    if (!dev) {
        std::fprintf(stderr, "[vpl] decoder: D3D11 设备创建失败\n");
        close();
        return false;
    }
    d3d11 = dev;
    allocator.device = dev;
    dyn.MFXVideoCORE_SetHandle(session, MFX_HANDLE_D3D11_DEVICE, (mfxHDL)dev);
#else
    std::fprintf(stderr, "[vpl] decoder: 非 Windows 平台不支持\n");
    close();
    return false;
#endif
    return true;
}

bool VplJpegDecoder::decode(const uint8_t* jpeg, size_t size,
                            std::vector<uint8_t>& bgra, uint32_t& w, uint32_t& h)
{
    bgra.clear();
    if (!session || !jpeg || size == 0)
        return false;

    mfxBitstream bs{};
    bs.Data = (mfxU8*)jpeg;
    bs.DataLength = (mfxU32)size;
    bs.MaxLength = (mfxU32)size;
    bs.DataOffset = 0;

    // DecodeHeader -> 解码参数。
    std::memset(&param, 0, sizeof(param));
    param.mfx.CodecId = MFX_CODEC_JPEG;
    mfxStatus sts = dyn.MFXVideoDECODE_DecodeHeader(session, &bs, &param);
    if (sts != MFX_ERR_NONE) {
        std::fprintf(stderr, "[vpl] decoder: DecodeHeader 失败 %s(%d)\n",
                     vpl_status_str(sts), (int)sts);
        return false;
    }
    mfxFrameInfo& info = param.mfx.FrameInfo;
    w = info.CropW ? info.CropW : info.Width;
    h = info.CropH ? info.CropH : info.Height;

    // NV12 系统内存输出。
    param.mfx.FrameInfo.FourCC       = MFX_FOURCC_NV12;
    param.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    param.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
    dyn.MFXVideoCORE_SetFrameAllocator(session, &allocator.alloc);   // 在 Init 之前
    sts = dyn.MFXVideoDECODE_Init(session, &param);
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        std::fprintf(stderr, "[vpl] decoder: DECODE_Init 失败 %s(%d)\n",
                     vpl_status_str(sts), (int)sts);
        return false;
    }

    // 重新喂入完整码流并逐帧解码。
    bs.DataOffset = 0;
    bs.DataLength = (mfxU32)size;
    mfxFrameSurface1* surf = nullptr;
    mfxSyncPoint syncp = nullptr;
    for (int attempt = 0; attempt < 8; attempt++) {
        mfxStatus sts2 = dyn.MFXVideoDECODE_DecodeFrameAsync(session, &bs, nullptr, &surf, &syncp);
        if (sts2 == MFX_ERR_NONE || sts2 == MFX_WRN_IN_EXECUTION) {
            if (syncp) {
                dyn.MFXVideoCORE_SyncOperation(session, syncp, 60000);
                if (surf && surf->Data.Y) {
                    uint32_t sPitch = surf->Data.Pitch ? surf->Data.Pitch : (mfxU16)w;
                    // NV12 -> BGRA32（JPEG 全范围 BT.601）。
                    bgra.resize((size_t)w * h * 4);
                    for (uint32_t row = 0; row < h; row++) {
                        const uint8_t* yrow = surf->Data.Y + (size_t)row * sPitch;
                        const uint8_t* uvrow = surf->Data.UV + (size_t)(row / 2) * sPitch;
                        uint8_t* d = bgra.data() + (size_t)row * w * 4;
                        for (uint32_t x = 0; x < w; x++) {
                            int yy = yrow[x];
                            int cb = uvrow[x & ~1u];
                            int cr = uvrow[(x & ~1u) + 1];
                            int r = yy + ((1436 * cr) >> 10);
                            int g = yy - ((352 * cb + 731 * cr) >> 10);
                            int b = yy + ((1816 * cb) >> 10);
                            if (r < 0) r = 0; else if (r > 255) r = 255;
                            if (g < 0) g = 0; else if (g > 255) g = 255;
                            if (b < 0) b = 0; else if (b > 255) b = 255;
                            d[0] = (uint8_t)b; d[1] = (uint8_t)g; d[2] = (uint8_t)r; d[3] = 255;
                        }
                    }
                    return true;
                }
                std::fprintf(stderr, "[vpl] decoder: 输出表面无系统内存数据\n");
            }
        } else if (sts2 == MFX_ERR_MORE_DATA) {
            break;
        } else {
            std::fprintf(stderr, "[vpl] decoder: DecodeFrameAsync 失败 %s(%d)\n",
                         vpl_status_str(sts2), (int)sts2);
            break;
        }
    }
    return false;
}

void VplJpegDecoder::close()
{
    if (session) {
        dyn.MFXVideoDECODE_Close(session);
        dyn.MFXClose(session);
        session = nullptr;
    }
    if (loader) {
        dyn.MFXUnload(loader);
        loader = nullptr;
    }
#if defined(_WIN32)
    if (d3d11) {
        ((ID3D11Device*)d3d11)->Release();
        d3d11 = nullptr;
    }
#endif
    dyn.unload();
}

}  // namespace onevpl
}  // namespace jpeg
}  // namespace oic
