#include "xcodec/xplayback.h"

#include <algorithm>
#include <atomic>
#include <string>

#include "xaudio_play.h"
#include "xaudio_tempo.h"
#include "xdecode_task.h"
#include "xdemux.h"
#include "xtools.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

namespace {

void DeliverFrame(AVFrame* frame, const AVRational& time_base, IXVideoSink* sink) {
    if (!sink || !frame) {
        return;
    }
    if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) {
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
    auto pts = frame->best_effort_timestamp;
    vf.pts_ms = (pts == AV_NOPTS_VALUE) ? -1 : av_rescale_q(pts, time_base, AVRational{1, 1000});
    sink->OnVideoFrame(vf);
}

// 本地文件回放的整个生命周期（打开、按 pts 节流转发、暂停、seek、单帧
// 步进）都在这一个 Main() 里，跑在 XThread::Start() 给的独立线程上——
// Start() 因此天然是异步的（跟 XLiveStream::OpenWorker / XRecorder 的
// RecordWorker 是同一个道理，不在调用者线程上做阻塞的打开操作）。
//
// 不复用 XDemuxTask：那条循环是"能读多快读多快"，没有暂停/倍速/seek 的
// 位置，直播场景也刻意没做（xdemux_task.h 里写明了）。这里直接持有
// XDemux 自己控制读取节奏。
class PlaybackWorker : public XThread {
public:
    void set_path(std::string path) { path_ = std::move(path); }
    void set_sink(IXVideoSink* sink) { sink_ = sink; }

    // 恢复播放必须把节流基准也重新打桩到"此刻"，跟 SetSpeed() 是同一个
    // 道理：暂停期间墙钟一直在走，但 pts_base_ms_/wall_base_ms_ 这对基准
    // 是暂停前定的，如果不刷新，恢复播放那一刻 WaitForPts() 算出来的
    // target_wall 早就落在过去了（暂停了多久，这个"过去"就有多久）——
    // 表现出来就是一恢复播放，画面会先不管三七二十一地飞速闪过一大段，
    // 直到追上"暂停时长"那笔账才恢复正常节奏，暂停得越久这段越明显。
    void Play() {
        long long pos = position_ms_.load(std::memory_order_relaxed);
        wall_base_ms_ = NowMs();
        pts_base_ms_ = pos < 0 ? 0 : pos;
        paused_ = false;
    }
    // 光把 paused_ 置位只能挡住"以后"要送去解码的包，已经喂给 XAudioPlay、
    // 排在它自己缓冲队列里的那几百毫秒 PCM 还是会被 SDL 回调照常播完——
    // 界面上就是画面停了、声音还在响一小段。一起清掉那个缓冲，暂停才是
    // 真正的暂停。seek/单帧步进同理：不清的话会先冒出一截"跳转前位置"
    // 的残留声音，跟新画面的位置对不上。
    void Pause() {
        paused_ = true;
        XAudioPlay::Instance()->Clear();
    }
    bool is_playing() const { return !paused_.load(std::memory_order_relaxed); }

    void SetAudioMuted(bool muted) { muted_ = muted; }

    void SeekTo(long long ms) {
        seek_target_ms_ = ms < 0 ? 0 : ms;
        XAudioPlay::Instance()->Clear();
    }
    void StepForward() {
        paused_ = true;
        step_forward_pending_ = true;
        XAudioPlay::Instance()->Clear();
    }
    void StepBackward() {
        paused_ = true;
        step_backward_pending_ = true;
        XAudioPlay::Instance()->Clear();
    }
    // 换倍速必须把"pts 相对墙钟"的节流基准也重新打一次桩，不然
    // WaitForPts() 算 target_wall 用的还是换速之前那个基准——比如在 2x 放了
    // 5 秒真实时间（这期间内容播放了 10 秒），再切回 1x：不重置的话，下一帧
    // 的 target_wall 是"从最初那个基准点，按 1x 换算到当前内容位置"算出来
    // 的，等于要求墙钟也追上那已经用 2x"提前"跑掉的 5 秒内容——表现出来
    // 就是切回 1x 之后画面卡住很久才继续走。换速这一刻直接把当前位置定成
    // 新基准（等效于此刻发生了一次"隐式 seek 到当前位置"，只是不真的
    // seek），旧倍速的历史不再影响之后的节流计算。
    void SetSpeed(double speed) {
        speed = speed > 0 ? speed : 1.0;
        long long pos = position_ms_.load(std::memory_order_relaxed);
        wall_base_ms_ = NowMs();
        pts_base_ms_ = pos < 0 ? 0 : pos;
        speed_ = speed;
    }
    double speed() const { return speed_.load(std::memory_order_relaxed); }

