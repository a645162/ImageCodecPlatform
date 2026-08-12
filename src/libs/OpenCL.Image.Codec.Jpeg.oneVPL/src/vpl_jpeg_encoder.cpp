// vpl_jpeg_encoder.cpp - oneVPL 硬件 JPEG 编码器实现（全部经 VplDyn 动态绑定）。
#include "vpl_jpeg_encoder.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include "rgb_nv12.h"

namespace oic {
namespace jpeg {
namespace onevpl {

bool VplJpegEncoder::init(uint32_t w, uint32_t h, uint32_t quality)
{
    width = (mfxU32)w;
    height = (mfxU32)h;

    if (!dyn.load()) {
        std::fprintf(stderr, "[vpl] encoder: libvpl.dll 加载失败\n");
        return false;
    }

    loader = dyn.MFXLoad();
    if (!loader) {
        std::fprintf(stderr, "[vpl] encoder: MFXLoad 失败\n");
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
        std::fprintf(stderr, "[vpl] encoder: 未找到 Intel 硬件实现\n");
        dyn.MFXUnload(loader);
        loader = nullptr;
        dyn.unload();
        return false;
    }
    if (dyn.MFXCreateSession(loader, chosen, &session) != MFX_ERR_NONE) {
        std::fprintf(stderr, "[vpl] encoder: MFXCreateSession 失败\n");
        dyn.MFXUnload(loader);
        loader = nullptr;
        dyn.unload();
        return false;
    }

    std::memset(&param, 0, sizeof(param));
    param.mfx.CodecId                       = MFX_CODEC_JPEG;
    param.mfx.CodecProfile                  = MFX_PROFILE_JPEG_BASELINE;
    param.mfx.LowPower                      = 0;
    param.mfx.FrameInfo.FourCC              = MFX_FOURCC_NV12;
    param.mfx.FrameInfo.ChromaFormat        = MFX_CHROMAFORMAT_YUV420;
    param.mfx.FrameInfo.Width               = (mfxU16)w;
    param.mfx.FrameInfo.Height              = (mfxU16)h;
    param.mfx.FrameInfo.CropW               = (mfxU16)w;
    param.mfx.FrameInfo.CropH               = (mfxU16)h;
    param.mfx.FrameInfo.PicStruct           = MFX_PICSTRUCT_PROGRESSIVE;
    param.mfx.FrameInfo.FrameRateExtN       = 30;
    param.mfx.FrameInfo.FrameRateExtD       = 1;
    param.mfx.FrameInfo.BitDepthLuma        = 8;
    param.mfx.FrameInfo.BitDepthChroma      = 8;
    param.mfx.Quality                       = (mfxU16)(quality > 100 ? 100 : quality);
    param.mfx.Interleaved                   = 1;
    param.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    mfxStatus sts = dyn.MFXVideoENCODE_Query(session, &param, &param);
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        std::fprintf(stderr, "[vpl] encoder: Query 失败 %s(%d)\n",
                     vpl_status_str(sts), (int)sts);
        close();
        return false;
    }
    dyn.MFXVideoENCODE_QueryIOSurf(session, &param, nullptr);
    sts = dyn.MFXVideoENCODE_Init(session, &param);
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        std::fprintf(stderr, "[vpl] encoder: Init 失败 %s(%d)\n",
                     vpl_status_str(sts), (int)sts);
        close();
        return false;
    }

