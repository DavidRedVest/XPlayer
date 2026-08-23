#ifndef XCODEC_XRECORDER_H_
#define XCODEC_XRECORDER_H_

#include "xcodec_api.h"

// 单路摄像头的后台录像：独立于 XLiveStream 的预览连接，自己开一路 demux，
// 按时长滚动切分写 mp4 文件到 save_dir 下。跟预览完全解耦——不受预览开关/
// 切换分屏影响，也不复用预览端已经在解码的数据。
class XCODEC_API XRecorder {
public:
    XRecorder();
    ~XRecorder();

    XRecorder(const XRecorder&) = delete;
    XRecorder& operator=(const XRecorder&) = delete;

    // 开始录像。跟 XLiveStream::Open 一样，真正的连接在后台线程里做，
    // Start() 立刻返回、不阻塞调用者；返回值只表示参数合法、请求已受理，
    // 不代表已经连上。
    // window_index：触发这次录像的画面窗口编号（从左到右、从上到下），
    // 会拼进文件名，方便事后知道这段录像是哪个窗口触发的。
    // segment_seconds：每隔多久滚动一个新文件（默认 45 分钟一段，避免
    // 单个文件太大）；文件名格式固定为
    // "<时间戳>_<该窗口这次录像里的分段序号>_<window_index>.mp4"。
    bool Start(const char* url, const char* save_dir, int window_index, int segment_seconds = 2700);

    // 幂等、阻塞直到内部线程完全退出、当前文件收尾写完才返回。
    void Stop();

    bool IsRecording() const;

    // 从这次开始录像到现在经过的毫秒数（跨分段滚动持续累计，不会在切分新
    // 文件时被重置）；没在录像时返回 0。
    long long ElapsedMs() const;

private:
    struct Impl;
    Impl* impl_;
};

#endif  // XCODEC_XRECORDER_H_
