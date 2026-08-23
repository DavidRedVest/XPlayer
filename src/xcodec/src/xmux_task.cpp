#include "xmux_task.h"

extern "C" {
#include <libavformat/avformat.h>
}

void XMuxTask::Do(AVPacket* pkt) { pkts_.Push(pkt); }

void XMuxTask::Main() {
    xmux_.WriteHead();

    // 等到第一个视频关键帧再真正开始写——这个关键帧本身也要写进去（不能
    // 丢），不然输出流从一个没有 I 帧参考的 P 帧开始，没法独立解码。跳过
    // 关键帧之前的所有包（含音频），保证录像文件从第一帧起就能独立解码，
    // 不会开头黑一段。注意这里要跟源（demux）的下标比，不是这个输出容器
    // 自己的下标——这一步发生在 xmux_.Write() 重映射之前。
    while (!is_exit_) {
        auto pkt = pkts_.Pop();
        if (!pkt) {
            MyDelay(1);
            continue;
        }
        bool is_video_key =
            pkt->stream_index == xmux_.src_video_index() && (pkt->flags & AV_PKT_FLAG_KEY);
        if (is_video_key) {
            xmux_.Write(pkt);
            av_packet_free(&pkt);
            break;
        }
        av_packet_free(&pkt);
    }

    while (!is_exit_) {
        auto pkt = pkts_.Pop();
        if (!pkt) {
            MyDelay(1);
            continue;
        }
        xmux_.Write(pkt);
        av_packet_free(&pkt);
    }

    // 退出主循环时队列里可能还剩最后几个包（Stop() 的 Exit() 和这里检查
    // is_exit_ 之间有个时间窗口）——写完排空再收尾，不然文件结尾会丢最后
    // 零点几秒。
    AVPacket* pkt = nullptr;
    while ((pkt = pkts_.Pop()) != nullptr) {
        xmux_.Write(pkt);
        av_packet_free(&pkt);
    }

    xmux_.WriteEnd();
    xmux_.set_c(nullptr);
}

bool XMuxTask::Open(const char* url, AVCodecParameters* video_para, AVRational* video_time_base,
                     AVCodecParameters* audio_para, AVRational* audio_time_base,
                     int src_video_index, int src_audio_index) {
    auto c = XMux::Open(url, video_para, audio_para);
    if (!c) {
        return false;
    }
    xmux_.set_c(c);
    xmux_.set_src_video_time_base(video_time_base);
    xmux_.set_src_audio_time_base(audio_time_base);
    xmux_.set_src_video_index(src_video_index);
    xmux_.set_src_audio_index(src_audio_index);
    return true;
}
