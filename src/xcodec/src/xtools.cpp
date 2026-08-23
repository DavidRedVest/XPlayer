#include "xtools.h"

using namespace std;

extern "C" {
#include <libavcodec/avcodec.h>
}

void PrintErr(int err) {
    char buf[1024] = {0};
    av_strerror(err, buf, sizeof(buf) - 1);
    cerr << buf << endl;
}

void XFreeFrame(AVFrame** frame) {
    if (!frame || !(*frame)) {
        return;
    }
    av_frame_free(frame);
}

long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
        .count();
}

void MyDelay(int timeout_ms) {
    if (timeout_ms <= 0) {
        std::this_thread::yield();
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
}

long long XRescale(long long pts, AVRational* src_time_base,
                    AVRational* des_time_base) {
    return av_rescale_q(pts, *src_time_base, *des_time_base);
}

void XThread::Start() {
    unique_lock<mutex> lock(m_);
    static int i = 0;
    ++i;
    index_ = i;

    is_exit_ = false;
    th_ = thread(&XThread::Main, this);
    LOGINFO("XThread::Start() " << index_);
}

void XThread::Wait() {
    if (th_.joinable()) {
        th_.join();
    }
    LOGINFO("XThread::Wait() end " << index_);
}

void XThread::Exit() {
    LOGINFO("XThread::Exit() " << index_);
    is_exit_ = true;
}

void XThread::Stop() {
    Exit();
    Wait();
}

XPara* XPara::Create() { return new XPara(); }

XPara::XPara() {
    para = avcodec_parameters_alloc();
    time_base = new AVRational();
}

XPara::~XPara() {
    if (para) {
        avcodec_parameters_free(&para);
    }
    delete time_base;
    time_base = nullptr;
}

AVPacket* XAVPacketList::Pop() {
    unique_lock<mutex> lock(mux_);
    if (pkts_.empty()) {
        return nullptr;
    }
    auto pkt = pkts_.front();
    pkts_.pop_front();
    return pkt;
}

int XAVPacketList::Size() {
    unique_lock<mutex> lock(mux_);
    return static_cast<int>(pkts_.size());
}

void XAVPacketList::Clear() {
    unique_lock<mutex> lock(mux_);
    while (!pkts_.empty()) {
        av_packet_free(&pkts_.front());
        pkts_.pop_front();
    }
}

void XAVPacketList::Push(AVPacket* pkt) {
    unique_lock<mutex> lock(mux_);
    auto p = av_packet_alloc();
    av_packet_ref(p, pkt);
    pkts_.push_back(p);

    if (static_cast<int>(pkts_.size()) <= max_packets_) {
        return;
    }

    // 队列超限：从队首开始丢包，直到丢到（且不含）下一个关键帧为止，
    // 这样保留的数据始终以关键帧开头、可独立解码。
    // 注意：player_v1 里这段逻辑有 bug——内层判断条件和外层完全一样，
    // 导致队首不是关键帧时永远不淘汰、队列无限增长，这里是修好的版本。
    while (!pkts_.empty() && !(pkts_.front()->flags & AV_PKT_FLAG_KEY)) {
        av_packet_free(&pkts_.front());
        pkts_.pop_front();
    }
}
