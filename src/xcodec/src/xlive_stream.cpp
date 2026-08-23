#include "xcodec/xlive_stream.h"

#include <atomic>
#include <string>
#include <thread>

#include "xaudio_play.h"
#include "xdecode_task.h"
#include "xdemux_task.h"
#include "xtools.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

namespace {

void DeliverFrame(AVFrame* frame, const AVRational& time_base, IXVideoSink* sink) {
    if (!sink || !frame) {
        return;
    }
    // MVP 只支持 YUV420P（RTSP H.264/H.265 摄像头的绝大多数场景）。
    if (frame->format != AV_PIX_FMT_YUV420P &&
        frame->format != AV_PIX_FMT_YUVJ420P) {
        return;
    }

    XVideoFrame vf;
    vf.width = frame->width;
    vf.height = frame->height;
    vf.y = frame->data[0];
    vf.y_stride = frame->linesize[0];
    vf.u = frame->data[1];
    vf.u_stride = frame->linesize[1];
    vf.v = frame->data[2];
    vf.v_stride = frame->linesize[2];

    // best_effort_timestamp 是 FFmpeg 推荐的显示时间戳来源（pts 缺失时会
    // 退回 dts 估算），单位是流的 time_base，这里换算成毫秒。
    auto pts = frame->best_effort_timestamp;
    vf.pts_ms = (pts == AV_NOPTS_VALUE)
                    ? -1
                    : av_rescale_q(pts, time_base, AVRational{1, 1000});

    sink->OnVideoFrame(vf);
}

}  // namespace

struct XLiveStream::Impl {
    XDemuxTask demux;
    XDecodeTask video_decode;
    XDecodeTask audio_decode;
    PacketFanOut fanout;
    IXVideoSink* sink = nullptr;
    AVRational video_time_base = {1, 1000};
    bool has_audio = false;
    std::atomic<bool> is_open{false};
    std::atomic<bool> audio_muted{true};
    // 真正的连接（avformat_open_input/avformat_find_stream_info，RTSP 握手+
    // 探流，耗时几百毫秒到几秒）放在这个后台线程里做，Open() 本身立刻返回，
    // 不阻塞调用者——调用者几乎总是 GUI 线程，阻塞在这上面会导致其它已经
    // 在播放的格子跟着卡顿几秒（Qt 主线程被占住，没法处理别的格子的重绘）。
    std::thread open_thread;
};

XLiveStream::XLiveStream() : impl_(new Impl()) {}

XLiveStream::~XLiveStream() {
    Close();
    delete impl_;
}

bool XLiveStream::Open(const char* url, IXVideoSink* sink, bool enable_audio) {
    if (!url || !sink) {
        LOGERROR("XLiveStream::Open: url/sink must not be null");
        return false;
    }
    // 保证之前那路（不管已经连上、还是还在后台连接中）先彻底收干净，再开始
    // 下一次连接——这两个状态重置必须在这里同步做完，不能留给后台线程：
    // 调用方紧接着可能会调 SetAudioMuted() 来设置初始静音状态，如果重置在
    // 后台线程里做，有极小概率晚于调用方的 SetAudioMuted() 执行，把调用方
    // 的设置又覆盖回去。
    Close();
    impl_->sink = sink;
    impl_->has_audio = false;
    impl_->audio_muted = true;  // 每次 Open() 都从静音开始，调用方显式解除

    std::string url_copy(url);
    impl_->open_thread = std::thread([this, url_copy, enable_audio] {
        OpenWorker(url_copy, enable_audio);
    });
    return true;
}