    // 正常播放时 Main() 每一帧都会经过 Dispatch()->XDecodeTask::Do()，
    // block_size_ 背压超限就在里面死等——那个等待只认 video_decode_/
    // audio_decode_ 自己的 is_exit_，不认 PlaybackWorker 自己的。如果直接
    // 调基类 XThread::Stop()（只置 PlaybackWorker 自己的 is_exit_），恰好
    // 在关闭的瞬间解码/渲染回调比分发慢一点、卡在这个背压等待里，Main()
    // 线程永远走不到外层 while 判断，调用方（GUI 线程，回放弹窗析构）在
    // Wait()/join() 上永远卡死——界面表现就是整个软件失去响应、连关闭
    // 按钮的红绿灯都变灰。这里先主动把两个解码任务自己的 is_exit_
    // 也置上，让背压等待立刻放行，Main() 才能真正跑到外层循环退出。
    void Stop() override {
        video_decode_.Exit();
        if (has_audio_) {
            audio_decode_.Exit();
        }
        XThread::Stop();
    }

    bool is_open() const { return is_open_.load(std::memory_order_relaxed); }
    long long duration_ms() const { return duration_ms_.load(std::memory_order_relaxed); }
    long long position_ms() const {
        long long p = position_ms_.load(std::memory_order_relaxed);
        return p < 0 ? 0 : p;
    }

private:
    void Main() override {
        if (!demux_.Open(path_.c_str())) {
            LOGERROR("XPlayback: demux.Open failed for " << path_);
            return;
        }
        auto vp = demux_.CopyVideoPara();
        if (!vp) {
            LOGERROR("XPlayback: no video stream found in " << path_);
            demux_.set_c(nullptr);
            return;
        }
        // thread_count=1：回放靠 seek/单帧步进不停地"发一个包、马上收一帧"，
        // 假设解码器按输入顺序同步吐输出——FFmpeg 默认给多线程 h264 解码
        // 开的是帧级流水线（内部跨帧缓冲、输出滞后于输入），一旦两次单帧
        // 步进之间有真实的间隔（用户点得不是绝对连续，很常见），就会跟这
        // 个内部流水线的假设对不上，解码器报 "reference picture missing
        // during reorder"，画面花屏（实测复现：连续快速点"前进一帧"
        // 十几到几十次后必现）。单线程解码没有这条内部流水线，1080p 这种
        // 规模单线程软解码也早就够快，不构成性能问题。
        if (!video_decode_.Open(vp->para, /*thread_count=*/1)) {
            LOGERROR("XPlayback: video decode.Open failed for " << path_);
            demux_.set_c(nullptr);
            return;
        }
        video_decode_.set_stream_index(demux_.video_index());
        // seek/单帧步进的"追帧"循环是不按 pts 节流、能塞多快塞多快地往
        // 解码任务的包队列里推——如果不限制队列上限，解码跟不上分发的速度，
        // 队列会攒下一大截积压：即使外层循环一看 position_ms_ 已经到了目标
        // 就提前退出，那截积压还是会在后台继续被解码、继续推进
        // position_ms_，表现出来就是 seek 完了画面还在自己往后跑（甚至跑
        // 过头、或者"后退"了一半又冒出一堆更靠后的画面）。给一个较小的
        // 队列上限，Do() 超过上限会阻塞分发方，天然把追帧速度摁到跟解码
        // 速度差不多，不会出现这种"分发和解码各跑各的"的滞后。
        video_decode_.set_block_size(3);
        video_time_base_ = *vp->time_base;
        // 用容器级别的时长，不用视频流自己的 duration 字段——实测过对录像
        // 文件不可靠（见 XFormat::DurationMs 的注释）。
        duration_ms_ = demux_.DurationMs();
        video_decode_.set_frame_callback([this](AVFrame* frame) { OnVideoFrame(frame); });

        auto ap = demux_.CopyAudioPara();
        if (ap && audio_decode_.Open(ap->para, /*thread_count=*/1)) {
            audio_decode_.set_stream_index(demux_.audio_index());
            audio_decode_.set_block_size(5);
            audio_decode_.set_frame_callback([this](AVFrame* frame) {
                // muted_ 默认 true：网格里可能同时有好几个 XPlayback 实例
                // 在跑，只有被选中的那一个应该真正出声，其余都要静音，
                // 跟 XLiveStream::SetAudioMuted 是同一个道理。catching_up_
                // 为 true 时也不推：seek/单帧步进的追帧循环是不按 pts 节流、
                // 能塞多快塞多快地解码，如果这期间解出来的音频也照常推给
                // XAudioPlay，会一次性把好几百毫秒甚至几秒的 PCM 堆进播放
                // 缓冲——画面早就停在目标位置了，声音还要按真实时长继续
                // 播完这一大截堆积的内容，听起来就是"声音不对、播放时间
                // 过长、跟画面对不上"。
                if (catching_up_.load(std::memory_order_relaxed) ||
                    muted_.load(std::memory_order_relaxed)) {
                    return;
                }
                double speed = speed_.load(std::memory_order_relaxed);
                if (speed == 1.0) {
                    XAudioPlay::Instance()->Push(frame);
                    return;
                }
                // 非 1x 倍速：喂给 atempo 变速滤镜做"变速不变调"处理（跟
                // VLC/mpv 的 scaletempo 同一算法家族），取代直接静音。滤镜
                // 内部会缓冲，不保证喂一帧就吐一帧，要循环取干净。
                if (audio_tempo_.Push(frame, speed)) {
                    AVFrame* out;
                    while ((out = audio_tempo_.PopFrame()) != nullptr) {
                        XAudioPlay::Instance()->Push(out);
                        av_frame_free(&out);
                    }
                }
            });
            fanout_.set_targets(&video_decode_, &audio_decode_);
            has_audio_ = true;
            audio_decode_.Start();
        }
        video_decode_.Start();
        is_open_ = true;

        wall_base_ms_ = NowMs();
        pts_base_ms_ = 0;

        AVPacket pkt = {};
        while (!is_exit_) {
            long long seek_target = seek_target_ms_.exchange(-1, std::memory_order_relaxed);
            if (seek_target >= 0) {
                PerformSeek(seek_target);
                continue;
            }
            if (step_forward_pending_.exchange(false, std::memory_order_relaxed)) {
                PerformStepForward();
                continue;
            }
            if (step_backward_pending_.exchange(false, std::memory_order_relaxed)) {
                PerformStepBackward();
                continue;
            }
            if (paused_.load(std::memory_order_relaxed)) {
                MyDelay(5);
                continue;
            }
            if (!demux_.Read(&pkt)) {
                paused_ = true;  // 读到文件末尾：停在最后一帧，不要空转刷屏
                av_packet_unref(&pkt);
                continue;
            }
            long long pts_ms = (pkt.pts == AV_NOPTS_VALUE)
                                    ? position_ms_.load(std::memory_order_relaxed)
                                    : demux_.RescaleToMs(pkt.pts, pkt.stream_index);
            if (WaitForPts(pts_ms)) {
                Dispatch(&pkt);
            }
            av_packet_unref(&pkt);
        }

        video_decode_.Stop();
        if (has_audio_) {
            audio_decode_.Stop();
        }
        demux_.set_c(nullptr);
        is_open_ = false;
    }

