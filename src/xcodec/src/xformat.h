#ifndef XFORMAT_H_
#define XFORMAT_H_

#include <atomic>
#include <memory>
#include <mutex>

extern "C" {
#include <libavformat/avio.h>
}

#include "xtools.h"

struct AVFormatContext;
struct AVCodecParameters;
struct AVPacket;
struct AVCodecContext;

struct XRational {
    int num;
    int den;
};

// 封装/解封装公共基类：持有 AVFormatContext，负责流索引识别、超时检测、
// pts 换算这些两边都要用的逻辑。
class XFormat {
public:
    virtual ~XFormat() = default;

    bool CopyPara(int stream_index, AVCodecParameters* dst);
    bool CopyPara(int stream_index, AVCodecContext* dst);

    std::shared_ptr<XPara> CopyVideoPara();
    std::shared_ptr<XPara> CopyAudioPara();

    // 传入 nullptr 相当于关闭当前上下文并释放资源。
    void set_c(AVFormatContext* c);

    int video_index() const { return video_index_; }
    int audio_index() const { return audio_index_; }
    XRational video_time_base() const { return video_time_base_; }
    XRational audio_time_base() const { return audio_time_base_; }

    bool RescaleTime(AVPacket* pkt, long long offset_pts, XRational time_base);
    bool RescaleTime(AVPacket* pkt, long long offset_pts, AVRational* time_base);
    long long RescaleToMs(long long pts, int index);

    int video_codec_id() const { return video_codec_id_; }

    // 容器整体的时长（毫秒），不是某一路流自己的 duration 字段——录像文件
    // 实测过，单路流（尤其是视频流）自己的 duration 有时会不准（跟实际
    // 内容对不上），容器级别的 AVFormatContext::duration 更可靠。
    long long DurationMs();

    void set_time_out_ms(int ms);
    bool IsTimeout();

    // 立刻打断当前正在进行的连接/读取（不用等 time_out_ms_ 超时）,
    // Stop() 时调用，避免"重连过程本身卡住"导致线程永远退不出去。
    void RequestAbort() { abort_requested_ = true; }

    bool is_connected() const { return is_connected_; }

protected:
    // 分配好 AVFormatContext、装好中断回调（用 this 已有的 time_out_ms_）后
    // 再发起 avformat_open_input，这样"连接"这一步本身也能被打断——不像只在
    // set_c() 里设置回调那样，只有连接*成功之后*的读取才受超时保护。
    AVFormatContext* AllocWithInterruptCB();

    static int InterruptCallback(void* opaque);

    int time_out_ms_ = 0;
    long long last_time_ = 0;
    bool is_connected_ = false;
    std::atomic<bool> abort_requested_{false};

    AVFormatContext* c_ = nullptr;
    std::mutex mux_;

    int video_index_ = -1;
    int audio_index_ = -1;
    XRational video_time_base_ = {1, 25};
    XRational audio_time_base_ = {1, 9000};
    int video_codec_id_ = 0;
};

#endif  // XFORMAT_H_
