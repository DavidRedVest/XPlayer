#ifndef XVIEWER_PLAYBACK_CONTROL_BAR_H_
#define XVIEWER_PLAYBACK_CONTROL_BAR_H_

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class CameraTileWidget;

// 网格下方共用的一条回放控制条：播放/暂停、后退/前进一帧、后退/快进
// 10 秒、倍速、拖动进度条、当前/总时长——跟原来独立弹窗版本的
// PlaybackDialog 是同一套控件，区别是操作目标不是固定的一个 XPlayback，
// 而是随选中格子切换的 CameraTileWidget*（BindTile()）。网格里可能同时
// 有好几格在回放，但同一时刻只有"选中"的那一格被这条控制条操作——跟
// 音频"同一时刻只有选中的格子出声音"是同一个道理，不是这条控制条自己的
// 限制，是选中态本来就只有一个。
//
// target_ 为空、或者绑定的格子不是回放模式（直播中/空格子）时整条控制条
// 隐藏；定时器照常跑，回调里直接判断 target_ 提前返回。
class PlaybackControlBar : public QWidget {
    Q_OBJECT

public:
    explicit PlaybackControlBar(QWidget* parent = nullptr);

    // tile 为空、或者不是回放模式，控制条隐藏且不再操作任何目标；否则
    // 立刻按 tile 当前的状态刷新一次显示（避免残留上一个目标的画面）。
    void BindTile(CameraTileWidget* tile);

private slots:
    void OnPlayPauseClicked();
    void OnSkipBack();
    void OnSkipForward();
    void OnStepBack();
    void OnStepForward();
    void OnSpeedChanged(int index);
    void OnSliderPressed();
    void OnSliderReleased();
    void UpdatePosition();

private:
    CameraTileWidget* target_ = nullptr;
    QPushButton* play_pause_btn_;
    QSlider* seek_slider_;
    QLabel* position_label_;
    QComboBox* speed_combo_;
    bool slider_dragging_ = false;
};

#endif  // XVIEWER_PLAYBACK_CONTROL_BAR_H_
