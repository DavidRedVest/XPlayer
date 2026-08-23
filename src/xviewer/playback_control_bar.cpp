#include "playback_control_bar.h"

#include <cmath>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>

#include "camera_tile_widget.h"

namespace {
constexpr long long kSkipMs = 10000;

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
}  // namespace

PlaybackControlBar::PlaybackControlBar(QWidget* parent) : QWidget(parent) {
    play_pause_btn_ = new QPushButton("暂停", this);
    auto* step_back_btn = new QPushButton("后退一帧", this);
    auto* skip_back_btn = new QPushButton("后退10秒", this);
    auto* skip_forward_btn = new QPushButton("快进10秒", this);
    auto* step_forward_btn = new QPushButton("前进一帧", this);

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

    seek_slider_ = new QSlider(Qt::Horizontal, this);
    connect(seek_slider_, &QSlider::sliderPressed, this, &PlaybackControlBar::OnSliderPressed);
    connect(seek_slider_, &QSlider::sliderReleased, this, &PlaybackControlBar::OnSliderReleased);

    position_label_ = new QLabel("00:00 / 00:00", this);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(play_pause_btn_);
    layout->addWidget(step_back_btn);
    layout->addWidget(skip_back_btn);
    layout->addWidget(skip_forward_btn);
    layout->addWidget(step_forward_btn);
    layout->addWidget(speed_combo_);
    layout->addWidget(seek_slider_, 1);
    layout->addWidget(position_label_);

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
    play_pause_btn_->setText(target_->IsPlaying() ? "暂停" : "播放");
}
