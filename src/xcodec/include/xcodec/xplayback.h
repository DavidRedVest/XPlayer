#ifndef XCODEC_XPLAYBACK_H_
#define XCODEC_XPLAYBACK_H_

#include "xcodec_api.h"
#include "xvideo_frame.h"

// 探测本地文件总时长（毫秒），不建解码器、只读容器头，给文件列表快速展示
// 用；本地磁盘文件，探测很快。失败返回 0。
XCODEC_API long long XProbeDurationMs(const char* path);

// 本地文件回放（读取录像功能写出来的 mp4）：跟 XLiveStream 类似的
// pimpl + 异步打开模式，但多了暂停/seek/倍速/单帧步进——专门伺候本地文件
// 场景：音视频 pts 在写入时已经从 0 统一起跳（同一条时间线），靠"pts 相对
// 墙钟"节流播放节奏，跟直播那套完全独立（不复用 XDemuxTask/XLiveStream，
// 见 xplayback.cpp 里的说明）。
class XCODEC_API XPlayback {
public:
    XPlayback();
    ~XPlayback();

    XPlayback(const XPlayback&) = delete;
    XPlayback& operator=(const XPlayback&) = delete;

    // 打开本地文件并自动开始播放。真正的打开在后台线程里做，这个函数
    // 立刻返回，不阻塞调用者；返回值只表示参数合法，不代表已经打开成功
    // （打开失败只会体现为 IsOpen() 一直是 false，日志里能看到原因）。
    bool Open(const char* path, IXVideoSink* sink);

    // 幂等、阻塞直到内部线程完全退出、资源释放完毕才返回。
    void Close();
    bool IsOpen() const;

    void Play();
    void Pause();
    bool IsPlaying() const;

    // 打开/关闭这一路的声音，跟 XLiveStream::SetAudioMuted 一样只是翻标志位，
    // 不影响解码/画面——多个 XPlayback/XLiveStream 实例可能同时存在（比如
    // 网格里好几格同时在回放），XAudioPlay 是进程级单例，同一时刻只应该有
    // 一路真正发声，由调用方保证。默认静音（Open() 之后需要显式调用
    // SetAudioMuted(false) 才会出声）。
    void SetAudioMuted(bool muted);

    // 跳转到指定位置（毫秒）；只是换个 pts 位置继续按原来的 pts 定的
    // 播放/暂停状态走，可能落在目标之前最近的关键帧（不保证精确到帧）。
    void SeekTo(long long ms);

    // 单帧步进：前进/后退一帧，会先暂停（如果还在播放）。后退是近似实现——
    // 没有帧索引，靠 seek 到目标附近的关键帧再往后解码来逼近，不保证正好
    // 是"上一帧"。
    void StepForward();
    void StepBackward();

    // 1.0 = 正常速度。非 1.0 时自动静音（不对音频做变速不变调处理，简单
    // 播放器的常见简化）。
    void SetSpeed(double speed);
    double Speed() const;

    long long DurationMs() const;
    long long PositionMs() const;

private:
    struct Impl;
    Impl* impl_;
};

#endif  // XCODEC_XPLAYBACK_H_
