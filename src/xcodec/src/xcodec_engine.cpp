#include "xcodec_engine.h"

#include <iostream>

#include "xtools.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

using namespace std;

AVCodecContext* XCodec::Create(int codec_id, bool is_encode, int thread_count) {
    const AVCodec* codec = is_encode
                                ? avcodec_find_encoder(static_cast<AVCodecID>(codec_id))
                                : avcodec_find_decoder(static_cast<AVCodecID>(codec_id));
    if (!codec) {
        cerr << "avcodec_find_" << (is_encode ? "encoder" : "decoder")
             << " failed: " << codec_id << endl;
        return nullptr;
    }

    auto c = avcodec_alloc_context3(codec);
    if (!c) {
        cerr << "avcodec_alloc_context3 failed: " << codec_id << endl;
        return nullptr;
    }

    c->time_base = {1, 25};
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->thread_count = thread_count;

    return c;
}

void XCodec::set_c(AVCodecContext* c) {
    unique_lock<mutex> lock(mux_);
    if (c_) {
        avcodec_free_context(&c_);
    }
    c_ = c;
}

bool XCodec::SetOpt(const char* key, const char* val) {
    unique_lock<mutex> lock(mux_);
    if (!c_) return false;
    auto re = av_opt_set(c_->priv_data, key, val, 0);
    if (re != 0) {
        PrintErr(re);
    }
    return re == 0;
}

bool XCodec::SetOpt(const char* key, int val) {
    unique_lock<mutex> lock(mux_);
    if (!c_) return false;
    auto re = av_opt_set_int(c_->priv_data, key, val, 0);
    if (re != 0) {
        PrintErr(re);
    }
    return re == 0;
}

bool XCodec::Open() {
    unique_lock<mutex> lock(mux_);
    if (!c_) return false;
    auto ret = avcodec_open2(c_, nullptr, nullptr);
    if (0 != ret) {
        PrintErr(ret);
        return false;
    }
    return true;
}

AVFrame* XCodec::CreateFrame() {
    unique_lock<mutex> lock(mux_);
    if (!c_) return nullptr;

    auto frame = av_frame_alloc();
    frame->width = c_->width;
    frame->height = c_->height;
    frame->format = c_->pix_fmt;
    auto ret = av_frame_get_buffer(frame, 0);
    if (0 != ret) {
        av_frame_free(&frame);
        PrintErr(ret);
        return nullptr;
    }
    return frame;
}

void XCodec::Clear() {
    unique_lock<mutex> lock(mux_);
    if (!c_) return;
    avcodec_flush_buffers(c_);
}
