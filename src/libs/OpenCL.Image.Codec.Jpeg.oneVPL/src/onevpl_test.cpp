// onevpl_test.cpp - oneVPL JPEG 后端 CLI 测试入口实现。
#include "onevpl_test.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "vpl_dyn.h"
#include "vpl_jpeg_decoder.h"
#include "vpl_jpeg_encoder.h"
#include "rgb_nv12.h"

namespace oic {
namespace jpeg {
namespace onevpl {
namespace {

#pragma pack(push, 1)
struct BmpFileHeader {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfRes1;
    uint16_t bfRes2;
    uint32_t bfOffBits;
};
struct BmpInfoHeader {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack(pop)

// 读文件进内存。
bool load_file(const std::string& path, std::vector<uint8_t>& data)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    data.resize((size_t)size);
    bool ok = std::fread(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

// 解码 24-bit 未压缩 BMP -> 顶向下 BGR24（紧致 w*h*3）。
bool decode_bmp24(const std::vector<uint8_t>& fileBuf,
                  uint32_t& width, uint32_t& height,
                  std::vector<uint8_t>& bgr)
{
    if (fileBuf.size() < 54)
        return false;
    const BmpFileHeader* fh = (const BmpFileHeader*)fileBuf.data();
    const BmpInfoHeader* ih = (const BmpInfoHeader*)(fileBuf.data() + 14);
    if (fh->bfType != 0x4D42)                 // 'BM'
        return false;
    if (ih->biBitCount != 24 || ih->biCompression != 0)
        return false;
    if (ih->biWidth <= 0 || ih->biHeight == 0)
        return false;
    uint32_t w = (uint32_t)ih->biWidth;
    uint32_t h = (uint32_t)(ih->biHeight < 0 ? -ih->biHeight : ih->biHeight);
    bool bottomUp = ih->biHeight > 0;
    uint32_t rowStride = ((w * 3 + 3) / 4) * 4;
    if (fileBuf.size() < fh->bfOffBits + (size_t)rowStride * h)
        return false;

    bgr.assign((size_t)w * h * 3, 0);
    const uint8_t* src = fileBuf.data() + fh->bfOffBits;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t srcRow = bottomUp ? (h - 1 - y) : y;
        std::memcpy(bgr.data() + (size_t)y * w * 3,
                    src + (size_t)srcRow * rowStride, (size_t)w * 3);
    }
    width = w;
    height = h;
    return true;
}

// 写 24-bit 未压缩 BMP（bottom-up）。
bool write_bmp24(const std::string& path, const uint8_t* bgr, uint32_t w, uint32_t h)
{
    uint32_t stride = ((w * 3 + 3) / 4) * 4;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    BmpFileHeader fh{};
    BmpInfoHeader ih{};
    fh.bfType = 0x4D42;
    fh.bfSize = 54 + stride * h;
    fh.bfOffBits = 54;
    ih.biSize = 40;
    ih.biWidth = (int32_t)w;
    ih.biHeight = (int32_t)h;          // 正 -> bottom-up
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biSizeImage = stride * h;
    std::fwrite(&fh, 1, sizeof(fh), f);
    std::fwrite(&ih, 1, sizeof(ih), f);
    std::vector<uint8_t> row(stride);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* srcRow = bgr + (size_t)(h - 1 - y) * w * 3;   // 翻转
        std::memcpy(row.data(), srcRow, (size_t)w * 3);
        std::fwrite(row.data(), 1, stride, f);
    }
    std::fclose(f);
    return true;
}

}  // namespace
}  // namespace onevpl
}  // namespace jpeg
}  // namespace oic

