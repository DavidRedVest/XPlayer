#ifndef XMUX_TASK_H_
#define XMUX_TASK_H_

#include "xmux.h"
#include "xtools.h"

// 封装写文件线程：上游（demux）把包 Do() 过来，这里先缓冲，等到第一个视频
// 关键帧才真正开始写（保证每段录像文件都能独立解码），之后来一个写一个。
class XMuxTask : public XThread {
public:
    void Main() override;

    // src_video_index/src_audio_index：这一路数据在源（demux）那边的流
    // 下标——源端的流顺序由源自己决定，跟这个输出容器里"先建视频流再建
    // 音频流"的固定顺序不一定一样，必须显式告诉 XMux 才能正确分类/重映射
    // 收到的包（见 xmux.h 里 set_src_video_index 的说明）。
    bool Open(const char* url, AVCodecParameters* video_para = nullptr,
              AVRational* video_time_base = nullptr, AVCodecParameters* audio_para = nullptr,
              AVRational* audio_time_base = nullptr, int src_video_index = -1,
              int src_audio_index = -1);

    void Do(AVPacket* pkt) override;

private:
    XMux xmux_;
    XAVPacketList pkts_;
};

#endif  // XMUX_TASK_H_
