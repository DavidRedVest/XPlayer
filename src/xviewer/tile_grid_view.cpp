#include "tile_grid_view.h"

#include <cmath>
#include <vector>

#include <QContextMenuEvent>
#include <QGridLayout>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>

#include "camera_tile_widget.h"
#include "recording_manager.h"

namespace {
int GridCols(int count) { return static_cast<int>(std::sqrt(static_cast<double>(count))); }
int GridRows(int count, int cols) { return (count + cols - 1) / cols; }

// 每一格外面包一层容器，用固定 3px 的边框宽度显示选中态——边框颜色变了但
// 宽度不变，切换选中格子时布局不会跳动。边框画在容器上而不是
// CameraTileWidget（QOpenGLWidget）本身，避开 GL 内容和样式表边框谁盖住
// 谁的不确定性。
constexpr int kTileBorderWidth = 3;
constexpr const char* kSelectedFrameStyle = "QWidget#tileFrame { border: 3px solid #00e000; }";
constexpr const char* kUnselectedFrameStyle = "QWidget#tileFrame { border: 3px solid transparent; }";
}  // namespace

TileGridView::TileGridView(bool is_live_grid, QWidget* parent)
    : QWidget(parent), is_live_grid_(is_live_grid) {
    grid_layout_ = new QGridLayout(this);
    grid_layout_->setContentsMargins(2, 2, 2, 2);
    grid_layout_->setSpacing(2);
    SetGridSize(4);
}

void TileGridView::SetFullScreenLabel(bool is_fullscreen) { is_fullscreen_ = is_fullscreen; }

void TileGridView::SetLeftPanelVisibleLabel(bool visible) { left_panel_visible_ = visible; }

CameraTileWidget* TileGridView::TileAt(const QPoint& pos_in_this_widget) const {
    for (int i = 0; i < kMaxTiles; ++i) {
        if (tiles_[i] && tiles_[i]->Container()->geometry().contains(pos_in_this_widget)) {
            return tiles_[i];
        }
    }
    return nullptr;
}

int TileGridView::WindowIndexOf(CameraTileWidget* tile) const {
    for (int i = 0; i < kMaxTiles; ++i) {
        if (tiles_[i] == tile) {
            return i;
        }
    }
    return -1;
}

void TileGridView::contextMenuEvent(QContextMenuEvent* event) {
    // "关闭"、切分屏、全屏、侧边栏显示/隐藏、录像（仅直播网格）全合并在
    // 同一个菜单里，不管右键点在正在播放的格子上还是空白处都能弹出来——
    // 正在播放的格子只会多一条"关闭"，切分屏必须先把所有格子清空才行、
    // 装满了就再也够不到分屏菜单这种死角不会出现。
    CameraTileWidget* tile = TileAt(event->pos());

    QMenu menu(this);
    QAction* close_action = nullptr;
    if (tile && tile->IsActive()) {
        close_action = menu.addAction("关闭");
        menu.addSeparator();
    }
    QAction* a1 = menu.addAction("1 画面");
    QAction* a4 = menu.addAction("4 画面");
    QAction* a9 = menu.addAction("9 画面");
    QAction* a16 = menu.addAction("16 画面");
    menu.addSeparator();
    QAction* fullscreen_action = menu.addAction(is_fullscreen_ ? "退出全屏显示" : "全屏显示");
    QAction* left_panel_action = menu.addAction(left_panel_visible_ ? "隐藏侧边栏" : "显示侧边栏");
    QAction* record_current_action = nullptr;
    QAction* stop_current_action = nullptr;
    QAction* record_all_action = nullptr;
    QAction* stop_all_action = nullptr;
    if (is_live_grid_) {
        menu.addSeparator();
        record_current_action = menu.addAction("录制当前画面");
        stop_current_action = menu.addAction("关闭当前录制");
        record_all_action = menu.addAction("录制所有画面");
        stop_all_action = menu.addAction("关闭所有录制");
    }
    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == close_action) {
        tile->CloseStream();
    } else if (chosen == a1) {
        SetGridSize(1);
    } else if (chosen == a4) {
        SetGridSize(4);
    } else if (chosen == a9) {
        SetGridSize(9);
    } else if (chosen == a16) {
        SetGridSize(16);
    } else if (chosen == fullscreen_action) {
        emit ToggleFullScreenRequested();
    } else if (chosen == left_panel_action) {
        emit ToggleLeftPanelRequested();
    } else if (chosen == record_current_action) {
        OnRecordCurrent();
    } else if (chosen == stop_current_action) {
        OnStopCurrentRecord();
    } else if (chosen == record_all_action) {
        OnRecordAll();
    } else if (chosen == stop_all_action) {
        OnStopAllRecord();
    }
    event->accept();
}

CameraTileWidget* TileGridView::CreateTile() {
    auto* frame = new QWidget(this);
    frame->setObjectName("tileFrame");
    frame->setStyleSheet(kUnselectedFrameStyle);
    auto* frame_layout = new QVBoxLayout(frame);
    frame_layout->setContentsMargins(kTileBorderWidth, kTileBorderWidth, kTileBorderWidth,
                                      kTileBorderWidth);
    frame_layout->setSpacing(0);

    auto* tile = new CameraTileWidget(frame);
    tile->setStyleSheet("background-color: black;");
    tile->SetContainer(frame);
    tile->SetAcceptsExternalFiles(!is_live_grid_);
    frame_layout->addWidget(tile);

    connect(tile, &CameraTileWidget::DoubleClicked, this, &TileGridView::OnTileDoubleClicked);
    connect(tile, &CameraTileWidget::Selected, this, &TileGridView::OnTileSelected);
    connect(tile, &CameraTileWidget::ItemDropped, this, &TileGridView::ItemDropped);
    return tile;
}

