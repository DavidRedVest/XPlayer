#include "xdemux.h"

#include "xtools.h"

extern "C" {
#include <libavformat/avformat.h>
}

using namespace std;

#define BERR(err)         \
    if (0 != (err)) {     \
        PrintErr(err);    \
        return 0;         \
    }

// EOF is an expected outcome for finite sources (local files), not a real
// error — don't spam PrintErr for it the way BERR does.
#define BERR_QUIET_EOF(err)              \
    if (0 != (err)) {                    \
        if ((err) != AVERROR_EOF) {      \
            PrintErr(err);               \
        }                                \
        return 0;                        \
    }

bool XDemux::Open(const char* url) {
    // 先分配、装好中断回调，再发起连接——不然只有连接*成功*之后 set_c()
    // 才会挂上回调，期间万一连接本身卡住（弱网/重连时摄像头没反应），
    // 这个线程就没有任何办法被打断，Stop() 也会跟着永远卡住。
    auto c = AllocWithInterruptCB();

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "stimeout", "5000000", 0);  // 协议层 socket 超时 5 秒，配合中断回调兜底
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);

    auto re = avformat_open_input(&c, url, nullptr, &opts);
    if (opts) {
        av_dict_free(&opts);
    }
    if (0 != re) {
        PrintErr(re);
        avformat_close_input(&c);
        return false;
    }

    re = avformat_find_stream_info(c, nullptr);
    if (0 != re) {
        PrintErr(re);
        avformat_close_input(&c);
        return false;
    }

    set_c(c);
    return true;
}

bool XDemux::Read(AVPacket* pkt) {
    unique_lock<mutex> lock(mux_);
    if (!c_) {
        return false;
    }
    auto re = av_read_frame(c_, pkt);
    BERR_QUIET_EOF(re);
    last_time_ = NowMs();
    return true;
}

bool XDemux::Seek(long long pts, int stream_index) {
    unique_lock<mutex> lock(mux_);
    if (!c_) {
        return false;
    }
    // 只用 AVSEEK_FLAG_BACKWARD：按 pts（stream_index 那一路的时间基）seek
    // 到目标之前最近的关键帧。不能带 AVSEEK_FLAG_FRAME——那个标志会把
    // timestamp 参数的含义从"时间戳"改成"第几帧"，我们这里传的明明是按
    // 时间基换算过的 pts，一旦被当成帧号解释，数值上会差出几个数量级，
    // 实际效果是直接冲到文件末尾（这里是回放 seek 用到的第一个真实调用者，
    // 之前这个函数虽然写了但从没被真正用过，这个 bug 一直没暴露出来）。
    auto re = av_seek_frame(c_, stream_index, pts, AVSEEK_FLAG_BACKWARD);
    BERR(re);
    return true;
}

#undef BERR
