#include "xmux.h"

extern "C" {
#include <libavformat/avformat.h>
}

using namespace std;

#define BERR(err)      \
    if (0 != (err)) {  \
        PrintErr(err); \
        return false;  \
    }

void XMux::set_src_video_time_base(AVRational* tb) {
    if (!tb) {
        return;
    }
    unique_lock<mutex> lock(mux_);
    if (!src_video_time_base_) {
        src_video_time_base_ = new AVRational();
    }
    *src_video_time_base_ = *tb;
}

void XMux::set_src_audio_time_base(AVRational* tb) {
    if (!tb) {
        return;
    }
    unique_lock<mutex> lock(mux_);
    if (!src_audio_time_base_) {
        src_audio_time_base_ = new AVRational();
    }
    *src_audio_time_base_ = *tb;
}

XMux::~XMux() {
    unique_lock<mutex> lock(mux_);
    delete src_video_time_base_;
    src_video_time_base_ = nullptr;
    delete src_audio_time_base_;
    src_audio_time_base_ = nullptr;
}

AVFormatContext* XMux::Open(const char* url, AVCodecParameters* video_para,
                             AVCodecParameters* audio_para) {
    AVFormatContext* c = nullptr;
    auto re = avformat_alloc_output_context2(&c, nullptr, nullptr, url);
    if (0 != re) {
        PrintErr(re);
        return nullptr;
    }

    if (video_para) {
        auto vs = avformat_new_stream(c, nullptr);
        avcodec_parameters_copy(vs->codecpar, video_para);
    }
    if (audio_para) {
        auto as = avformat_new_stream(c, nullptr);
        avcodec_parameters_copy(as->codecpar, audio_para);
    }

    re = avio_open(&c->pb, url, AVIO_FLAG_WRITE);
    if (0 != re) {
        PrintErr(re);
        avformat_free_context(c);
        return nullptr;
    }
    return c;
}

bool XMux::WriteHead() {
    unique_lock<mutex> lock(mux_);
    if (!c_) {
        return false;
    }
    auto re = avformat_write_header(c_, nullptr);
    BERR(re);
    begin_video_pts_ = -1;
    begin_audio_pts_ = -1;
    last_video_dts_ = INT64_MIN;
    last_audio_dts_ = INT64_MIN;
    return true;
}

bool XMux::Write(AVPacket* pkt) {
    if (!pkt) {
        return false;
    }
    unique_lock<mutex> lock(mux_);
    if (!c_) {
        return false;
    }
    // 拿不到 pts 就退化成从 0 开始，好过直接丢弃这个包。
    if (pkt->pts == AV_NOPTS_VALUE) {
        pkt->pts = 0;
        pkt->dts = 0;
    }

    // pkt->stream_index 现在还是源（demux）那一路的下标，跟这个输出容器
    // 自己的 video_index_/audio_index_ 不一定相等（两边流的先后顺序是各自
    // 独立决定的）——必须先按源下标分类，重映射成输出下标，再用输出下标
    // 去查 RescaleTime 里 c_->streams[pkt->stream_index] 用的时间基，最后
    // av_interleaved_write_frame 也要用重映射之后的下标才能写进正确的轨道，
    // 不然音视频会串轨、pts 换算也会用错时间基（历史上出现过"多数视频帧
    // 静默丢失、只剩音频"就是这个原因）。
    long long* last_dts = nullptr;
    if (pkt->stream_index == src_video_index_) {
        pkt->stream_index = video_index_;
        if (begin_video_pts_ < 0) {
            begin_video_pts_ = pkt->pts;
        }
        lock.unlock();
        RescaleTime(pkt, begin_video_pts_, src_video_time_base_);
        lock.lock();
        last_dts = &last_video_dts_;
    } else if (pkt->stream_index == src_audio_index_) {
        pkt->stream_index = audio_index_;
        if (begin_audio_pts_ < 0) {
            begin_audio_pts_ = pkt->pts;
        }
        lock.unlock();
        RescaleTime(pkt, begin_audio_pts_, src_audio_time_base_);
        lock.lock();
        last_dts = &last_audio_dts_;
    } else {
        return false;  // 源里其它不关心的流（比如源端还有别的杂项轨道）
    }

    // RTSP 源的包时间戳偶尔会有轻微抖动/乱序，直接写会被 mp4 muxer 当成
    // "非单调递增"拒绝——顶到"上一次 + 1"兜底，牺牲一点点时间戳精度换取
    // 不丢帧（不然每次撞上都是整个包被拒绝、静默丢失）。
    if (pkt->dts <= *last_dts) {
        pkt->dts = *last_dts + 1;
        if (pkt->pts < pkt->dts) {
            pkt->pts = pkt->dts;
        }
    }
    *last_dts = pkt->dts;

    auto re = av_interleaved_write_frame(c_, pkt);
    BERR(re);
    return true;
}

bool XMux::WriteEnd() {
    unique_lock<mutex> lock(mux_);
    if (!c_) {
        return false;
    }
    av_interleaved_write_frame(c_, nullptr);  // 写入内部缓冲里剩下的包
    auto re = av_write_trailer(c_);
    BERR(re);
    return true;
}

#undef BERR
