// onevpl_test.h - oneVPL JPEG 后端 CLI 测试入口（供 lead 集成，签名固定）。
//
// 所有函数返回 0 表示成功，非 0 表示失败（错误已打印到 stderr）。
// 无 libvpl.dll / 无 Intel 硬件 / 无 D3D11 时返回明确错误码，保证不崩溃。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 编码：in_bmp(24-bit 未压缩 BMP) -> out_jpeg(JPEG)。quality 0..100。
int oic_onevpl_encode(const char* in_bmp, const char* out_jpeg, int quality);

// 解码：in_jpeg -> out_bmp(24-bit 未压缩 BMP)。
int oic_onevpl_decode(const char* in_jpeg, const char* out_bmp);

// 探测：0 = libvpl.dll 可用（可加载且全部 MFX 入口可解析），1 = 不可用。
int oic_onevpl_available(void);

#ifdef __cplusplus
}
#endif
