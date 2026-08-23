#include "playback_control_bar.h"

#include <algorithm>
#include <cmath>

#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QStringList>
#include <QTimer>

#include "camera_tile_widget.h"

namespace {
constexpr long long kSkipMs = 10000;
// 播放到结尾时，最后一帧的 pts 不一定精确等于 duration_ms_，留点容差再判定
// "已经到结尾了"，避免循环判定一直卡在临界值附近来回抖动。
constexpr long long kEndToleranceMs = 500;

QString FormatMs(long long ms) {
    if (ms < 0) {
        ms = 0;
    }
    long long total_sec = ms / 1000;
    int hh = static_cast<int>(total_sec / 3600);
    int mm = static_cast<int>((total_sec % 3600) / 60);
    int ss = static_cast<int>(total_sec % 60);
    return hh > 0
               ? QString("%1:%2:%3").arg(hh).arg(mm, 2, 10, QChar('0')).arg(ss, 2, 10, QChar('0'))
               : QString("%1:%2").arg(mm, 2, 10, QChar('0')).arg(ss, 2, 10, QChar('0'));
}

// 解析用户在跳转输入框里敲的时间：支持 "HH:MM:SS" / "MM:SS" / 纯秒数三种
// 格式。解析失败返回 -1（调用方直接忽略这次跳转，不弹错误框）。
long long ParseTimestampMs(const QString& text) {
    QStringList parts = text.trimmed().split(':');
    if (parts.isEmpty() || parts.size() > 3) {
        return -1;
    }
    long long total_sec = 0;
    for (const QString& part : parts) {
        bool ok = false;
        int value = part.toInt(&ok);
        if (!ok || value < 0) {
            return -1;
        }
        total_sec = total_sec * 60 + value;
    }
    return total_sec * 1000;
}

// 倍速下拉框里找跟 speed 最接近的一项的下标（浮点直接比较可能因为存取
// 精度对不上，找最近的更稳妥）。
int ClosestSpeedIndex(QComboBox* combo, double speed) {
    int best = 0;
    double best_diff = -1;
    for (int i = 0; i < combo->count(); ++i) {
        double diff = std::abs(combo->itemData(i).toDouble() - speed);
        if (best_diff < 0 || diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }
    return best;
}

constexpr int kIconSize = 20;

// 纯图标按钮（不带文字），文字挪到 tooltip 里——跟用户提供的图标搭配的
// 播放器传输条常见样式。
QPushButton* MakeIconButton(QWidget* parent, const QString& icon_path, const QString& tooltip) {
    auto* btn = new QPushButton(parent);
    btn->setIcon(QIcon(icon_path));
    btn->setIconSize(QSize(kIconSize, kIconSize));
    btn->setToolTip(tooltip);
    return btn;
}
}  // namespace

PlaybackControlBar::PlaybackControlBar(QWidget* parent) : QWidget(parent) {
    // 顺序：后退10秒、后退一帧、播放/暂停、前进一帧、快进10秒、倍速播放、
    // 单次播放/循环播放——之后是进度条、时长、跳转框（位置不变）。
    auto* skip_back_btn = MakeIconButton(this, ":/icons/skip_back_10s.svg", "后退10秒");
    auto* step_back_btn = MakeIconButton(this, ":/icons/step_back.svg", "后退一帧");
    play_pause_btn_ = MakeIconButton(this, ":/icons/pause.svg", "暂停");
    auto* step_forward_btn = MakeIconButton(this, ":/icons/step_forward.svg", "前进一帧");
    auto* skip_forward_btn = MakeIconButton(this, ":/icons/skip_forward_10s.svg", "快进10秒");

    auto* speed_icon = new QLabel(this);
    speed_icon->setPixmap(QIcon(":/icons/speed.svg").pixmap(kIconSize, kIconSize));
    speed_icon->setToolTip("倍速播放");

    speed_combo_ = new QComboBox(this);
    speed_combo_->addItem("0.5x", 0.5);
    speed_combo_->addItem("1x", 1.0);
    speed_combo_->addItem("1.5x", 1.5);
    speed_combo_->addItem("2x", 2.0);
    speed_combo_->addItem("4x", 4.0);
    speed_combo_->setCurrentIndex(1);

    connect(play_pause_btn_, &QPushButton::clicked, this, &PlaybackControlBar::OnPlayPauseClicked);
    connect(step_back_btn, &QPushButton::clicked, this, &PlaybackControlBar::OnStepBack);
    connect(skip_back_btn, &QPushButton::clicked, this, &PlaybackControlBar::OnSkipBack);
    connect(skip_forward_btn, &QPushButton::clicked, this, &PlaybackControlBar::OnSkipForward);
    connect(step_forward_btn, &QPushButton::clicked, this, &PlaybackControlBar::OnStepForward);
    connect(speed_combo_, &QComboBox::currentIndexChanged, this, &PlaybackControlBar::OnSpeedChanged);

    loop_btn_ = MakeIconButton(this, ":/icons/single_play.svg", "单次播放");
    loop_btn_->setCheckable(true);
    loop_btn_->setChecked(false);  // 默认单次播放
    connect(loop_btn_, &QPushButton::toggled, this, &PlaybackControlBar::OnLoopToggled);

    seek_slider_ = new QSlider(Qt::Horizontal, this);
    connect(seek_slider_, &QSlider::sliderPressed, this, &PlaybackControlBar::OnSliderPressed);
    connect(seek_slider_, &QSlider::sliderReleased, this, &PlaybackControlBar::OnSliderReleased);

    position_label_ = new QLabel("00:00 / 00:00", this);

    jump_edit_ = new QLineEdit(this);
    jump_edit_->setPlaceholderText("mm:ss 跳转");
    jump_edit_->setFixedWidth(80);
    connect(jump_edit_, &QLineEdit::returnPressed, this, &PlaybackControlBar::OnJumpToTimestamp);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(skip_back_btn);
    layout->addWidget(step_back_btn);
    layout->addWidget(play_pause_btn_);
    layout->addWidget(step_forward_btn);
    layout->addWidget(skip_forward_btn);
    layout->addWidget(speed_icon);
    layout->addWidget(speed_combo_);
    layout->addWidget(loop_btn_);
    layout->addWidget(seek_slider_, 1);
    layout->addWidget(position_label_);
    layout->addWidget(jump_edit_);

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PlaybackControlBar::UpdatePosition);
    timer->start(250);