void TileGridView::SetGridSize(int count) {
    // 先把现有格子按"正在播放"/"空闲"分组，都从布局里摘出来——播放中的格
    // 子优先占据新网格的前面槽位，这样只要新画面数装得下当前播放路数，就
    // 不会丢流；真正装不下、或者本来就是空的格子才会被销毁（关流）。
    std::vector<CameraTileWidget*> active_tiles;
    std::vector<CameraTileWidget*> idle_tiles;
    for (int i = 0; i < kMaxTiles; ++i) {
        if (!tiles_[i]) {
            continue;
        }
        grid_layout_->removeWidget(tiles_[i]->Container());
        (tiles_[i]->IsActive() ? active_tiles : idle_tiles).push_back(tiles_[i]);
        tiles_[i] = nullptr;
    }

    grid_size_ = count;
    fullscreen_tile_ = nullptr;
    int cols = GridCols(count);

    bool selection_lost = false;
    int idx = 0;
    auto place_or_drop = [&](CameraTileWidget* tile) {
        if (idx < count) {
            tiles_[idx] = tile;
            grid_layout_->addWidget(tile->Container(), idx / cols, idx % cols);
            tile->Container()->show();
            ++idx;
        } else {
            if (tile == selected_tile_) {
                selected_tile_ = nullptr;
                selection_lost = true;
            }
            QWidget* container = tile->Container();
            delete tile;  // 析构会经过 XLiveStream/XPlayback 的完整关流/关回放
            delete container;
        }
    };
    for (auto* tile : active_tiles) {
        place_or_drop(tile);
    }
    for (auto* tile : idle_tiles) {
        place_or_drop(tile);
    }
    for (; idx < count; ++idx) {
        CameraTileWidget* tile = CreateTile();
        tiles_[idx] = tile;
        grid_layout_->addWidget(tile->Container(), idx / cols, idx % cols);
        tile->Container()->show();
    }
    if (selection_lost) {
        emit SelectionChanged(nullptr);
    }
}

void TileGridView::OnTileDoubleClicked(CameraTileWidget* tile) {
    if (fullscreen_tile_ == tile) {
        // 退出全屏：把这一格放回进全屏之前记下的原始行/列，其余格子原样
        // 显示回来——不能调用 SetGridSize(grid_size_) 走"缩放画面数"那套
        // 逻辑，那套逻辑是按"活跃/空闲"重新分配位置，不保证保留原来的
        // 窗口编号（实测复现过：4 格里 0 号、2 号有画面，双击 2 号全屏再
        // 双击退出，2 号的画面会被挤到 1 号去，2 号反而变空）。
        grid_layout_->removeWidget(tile->Container());
        grid_layout_->addWidget(tile->Container(), fullscreen_orig_row_, fullscreen_orig_col_);
        for (int i = 0; i < kMaxTiles; ++i) {
            if (tiles_[i]) {
                tiles_[i]->Container()->show();
            }
        }
        fullscreen_tile_ = nullptr;
        return;
    }
    if (fullscreen_tile_ != nullptr) {
        return;
    }

    int cols = GridCols(grid_size_);
    int rows = GridRows(grid_size_, cols);
    int idx = WindowIndexOf(tile);
    fullscreen_orig_row_ = idx / cols;
    fullscreen_orig_col_ = idx % cols;
    for (int i = 0; i < kMaxTiles; ++i) {
        if (tiles_[i] && tiles_[i] != tile) {
            tiles_[i]->Container()->hide();
        }
    }
    grid_layout_->removeWidget(tile->Container());
    grid_layout_->addWidget(tile->Container(), 0, 0, rows, cols);
    fullscreen_tile_ = tile;
}

void TileGridView::OnTileSelected(CameraTileWidget* tile) {
    if (selected_tile_ == tile) {
        return;
    }
    if (selected_tile_) {
        selected_tile_->SetAudioEnabled(false);
        selected_tile_->Container()->setStyleSheet(kUnselectedFrameStyle);
    }
    selected_tile_ = tile;
    tile->SetAudioEnabled(true);
    tile->Container()->setStyleSheet(kSelectedFrameStyle);
    emit SelectionChanged(tile);
}

void TileGridView::UpdateRecordingIndicators() {
    if (!is_live_grid_) {
        return;
    }
    for (int i = 0; i < kMaxTiles; ++i) {
        if (!tiles_[i]) {
            continue;
        }
        bool recording = RecordingManager::Instance()->IsRecording(i);
        tiles_[i]->SetRecordingIndicator(recording, recording ? RecordingManager::Instance()->ElapsedMs(i) : 0);
    }
}

void TileGridView::OnRecordCurrent() {
    if (!selected_tile_ || !selected_tile_->IsActive() || selected_tile_->IsPlaybackMode()) {
        QMessageBox::information(this, "提示", "请先单击选中一个正在直播的画面");
        return;
    }
    int window_index = WindowIndexOf(selected_tile_);
    if (window_index < 0) {
        return;
    }
    RecordingManager::Instance()->Start(window_index, selected_tile_->MainUrl());
}

void TileGridView::OnRecordAll() {
    for (int i = 0; i < kMaxTiles; ++i) {
        if (tiles_[i] && tiles_[i]->IsActive() && !tiles_[i]->IsPlaybackMode()) {
            RecordingManager::Instance()->Start(i, tiles_[i]->MainUrl());
        }
    }
}

void TileGridView::OnStopCurrentRecord() {
    if (!selected_tile_) {
        return;
    }
    int window_index = WindowIndexOf(selected_tile_);
    if (window_index < 0) {
        return;
    }
    RecordingManager::Instance()->Stop(window_index);
}

void TileGridView::OnStopAllRecord() { RecordingManager::Instance()->StopAll(); }
