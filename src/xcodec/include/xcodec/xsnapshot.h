#ifndef XCODEC_XSNAPSHOT_H_
#define XCODEC_XSNAPSHOT_H_

#include "xcodec_api.h"

// 把一帧 YUV420P 转成 RGB24（3 字节/像素，行内不 padding，width*3 一行），
// 方便调用方直接包一层 QImage 存文件——PNG/BMP 之类的图片编码交给调用方
// 处理（xviewer 那边直接用 Qt 自带的 QImage::save()，不需要在 xcodec 里
// 再引入图片编码器）。
// rgb_out 必须指向至少 width*height*3 字节的缓冲区，由调用方分配。
// 失败（参数不合法、内部转换失败）返回 false，rgb_out 内容不保证有效。
XCODEC_API bool ConvertYuv420pToRgb24(const unsigned char* y, int y_stride,
                                       const unsigned char* u, int u_stride,
                                       const unsigned char* v, int v_stride, int width,
                                       int height, unsigned char* rgb_out);

#endif  // XCODEC_XSNAPSHOT_H_
