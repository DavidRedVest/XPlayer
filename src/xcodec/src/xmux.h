#ifndef XMUX_H_
#define XMUX_H_

#include <cstdint>

#include "xformat.h"

// 封装写文件：stream copy，不重新编码，demux 读到的包原样写进输出容器。
class XMux : public XFormat {
public:
    ~XMux();

    // 新建一个输出封装上下文：url 是输出文件路径，video_para/audio_para 是
    // 从源（demux）拷贝来的编码参数，为空表示这一路没有对应的媒体流。
    static AVFormatContext* Open(const char* url, AVCodecParameters* video_para = nullptr,
                                  AVCodecParameters* audio_para = nullptr);

    bool WriteHead();
    bool Write(AVPacket* pkt);
    bool WriteEnd();

    // 源（demux 那一路）的时间基，写入前要把包的 pts/dts 换算过来。
    void set_src_video_time_base(AVRational* tb);
    void set_src_audio_time_base(AVRational* tb);

    // 源（demux 那一路）的音视频流下标——不一定跟这个输出容器里的下标一样
    // （XMux::Open 固定按"先建视频流再建音频流"的顺序，源端摄像头的 SDP
    // 顺序谁先谁后完全由摄像头自己决定，不能假设两边一致）。Write() 靠这
    // 两个值判断收到的包到底是视频还是音频，再重映射成输出容器自己的下标；
    // 不设置（保持默认 -1）的话，所有包都不会命中，等于没法正确写入。
    void set_src_video_index(int i) { src_video_index_ = i; }
    void set_src_audio_index(int i) { src_audio_index_ = i; }
    int src_video_index() const { return src_video_index_; }
    int src_audio_index() const { return src_audio_index_; }

private:
    AVRational* src_video_time_base_ = nullptr;
    AVRational* src_audio_time_base_ = nullptr;
    int src_video_index_ = -1;
    int src_audio_index_ = -1;

    // 源流第一个包的 pts，用来把这一段文件的时间戳从 0 开始重新计算——
    // 不然分段录像后面几段文件会带着一个巨大的起始 pts，播放器不好处理。
    long long begin_video_pts_ = -1;
    long long begin_audio_pts_ = -1;

    // 上一次实际写进输出容器的 dts（已经是输出流时间基下的值）。RTSP 源
    // 的包时间戳偶尔会有轻微抖动/乱序（尤其是网络不太稳定的时候），直接
    // 把这种 dts 交给 av_interleaved_write_frame 会被当成"非单调递增"拒绝
    // 写入——用这两个值兜底：新包的 dts 不能比上一次写的还小或相等，撞上
    // 了就顶到"上一次 + 1"，牺牲一点点时间戳精度换取不丢帧。
    long long last_video_dts_ = INT64_MIN;
    long long last_audio_dts_ = INT64_MIN;
};

#endif  // XMUX_H_
