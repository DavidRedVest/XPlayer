#include "xcodec/xrecorder.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include "xdemux_task.h"
#include "xmux_task.h"
#include "xtools.h"

extern "C" {
#include <libavformat/avformat.h>
}

namespace {

// 可重入的 localtime，而不是 player_v1 原来用的 localtime：多路摄像头可能
// 同一时刻各自在自己的线程里滚动新文件，用非线程安全的 localtime 会在内部
// 静态缓冲区上打架。POSIX 是 localtime_r（参数顺序 time_t* 在前），MSVC 只有
// localtime_s（参数顺序反过来，tm* 在前），两边都是线程安全的，包一层统一。
void ThreadSafeLocalTime(std::time_t t, std::tm& tm_buf) {
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
}

// 文件名格式固定为 "<时间戳>_<序列号>_<窗口编号>.mp4"：时间戳是这一段
// 文件开始写的时刻，序列号是这次录像（从点"录制"到点"关闭录制"算一次）
// 里的分段计数（从 1 开始），窗口编号是触发这次录像的画面格子编号
// （左到右、上到下）。
std::string NextSegmentPath(const std::string& save_dir, int window_index, int seq) {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_buf{};
    ThreadSafeLocalTime(t, tm_buf);
    std::ostringstream ss;
    ss << save_dir << "/" << std::put_time(&tm_buf, "%Y_%m_%d_%H_%M_%S") << "_" << seq << "_"
       << window_index << ".mp4";
    return ss.str();
}

// 连接 + 按时长滚动切分文件的整个生命周期都在这一个 Main() 里，跑在
// XThread::Start() 给的独立线程上——Start() 因此天然是异步的，不会阻塞
// 调用者（跟 XLiveStream::OpenWorker 是同一个道理：别在调用者线程上做
// 阻塞的网络连接）。
class RecordWorker : public XThread {
public:
    void set_url(std::string url) { url_ = std::move(url); }
    void set_save_dir(std::string dir) { save_dir_ = std::move(dir); }
    void set_window_index(int w) { window_index_ = w; }
    void set_segment_seconds(int s) { segment_seconds_ = s > 0 ? s : 2700; }
    bool is_recording() const { return is_recording_.load(std::memory_order_relaxed); }

    long long elapsed_ms() const {
        long long start = start_time_ms_.load(std::memory_order_relaxed);
        return start < 0 ? 0 : NowMs() - start;
    }

    void Exit() override {
        XThread::Exit();
        // 打断可能还卡在 avformat_open_input 里的连接/重连尝试，不然 Stop()
        // 的 Wait() 得等那次尝试自己超时（最多 5 秒）才能返回。
        demux_.Exit();
    }

private:
    void Main() override {
        // 连不上就按秒重试，直到连上或者被要求退出——对应 player_v1
        // XCameraRecord::Main() 里的自动重连逻辑。
        while (!is_exit_ && !demux_.Open(url_)) {
            MyDelay(1000);
        }
        if (is_exit_) {
            return;
        }

        auto vp = demux_.CopyVideoPara();
        if (!vp) {
            LOGERROR("XRecorder: no video stream found for " << url_);
            demux_.Stop();
            return;
        }
        demux_.Start();

        auto ap = demux_.CopyAudioPara();
        AVCodecParameters* audio_para = ap ? ap->para : nullptr;
        AVRational* audio_tb = ap ? ap->time_base : nullptr;

        int src_video_index = demux_.video_index();
        int src_audio_index = demux_.audio_index();

        int seq = 1;
        XMuxTask mux;
        if (!mux.Open(NextSegmentPath(save_dir_, window_index_, seq).c_str(), vp->para,
                      vp->time_base, audio_para, audio_tb, src_video_index, src_audio_index)) {
            LOGERROR("XRecorder: mux.Open failed for " << save_dir_);
            demux_.Stop();
            return;
        }
        demux_.set_next(&mux);
        mux.Start();
        start_time_ms_ = NowMs();
        is_recording_ = true;

        auto segment_start = NowMs();
        while (!is_exit_) {
            if (NowMs() - segment_start > static_cast<long long>(segment_seconds_) * 1000) {
                segment_start = NowMs();
                mux.Stop();
                if (!mux.Open(NextSegmentPath(save_dir_, window_index_, ++seq).c_str(), vp->para,
                              vp->time_base, audio_para, audio_tb, src_video_index,
                              src_audio_index)) {
                    LOGERROR("XRecorder: mux.Open failed while rolling segment for " << save_dir_);
                    break;
                }
                mux.Start();
            }
            MyDelay(50);
        }

        is_recording_ = false;
        start_time_ms_ = -1;
        mux.Stop();
        demux_.Stop();
    }

    XDemuxTask demux_;
    std::string url_;
    std::string save_dir_;
    int window_index_ = 0;
    int segment_seconds_ = 2700;
    std::atomic<bool> is_recording_{false};
    std::atomic<long long> start_time_ms_{-1};
};

}  // namespace

struct XRecorder::Impl {
    RecordWorker worker;
};

XRecorder::XRecorder() : impl_(new Impl()) {}

XRecorder::~XRecorder() {
    Stop();
    delete impl_;
}

bool XRecorder::Start(const char* url, const char* save_dir, int window_index,
                      int segment_seconds) {
    if (!url || !*url || !save_dir || !*save_dir) {
        LOGERROR("XRecorder::Start: url/save_dir must not be empty");
        return false;
    }
    Stop();
    impl_->worker.set_url(url);
    impl_->worker.set_save_dir(save_dir);
    impl_->worker.set_window_index(window_index);
    impl_->worker.set_segment_seconds(segment_seconds);
    impl_->worker.Start();
    return true;
}

void XRecorder::Stop() { impl_->worker.Stop(); }

bool XRecorder::IsRecording() const { return impl_->worker.is_recording(); }

long long XRecorder::ElapsedMs() const { return impl_->worker.elapsed_ms(); }
