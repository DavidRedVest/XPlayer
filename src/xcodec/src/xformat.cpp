#include "xformat.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

using namespace std;

int XFormat::InterruptCallback(void* opaque) {
    return static_cast<XFormat*>(opaque)->IsTimeout() ? 1 : 0;
}

AVFormatContext* XFormat::AllocWithInterruptCB() {
    auto c = avformat_alloc_context();
    abort_requested_ = false;
    last_time_ = NowMs();
    if (time_out_ms_ > 0) {
        c->interrupt_callback = AVIOInterruptCB{InterruptCallback, this};
    }
    return c;
}

void XFormat::set_c(AVFormatContext* c) {
    unique_lock<mutex> lock(mux_);
    if (c_) {
        if (c_->oformat) {
            if (c_->pb) {
                avio_closep(&c_->pb);
            }
            avformat_free_context(c_);
        } else if (c_->iformat) {
            avformat_close_input(&c_);
        } else {
            avformat_free_context(c_);
        }
    }
    c_ = c;
    if (!c_) {
        is_connected_ = false;
        return;
    }
    is_connected_ = true;
    last_time_ = NowMs();

    if (time_out_ms_ > 0) {
        c_->interrupt_callback = AVIOInterruptCB{InterruptCallback, this};
    }

    // 优先选第一条匹配的音/视频流：有些容器（比如手机拍的 mp4）会在主视频流
    // 之后再附加一条封面缩略图的单帧视频流，选最后一条会误选到封面。
    audio_index_ = -1;
    video_index_ = -1;
    for (unsigned int i = 0; i < c->nb_streams; ++i) {
        if (video_index_ >= 0 && audio_index_ >= 0) {
            break;
        }
        if (audio_index_ < 0 &&
            AVMEDIA_TYPE_AUDIO == c->streams[i]->codecpar->codec_type) {
            audio_index_ = static_cast<int>(i);
            audio_time_base_.den = c->streams[i]->time_base.den;
            audio_time_base_.num = c->streams[i]->time_base.num;
        } else if (video_index_ < 0 &&
                   AVMEDIA_TYPE_VIDEO == c->streams[i]->codecpar->codec_type) {
            video_index_ = static_cast<int>(i);
            video_time_base_.den = c->streams[i]->time_base.den;
            video_time_base_.num = c->streams[i]->time_base.num;
            video_codec_id_ = static_cast<int>(c->streams[i]->codecpar->codec_id);
        }
    }
}

bool XFormat::CopyPara(int stream_index, AVCodecParameters* dst) {
    unique_lock<mutex> lock(mux_);
    if (!c_ || stream_index < 0 ||
        stream_index >= static_cast<int>(c_->nb_streams)) {
        return false;
    }
    return avcodec_parameters_copy(dst, c_->streams[stream_index]->codecpar) >= 0;
}

bool XFormat::CopyPara(int stream_index, AVCodecContext* dst) {
    unique_lock<mutex> lock(mux_);
    if (!c_ || stream_index < 0 ||
        stream_index >= static_cast<int>(c_->nb_streams)) {
        return false;
    }
    return avcodec_parameters_to_context(dst, c_->streams[stream_index]->codecpar) >= 0;
}

std::shared_ptr<XPara> XFormat::CopyVideoPara() {
    int index = video_index();
    shared_ptr<XPara> re;
    unique_lock<mutex> lock(mux_);
    if (index < 0 || !c_) {
        return re;
    }
    re.reset(XPara::Create());
    *re->time_base = c_->streams[index]->time_base;
    avcodec_parameters_copy(re->para, c_->streams[index]->codecpar);
    if (c_->streams[index]->duration != AV_NOPTS_VALUE) {
        re->total_ms = av_rescale_q(c_->streams[index]->duration,
                                     c_->streams[index]->time_base, {1, 1000});
    }
    return re;
}

std::shared_ptr<XPara> XFormat::CopyAudioPara() {
    int index = audio_index();
    shared_ptr<XPara> re;
    unique_lock<mutex> lock(mux_);
    if (index < 0 || !c_) {
        return re;
    }
    re.reset(XPara::Create());
    *re->time_base = c_->streams[index]->time_base;
    avcodec_parameters_copy(re->para, c_->streams[index]->codecpar);
    if (c_->streams[index]->duration != AV_NOPTS_VALUE) {
        re->total_ms = av_rescale_q(c_->streams[index]->duration,
                                     c_->streams[index]->time_base, {1, 1000});
    }
    return re;
}

bool XFormat::RescaleTime(AVPacket* pkt, long long offset_pts, XRational time_base) {
    AVRational in_time_base;
    in_time_base.num = time_base.num;
    in_time_base.den = time_base.den;
    return RescaleTime(pkt, offset_pts, &in_time_base);
}

bool XFormat::RescaleTime(AVPacket* pkt, long long offset_pts, AVRational* time_base) {
    if (!pkt || !time_base) {
        return false;
    }
    unique_lock<mutex> lock(mux_);
    if (!c_) {
        return false;
    }
    auto out_stream = c_->streams[pkt->stream_index];

    pkt->pts = av_rescale_q_rnd(pkt->pts - offset_pts, *time_base,
                                 out_stream->time_base,
                                 static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    pkt->dts = av_rescale_q_rnd(pkt->dts - offset_pts, *time_base,
                                 out_stream->time_base,
                                 static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    pkt->duration = av_rescale_q(pkt->duration, *time_base, out_stream->time_base);
    pkt->pos = -1;
    return true;
}

long long XFormat::RescaleToMs(long long pts, int index) {
    unique_lock<mutex> lock(mux_);
    if (!c_ || index < 0 || index >= static_cast<int>(c_->nb_streams)) {
        return 0;
    }
    auto in_timebase = c_->streams[index]->time_base;
    AVRational out_timebase = {1, 1000};
    return av_rescale_q(pts, in_timebase, out_timebase);
}

long long XFormat::DurationMs() {
    unique_lock<mutex> lock(mux_);
    if (!c_ || c_->duration == AV_NOPTS_VALUE) {
        return 0;
    }
    // AVFormatContext::duration 单位是 AV_TIME_BASE（微秒，固定 1000000）。
    return c_->duration / 1000;
}

void XFormat::set_time_out_ms(int ms) {
    unique_lock<mutex> lock(mux_);
    time_out_ms_ = ms;
    if (c_) {
        c_->interrupt_callback = AVIOInterruptCB{InterruptCallback, this};
    }
}

bool XFormat::IsTimeout() {
    if (abort_requested_) {
        return true;
    }
    if (NowMs() - last_time_ > time_out_ms_) {
        last_time_ = NowMs();
        is_connected_ = false;
        return true;
    }
    return false;
}
