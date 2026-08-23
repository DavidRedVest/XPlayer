#ifndef XVIEWER_MAIN_WINDOW_H_
#define XVIEWER_MAIN_WINDOW_H_

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class CameraTileWidget;
class PlaybackControlBar;
class TileGridView;

// 顶部一个按钮在"实时预览"/"录像回放"两个完全独立的页面之间切换：
// - 实时预览页：左边是摄像头列表（增/改/删，可拖拽），右边是直播专属的
//   网格（`live_grid_`）——从列表拖一项到某一格，该格打开对应摄像头的
//   直播（辅码流优先）。
// - 录像回放页：左边换成只读的录像文件列表，右边是回放专属的网格
//   （`playback_grid_`）——从列表拖一项到某一格，该格回放这个录像文件
//   （`XPlayback`），可以同时有好几格在回放不同的文件，互不干扰。
// 两个网格互相隐藏、互不可见，但都不会因为切到另一页就被关掉——切走的
// 那一页在后台继续跑（直播流、回放都不受影响，跟双击全屏/切分屏"隐藏但
// 不关流"是同一个道理），只是当前不可见那一页里"选中"的格子会被静音
// （不然看不见的画面还在背景里出声音）。
// 网格内部的交互（单击选中出声音、双击全屏、右键菜单分屏/关闭/录像）都
// 封装在 `TileGridView` 里，`MainWindow` 只负责："当前该显示哪一页"、
// "拖拽的这一行到底代表哪个摄像头/哪个录像文件"、"回放控制条要绑定给
// 谁"这几件跨网格才知道的事。
class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void RefreshCameraList();
    void RefreshRecordingsList();
    void OnAddCamera();
    void OnEditCamera();
    void OnDeleteCamera();
    void OnLiveGridItemDropped(CameraTileWidget* tile, QListWidget* source, int row);
    void OnPlaybackGridItemDropped(CameraTileWidget* tile, QListWidget* source, int row);
    void OnRecordingsListContextMenu(const QPoint& pos);
    void UpdateRecordingIndicators();
    void ToggleFullScreen();
    void ToggleLeftPanel();
    void ToggleMode();

    QListWidget* camera_list_;
    QListWidget* recordings_list_;
    QWidget* camera_panel_;       // 摄像头列表 + 增/改/删按钮，实时预览模式下显示
    QWidget* recordings_panel_;   // 录像文件列表，录像回放模式下显示
    QPushButton* mode_toggle_btn_;
    QWidget* left_panel_;
    TileGridView* live_grid_;
    TileGridView* playback_grid_;
    PlaybackControlBar* playback_bar_;
    bool playback_mode_ = false;  // false=实时预览，true=录像回放
};

#endif  // XVIEWER_MAIN_WINDOW_H_