    void Dispatch(AVPacket* pkt) {
        if (has_audio_) {
            fanout_.Do(pkt);
        } else {
            video_decode_.Do(pkt);
        }
    }

    // 按包的 pts 相对墙钟等到点再转发；如果暂停/seek/单帧步进请求插了
    // 进来就提前放弃（返回 false），这个包直接丢弃不转发——不用等硬凑够
    // 时间，用户的操作应该立刻生效。
    bool WaitForPts(long long pts_ms) {
        double speed = speed_.load(std::memory_order_relaxed);
        long long target_wall = wall_base_ms_ + static_cast<long long>((pts_ms - pts_base_ms_) / speed);
        while (!is_exit_) {
            if (seek_target_ms_.load(std::memory_order_relaxed) >= 0 ||
                step_forward_pending_.load(std::memory_order_relaxed) ||
                step_backward_pending_.load(std::memory_order_relaxed) ||
                paused_.load(std::memory_order_relaxed)) {
                return false;
            }
            long long now = NowMs();
            if (now >= target_wall) {
                return true;
            }
            MyDelay(static_cast<int>(std::min<long long>(5, target_wall - now)));
        }
        return false;
    }

    void OnVideoFrame(AVFrame* frame) {
        auto pts = frame->best_effort_timestamp;
        long long pts_ms = (pts == AV_NOPTS_VALUE)
                                ? position_ms_.load(std::memory_order_relaxed)
                                : av_rescale_q(pts, video_time_base_, AVRational{1, 1000});
        long long prev = position_ms_.exchange(pts_ms, std::memory_order_relaxed);
        // 只在正常播放的小间隔里更新帧间隔估算：seek/单帧步进期间的跳变
        // 会把这个估算带偏（后退一帧就是靠它估算往前 seek 多少）。
        if (prev >= 0 && pts_ms > prev) {
            long long delta = pts_ms - prev;
            if (delta > 0 && delta < 2000) {
                frame_interval_ms_ = delta;
            }
        }
        // PerformStepBackward() 追帧追到/追过原位置的那一帧被明确认定为
        // "不要"（真正采用的是它前面记的 last_good），但解码不可能真的
        // 跳过它——它已经被解出来了。如果照常交给 sink 画出来，画面会先
        // 正确地一路闪到 last_good，最后又被这一帧"追过头"的画面盖回去，
        // 数字（position_ms_/进度条）明明退回去了，画面却停在原地没变——
        // 这里按 render_suppress_from_ 这个阈值把它单独拦下来，不送去渲染
        // （但 position_ms_ 该怎么更新还是怎么更新，只影响画不画）。
        long long suppress_from = render_suppress_from_.load(std::memory_order_relaxed);
        if (suppress_from >= 0 && pts_ms >= suppress_from) {
            return;
        }
        DeliverFrame(frame, video_time_base_, sink_);
    }

