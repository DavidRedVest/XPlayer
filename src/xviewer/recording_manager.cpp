#include "recording_manager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "xcodec/xrecorder.h"

namespace {
// 实际建目录 + 写一个探测文件确认真的可写——不能只看 mkpath() 的返回值，
// macOS 上从浏览器下载、还没脱离隔离属性(quarantine)的未公证 App 会被
// Gatekeeper "App Translocation" 挪到一个只读的临时挂载点运行，这种情况下
// mkpath() 在某些 Qt/系统组合下仍可能返回 true，但目录其实没法真正写入。
bool TryUseAsRecordingDir(const QString& path) {
    QDir().mkpath(path);
    QFileInfo probe_path(path);
    if (!probe_path.exists() || !probe_path.isWritable()) {
        return false;
    }
    QFile probe(path + "/.write_test");
    if (!probe.open(QIODevice::WriteOnly)) {
        return false;
    }
    probe.remove();
    return true;
}
}  // namespace

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
    if (TryUseAsRecordingDir(path)) {
        return path;
    }

    // 首选位置写不进去——最常见的原因是 macOS 上从浏览器下载、还没被
    // Gatekeeper 信任(未公证的临时签名 App 常见)的应用被 App Translocation
    // 挪到了一个只读的临时挂载点运行，这时"跟 App 同级"的目录天生不可能
    // 写进去。退到一个保证可写的用户目录，并把实际用的位置打进日志，不能
    // 让录像功能在这种情况下悄悄失效却没有任何提示。
    QString fallback = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) +
                        "/XPlayer/recordings";
    qWarning() << "RecordingDir: cannot write to" << path << "-- falling back to" << fallback;
    QDir().mkpath(fallback);
    return fallback;
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