void XLiveStream::OpenWorker(const std::string& url, bool enable_audio) {
    if (!impl_->demux.Open(url)) {
        LOGERROR("XLiveStream::Open: demux.Open failed for " << url);
        return;
    }

    auto vp = impl_->demux.CopyVideoPara();
    if (!vp) {
        LOGERROR("XLiveStream::Open: no video stream found");
        impl_->demux.Stop();
        return;
    }

    // thread_count=1：FFmpeg 默认给多线程 h264 解码开的是帧级流水线（内部
    // 跨帧缓冲、参考帧管理更复杂），一旦输入有任何不规则（网络抖动、丢包
    // 重传、RTSP 连接时机不巧落在 GOP 中间），就可能在参考帧/协同定位信息
    // 还没备齐时触发解码器内部错误（"reference picture missing"/"co
    // located POCs unavailable" 这类）——xplayback.cpp 里回放场景已经复现
    // 并验证过同一根因（连续单帧步进必现），当时改成单线程解决，这里直播
    // 场景补上同样的设置。单线程软解码 1080p~2K 这种规模早就够实时用，不
    // 构成性能问题。
    if (!impl_->video_decode.Open(vp->para, /*thread_count=*/1)) {
        LOGERROR("XLiveStream::Open: video decode.Open failed");
        impl_->demux.Stop();
        return;
    }
    impl_->video_decode.set_stream_index(impl_->demux.video_index());
    // 直播场景不能给 Do() 设背压上限（block_size_）：一旦某个解码任务的
    // 队列超限，demux 线程会阻塞在 Do() 里等队列变短——但同时 demux 线程
    // 又是唯一往音频那边送包的入口，一旦音频跟着断供，音频时钟就不再前进，
    // 而视频解码正好在等音频时钟追上来才能继续，三者转一圈刚好死锁（黑屏
    // 无声但状态还显示"已连接"，就是这样卡住的）。丢帧交给下面
    // XAVPacketList::Push 自己按数量上限淘汰非关键帧，不阻塞生产者。
    impl_->video_time_base = *vp->time_base;

    impl_->video_decode.set_frame_callback([this](AVFrame* frame) {
        DeliverFrame(frame, impl_->video_time_base, impl_->sink);
    });

    // 音频是可选的：没有音轨就退化成纯视频。视频、音频各自按自己的节奏解码
    // /播放，不做跨流的 pts 同步节流——RTSP 两路轨道的 pts 通常各有各的起始
    // 基准（不像本地封装文件那样共享同一条从 0 开始的时间线），拿音频的
    // 原始 pts 换算到视频的 time_base 去比较，数值上可能完全对不上，会把
    // 视频节流卡死在"永远等不到"的状态。直播场景本来也不需要精确对轴，
    // 网络本身就是天然的限速器。
    auto ap = enable_audio ? impl_->demux.CopyAudioPara() : nullptr;
    if (ap && impl_->audio_decode.Open(ap->para)) {
        impl_->audio_decode.set_stream_index(impl_->demux.audio_index());
        impl_->audio_decode.set_frame_callback([this](AVFrame* frame) {
            // 静音只是不推给 XAudioPlay，解码本身照常进行——这样切换哪一路
            // 有声音时只需要翻这个标志位，不用重连整路流（重连会连视频一起
            // 断线重来，造成画面卡顿）。
            if (!impl_->audio_muted.load(std::memory_order_relaxed)) {
                XAudioPlay::Instance()->Push(frame);
            }
        });

        // 不在这里 Open() 播放设备：AVCodecParameters 里预先声明的采样格式
        // 对 RTSP 音频常常不可靠，真正靠谱的格式要等解码出第一帧才知道，
        // XAudioPlay::Push() 内部会在收到第一帧时按帧的真实参数自动配置。
        impl_->has_audio = true;

        impl_->fanout.set_targets(&impl_->video_decode, &impl_->audio_decode);
        impl_->demux.set_next(&impl_->fanout);
        impl_->audio_decode.Start();
    } else {
        impl_->demux.set_next(&impl_->video_decode);
    }

    impl_->video_decode.Start();
    impl_->demux.Start();
    impl_->is_open = true;
}

void XLiveStream::Close() {
    // 先打断、等掉后台连接线程（如果还在跑）：它可能正卡在
    // avformat_open_input/avformat_find_stream_info 里，不主动打断的话
    // join() 要等它自己超时（最多 5 秒）才会返回。demux.Exit() 对从没
    // Start() 过的任务也是安全的空操作（只是设个标志位）。
    impl_->demux.Exit();
    if (impl_->open_thread.joinable()) {
        impl_->open_thread.join();
    }
    if (!impl_->is_open) {
        return;
    }
    // 停止顺序：先切断上游（demux），再停子任务——每一步都调用完整的
    // Stop()（内部 Exit()+Wait() 等线程真正退出再释放资源），不能像
    // player_v1 里 XPlayer::Stop() 那样只做 Exit()+Wait() 跳过资源释放。
    impl_->demux.Stop();
    if (impl_->has_audio) {
        // 只有这一路当前确实没静音（真的在往 XAudioPlay 推数据）才去关设备：
        // 现在网格里每一格都可能带音频解码管线，关掉一格静音的格子不该把
        // 另一格正在出声音的设备也带着关掉。
        bool was_audible = !impl_->audio_muted.load(std::memory_order_relaxed);
        impl_->audio_decode.Stop();
        if (was_audible) {
            XAudioPlay::Instance()->Close();
        }
    }
    impl_->video_decode.Stop();
    impl_->sink = nullptr;
    impl_->has_audio = false;
    impl_->is_open = false;
}

void XLiveStream::SetAudioMuted(bool muted) { impl_->audio_muted.store(muted, std::memory_order_relaxed); }

bool XLiveStream::IsOpen() const { return impl_->is_open; }