    long long ToVideoPts(long long ms) const {
        return av_rescale_q(ms, AVRational{1, 1000}, video_time_base_);
    }

    // seek 到目标位置：落到目标之前最近的关键帧，再飞快解码（不按 pts
    // 节流）追到目标位置为止——这段"追帧"期间画面会快速闪过几帧，比停在
    // 关键帧不动、或者引入额外的丢帧/不显示逻辑要简单，实际观感也是常见
    // 简单播放器 seek 的样子。
    void PerformSeek(long long target_ms) {
        if (target_ms < 0) {
            target_ms = 0;
        }
        catching_up_ = true;
        audio_tempo_.Reset();  // 位置要跳变了，WSOLA 滑动窗口里还没吐出来的旧样本不能留着
        demux_.Seek(ToVideoPts(target_ms), demux_.video_index());
        video_decode_.Flush();
        if (has_audio_) {
            audio_decode_.Flush();
        }

        // 必须先把旧位置清掉：往回 seek 时，seek 前的位置比新目标还大，如果
        // 不清掉，下面的终止条件 "position_ms_ >= target_ms" 会在真正读到
        // 任何一帧新画面之前就被这个陈旧的值满足，循环直接提前退出——这正是
        // 之前"后退/跳转经常没反应"的根因：不是没 seek，是 seek 完了但循环
        // 一个新包都没处理就以为到点了。
        position_ms_ = -1;

        AVPacket pkt = {};
        while (!is_exit_) {
            if (!demux_.Read(&pkt)) {
                break;  // EOF：停在能读到的最后位置
            }
            Dispatch(&pkt);
            av_packet_unref(&pkt);
            long long pos = position_ms_.load(std::memory_order_relaxed);
            if (pos >= 0 && pos >= target_ms) {
                break;
            }
        }
        // 循环跳出时，解码队列里可能还压着几个已经 Dispatch 但还没真正解码
        // 完的包（block_size_ 限流最多允许这么多"在飞"）——如果不在这里再
        // 等一下，它们会在函数返回之后才解码完，通过 OnVideoFrame 把
        // position_ms_ 悄悄改到我们刚刚落定的值之后，界面上看到的位置就会
        // 跟这里算出来的对不上。用 WaitIdle() 而不是 Flush()：只排空队列、
        // 等在飞的最后一次 callback 收尾，不重置解码器的参考帧状态——这里
        // 落定之后大概率是继续正常播放（demux 位置还在 GOP 中间，不是关键
        // 帧），要是像 Flush() 那样清掉参考帧，下一个 P 帧会因为没有参考帧
        // 解不出画面，得等到下一个关键帧才重新出图，表现为画面/位置突然
        // 跳过一大截（seek 场景不受影响：seek 场景的关键帧对齐已经在下面
        // demux_.Seek() 之后的那次 Flush() 里做过了）。
        video_decode_.WaitIdle();
        if (has_audio_) {
            audio_decode_.WaitIdle();
        }

        long long landed = position_ms_.load(std::memory_order_relaxed);
        if (landed < 0) {
            landed = target_ms;
        }
        position_ms_ = landed;
        wall_base_ms_ = NowMs();
        pts_base_ms_ = landed;
        catching_up_ = false;
    }

