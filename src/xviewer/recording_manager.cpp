#include "recording_manager.h"

#include <QCoreApplication>
#include <QDir>

#include "xcodec/xrecorder.h"

RecordingManager* RecordingManager::Instance() {
    static RecordingManager instance;
    return &instance;
}

RecordingManager::~RecordingManager() { StopAll(); }

QString RecordingManager::RecordingDir() {
    // 跟可执行程序放在同一层目录，方便查找——不是 QStandardPaths 那种
    // 用户目录深处的固定位置。applicationDirPath() 在 macOS .app 包里
    // 是 xxx.app/Contents/MacOS，要往上退三级到 .app 包本身所在的目录，
    // 录像文件夹才会跟 .app 平级（在 Finder 里和应用程序本身一样好找），
    // 不是退到包内部——包内部按惯例应该只放程序自带的资源，不该写用户
    // 数据。不是 .app 包（比如直接跑裸可执行文件）就还是用可执行文件
    // 所在目录。
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.dirName() == "MacOS") {
        QDir up = dir;
        if (up.cdUp() && up.dirName() == "Contents" && up.cdUp() && up.dirName().endsWith(".app")) {
            up.cdUp();
            dir = up;
        }
    }
    QString path = dir.absoluteFilePath("recordings");
    QDir().mkpath(path);
    return path;
}

bool RecordingManager::Start(int window_index, const QString& url) {
    if (url.isEmpty()) {
        return false;
    }
    auto it = recorders_.find(window_index);
    if (it != recorders_.end()) {
        return true;  // 已经在录了，不重开
    }
    auto* recorder = new XRecorder();
    recorder->Start(url.toUtf8().constData(), RecordingDir().toUtf8().constData(), window_index);
    recorders_.insert(window_index, recorder);
    return true;
}

void RecordingManager::Stop(int window_index) {
    auto it = recorders_.find(window_index);
    if (it == recorders_.end()) {
        return;
    }
    delete it.value();  // 阻塞直到当前文件收尾写完
    recorders_.erase(it);
}

void RecordingManager::StopAll() {
    for (auto it = recorders_.begin(); it != recorders_.end(); ++it) {
        delete it.value();
    }
    recorders_.clear();
}

bool RecordingManager::IsRecording(int window_index) const {
    auto it = recorders_.find(window_index);
    return it != recorders_.end() && it.value() && it.value()->IsRecording();
}

long long RecordingManager::ElapsedMs(int window_index) const {
    auto it = recorders_.find(window_index);
    return (it != recorders_.end() && it.value()) ? it.value()->ElapsedMs() : 0;
}
