// vpl_dyn.h - oneVPL (libvpl.dll) 动态加载绑定。
//
// 通过 LoadLibrary/GetProcAddress 运行时绑定 MFX 入口，无需导入库。
// 头文件来自 libvpl 子模块 api/vpl（见 CMakeLists.txt 说明）。
// 仅在 Windows 上可用；无 libvpl.dll 时 vpl_available() 返回 false，
// 所有调用方应据此走错误路径，保证不崩溃。
#pragma once

#include <cstdint>

#include "mfxvideo.h"
#include "mfxdispatcher.h"
#include "mfxjpeg.h"

namespace oic {
namespace jpeg {
namespace onevpl {

// libvpl.dll 是否存在且可加载（轻量探测，LoadLibrary + FreeLibrary）。
// 有 dll 返回 true；无 dll（或加载失败）返回 false。
bool vpl_available();

// mfxStatus -> 可读字符串（用于错误上报）。
const char* vpl_status_str(mfxStatus sts);

// 动态加载 libvpl.dll 并绑定 MFX 函数指针（无导入库）。
// 一次 load() 绑定编码/解码/CORE 全部入口；失败返回 false 并已 unload。
struct VplDyn {
    void* hDll = nullptr;
    // loader / session（oneVPL dispatcher 风格）
    mfxLoader(MFX_CDECL* MFXLoad)() = nullptr;
    void(MFX_CDECL* MFXUnload)(mfxLoader) = nullptr;
    mfxConfig(MFX_CDECL* MFXCreateConfig)(mfxLoader) = nullptr;
    mfxStatus(MFX_CDECL* MFXSetConfigFilterProperty)(mfxConfig, const mfxU8*, mfxVariant) = nullptr;
    mfxStatus(MFX_CDECL* MFXEnumImplementations)(mfxLoader, mfxU32, mfxImplCapsDeliveryFormat, mfxHDL*) = nullptr;
    mfxStatus(MFX_CDECL* MFXCreateSession)(mfxLoader, mfxU32, mfxSession*) = nullptr;
    mfxStatus(MFX_CDECL* MFXClose)(mfxSession) = nullptr;
    // encode
    mfxStatus(MFX_CDECL* MFXVideoENCODE_Query)(mfxSession, mfxVideoParam*, mfxVideoParam*) = nullptr;
    mfxStatus(MFX_CDECL* MFXVideoENCODE_QueryIOSurf)(mfxSession, mfxVideoParam*, mfxFrameAllocRequest*) = nullptr;
    mfxStatus(MFX_CDECL* MFXVideoENCODE_Init)(mfxSession, mfxVideoParam*) = nullptr;
    mfxStatus(MFX_CDECL* MFXVideoENCODE_EncodeFrameAsync)(mfxSession, mfxEncodeCtrl*, mfxFrameSurface1*, mfxBitstream*, mfxSyncPoint*) = nullptr;
    mfxStatus(MFX_CDECL* MFXVideoENCODE_Close)(mfxSession) = nullptr;
    // decode
    mfxStatus(MFX_CDECL* MFXVideoDECODE_DecodeHeader)(mfxSession, mfxBitstream*, mfxVideoParam*) = nullptr;
    mfxStatus(MFX_CDECL* MFXVideoDECODE_Init)(mfxSession, mfxVideoParam*) = nullptr;
    mfxStatus(MFX_CDECL* MFXVideoDECODE_DecodeFrameAsync)(mfxSession, mfxBitstream*, mfxFrameSurface1*, mfxFrameSurface1**, mfxSyncPoint*) = nullptr;
    mfxStatus(MFX_CDECL* MFXVideoDECODE_Close)(mfxSession) = nullptr;
    // core
    mfxStatus(MFX_CDECL* MFXVideoCORE_SyncOperation)(mfxSession, mfxSyncPoint, mfxU32) = nullptr;
    mfxStatus(MFX_CDECL* MFXVideoCORE_SetHandle)(mfxSession, mfxHandleType, mfxHDL) = nullptr;
    mfxStatus(MFX_CDECL* MFXVideoCORE_SetFrameAllocator)(mfxSession, mfxFrameAllocator*) = nullptr;

    bool load();
    void unload();
};

}  // namespace onevpl
}  // namespace jpeg
}  // namespace oic