    hide();  // 默认没有绑定任何回放中的格子，不占地方。
}

void PlaybackControlBar::BindTile(CameraTileWidget* tile) {
    target_ = (tile && tile->IsPlaybackMode()) ? tile : nullptr;
    slider_dragging_ = false;
    if (!target_) {
        hide();
        return;
    }
    show();
    {
        // 切换绑定目标时把倍速下拉框同步成这一格自己的当前倍速，不然会
        // 出现"格子 A 设成 2x，切到格子 B（1x）又切回 A，下拉框却一直
        // 停在切换前的显示"这种显示跟实际对不上的情况。阻塞信号：这里是
        // 程序设置显示状态，不是用户操作，不需要（也不应该）再触发一次
        // OnSpeedChanged 去改 target_ 的倍速。
        QSignalBlocker blocker(speed_combo_);
        speed_combo_->setCurrentIndex(ClosestSpeedIndex(speed_combo_, target_->Speed()));
    }
    UpdatePosition();
}

void PlaybackControlBar::OnPlayPauseClicked() {
    if (!target_) {
        return;
    }
    if (target_->IsPlaying()) {
        target_->Pause();
    } else {
        target_->Play();
    }
}

void PlaybackControlBar::OnSkipBack() {
    if (target_) {
        target_->SeekTo(target_->PositionMs() - kSkipMs);
    }
}

void PlaybackControlBar::OnSkipForward() {
    if (target_) {
        target_->SeekTo(target_->PositionMs() + kSkipMs);
    }
}

void PlaybackControlBar::OnStepBack() {
    if (target_) {
        target_->StepBackward();
    }
}

void PlaybackControlBar::OnStepForward() {
    if (target_) {
        target_->StepForward();
    }
}

void PlaybackControlBar::OnSpeedChanged(int index) {
    if (target_) {
        target_->SetSpeed(speed_combo_->itemData(index).toDouble());
    }
}

void PlaybackControlBar::OnSliderPressed() { slider_dragging_ = true; }

void PlaybackControlBar::OnSliderReleased() {
    slider_dragging_ = false;
    if (target_) {
        target_->SeekTo(seek_slider_->value());
    }
}

void PlaybackControlBar::OnLoopToggled(bool checked) {
    loop_enabled_ = checked;
    loop_btn_->setIcon(QIcon(checked ? ":/icons/loop.svg" : ":/icons/single_play.svg"));
    loop_btn_->setToolTip(checked ? "循环播放" : "单次播放");
}

void PlaybackControlBar::OnJumpToTimestamp() {
    if (!target_) {
        return;
    }
    long long ms = ParseTimestampMs(jump_edit_->text());
    if (ms < 0) {
        return;  // 解析失败：忽略这次跳转，不弹错误框
    }
    ms = std::clamp(ms, 0LL, target_->DurationMs());
    target_->SeekTo(ms);
    jump_edit_->clear();
}

void PlaybackControlBar::UpdatePosition() {
    if (!target_) {
        return;
    }
    long long duration = target_->DurationMs();
    long long position = target_->PositionMs();
    if (seek_slider_->maximum() != static_cast<int>(duration)) {
        seek_slider_->setRange(0, static_cast<int>(duration));
    }
    if (!slider_dragging_) {
        seek_slider_->setValue(static_cast<int>(position));
    }
    position_label_->setText(FormatMs(position) + " / " + FormatMs(duration));
    bool playing = target_->IsPlaying();
    play_pause_btn_->setIcon(QIcon(playing ? ":/icons/pause.svg" : ":/icons/play.svg"));
    play_pause_btn_->setToolTip(playing ? "暂停" : "播放");

    // 循环播放：读到文件末尾时 XPlayback::Main() 会自己把状态停在"暂停"，
    // 这里检测到"停在结尾附近"且用户开了循环，就重新跳回开头继续播放。
    if (loop_enabled_ && !playing && duration > 0 && position >= duration - kEndToleranceMs) {
        target_->SeekTo(0);
        target_->Play();
    }
}
