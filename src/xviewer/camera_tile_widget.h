#ifndef XVIEWER_CAMERA_TILE_WIDGET_H_
#define XVIEWER_CAMERA_TILE_WIDGET_H_

#include <atomic>

#include <QString>

#include "x_gl_video_widget.h"
#include "xcodec/xlive_stream.h"
#include "xcodec/xplayback.h"

class QLabel;
class QListWidget;
class QResizeEvent;

// 网格视图里的一格：复用 XGLVideoWidget 的渲染能力，自己额外挂一路
// XLiveStream（直播）或者一路 XPlayback（回放录像文件），二选一，同一时刻
// 只有一路真正在跑（mode_ 记录当前是哪一种，也可能两路都没开——kEmpty）。
// 两路都始终带音频解码管线，但默认静音——XAudioPlay 是进程级单例，多格
// 同时真的出声会互相打断，所以同一时刻只有"选中"的那一格通过
// SetAudioEnabled() 解除静音。静音切换走各自的 SetAudioMuted，不重连/
// 重新打开，视频不受影响。
//
// 交互：
// - 从列表拖一项到格子上 → 发 ItemDropped 信号（带上源列表指针和行号），
//   具体这一行代表"打开哪个摄像头"还是"回放哪个录像文件"由 MainWindow
//   判断（它同时管着摄像头列表和录像文件列表，这一格自己不知道当前是
//   直播模式还是回放模式）。
// - 单击 → 发 Selected 信号，是否给这一格开音频、回放控制条要不要绑定
//   到这一格，都由 MainWindow 决定。
// - 双击 → 发 DoubleClicked 信号，全屏/还原的实际布局操作由 MainWindow
//   处理（一个格子自己不知道整个网格的布局状态）。
// - 右键：不在这里处理，统一交给 MainWindow（它能知道这一格是不是在播放、
//   要不要出现"关闭"，还知道整个网格的分屏状态）。
// - 左上角显示分辨率/帧率，左下角显示录像状态（由 MainWindow 定时推送，
//   这一格自己不知道自己在网格里的窗口编号，录像是不是在录、录了多久都
//   是 RecordingManager 按窗口编号管理的）。
class CameraTileWidget : public XGLVideoWidget {
    Q_OBJECT

public:
    explicit CameraTileWidget(QWidget* parent = nullptr);
    ~CameraTileWidget() override;

    // 直播：打开一路 RTSP/文件 URL（会话式，跟 XLiveStream 一样）。
    bool OpenUrl(const QString& url);
    // 回放：打开一个本地录像文件（XPlayback）。跟 OpenUrl 互斥，打开
    // 任意一路之前都会先把另一路关掉。
    bool OpenPlaybackFile(const QString& path);
    void CloseStream();
    bool IsActive() const;
    bool IsPlaybackMode() const { return mode_ == Mode::kPlayback; }

    // 打开/关闭这一格的音频：只是翻一下静音标志位，不重连/重开。两路的
    // 标志都会设，反正同一时刻只有其中一路真正在跑。
    void SetAudioEnabled(bool enabled);

    // MainWindow 用来给这一格套一层带边框的容器（选中态绿色边框），
    // 具体布局细节由 MainWindow 管理，这里只是存个指针方便它取回来用。
    void SetContainer(QWidget* container) { container_ = container; }
    QWidget* Container() const { return container_; }

    // 当前播放的摄像头的主码流地址（画质高，录像用这个）；没在直播、或者
    // 正在回放录像文件时是空字符串（回放中的格子没有"主码流"这个概念，
    // 不应该被"录制"）。OpenUrl() 打开的是辅码流（预览用），主码流地址由
    // MainWindow 在 OpenUrl() 成功之后另外调用 SetMainUrl() 记下来——这一格
    // 自己不查 CameraConfig，不知道"辅码流对应的主码流是哪个"。
    QString MainUrl() const { return main_url_; }
    void SetMainUrl(const QString& url) { main_url_ = url; }

    // MainWindow 每秒推一次这一格对应的窗口是不是正在被录像、录了多久——
    // 纯展示用，这一格自己不查 RecordingManager（它不知道自己的窗口编号，
    // 那是 MainWindow 的概念）。
    void SetRecordingIndicator(bool recording, long long elapsed_ms);

    void OnVideoFrame(const XVideoFrame& frame) override;

    // 回放控制转发：只在 IsPlaybackMode() 时有意义，否则是空操作/返回
    // 默认值——供 MainWindow 的回放控制条统一调用，不用先判断模式。
    void Play();
    void Pause();
    bool IsPlaying() const;
    void SeekTo(long long ms);
    void StepForward();
    void StepBackward();
    void SetSpeed(double speed);
    double Speed() const;
    long long DurationMs() const;
    long long PositionMs() const;

signals:
    void DoubleClicked(CameraTileWidget* self);
    void Selected(CameraTileWidget* self);
    void ItemDropped(CameraTileWidget* self, QListWidget* source, int row);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void UpdateVideoInfo();

private:
    void RepositionOverlay();

    enum class Mode { kEmpty, kLive, kPlayback };

    XLiveStream stream_;
    XPlayback playback_;
    Mode mode_ = Mode::kEmpty;
    bool audio_enabled_ = false;
    QWidget* container_ = nullptr;
    QString main_url_;

    // OnVideoFrame 在解码线程上被调用，UpdateVideoInfo 由 GUI 线程的定时器
    // 调用，用原子量跨线程传帧率统计，不用另开锁。
    std::atomic<int> frame_count_{0};
    std::atomic<int> video_width_{0};
    std::atomic<int> video_height_{0};

    QLabel* video_info_label_;  // 左上角：分辨率 + 帧率
    QLabel* record_indicator_;  // 左下角：录像中的红点+时长（纯展示）
};

#endif  // XVIEWER_CAMERA_TILE_WIDGET_H_
