#include "xdemux_task.h"

extern "C" {
#include <libavformat/avformat.h>
}

using namespace std;

void XDemuxTask::Exit() {
    XThread::Exit();
    // 打断当前可能卡在 avformat_open_input/av_read_frame 里的调用——不这样
    // 做的话，Stop() 里的 Wait()/join() 得等到那次调用自己超时才能返回，
    // 万一那次调用本身没有配置超时保护（旧 bug）就会永远卡住。
    demux_.RequestAbort();
}

void XDemuxTask::Stop() {
    XThread::Stop();
    demux_.set_c(nullptr);
}

bool XDemuxTask::Open(std::string url, int timeout_ms) {
    LOGDEBUG("XDemuxTask::Open begin");
    demux_.set_c(nullptr);
    url_ = url;
    timeout_ms_ = timeout_ms;
    demux_.set_time_out_ms(timeout_ms);
    if (!demux_.Open(url.c_str())) {
        return false;
    }
    LOGDEBUG("XDemuxTask::Open end");
    return true;
}

void XDemuxTask::Main() {
    // 显式零初始化：av_read_frame() 的实现会直接用读到的数据覆盖 pkt 里的
    // 字段而不是先 unref 旧内容，如果 pkt 是未初始化的栈内存，第一次调用时
    // 读到的就是垃圾值。循环体末尾的 av_packet_unref 只保证"下一轮"是干净的。
    AVPacket pkt = {};
    while (!is_exit_) {
        if (!demux_.Read(&pkt)) {
            if (!demux_.is_connected()) {
                Open(url_, timeout_ms_);
            }
            MyDelay(1);
            continue;
        }

        Next(&pkt);
        av_packet_unref(&pkt);
    }
}
