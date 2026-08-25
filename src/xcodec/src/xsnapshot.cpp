#include "xcodec/xsnapshot.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

bool ConvertYuv420pToRgb24(const unsigned char* y, int y_stride, const unsigned char* u,
                            int u_stride, const unsigned char* v, int v_stride, int width,
                            int height, unsigned char* rgb_out) {
    if (width <= 0 || height <= 0 || !y || !u || !v || !rgb_out) {
        return false;
    }
    SwsContext* ctx = sws_getContext(width, height, AV_PIX_FMT_YUV420P, width, height,
                                      AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!ctx) {
        return false;
    }
    const uint8_t* src_planes[3] = {y, u, v};
    int src_strides[3] = {y_stride, u_stride, v_stride};
    uint8_t* dst_planes[1] = {rgb_out};
    int dst_strides[1] = {width * 3};
    sws_scale(ctx, src_planes, src_strides, 0, height, dst_planes, dst_strides);
    sws_freeContext(ctx);
    return true;
}
