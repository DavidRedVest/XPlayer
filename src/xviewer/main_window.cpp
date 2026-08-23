#include "main_window.h"

#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSize>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "camera_config.h"
#include "camera_edit_dialog.h"
#include "camera_tile_widget.h"
#include "playback_control_bar.h"
#include "recording_manager.h"
#include "tile_grid_view.h"
#include "xcodec/xplayback.h"

namespace {
QString FormatRecordingDuration(long long ms) {
    long long total_sec = ms / 1000;
    int hh = static_cast<int>(total_sec / 3600);
    int mm = static_cast<int>((total_sec % 3600) / 60);
    int ss = static_cast<int>(total_sec % 60);
    return hh > 0
               ? QString("%1:%2:%3").arg(hh).arg(mm, 2, 10, QChar('0')).arg(ss, 2, 10, QChar('0'))
               : QString("%1:%2").arg(mm, 2, 10, QChar('0')).arg(ss, 2, 10, QChar('0'));
}

// 文件名格式固定是 "<时间戳 6 段>_<分段序号>_<窗口编号>.mp4"（见
// XRecorder 里的 NextSegmentPath），从文件名直接解析出可读的时间戳和
// 窗口编号，不用额外存元数据。解析失败就退回用文件名本身占位。
QString DescribeRecordingFile(const QFileInfo& fi) {
    QStringList parts = fi.completeBaseName().split('_');
    if (parts.size() != 8) {
        return fi.fileName();
    }
    QString timestamp = QString("%1-%2-%3 %4:%5:%6")
                             .arg(parts[0], parts[1], parts[2])
                             .arg(parts[3], parts[4], parts[5]);
    return QString("%1  窗口%2").arg(timestamp, parts[7]);
}

// 在系统文件管理器里定位到这个文件。macOS/Windows 都有"打开文件夹并选中
// 这个文件"的原生方式；Linux 没有统一标准，退化成只打开所在文件夹。
void RevealInFileManager(const QString& path) {
#if defined(Q_OS_MACOS)
    QProcess::startDetached("open", {"-R", path});
#elif defined(Q_OS_WIN)
    QProcess::startDetached("explorer", {"/select,", QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

constexpr int kIconSize = 20;

// 纯图标按钮（不带文字），跟 PlaybackControlBar 的传输条按钮同一个风格——
// 文字挪到 tooltip 里。
QPushButton* MakeIconButton(QWidget* parent, const QString& icon_path, const QString& tooltip) {
    auto* btn = new QPushButton(parent);
    btn->setIcon(QIcon(icon_path));
    btn->setIconSize(QSize(kIconSize, kIconSize));
    btn->setToolTip(tooltip);
    return btn;
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("xviewer (player_v2)");
    resize(1200, 800);

    // 顶部：实时预览/录像回放模式切换，一个按钮二选一——切换的是整个
    // 右侧显示哪一份网格（连同左侧列表），不是同一份网格里格子的模式。
    mode_toggle_btn_ = new QPushButton(this);
    connect(mode_toggle_btn_, &QPushButton::clicked, this, &MainWindow::ToggleMode);

    // 摄像头列表面板：实时预览模式下显示。
    camera_list_ = new QListWidget(this);
    camera_list_->setDragEnabled(true);

    auto* add_btn = MakeIconButton(this, ":/icons/add.svg", "新增");
    auto* edit_btn = MakeIconButton(this, ":/icons/edit.svg", "修改");
    auto* delete_btn = MakeIconButton(this, ":/icons/delete.svg", "删除");
    connect(add_btn, &QPushButton::clicked, this, &MainWindow::OnAddCamera);
    connect(edit_btn, &QPushButton::clicked, this, &MainWindow::OnEditCamera);
    connect(delete_btn, &QPushButton::clicked, this, &MainWindow::OnDeleteCamera);

    auto* btn_row = new QHBoxLayout();
    btn_row->addWidget(add_btn);
    btn_row->addWidget(edit_btn);
    btn_row->addWidget(delete_btn);

    camera_panel_ = new QWidget(this);
    auto* camera_layout = new QVBoxLayout(camera_panel_);
    camera_layout->setContentsMargins(0, 0, 0, 0);
    camera_layout->addWidget(new QLabel("摄像头列表（拖动到右侧画面播放）", camera_panel_));
    camera_layout->addWidget(camera_list_, 1);
    camera_layout->addLayout(btn_row);

    // 录像文件列表面板：录像回放模式下显示，只读（没有增/改/删），拖动
    // 到格子上让那一格回放这个文件。
    recordings_list_ = new QListWidget(this);
    recordings_list_->setDragEnabled(true);
    recordings_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(recordings_list_, &QListWidget::customContextMenuRequested, this,
            &MainWindow::OnRecordingsListContextMenu);

    recordings_panel_ = new QWidget(this);
    auto* recordings_layout = new QVBoxLayout(recordings_panel_);
    recordings_layout->setContentsMargins(0, 0, 0, 0);
    recordings_layout->addWidget(new QLabel("录像文件（拖动到右侧画面回放）", recordings_panel_));
    recordings_layout->addWidget(recordings_list_, 1);

    left_panel_ = new QWidget(this);
    left_panel_->setFixedWidth(220);
    auto* left_layout = new QVBoxLayout(left_panel_);
    left_layout->addWidget(mode_toggle_btn_);
    left_layout->addWidget(camera_panel_, 1);
    left_layout->addWidget(recordings_panel_, 1);

    // 右侧：两份完全独立的网格（直播/回放），同一时刻只显示一份；底部
    // 回放控制条只在回放网格里有格子被选中时才出现。
    live_grid_ = new TileGridView(/*is_live_grid=*/true, this);
    playback_grid_ = new TileGridView(/*is_live_grid=*/false, this);
    connect(live_grid_, &TileGridView::ItemDropped, this, &MainWindow::OnLiveGridItemDropped);
    connect(playback_grid_, &TileGridView::ItemDropped, this, &MainWindow::OnPlaybackGridItemDropped);
    connect(live_grid_, &TileGridView::ToggleFullScreenRequested, this, &MainWindow::ToggleFullScreen);
    connect(playback_grid_, &TileGridView::ToggleFullScreenRequested, this, &MainWindow::ToggleFullScreen);
    connect(live_grid_, &TileGridView::ToggleLeftPanelRequested, this, &MainWindow::ToggleLeftPanel);
    connect(playback_grid_, &TileGridView::ToggleLeftPanelRequested, this, &MainWindow::ToggleLeftPanel);

    playback_bar_ = new PlaybackControlBar(this);
    connect(playback_grid_, &TileGridView::SelectionChanged, playback_bar_, &PlaybackControlBar::BindTile);

    auto* right_layout = new QVBoxLayout();
    right_layout->addWidget(live_grid_, 1);
    right_layout->addWidget(playback_grid_, 1);
    right_layout->addWidget(playback_bar_);

    auto* main_layout = new QHBoxLayout(this);
    main_layout->addWidget(left_panel_);
    main_layout->addLayout(right_layout, 1);

    RefreshCameraList();
    // 初始状态：实时预览页（跟 playback_mode_ 的默认值一致）。
    mode_toggle_btn_->setText("当前：实时预览（点击切到录像回放）");
    camera_panel_->show();
    recordings_panel_->hide();
    live_grid_->show();
    playback_grid_->hide();

    // 录像连接是异步的（跟 XLiveStream 一样，Start() 立刻返回不等连接结
    // 果），点完按钮那一刻 IsRecording() 大概率还是 false——用定时器把每
    // 一格左下角的录像状态/时长补上。
    auto* status_timer = new QTimer(this);
    connect(status_timer, &QTimer::timeout, this, &MainWindow::UpdateRecordingIndicators);
    status_timer->start(1000);
}

MainWindow::~MainWindow() {
    // 后台录像跟网格预览完全独立，不会随着某个格子/某个网格关闭而停，
    // 只有整个应用退出时才一起收尾。两份网格自己的格子跟着 QWidget 的
    // 父子关系自动析构（CameraTileWidget 的析构会完整关流/关回放）。
    RecordingManager::Instance()->StopAll();
}

void MainWindow::ToggleFullScreen() {
    if (isFullScreen()) {
        showNormal();
        left_panel_->show();
    } else {
        // 全屏是"视频墙"模式，侧边栏跟着一起隐藏，退出全屏再还原。
        left_panel_->hide();
        showFullScreen();
    }
    live_grid_->SetFullScreenLabel(isFullScreen());
    playback_grid_->SetFullScreenLabel(isFullScreen());
}

void MainWindow::ToggleLeftPanel() {
    left_panel_->setVisible(!left_panel_->isVisible());
    live_grid_->SetLeftPanelVisibleLabel(left_panel_->isVisible());
    playback_grid_->SetLeftPanelVisibleLabel(left_panel_->isVisible());
}

void MainWindow::ToggleMode() {
    // 切走的那一页里如果有格子被选中，先静音它——那一页马上就要看不见了，
    // 不该在背景里继续出声音。切进来的那一页如果之前选过格子，恢复它的
    // 声音（跟切走前的状态对称）。
    TileGridView* leaving = playback_mode_ ? playback_grid_ : live_grid_;
    TileGridView* entering = playback_mode_ ? live_grid_ : playback_grid_;
    if (leaving->SelectedTile()) {
        leaving->SelectedTile()->SetAudioEnabled(false);
    }

    playback_mode_ = !playback_mode_;
    if (playback_mode_) {
        mode_toggle_btn_->setText("当前：录像回放（点击切到实时预览）");
        camera_panel_->hide();
        recordings_panel_->show();
        live_grid_->hide();
        playback_grid_->show();
        RefreshRecordingsList();
    } else {
        mode_toggle_btn_->setText("当前：实时预览（点击切到录像回放）");
        recordings_panel_->hide();
        camera_panel_->show();
        playback_grid_->hide();
        live_grid_->show();
    }

    if (entering->SelectedTile()) {
        entering->SelectedTile()->SetAudioEnabled(true);
    }

    // 回放控制条只在"回放页正显示着"的时候才该出现——它只接了
    // playback_grid_ 自己的 SelectionChanged 信号，选中的格子在切页前后
    // 没变的话那个信号不会再发一次，控制条会停留在上一个状态，变成"看不
    // 见的那一页，控制条却还开着、还能操作"。这里每次切页都显式同步一次：
    // 切进直播页强制隐藏，切进回放页按 playback_grid_ 当前的选中格子
    // 重新绑定（可能是 nullptr，BindTile 自己会处理）。
    playback_bar_->BindTile(playback_mode_ ? playback_grid_->SelectedTile() : nullptr);
}

void MainWindow::RefreshRecordingsList() {
    recordings_list_->clear();
    QDir dir(RecordingManager::RecordingDir());
    QFileInfoList files = dir.entryInfoList(QStringList() << "*.mp4", QDir::Files, QDir::Time);
    for (const QFileInfo& fi : files) {
        long long duration_ms = XProbeDurationMs(fi.absoluteFilePath().toUtf8().constData());
        QString text = QString("%1  时长 %2")
                           .arg(DescribeRecordingFile(fi), FormatRecordingDuration(duration_ms));
        auto* item = new QListWidgetItem(text, recordings_list_);
        item->setData(Qt::UserRole, fi.absoluteFilePath());
    }
}

void MainWindow::RefreshCameraList() {
    camera_list_->clear();
    int count = CameraConfig::Instance()->Count();
    for (int i = 0; i < count; ++i) {
        camera_list_->addItem(CameraConfig::Instance()->Get(i).name);
    }
}

void MainWindow::OnLiveGridItemDropped(CameraTileWidget* tile, QListWidget* source, int row) {
    if (source != camera_list_ || row < 0 || row >= CameraConfig::Instance()->Count()) {
        return;
    }
    CameraInfo cam = CameraConfig::Instance()->Get(row);
    QString url = cam.sub_url.isEmpty() ? cam.url : cam.sub_url;
    if (tile->OpenUrl(url)) {
        tile->SetMainUrl(cam.url);  // 主码流地址记下来，给"录制当前画面/所有"用
    }
}

void MainWindow::OnPlaybackGridItemDropped(CameraTileWidget* tile, QListWidget* source, int row) {
    if (source != recordings_list_) {
        return;
    }
    auto* item = recordings_list_->item(row);
    if (!item) {
        return;
    }
    QVariant data = item->data(Qt::UserRole);
    if (data.isValid()) {
        tile->OpenPlaybackFile(data.toString());
    }
}

void MainWindow::OnRecordingsListContextMenu(const QPoint& pos) {
    QListWidgetItem* item = recordings_list_->itemAt(pos);
    if (!item) {
        return;
    }
    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) {
        return;
    }
    QMenu menu(this);
    QAction* reveal_action = menu.addAction("打开文件所在位置");
    if (menu.exec(recordings_list_->mapToGlobal(pos)) == reveal_action) {
        RevealInFileManager(path);
    }
}

void MainWindow::UpdateRecordingIndicators() { live_grid_->UpdateRecordingIndicators(); }

void MainWindow::OnAddCamera() {
    CameraEditDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        CameraConfig::Instance()->Add(dialog.Result());
        RefreshCameraList();
    }
}

void MainWindow::OnEditCamera() {
    int row = camera_list_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先在列表中选择一个摄像头");
        return;
    }
    CameraInfo existing = CameraConfig::Instance()->Get(row);
    CameraEditDialog dialog(this, &existing);
    if (dialog.exec() == QDialog::Accepted) {
        CameraConfig::Instance()->Set(row, dialog.Result());
        RefreshCameraList();
    }
}

void MainWindow::OnDeleteCamera() {
    int row = camera_list_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先在列表中选择一个摄像头");
        return;
    }
    CameraInfo existing = CameraConfig::Instance()->Get(row);
    auto reply = QMessageBox::question(this, "确认删除", "确定要删除摄像头「" + existing.name + "」吗？");
    if (reply == QMessageBox::Yes) {
        CameraConfig::Instance()->Remove(row);
        RefreshCameraList();
    }
}