// ---- 对外入口（C 链接，签名固定） ----
extern "C" {

int oic_onevpl_encode(const char* in_bmp, const char* out_jpeg, int quality)
{
    using namespace oic::jpeg::onevpl;
    if (!in_bmp || !out_jpeg) {
        std::fprintf(stderr, "[onevpl] encode: 参数为空\n");
        return 1;
    }
    std::vector<uint8_t> fileBuf;
    if (!load_file(in_bmp, fileBuf)) {
        std::fprintf(stderr, "[onevpl] encode: 读取 BMP 失败: %s\n", in_bmp);
        return 1;
    }
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> bgr;
    if (!decode_bmp24(fileBuf, w, h, bgr)) {
        std::fprintf(stderr, "[onevpl] encode: BMP 解析失败（需 24-bit 未压缩）: %s\n", in_bmp);
        return 1;
    }
    if (w > 16384 || h > 16384) {
        std::fprintf(stderr, "[onevpl] encode: 尺寸过大(%ux%u)，硬件限制上限 16384\n", w, h);
        return 1;
    }
    if ((w % 2) || (h % 2)) {
        std::fprintf(stderr, "[onevpl] encode: 尺寸需为偶数(%ux%u)\n", w, h);
        return 1;
    }

    VplJpegEncoder enc;
    if (!enc.init(w, h, (uint32_t)(quality < 0 ? 0 : quality))) {
        std::fprintf(stderr, "[onevpl] encode: 编码器初始化失败（impl=%s）\n",
                     enc.impl_name.empty() ? "?" : enc.impl_name.c_str());
        return 1;
    }
    std::vector<uint8_t> jpeg;
    bool ok = enc.encode_bgr24(bgr.data(), bgr.size(), jpeg);
    std::string impl = enc.impl_name;
    enc.close();
    if (!ok || jpeg.empty()) {
        std::fprintf(stderr, "[onevpl] encode: 编码失败\n");
        return 1;
    }
    FILE* fo = std::fopen(out_jpeg, "wb");
    if (!fo) {
        std::fprintf(stderr, "[onevpl] encode: 打开输出失败: %s\n", out_jpeg);
        return 1;
    }
    std::fwrite(jpeg.data(), 1, jpeg.size(), fo);
    std::fclose(fo);
    std::printf("[onevpl] encode ok: %ux%u q=%d -> %s (%zu bytes, impl=%s)\n",
                w, h, quality, out_jpeg, jpeg.size(), impl.c_str());
    return 0;
}

int oic_onevpl_decode(const char* in_jpeg, const char* out_bmp)
{
    using namespace oic::jpeg::onevpl;
    if (!in_jpeg || !out_bmp) {
        std::fprintf(stderr, "[onevpl] decode: 参数为空\n");
        return 1;
    }
    std::vector<uint8_t> fileBuf;
    if (!load_file(in_jpeg, fileBuf)) {
        std::fprintf(stderr, "[onevpl] decode: 读取 JPEG 失败: %s\n", in_jpeg);
        return 1;
    }

    VplJpegDecoder dec;
    if (!dec.init()) {
        std::fprintf(stderr, "[onevpl] decode: 解码器初始化失败（impl=%s）\n",
                     dec.impl_name.empty() ? "?" : dec.impl_name.c_str());
        return 1;
    }
    std::vector<uint8_t> bgra;
    uint32_t w = 0, h = 0;
    bool ok = dec.decode(fileBuf.data(), fileBuf.size(), bgra, w, h);
    std::string impl = dec.impl_name;
    dec.close();
    if (!ok || bgra.empty()) {
        std::fprintf(stderr, "[onevpl] decode: 解码失败\n");
        return 1;
    }

    // BGRA -> BGR24。
    std::vector<uint8_t> bgr((size_t)w * h * 3);
    for (size_t i = 0, n = (size_t)w * h; i < n; i++) {
        bgr[i * 3 + 0] = bgra[i * 4 + 0];
        bgr[i * 3 + 1] = bgra[i * 4 + 1];
        bgr[i * 3 + 2] = bgra[i * 4 + 2];
    }
    if (!write_bmp24(out_bmp, bgr.data(), w, h)) {
        std::fprintf(stderr, "[onevpl] decode: 写 BMP 失败: %s\n", out_bmp);
        return 1;
    }
    std::printf("[onevpl] decode ok: %ux%u -> %s (%zu bytes, impl=%s)\n",
                w, h, out_bmp, (size_t)w * h * 3 + 54, impl.c_str());
    return 0;
}

int oic_onevpl_available(void)
{
    using namespace oic::jpeg::onevpl;
    // 完整加载校验：dll 存在 + 全部 MFX 入口可解析。
    VplDyn dyn;
    if (!dyn.load())
        return 1;   // 不可用
    dyn.unload();
    return 0;       // 可用
}

}  // extern "C"
