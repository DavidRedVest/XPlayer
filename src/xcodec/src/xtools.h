#ifndef XTOOLS_H_
#define XTOOLS_H_

#include <chrono>
#include <iostream>
#include <list>
#include <mutex>
#include <thread>

struct AVPacket;
struct AVCodecParameters;
struct AVRational;
struct AVFrame;
struct AVCodecContext;

void PrintErr(int err);

enum XLogLevel {
    XLOG_TYPE_DEBUG = 0,
    XLOG_TYPE_INFO,
    XLOG_TYPE_ERROR,
    XLOG_TYPE_FATAL
};
#define LOG_MIN_LEVEL (XLOG_TYPE_INFO)

#define XLOG(s, level)                                                     \
    if (level >= LOG_MIN_LEVEL)                                            \
    std::cout << level << ":" << __FILE__ << ":" << __LINE__ << ": " << s  \
              << std::endl;

#define LOGDEBUG(s) XLOG(s, XLOG_TYPE_DEBUG)
#define LOGINFO(s) XLOG(s, XLOG_TYPE_INFO)
#define LOGERROR(s) XLOG(s, XLOG_TYPE_ERROR)
#define LOGFATAL(s) XLOG(s, XLOG_TYPE_FATAL)

// 单调时钟毫秒数，不用 clock()：clock() 在部分平台上是 32 位 CPU 时间计数器，
// 长时间跑（NVR 场景要求 7x24 小时）会溢出/不准。
long long NowMs();

// 真正睡眠而不是自旋忙等。
void MyDelay(int timeout_ms);

void XFreeFrame(AVFrame** frame);

long long XRescale(long long pts, AVRational* src_time_base,
                    AVRational* des_time_base);

// 线程基类：Main() 是线程入口，通过 set_next()/Next() 组成处理链
// （demux -> decode -> ...），每一级把数据 Do() 给下一级。
class XThread {
public:
    virtual ~XThread() = default;

    virtual void Start();

    // 只置退出标志，不等待。
    virtual void Exit();

    // 置退出标志并等待线程真正退出（Exit() + Wait()）。
    virtual void Stop();

    virtual void Wait();

    virtual void Do(AVPacket* pkt) {}

    virtual void Next(AVPacket* pkt) {
        std::unique_lock<std::mutex> lock(m_);
        if (next_) {
            next_->Do(pkt);
        }
    }

    void set_next(XThread* xt) {
        std::unique_lock<std::mutex> lock(m_);
        next_ = xt;
    }

    virtual void Pause(bool is_pause) { is_pause_ = is_pause; }
    bool is_pause() const { return is_pause_; }

protected:
    virtual void Main() = 0;

    bool is_exit_ = false;
    bool is_pause_ = false;
    int index_ = 0;

private:
    std::thread th_;
    std::mutex m_;
    XThread* next_ = nullptr;
};

// 单路流的音视频参数快照。
class XPara {
public:
    AVCodecParameters* para = nullptr;
    AVRational* time_base = nullptr;
    long long total_ms = 0;

    static XPara* Create();
    ~XPara();

private:
    XPara();
};

// 线程安全的 AVPacket 队列，内部持有独立引用（Push 时 av_packet_ref）。
class XAVPacketList {
public:
    AVPacket* Pop();
    void Push(AVPacket* pkt);
    int Size();
    void Clear();

private:
    std::list<AVPacket*> pkts_;
    int max_packets_ = 1000;
    std::mutex mux_;
};

// demux 的 Next() 只能挂一个下游，同一路流要同时喂给音频、视频两个解码
// 任务时用这个小转发器分发（对应 player_v1 里 XPlayer::Do() 手动转发的
// 写法）。XLiveStream（直播）、XPlayback（回放）都要用，抽成共享的。
class PacketFanOut : public XThread {
public:
    void set_targets(XThread* a, XThread* b) {
        a_ = a;
        b_ = b;
    }
    void Do(AVPacket* pkt) override {
        if (a_) a_->Do(pkt);
        if (b_) b_->Do(pkt);
    }

private:
    void Main() override {}
    XThread* a_ = nullptr;
    XThread* b_ = nullptr;
};

#endif  // XTOOLS_H_
