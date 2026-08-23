#include "xdecode_task.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

using namespace std;

bool XDecodeTask::Open(AVCodecParameters* para, int thread_count) {
    if (!para) {
        LOGERROR("XDecodeTask::Open: para is null");
        return false;
    }
    unique_lock<mutex> lock(mux_);
    is_open_ = false;
    auto c = decode_.Create(static_cast<int>(para->codec_id), false, thread_count);
    if (!c) {
        LOGERROR("XDecodeTask::Open: decode_.Create failed");
        return false;
    }
    avcodec_parameters_to_context(c, para);
    decode_.set_c(c);
    if (!decode_.Open()) {
        LOGERROR("XDecodeTask::Open: decode_.Open failed");
        return false;
    }
    is_open_ = true;
    return true;
}

void XDecodeTask::Do(AVPacket* pkt) {
    if (!pkt || pkt->stream_index != stream_index_) {
        return;
    }
    pkt_list_.Push(pkt);

    if (block_size_ <= 0) {
        return;
    }
    while (!is_exit_ && pkt_list_.Size() > block_size_) {
        MyDelay(1);
    }
}

void XDecodeTask::Main() {
    {
        unique_lock<mutex> lock(mux_);
        if (!frame_) {
            frame_ = av_frame_alloc();
        }
    }

    while (!is_exit_) {
        auto pkt = pkt_list_.Pop();
        if (!pkt) {
            MyDelay(1);
            continue;
        }
        busy_ = true;

        // avcodec_send_packet 失败最常见的原因是 EAGAIN：不是真的出错，是
        // 解码器内部还攒着没被 Recv() 取走的输出帧、缓冲堆满了，必须先腾
        // 地方才肯收下一个包。以前这里一旦 Send() 失败就直接丢弃这个包——
        // 正常按 pts 节流播放时，包之间有足够的间隔让每次 Send()+Recv() 都
        // 能配平，很少撞上这个情况；但 seek/单帧步进的追帧循环、或者用户
        // 连续快速点单帧步进按钮，是不按节流、能塞多快塞多快地喂包，一旦
        // 解码器内部积压触发 EAGAIN，被丢掉的这个包如果恰好是后面某个 P
        // 帧要用的参考帧，画面就会花屏——这里改成失败时先 Recv() 腾出一帧
        // 空间再重试发送同一个包（顺序不能变，不能跳到下一个包），而不是
        // 直接放弃。
        bool sent = false;
        for (int attempt = 0; attempt < 8 && !is_exit_; ++attempt) {
            sent = decode_.Send(pkt);
            if (sent) {
                break;
            }
            unique_lock<mutex> lock(mux_);
            if (decode_.Recv(frame_) && frame_->buf[0]) {
                if (on_frame_) {
                    on_frame_(frame_);
                }
            }
        }
        av_packet_free(&pkt);
        if (!sent) {
            busy_ = false;
            MyDelay(1);
            continue;
        }

        {
            unique_lock<mutex> lock(mux_);
            if (decode_.Recv(frame_) && frame_->buf[0]) {
                if (on_frame_) {
                    on_frame_(frame_);
                }
            }
        }
        busy_ = false;
    }

    unique_lock<mutex> lock(mux_);
    if (frame_) {
        av_frame_free(&frame_);
    }
}

void XDecodeTask::Stop() {
    XThread::Stop();
    pkt_list_.Clear();
    unique_lock<mutex> lock(mux_);
    decode_.set_c(nullptr);
    is_open_ = false;
}

void XDecodeTask::Flush() {
    pkt_list_.Clear();
    unique_lock<mutex> lock(mux_);
    decode_.Clear();
}

void XDecodeTask::WaitIdle() {
    // 只清一次队列不够：有可能刚好有个包已经被 Main() 弹出、队列里已经查不到
    // 它了，但它的 Send/Recv/回调还没跑完——必须连 busy_ 一起等，确认"没有
    // 正在处理中的包"之后再返回，不然那个包解码完之后触发的回调会在这个
    // 函数返回、调用方已经认定"追帧结束、可以定下最终位置"之后才姗姗来迟，
    // 把已经定下的位置又悄悄改掉。调用方不再往队列里塞新包（追帧循环已经
    // 结束），所以这里只需要等存量处理完，不用担心队列重新变满。
    pkt_list_.Clear();
    while (busy_.load(std::memory_order_acquire) || pkt_list_.Size() > 0) {
        pkt_list_.Clear();
        MyDelay(1);
    }

    // 光排空外部队列还不够：解码器内部（B 帧重排序缓冲）可能还攒着几帧
    // 已经解出来、只是还没被 Recv() 取走的输出——Main() 里"发一个包、收一
    // 次"的节奏不保证每次 Send() 都能把对应的输出一次性取干净。追帧循环
    // 一结束就不再喂包了，这些攒下的输出没机会在后续的 Send()/Recv() 里
    // 自然被取走，会一直留在解码器内部；如果调用方是单帧步进这种"追一次、
    // 停下来、等下一次点击再追"的模式，每次都会留一点没取走的，次数一多
    // 就在解码器内部越攒越多，最终撑爆参考帧管理，表现为花屏（实测：连续
    // 点几十次"前进一帧"必现，跟点得快慢无关，是纯粹按操作次数累积的，
    // 不是时序竞争——一次性排查过 EAGAIN 丢包、多线程解码流水线两个方向
    // 都不是根因，才定位到这里）。这里主动把解码器内部剩的输出全部取干净
    // （不经过回调、不渲染，用户看到的画面停在追帧循环自己选定的那一帧
    // 就够了，这些多出来的不是"这一步"该显示的内容）。
    unique_lock<mutex> lock(mux_);
    while (decode_.Recv(frame_)) {
    }
}