    // 单帧前进：跟 PerformStepBackward 同一套策略——往回 seek 到当前位置
    // 之前最近的关键帧、flush 掉解码器状态，再从关键帧往后解码，遇到第一个
    // 位置超过当前值的帧就是"下一帧"，停在那。
    //
    // 早先的实现是"不 seek、不 flush，就在当前 demux 位置继续读一个包、
    // 解一帧"——理论上更省事（不用从关键帧重新解码一遍），也确实修好了
    // 当时的另一个 bug（WaitIdle 之前用 Flush 会导致紧跟着的下一帧因为
    // 参考帧被清空而跳到下一个关键帧）。但实测发现：连续快速点"前进一帧"
    // 几十次之后必现花屏，FFmpeg 内部报 "reference picture missing
    // during reorder"——排查过 EAGAIN 导致的丢包、h264 帧级多线程流水线、
    // 解码器内部输出缓冲没排空三个方向都不是根因（分别验证：Send() 全程
    // 没失败过；thread_count 强制设成 1 之后同样复现；WaitIdle 里加上
    // 排空解码器内部剩余输出的循环之后同样复现），说明"长期不 flush、
    // 指望参考帧状态能撑住无限次'解一帧就停下来'的循环"这个假设本身在
    // FFmpeg 的 h264 解码器实现里站不住脚，具体是内部哪个环节的限制没有
    // 继续深挖。改成跟 PerformStepBackward 一样"每次都从关键帧重新开始"，
    // 用已经验证过没有这个问题的策略，代价是每次单帧前进都要多解码一小段
    // （从关键帧到当前位置这一截，画面会快速闪过，观感跟 PerformSeek 的
    // seek 过程一样，是被接受的简单播放器行为），换正确性。
    void PerformStepForward() {
        catching_up_ = true;
        audio_tempo_.Reset();
        long long current = position_ms_.load(std::memory_order_relaxed);
        if (current < 0) {
            current = 0;
        }

        demux_.Seek(ToVideoPts(current), demux_.video_index());
        video_decode_.Flush();
        if (has_audio_) {
            audio_decode_.Flush();
        }

        position_ms_ = -1;

        long long landed = -1;
        AVPacket pkt = {};
        while (!is_exit_) {
            if (!demux_.Read(&pkt)) {
                break;
            }
            Dispatch(&pkt);
            av_packet_unref(&pkt);
            long long pos = position_ms_.load(std::memory_order_relaxed);
            if (pos < 0) {
                continue;  // 这一帧还没解码完，position_ms_ 还没更新，继续等
            }
            if (pos > current) {
                landed = pos;
                break;  // 追到了第一个比当前位置更靠后的帧，就是"下一帧"
            }
        }
        video_decode_.WaitIdle();
        if (has_audio_) {
            audio_decode_.WaitIdle();
        }
        if (landed < 0) {
            landed = current;  // 已经是最后一帧，前进不了，停在原地
        }
        position_ms_ = landed;
        wall_base_ms_ = NowMs();
        pts_base_ms_ = landed;
        catching_up_ = false;
    }

