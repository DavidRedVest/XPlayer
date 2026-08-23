#include "xdecode.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

using namespace std;

bool XDecode::Send(const AVPacket* pkt) {
    unique_lock<mutex> lock(mux_);
    if (!c_) {
        return false;
    }
    return avcodec_send_packet(c_, pkt) == 0;
}

bool XDecode::Recv(AVFrame* frame) {
    unique_lock<mutex> lock(mux_);
    if (!c_) {
        return false;
    }
    return avcodec_receive_frame(c_, frame) == 0;
}

std::vector<AVFrame*> XDecode::End() {
    std::vector<AVFrame*> res;
    unique_lock<mutex> lock(mux_);
    if (!c_) {
        return res;
    }
    int ret = avcodec_send_packet(c_, nullptr);
    while (ret >= 0) {
        auto frame = av_frame_alloc();
        ret = avcodec_receive_frame(c_, frame);
        if (ret < 0) {
            av_frame_free(&frame);
            break;
        }
        res.push_back(frame);
    }
    return res;
}
