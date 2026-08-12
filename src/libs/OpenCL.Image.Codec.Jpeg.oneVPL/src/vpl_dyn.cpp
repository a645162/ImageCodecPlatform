// vpl_dyn.cpp - oneVPL (libvpl.dll) 动态加载绑定实现。
#include "vpl_dyn.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace oic {
namespace jpeg {
namespace onevpl {

namespace {

const char* kDllName = "libvpl.dll";

}  // namespace

bool vpl_available()
{
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(kDllName);
    if (!h)
        return false;
    FreeLibrary(h);
    return true;
#else
    return false;  // oneVPL 后端仅 Windows
#endif
}

const char* vpl_status_str(mfxStatus sts)
{
    switch (sts) {
        case MFX_ERR_NONE:                        return "MFX_ERR_NONE";
        case MFX_ERR_UNKNOWN:                     return "MFX_ERR_UNKNOWN";
        case MFX_ERR_UNSUPPORTED:                 return "MFX_ERR_UNSUPPORTED";
        case MFX_ERR_MEMORY_ALLOC:                return "MFX_ERR_MEMORY_ALLOC";
        case MFX_ERR_NOT_ENOUGH_BUFFER:           return "MFX_ERR_NOT_ENOUGH_BUFFER";
        case MFX_ERR_INVALID_HANDLE:              return "MFX_ERR_INVALID_HANDLE";
        case MFX_ERR_LOCK_MEMORY:                 return "MFX_ERR_LOCK_MEMORY";
        case MFX_ERR_NOT_INITIALIZED:             return "MFX_ERR_NOT_INITIALIZED";
        case MFX_ERR_NOT_FOUND:                   return "MFX_ERR_NOT_FOUND";
        case MFX_ERR_NULL_PTR:                    return "MFX_ERR_NULL_PTR";
        case MFX_ERR_UNDEFINED_BEHAVIOR:          return "MFX_ERR_UNDEFINED_BEHAVIOR";
        case MFX_ERR_INVALID_VIDEO_PARAM:         return "MFX_ERR_INVALID_VIDEO_PARAM";
        case MFX_ERR_INCOMPATIBLE_VIDEO_PARAM:    return "MFX_ERR_INCOMPATIBLE_VIDEO_PARAM";
        case MFX_ERR_ABORTED:                     return "MFX_ERR_ABORTED";
        case MFX_ERR_MORE_DATA:                   return "MFX_ERR_MORE_DATA";
        case MFX_ERR_MORE_SURFACE:                return "MFX_ERR_MORE_SURFACE";
        case MFX_WRN_IN_EXECUTION:                return "MFX_WRN_IN_EXECUTION";
        case MFX_WRN_DEVICE_BUSY:                 return "MFX_WRN_DEVICE_BUSY";
        case MFX_WRN_VIDEO_PARAM_CHANGED:         return "MFX_WRN_VIDEO_PARAM_CHANGED";
        case MFX_WRN_PARTIAL_ACCELERATION:        return "MFX_WRN_PARTIAL_ACCELERATION";
        case MFX_WRN_INCOMPATIBLE_VIDEO_PARAM:    return "MFX_WRN_INCOMPATIBLE_VIDEO_PARAM";
        case MFX_WRN_VALUE_NOT_CHANGED:           return "MFX_WRN_VALUE_NOT_CHANGED";
        case MFX_WRN_OUT_OF_RANGE:                return "MFX_WRN_OUT_OF_RANGE";
        case MFX_WRN_FILTER_SKIPPED:              return "MFX_WRN_FILTER_SKIPPED";
        default:                                  return "MFX_STATUS_UNKNOWN";
    }
}

#define LOAD_SYM(name)                                                          \
    name = (decltype(name))GetProcAddress((HMODULE)hDll, #name);                \
    if (!name) {                                                                \
        std::fprintf(stderr, "[vpl] GetProcAddress(%s) failed\n", #name);       \
        unload();                                                               \
        return false;                                                           \
    }

bool VplDyn::load()
{
#if !defined(_WIN32)
    return false;
#else
    if (hDll)
        return true;  // 已加载
    hDll = (void*)LoadLibraryA(kDllName);
    if (!hDll) {
        std::fprintf(stderr, "[vpl] LoadLibrary(%s) failed\n", kDllName);
        return false;
    }
    // loader / session
    LOAD_SYM(MFXLoad);
    LOAD_SYM(MFXUnload);
    LOAD_SYM(MFXCreateConfig);
    LOAD_SYM(MFXSetConfigFilterProperty);
    LOAD_SYM(MFXEnumImplementations);
    LOAD_SYM(MFXCreateSession);
    LOAD_SYM(MFXClose);
    // encode
    LOAD_SYM(MFXVideoENCODE_Query);
    LOAD_SYM(MFXVideoENCODE_QueryIOSurf);
    LOAD_SYM(MFXVideoENCODE_Init);
    LOAD_SYM(MFXVideoENCODE_EncodeFrameAsync);
    LOAD_SYM(MFXVideoENCODE_Close);
    // decode
    LOAD_SYM(MFXVideoDECODE_DecodeHeader);
    LOAD_SYM(MFXVideoDECODE_Init);
    LOAD_SYM(MFXVideoDECODE_DecodeFrameAsync);
    LOAD_SYM(MFXVideoDECODE_Close);
    // core
    LOAD_SYM(MFXVideoCORE_SyncOperation);
    LOAD_SYM(MFXVideoCORE_SetHandle);
    LOAD_SYM(MFXVideoCORE_SetFrameAllocator);
    return true;
#endif
}

void VplDyn::unload()
{
#if defined(_WIN32)
    if (hDll) {
        FreeLibrary((HMODULE)hDll);
        hDll = nullptr;
    }
#endif
}

}  // namespace onevpl
}  // namespace jpeg
}  // namespace oic