    // 近似的"后退一帧"：往前 seek 大约两帧的时长（帧间隔用最近正常播放
    // 时观察到的间隔估算），再从那个关键帧往后解码，一路记下每一帧的
    // 位置，直到追到/超过原来的位置为止——追上的那一帧不要，停在它前面
    // 记的最后一个位置上。不保证正好是"上一帧"，是本地回放没有帧索引时
    // 的标准妥协。
    void PerformStepBackward() {
        catching_up_ = true;
        audio_tempo_.Reset();
        long long current = position_ms_.load(std::memory_order_relaxed);
        if (current < 0) {
            current = 0;
        }
        long long frame_ms = frame_interval_ms_.load(std::memory_order_relaxed);
        long long target = current - frame_ms * 2;
        if (target < 0) {
            target = 0;
        }

        // 追帧循环解码到的帧只要 pts 达到/超过 current 就不该被画出来（那是
        // "追过头"、明确要丢弃的那一帧，真正落定的是它前面记的
        // last_good）——OnVideoFrame 里靠这个阈值把它单独拦下来，不然数字
        // 上退回去了，画面却因为这一帧照常被渲染而看起来"没动"，见那边的
        // 注释。
        render_suppress_from_ = current;

        demux_.Seek(ToVideoPts(target), demux_.video_index());
        video_decode_.Flush();
        if (has_audio_) {
            audio_decode_.Flush();
        }

        // 跟 PerformSeek 同样的道理：先清掉旧位置，不然第一次比较用的是
        // seek 前的陈旧值，会让下面"有没有解出新帧"的判断从一开始就不准。
        position_ms_ = -1;

        long long last_good = -1;
        AVPacket pkt = {};
        while (!is_exit_) {
            if (!demux_.Read(&pkt)) {
                break;
            }
            Dispatch(&pkt);
            av_packet_unref(&pkt);
            long long pos = position_ms_.load(std::memory_order_relaxed);
            if (pos < 0) {
                continue;  // 这一帧还没解码完，position_ms_ 还没更新，继续等
            }
            if (pos >= current) {
                break;  // 追到/超过原位置了，不要这一帧，停在上面记的 last_good
            }
            last_good = pos;
        }
        if (last_good < 0) {
            last_good = target;
        }

        // 再等一下（WaitIdle，不是 Flush）：追帧循环里 block_size_ 限流下
        // 最多还有几个包排在解码队列里没处理完，不清掉的话它们会在这个
        // 函数返回之后才解码完，靠 OnVideoFrame 把 position_ms_ 悄悄改回
        // 追到/超过的那个值，界面上看到的位置就会跟这里选定的 last_good
        // 对不上（实测出现过：这里算出 last_good=501，没加这一步之前界面
        // 最终却停在了 709，就是队列里剩下的包解码完之后把值又推回去了）。
        // 用 WaitIdle() 而不是 Flush()：这里落定之后大概率紧接着是正常播放
        // 或者再一次单帧前进，demux 位置还停在 GOP 中间不是关键帧，要是把
        // 解码器参考帧状态也清掉，下一个 P 帧解不出画面，会一直跳到下一个
        // 关键帧才重新出图（这个坑实测踩过：加完 Flush 版本后单帧前进直接
        // 从 11220 跳到了 14264，跳过了大半个 GOP）。
        video_decode_.WaitIdle();
        if (has_audio_) {
            audio_decode_.WaitIdle();
        }

        position_ms_ = last_good;
        wall_base_ms_ = NowMs();
        pts_base_ms_ = last_good;
        catching_up_ = false;
        render_suppress_from_ = -1;
    }

