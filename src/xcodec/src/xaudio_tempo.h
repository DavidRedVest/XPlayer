#ifndef XAUDIO_TEMPO_H_
#define XAUDIO_TEMPO_H_

#include <mutex>

struct AVFrame;
struct AVFilterGraph;
struct AVFilterContext;

// 回放非 1x 倍速时，把解码出来的音频帧喂给 FFmpeg 自带的 atempo 滤镜
// （WSOLA 算法，跟 VLC/mpv 的 scaletempo 是同一算法家族）做"变速不变调"
// 处理，取代直接静音。内部是一个 abuffer -> atempo[,atempo...] ->
// abuffersink 的小 filter graph；atempo 单级只认 [0.5, 2.0] 的倍速，超出
// 范围时自动拆成多级串联（比如 4.0 拆成两级 2.0 * 2.0）。
//
// 只在 XPlayback（本地回放）里用，倍速播放只存在于回放场景；直播
// （XLiveStream）没有倍速概念，不涉及这个类。
class XAudioTempo {
public:
    XAudioTempo() = default;
    ~XAudioTempo();

    XAudioTempo(const XAudioTempo&) = delete;
    XAudioTempo& operator=(const XAudioTempo&) = delete;

    // 喂一帧解码出来的音频进去（只应该在 speed != 1.0 时调用，1.0 时直接
    // 跳过整个 XAudioTempo 原样播放）。输入的采样格式/采样率/声道数、或者
    // speed 本身，只要跟上一次不一样就会重新建图——重建很轻量（用户手动
    // 切换倍速的频率远低于逐帧调用），不做"运行时改参数不重建"的优化，
    // 换取实现简单可靠。返回 false 表示这一帧没能正常送进滤镜图，调用方
    // 直接丢弃即可，不是致命错误。
    //
    // 这个方法在音频解码任务自己的线程上调用（音频帧回调就是在那条线程上
    // 触发的）；Reset() 则是从 XPlayback 的 Main() 线程调用（seek/单帧
    // 步进逻辑跑在那）——两条线程都会碰这个对象，内部用 mux_ 保护，不然
    // Reset() 拆图的时候 Push()/PopFrame() 可能正好在用同一批指针，是真实
    // 的跨线程竞争。
    bool Push(AVFrame* frame, double speed);

    // 循环调用直到返回 nullptr，取出所有已经处理好的帧——atempo 内部会
    // 缓冲，不保证每喂一帧就吐一帧，也可能一次吐好几帧。调用方拿到返回的
    // 帧之后负责 av_frame_free 释放。
    AVFrame* PopFrame();

    // seek/单帧步进导致播放位置跳变之后调用：WSOLA 的滑动窗口对输入位置
    // 的不连续很敏感，不清空的话新位置的音频会跟窗口里还没吐出来的旧
    // 位置样本混在一起，听感上是一小段错乱的声音。
    void Reset();

private:
    bool EnsureGraph(AVFrame* frame, double speed);  // 调用方必须已经持有 mux_
    void TeardownGraph();                            // 调用方必须已经持有 mux_

    std::mutex mux_;
    AVFilterGraph* graph_ = nullptr;
    AVFilterContext* src_ctx_ = nullptr;
    AVFilterContext* sink_ctx_ = nullptr;

    // 记录当前图是照哪些参数建的，任何一个变了就整个重建。
    int cur_format_ = -1;
    int cur_rate_ = 0;
    int cur_channels_ = 0;
    double cur_speed_ = 1.0;
};

#endif  // XAUDIO_TEMPO_H_
