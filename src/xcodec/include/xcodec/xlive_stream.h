#ifndef XCODEC_XLIVE_STREAM_H_
#define XCODEC_XLIVE_STREAM_H_

#include <string>

#include "xcodec_api.h"
#include "xvideo_frame.h"

// 单路直播流（RTSP/本地文件均可）的解封装+解码管线。
// 只做视频：对应 player_v1 里 XCameraWidget 手动拼 XDemuxTask+XDecodeTask 的角色，
// 音频会在后续里程碑加入。这是解码库对外的唯一业务入口，不暴露任何 FFmpeg 类型。
class XCODEC_API XLiveStream {
public:
    XLiveStream();
    ~XLiveStream();

    XLiveStream(const XLiveStream&) = delete;
    XLiveStream& operator=(const XLiveStream&) = delete;

    // sink 生命周期必须覆盖 Open()到Close()之间的整个区间，调用方负责保证。
    //
    // 真正的连接（RTSP 握手、探流）在内部后台线程里做，Open() 本身立刻
    // 返回，不会阻塞调用者——返回值只表示"请求已受理"，不代表已经连上；
    // 连接是否成功要看之后 IsOpen() 的状态。参数校验失败（url/sink 为空）
    // 才会同步返回 false。
    //
    // enable_audio=false 时完全跳过音频解码管线（不解码、不碰 XAudioPlay
    // 单例）。enable_audio=true 时会解码音频，但默认是静音的（不会调用
    // XAudioPlay::Push），要通过 SetAudioMuted(false) 显式打开声音——
    // 因为 XAudioPlay 是进程内单例，多路同时真正发声没有意义，只会互相
    // 打断，调用方（比如网格视图）需要自己保证同一时刻只有一路解除静音。
    bool Open(const char* url, IXVideoSink* sink, bool enable_audio = true);

    // 打开/关闭这一路的声音。只是切换"解码出的音频帧要不要送进 XAudioPlay"
    // 这一个判断，不重启解码线程、不重连流，视频完全不受影响，可以在播放
    // 过程中随时调用（比如网格里切换选中的格子）。对没有音轨、或者 Open()
    // 时 enable_audio=false 的流是空操作。
    void SetAudioMuted(bool muted);

    // 幂等、阻塞直到内部线程完全退出、所有解码资源释放完毕才返回。
    void Close();

    bool IsOpen() const;

private:
    void OpenWorker(const std::string& url, bool enable_audio);

    struct Impl;
    Impl* impl_;
};

#endif  // XCODEC_XLIVE_STREAM_H_