    std::string path_;
    IXVideoSink* sink_ = nullptr;

    XDemux demux_;
    XDecodeTask video_decode_;
    XDecodeTask audio_decode_;
    PacketFanOut fanout_;
    XAudioTempo audio_tempo_;  // 非 1x 倍速时的"变速不变调"处理，只在 Main() 自己的线程上用
    bool has_audio_ = false;
    AVRational video_time_base_{1, 1000};

    std::atomic<bool> is_open_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> muted_{true};
    // seek/单帧步进的追帧循环（含 WaitIdle() 排空尾巴）期间为 true，
    // 期间解出来的音频不推给 XAudioPlay（见音频回调里的注释）。
    std::atomic<bool> catching_up_{false};
    // >=0 时，pts_ms 达到/超过这个阈值的视频帧不送去渲染——只有
    // PerformStepBackward() 会用到（丢弃"追过头"的那一帧，见 OnVideoFrame
    // 里的注释），其它操作期间恒为 -1（不生效）。
    std::atomic<long long> render_suppress_from_{-1};
    std::atomic<long long> seek_target_ms_{-1};
    std::atomic<bool> step_forward_pending_{false};
    std::atomic<bool> step_backward_pending_{false};
    std::atomic<double> speed_{1.0};
    std::atomic<long long> duration_ms_{0};
    std::atomic<long long> position_ms_{-1};
    std::atomic<long long> frame_interval_ms_{40};

    // 以前只在 Main() 自己的线程上读写，不需要原子——现在 SetSpeed()
    // 会从 GUI 线程直接写这两个值（换速要重新打节流基准的桩，见 SetSpeed()
    // 的注释），所以必须是原子的，不然是一个真实的跨线程数据竞争。
    std::atomic<long long> wall_base_ms_{0};
    std::atomic<long long> pts_base_ms_{0};
};

}  // namespace

long long XProbeDurationMs(const char* path) {
    if (!path || !*path) {
        return 0;
    }
    XDemux demux;
    if (!demux.Open(path)) {
        return 0;
    }
    long long ms = demux.DurationMs();
    demux.set_c(nullptr);
    return ms;
}

struct XPlayback::Impl {
    PlaybackWorker worker;
};

XPlayback::XPlayback() : impl_(new Impl()) {}

XPlayback::~XPlayback() {
    Close();
    delete impl_;
}

bool XPlayback::Open(const char* path, IXVideoSink* sink) {
    if (!path || !*path || !sink) {
        LOGERROR("XPlayback::Open: path/sink must not be empty");
        return false;
    }
    Close();
    impl_->worker.set_path(path);
    impl_->worker.set_sink(sink);
    impl_->worker.Start();
    return true;
}

void XPlayback::Close() { impl_->worker.Stop(); }

bool XPlayback::IsOpen() const { return impl_->worker.is_open(); }

void XPlayback::Play() { impl_->worker.Play(); }

void XPlayback::Pause() { impl_->worker.Pause(); }

bool XPlayback::IsPlaying() const { return impl_->worker.is_playing(); }

void XPlayback::SetAudioMuted(bool muted) { impl_->worker.SetAudioMuted(muted); }

double XPlayback::Speed() const { return impl_->worker.speed(); }

void XPlayback::SeekTo(long long ms) { impl_->worker.SeekTo(ms); }

void XPlayback::StepForward() { impl_->worker.StepForward(); }

void XPlayback::StepBackward() { impl_->worker.StepBackward(); }

void XPlayback::SetSpeed(double speed) { impl_->worker.SetSpeed(speed); }

long long XPlayback::DurationMs() const { return impl_->worker.duration_ms(); }

long long XPlayback::PositionMs() const { return impl_->worker.position_ms(); }