    // 表面指向调用方传入的 NV12 缓冲（Y + 交错 UV），pitch = width。
    surface.Info = param.mfx.FrameInfo;
    surface.Data.Pitch = (mfxU16)w;
    return true;
}

bool VplJpegEncoder::encode(const uint8_t* nv12, size_t nv12_bytes, std::vector<uint8_t>& jpeg)
{
    jpeg.clear();
    if (!session || !nv12)
        return false;
    const size_t need = (size_t)width * height * 3 / 2;
    if (nv12_bytes < need)
        return false;

    surface.Data.Y  = (mfxU8*)nv12;
    surface.Data.UV = (mfxU8*)nv12 + (size_t)width * height;
    surface.Data.Pitch = (mfxU16)width;

    size_t cap = need + (64u * 1024);            // 初值：帧大小 + 64KiB
    std::vector<uint8_t> bs(cap);
    mfxBitstream bst{};
    bst.MaxLength = (mfxU32)cap;
    bst.Data = bs.data();

    mfxEncodeCtrl ctrl{};
    ctrl.FrameType = MFX_FRAMETYPE_I;            // 单帧 I 帧
    mfxSyncPoint syncp = nullptr;
    mfxStatus sts = MFX_ERR_NONE;

    for (int attempt = 0; attempt < 8; attempt++) {
        syncp = nullptr;
        bst.DataOffset = 0;
        bst.DataLength = 0;
        sts = dyn.MFXVideoENCODE_EncodeFrameAsync(session, &ctrl, &surface, &bst, &syncp);
        if (sts == MFX_ERR_NONE) {
            if (syncp)
                dyn.MFXVideoCORE_SyncOperation(session, syncp, 60000);
            jpeg.assign(bst.Data + bst.DataOffset, bst.Data + bst.DataOffset + bst.DataLength);
            break;
        } else if (sts == MFX_ERR_NOT_ENOUGH_BUFFER) {
            cap *= 2;
            bs.resize(cap);
            bst.MaxLength = (mfxU32)cap;
            bst.Data = bs.data();
            bst.DataOffset = 0;
            bst.DataLength = 0;
            continue;
        } else if (sts == MFX_ERR_MORE_DATA) {
            break;                                 // 无输出
        } else {
            std::fprintf(stderr, "[vpl] encoder: EncodeFrameAsync 失败 %s(%d)\n",
                         vpl_status_str(sts), (int)sts);
            return false;
        }
    }

    // Drain flush。
    for (int attempt = 0; attempt < 8; attempt++) {
        syncp = nullptr;
        bst.DataOffset = 0;
        bst.DataLength = 0;
        sts = dyn.MFXVideoENCODE_EncodeFrameAsync(session, nullptr, nullptr, &bst, &syncp);
        if (sts == MFX_ERR_MORE_DATA)
            break;
        if (sts == MFX_ERR_NONE || sts == MFX_ERR_NOT_ENOUGH_BUFFER) {
            if (syncp)
                dyn.MFXVideoCORE_SyncOperation(session, syncp, 60000);
            jpeg.insert(jpeg.end(),
                        bst.Data + bst.DataOffset, bst.Data + bst.DataOffset + bst.DataLength);
            if (sts == MFX_ERR_NOT_ENOUGH_BUFFER) {
                cap *= 2;
                bs.resize(cap);
                bst.MaxLength = (mfxU32)cap;
                bst.Data = bs.data();
                continue;
            }
        } else {
            break;
        }
    }
    return !jpeg.empty();
}

bool VplJpegEncoder::encode_rgb24(const uint8_t* rgb, size_t rgb_bytes, std::vector<uint8_t>& jpeg)
{
    const size_t need = (size_t)width * height * 3;
    if (!rgb || rgb_bytes < need)
        return false;
    std::vector<uint8_t> nv12((size_t)width * height * 3 / 2);
    rgb24_to_nv12(rgb, nv12.data(), width, height);
    return encode(nv12.data(), nv12.size(), jpeg);
}

bool VplJpegEncoder::encode_bgr24(const uint8_t* bgr, size_t bgr_bytes, std::vector<uint8_t>& jpeg)
{
    const size_t need = (size_t)width * height * 3;
    if (!bgr || bgr_bytes < need)
        return false;
    std::vector<uint8_t> nv12((size_t)width * height * 3 / 2);
    bgr24_to_nv12(bgr, nv12.data(), width, height);
    return encode(nv12.data(), nv12.size(), jpeg);
}

void VplJpegEncoder::close()
{
    if (session) {
        dyn.MFXVideoENCODE_Close(session);
        dyn.MFXClose(session);
        session = nullptr;
    }
    if (loader) {
        dyn.MFXUnload(loader);
        loader = nullptr;
    }
    dyn.unload();
}

}  // namespace onevpl
}  // namespace jpeg
}  // namespace oic
