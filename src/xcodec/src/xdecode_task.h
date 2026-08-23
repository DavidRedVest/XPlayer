#ifndef XDECODE_TASK_H_
#define XDECODE_TASK_H_

#include <atomic>
#include <functional>

#include "xdecode.h"
#include "xtools.h"

struct AVCodecParameters;

// 解码线程：从上游（demux）收包入队，Main() 里串行送去解码，解出一帧就在
// 解码线程上同步调用 on_frame_ 回调（frame 只在回调期间有效，回调内部
// 只做快速拷贝，不能阻塞——耗时的渲染/GPU 上传交给回调实现方自己的线程）。
class XDecodeTask : public XThread {
public:
    // thread_count 默认 4（跟直播场景一致，多线程解码换吞吐量）。本地文件
    // 回放场景（XPlayback）显式传 1——那边的调用模式是"发一个包、马上收
    // 一帧"的严格同步节奏（seek/单帧步进靠 position_ms_ 逐包判断是否已经
    // 追到目标位置，backpressure 用 busy_ 标记一次收发的起止），这个节奏
    // 假设的是解码器按输入顺序同步吐输出；FFmpeg 默认给 thread_count>1 的
    // h264 打开的是帧级多线程（内部有跨帧的流水线，输出会滞后、乱序），
    // 一旦回放操作在两次单帧步进之间有真实的间隔停顿（用户点得不是绝对
    // 连续），跟这种内部流水线的假设对不上，解码器会报
    // "reference picture missing during reorder"，画面花屏——单线程解码
    // 没有这个内部流水线，跟我们这里简单的同步收发节奏完全匹配，1080p
    // 这种规模的软解码单线程也早就够快，不构成实际的性能问题。
    bool Open(AVCodecParameters* para, int thread_count = 4);

    void Do(AVPacket* pkt) override;
    void Main() override;

    void set_stream_index(int i) { stream_index_ = i; }
    bool is_open() const { return is_open_; }

    // 队列上限，超过时 Do() 会阻塞上游，做背压；<=0 表示不限流。
    void set_block_size(int s) { block_size_ = s; }

    void set_frame_callback(std::function<void(AVFrame*)> cb) {
        on_frame_ = std::move(cb);
    }

    void Stop() override;

    // seek 之后调用：扔掉队列里 seek 前排的旧包，并清掉解码器内部还留着的
    // 参考帧状态（不然继续喂 seek 之后的包会花屏/花音）。
    void Flush();

    // 只排空队列、等在飞的那一次 Recv+回调结束，不重置解码器内部的参考帧
    // 状态——用在"追帧循环已经跑到目标位置，后面还想接着正常往下解码"的
    // 场景（单帧步进的追帧循环之后紧接着可能就是继续播放/再单帧前进，
    // demux 读到的位置还停在 GOP 中间，不是关键帧；这时如果调用 Flush()
    // 把解码器参考帧状态清掉，下一个 P 帧因为没有参考帧会解不出画面，要
    // 等到下一个关键帧才会重新出图，表现为画面/位置突然跳过一大截）。
    // 只用来防止追帧循环里还没解码完的积压包在函数返回之后才解码、把
    // position 悄悄改回去。
    void WaitIdle();

private:
    int block_size_ = 0;
    bool is_open_ = false;
    int stream_index_ = 0;

    std::mutex mux_;
    XDecode decode_;
    XAVPacketList pkt_list_;
    // 覆盖"从队列里弹出一个包"到"这个包的 Recv+回调彻底跑完"的整个区间。
    // WaitIdle() 只清队列不够：可能刚好有一个包已经被 Main() 弹出、还没处理
    // 完，队列里查不到它了，但它的回调还是会在稍后触发——必须再等这个标记
    // 变回 false 才能保证真的没有回调会在 WaitIdle() 返回之后才冒出来。
    std::atomic<bool> busy_{false};

    AVFrame* frame_ = nullptr;
    std::function<void(AVFrame*)> on_frame_;
};

#endif  // XDECODE_TASK_H_
