#ifndef XCODEC_XVIDEO_FRAME_H_
#define XCODEC_XVIDEO_FRAME_H_

#include "xcodec_api.h"

// 解码库对外暴露的唯一帧格式：YUV420P 三平面裸数据。
// 指针只在 OnVideoFrame 回调期间有效，实现方需要显示的话必须自己拷贝一份，
// 不要保存指针跨回调使用。
struct XVideoFrame {
    int width = 0;
    int height = 0;

    const unsigned char* y = nullptr;
    int y_stride = 0;

    const unsigned char* u = nullptr;
    int u_stride = 0;

    const unsigned char* v = nullptr;
    int v_stride = 0;

    long long pts_ms = -1;  // -1 表示时间戳未知
};

// 解码线程的帧输出回调接口。解码库不知道、也不关心实现方是 Qt 控件、
// 命令行工具还是别的什么——这是解码库与界面之间唯一的耦合点。
class XCODEC_API IXVideoSink {
public:
    virtual ~IXVideoSink() = default;

    // 在解码线程上被调用，不要在这里做耗时/阻塞操作。
    virtual void OnVideoFrame(const XVideoFrame& frame) = 0;
};

#endif  // XCODEC_XVIDEO_FRAME_H_
