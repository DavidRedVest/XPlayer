#include "camera_tile_widget.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTimer>

CameraTileWidget::CameraTileWidget(QWidget* parent) : XGLVideoWidget(parent) {
    setMinimumSize(160, 120);
    setAcceptDrops(true);

    video_info_label_ = new QLabel(this);
    video_info_label_->setStyleSheet(
        "color: white; background-color: rgba(0,0,0,140); padding: 1px 5px; border-radius: 3px;");
    video_info_label_->hide();

    record_indicator_ = new QLabel(this);
    record_indicator_->setStyleSheet(
        "color: white; background-color: rgba(200,20,20,210); padding: 1px 5px; border-radius: 3px;");
    record_indicator_->hide();

    auto* ui_timer = new QTimer(this);
    connect(ui_timer, &QTimer::timeout, this, &CameraTileWidget::UpdateVideoInfo);
    ui_timer->start(1000);

    RepositionOverlay();
}

CameraTileWidget::~CameraTileWidget() { CloseStream(); }

bool CameraTileWidget::IsActive() const { return stream_.IsOpen() || playback_.IsOpen(); }

bool CameraTileWidget::OpenUrl(const QString& url) {
    auto trimmed = url.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    // 无条件先关一次（两路都关）：Close() 对没打开过的流/回放是安全的空
    // 操作，即使当前正处在"还没连上"的中间状态也会正确打断、回收——不能
    // 只在 IsActive() 时才关，否则快速换台会把上一次还没连完的连接漏掉。
    CloseStream();
    // enable_audio 始终为 true：解码管线一直带着音频，是否真的出声音由
    // SetAudioMuted 控制，这样之后切换"哪一格有声音"不需要重新 Open。
    bool ok = stream_.Open(trimmed.toUtf8().constData(), this, /*enable_audio=*/true);
    if (ok) {
        mode_ = Mode::kLive;
        stream_.SetAudioMuted(!audio_enabled_);
    }
    return ok;
}

bool CameraTileWidget::OpenPlaybackFile(const QString& path) {
    auto trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    CloseStream();
    bool ok = playback_.Open(trimmed.toUtf8().constData(), this);
    if (ok) {
        mode_ = Mode::kPlayback;
        playback_.SetAudioMuted(!audio_enabled_);
    }
    return ok;
}

void CameraTileWidget::CloseStream() {
    stream_.Close();
    playback_.Close();
    mode_ = Mode::kEmpty;
    Clear();
    main_url_.clear();
    video_width_ = 0;
    video_height_ = 0;
    frame_count_ = 0;
    video_info_label_->hide();
}

void CameraTileWidget::SetAudioEnabled(bool enabled) {
    audio_enabled_ = enabled;
    // 不管这一格当前是不是已经连上（可能还在异步连接中），也不管当前是
    // 直播还是回放模式，两路的标志都直接翻——反正同一时刻只有其中一路
    // 真正在跑，SetAudioMuted 本身就是随时可调用的原子标志，等连上之后
    // 音频回调会自然按最新的状态走。
    stream_.SetAudioMuted(!enabled);
    playback_.SetAudioMuted(!enabled);
}

void CameraTileWidget::Play() {
    if (IsPlaybackMode()) {
        playback_.Play();
    }
}

void CameraTileWidget::Pause() {
    if (IsPlaybackMode()) {
        playback_.Pause();
    }
}

bool CameraTileWidget::IsPlaying() const { return IsPlaybackMode() && playback_.IsPlaying(); }

void CameraTileWidget::SeekTo(long long ms) {
    if (IsPlaybackMode()) {
        playback_.SeekTo(ms);
    }
}

void CameraTileWidget::StepForward() {
    if (IsPlaybackMode()) {
        playback_.StepForward();
    }
}

void CameraTileWidget::StepBackward() {
    if (IsPlaybackMode()) {
        playback_.StepBackward();
    }
}

void CameraTileWidget::SetSpeed(double speed) {
    if (IsPlaybackMode()) {
        playback_.SetSpeed(speed);
    }
}

double CameraTileWidget::Speed() const { return IsPlaybackMode() ? playback_.Speed() : 1.0; }

long long CameraTileWidget::DurationMs() const {
    return IsPlaybackMode() ? playback_.DurationMs() : 0;
}

long long CameraTileWidget::PositionMs() const {
    return IsPlaybackMode() ? playback_.PositionMs() : 0;
}

void CameraTileWidget::OnVideoFrame(const XVideoFrame& frame) {
    XGLVideoWidget::OnVideoFrame(frame);
    video_width_.store(frame.width, std::memory_order_relaxed);
    video_height_.store(frame.height, std::memory_order_relaxed);
    frame_count_.fetch_add(1, std::memory_order_relaxed);
}

void CameraTileWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit Selected(this);
    }
    event->accept();
}

void CameraTileWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    emit DoubleClicked(this);
    event->accept();
}

void CameraTileWidget::dragEnterEvent(QDragEnterEvent* event) { event->acceptProposedAction(); }

void CameraTileWidget::dropEvent(QDropEvent* event) {
    auto* list = qobject_cast<QListWidget*>(event->source());
    if (!list) {
        return;
    }
    int row = list->currentRow();
    if (row < 0) {
        return;
    }
    // 这一格自己不知道现在是"直播模式"还是"回放模式"（源列表现在到底是
    // 摄像头列表还是录像文件列表），也不知道怎么根据行号查到真正的
    // URL/文件路径——那是 MainWindow 的概念（它同时管着两份列表），这里
    // 只负责把"拖拽这个动作本身"报告出去。
    emit ItemDropped(this, list, row);
    event->acceptProposedAction();
}

void CameraTileWidget::resizeEvent(QResizeEvent* event) {
    XGLVideoWidget::resizeEvent(event);
    RepositionOverlay();
}

void CameraTileWidget::RepositionOverlay() {
    const int margin = 6;
    video_info_label_->move(margin, margin);
    record_indicator_->move(margin, height() - record_indicator_->height() - margin);
}

void CameraTileWidget::UpdateVideoInfo() {
    int frames = frame_count_.exchange(0, std::memory_order_relaxed);
    int w = video_width_.load(std::memory_order_relaxed);
    int h = video_height_.load(std::memory_order_relaxed);
    if (!IsActive() || w <= 0 || h <= 0) {
        video_info_label_->hide();
        return;
    }
    video_info_label_->setText(QString("%1x%2 %3fps").arg(w).arg(h).arg(frames));
    video_info_label_->adjustSize();
    video_info_label_->show();
    RepositionOverlay();
}

void CameraTileWidget::SetRecordingIndicator(bool recording, long long elapsed_ms) {
    if (!recording) {
        record_indicator_->hide();
        return;
    }
    long long total_sec = elapsed_ms / 1000;
    int hh = static_cast<int>(total_sec / 3600);
    int mm = static_cast<int>((total_sec % 3600) / 60);
    int ss = static_cast<int>(total_sec % 60);
    QString text = hh > 0 ? QString("● REC %1:%2:%3")
                                 .arg(hh)
                                 .arg(mm, 2, 10, QChar('0'))
                                 .arg(ss, 2, 10, QChar('0'))
                           : QString("● REC %1:%2").arg(mm, 2, 10, QChar('0')).arg(ss, 2, 10, QChar('0'));
    record_indicator_->setText(text);
    record_indicator_->adjustSize();
    record_indicator_->show();
    RepositionOverlay();
}
