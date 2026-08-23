#ifndef XVIEWER_TILE_GRID_VIEW_H_
#define XVIEWER_TILE_GRID_VIEW_H_

#include <QWidget>

class QGridLayout;
class QListWidget;
class QContextMenuEvent;
class CameraTileWidget;

// 一整块"网格预览区"：格子数组、分屏大小、选中态、双击全屏、右键菜单
// （分屏/全屏/侧边栏显示/关闭，直播网格还多出录制四项）。MainWindow 持有
// 两份实例（直播、回放各一份），同一时刻只显示一份，互不可见、互不感知
// 对方的存在——这就是"直播和回放不共用同一块显示区域"的实现方式：不是
// 靠一份网格里单个格子在直播/回放之间切换，是靠两份完全独立的网格谁可见。
class TileGridView : public QWidget {
    Q_OBJECT

public:
    // is_live_grid 只影响右键菜单要不要出现"录制"那四项、UpdateRecordingIndicators()
    // 是不是真的做事——回放网格上的格子永远没有主码流，录制对它没有意义。
    explicit TileGridView(bool is_live_grid, QWidget* parent = nullptr);

    void SetGridSize(int count);
    CameraTileWidget* SelectedTile() const { return selected_tile_; }

    // 每秒推一次录制状态到每一格左下角；回放网格上调用是空操作。
    void UpdateRecordingIndicators();

    // 全屏/侧边栏显示是整个窗口（MainWindow）的状态，这个网格自己不知道，
    // 只用来决定右键菜单里那两项该显示"开"还是"关"的文字。
    void SetFullScreenLabel(bool is_fullscreen);
    void SetLeftPanelVisibleLabel(bool visible);

signals:
    void ItemDropped(CameraTileWidget* tile, QListWidget* source, int row);
    // 选中的格子变化时发出；tile 可能是 nullptr（比如缩小画面数导致原选
    // 中格子被销毁）。
    void SelectionChanged(CameraTileWidget* tile);
    void ToggleFullScreenRequested();
    void ToggleLeftPanelRequested();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    static constexpr int kMaxTiles = 16;

    CameraTileWidget* CreateTile();
    CameraTileWidget* TileAt(const QPoint& pos_in_this_widget) const;
    int WindowIndexOf(CameraTileWidget* tile) const;
    void OnTileDoubleClicked(CameraTileWidget* tile);
    void OnTileSelected(CameraTileWidget* tile);

    void OnRecordCurrent();
    void OnRecordAll();
    void OnStopCurrentRecord();
    void OnStopAllRecord();

    bool is_live_grid_;
    QGridLayout* grid_layout_;
    CameraTileWidget* tiles_[kMaxTiles] = {nullptr};
    CameraTileWidget* fullscreen_tile_ = nullptr;
    // 进全屏前这一格在网格里的行/列，退出全屏时直接放回这个位置——不能靠
    // SetGridSize(grid_size_) 走一遍"缩放画面数"的逻辑，那套逻辑按"活跃/
    // 空闲"重新分配位置，不保证保留原来的窗口编号（比如 4 格里 0 号、2 号
    // 有画面，退出全屏会把 2 号的画面挤到 1 号去）。
    int fullscreen_orig_row_ = 0;
    int fullscreen_orig_col_ = 0;
    CameraTileWidget* selected_tile_ = nullptr;
    int grid_size_ = 4;
    bool is_fullscreen_ = false;
    bool left_panel_visible_ = true;
};

#endif  // XVIEWER_TILE_GRID_VIEW_H_
