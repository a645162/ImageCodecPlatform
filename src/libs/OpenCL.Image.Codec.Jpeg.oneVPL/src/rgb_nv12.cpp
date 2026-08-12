// rgb_nv12.cpp - RGB/BGR <-> NV12 标量转换（BT.601 全范围，与 JPEG 定义一致）。
#include "rgb_nv12.h"

namespace oic {
namespace jpeg {
namespace onevpl {

namespace {

// BT.601 全范围 RGB->YCbCr，1024 定点（>>10）。
// Y  = 0.299R + 0.587G + 0.114B
// Cb = -0.168736R - 0.331264G + 0.5B + 128
// Cr = 0.5R - 0.418688G - 0.081312B + 128
inline int rgb_to_y(int r, int g, int b)
{
    return (306 * r + 601 * g + 117 * b) >> 10;
}
inline int rgb_to_cb(int r, int g, int b)
{
    return ((-173 * r - 339 * g + 512 * b) >> 10) + 128;
}
inline int rgb_to_cr(int r, int g, int b)
{
    return ((512 * r - 429 * g - 83 * b) >> 10) + 128;
}

// NV12(Y,Cb,Cr) -> 单个 RGB 通道，1024 定点（JPEG 全范围）。
inline void ycbcr_to_rgb(int yy, int cb, int cr, int& r, int& g, int& b)
{
    int dr = cr - 128;
    int db = cb - 128;
    r = yy + ((1436 * dr) >> 10);
    g = yy - ((352 * db + 731 * dr) >> 10);
    b = yy + ((1816 * db) >> 10);
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
}

template <typename F>
void rgb_to_nv12_impl(const uint8_t* rgb, uint8_t* nv12, uint32_t w, uint32_t h, F sample)
{
    // sample(pixel) -> {r,g,b}
    const uint32_t ew = w & ~1u;   // 偶数宽/高覆盖范围
    const uint32_t eh = h & ~1u;
    uint8_t* Y  = nv12;
    uint8_t* UV = nv12 + (size_t)w * h;
    for (uint32_t y = 0; y < eh; y += 2) {
        for (uint32_t x = 0; x < ew; x += 2) {
            int r0, g0, b0, r1, g1, b1, r2, g2, b2, r3, g3, b3;
            sample(rgb, w, y, x,     r0, g0, b0);
            sample(rgb, w, y, x + 1, r1, g1, b1);
            sample(rgb, w, y + 1, x,     r2, g2, b2);
            sample(rgb, w, y + 1, x + 1, r3, g3, b3);
            // 2x2 chroma 平均
            int cb = (rgb_to_cb(r0, g0, b0) + rgb_to_cb(r1, g1, b1) +
                      rgb_to_cb(r2, g2, b2) + rgb_to_cb(r3, g3, b3) + 2) >> 2;
            int cr = (rgb_to_cr(r0, g0, b0) + rgb_to_cr(r1, g1, b1) +
                      rgb_to_cr(r2, g2, b2) + rgb_to_cr(r3, g3, b3) + 2) >> 2;
            Y[(size_t)y * w + x]         = (uint8_t)rgb_to_y(r0, g0, b0);
            Y[(size_t)y * w + x + 1]     = (uint8_t)rgb_to_y(r1, g1, b1);
            Y[(size_t)(y + 1) * w + x]     = (uint8_t)rgb_to_y(r2, g2, b2);
            Y[(size_t)(y + 1) * w + x + 1] = (uint8_t)rgb_to_y(r3, g3, b3);
            size_t uv = (size_t)(y / 2) * w + x;
            UV[uv]     = (uint8_t)cb;
            UV[uv + 1] = (uint8_t)cr;
        }
    }
}

}  // namespace

void bgr24_to_nv12(const uint8_t* bgr, uint8_t* nv12, uint32_t w, uint32_t h)
{
    rgb_to_nv12_impl(bgr, nv12, w, h,
        [](const uint8_t* src, uint32_t width, uint32_t y, uint32_t x,
           int& r, int& g, int& b) {
            const uint8_t* p = src + ((size_t)y * width + x) * 3;
            b = p[0]; g = p[1]; r = p[2];
        });
}

void rgb24_to_nv12(const uint8_t* rgb, uint8_t* nv12, uint32_t w, uint32_t h)
{
    rgb_to_nv12_impl(rgb, nv12, w, h,
        [](const uint8_t* src, uint32_t width, uint32_t y, uint32_t x,
           int& r, int& g, int& b) {
            const uint8_t* p = src + ((size_t)y * width + x) * 3;
            r = p[0]; g = p[1]; b = p[2];
        });
}

void nv12_to_bgra32(const uint8_t* nv12, uint8_t* bgra, uint32_t w, uint32_t h)
{
    const uint8_t* Y  = nv12;
    const uint8_t* UV = nv12 + (size_t)w * h;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* yrow  = Y + (size_t)y * w;
        const uint8_t* uvrow = UV + (size_t)(y / 2) * w;
        uint8_t* d = bgra + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; x++) {
            int yy = yrow[x];
            int cb = uvrow[x & ~1u];
            int cr = uvrow[(x & ~1u) + 1];
            int r, g, b;
            ycbcr_to_rgb(yy, cb, cr, r, g, b);
            d[0] = (uint8_t)b;
            d[1] = (uint8_t)g;
            d[2] = (uint8_t)r;
            d[3] = 255;
        }
    }
}

void nv12_to_bgr24(const uint8_t* nv12, uint8_t* bgr, uint32_t w, uint32_t h)
{
    const uint8_t* Y  = nv12;
    const uint8_t* UV = nv12 + (size_t)w * h;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* yrow  = Y + (size_t)y * w;
        const uint8_t* uvrow = UV + (size_t)(y / 2) * w;
        uint8_t* d = bgr + (size_t)y * w * 3;
        for (uint32_t x = 0; x < w; x++) {
            int yy = yrow[x];
            int cb = uvrow[x & ~1u];
            int cr = uvrow[(x & ~1u) + 1];
            int r, g, b;
            ycbcr_to_rgb(yy, cb, cr, r, g, b);
            d[0] = (uint8_t)b;
            d[1] = (uint8_t)g;
            d[2] = (uint8_t)r;
        }
    }
}

void nv12_to_rgb24(const uint8_t* nv12, uint8_t* rgb, uint32_t w, uint32_t h)
{
    const uint8_t* Y  = nv12;
    const uint8_t* UV = nv12 + (size_t)w * h;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* yrow  = Y + (size_t)y * w;
        const uint8_t* uvrow = UV + (size_t)(y / 2) * w;
        uint8_t* d = rgb + (size_t)y * w * 3;
        for (uint32_t x = 0; x < w; x++) {
            int yy = yrow[x];
            int cb = uvrow[x & ~1u];
            int cr = uvrow[(x & ~1u) + 1];
            int r, g, b;
            ycbcr_to_rgb(yy, cb, cr, r, g, b);
            d[0] = (uint8_t)r;
            d[1] = (uint8_t)g;
            d[2] = (uint8_t)b;
        }
    }
}

}  // namespace onevpl
}  // namespace jpeg
}  // namespace oic
