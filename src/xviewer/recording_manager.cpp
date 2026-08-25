#include "recording_manager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include "xcodec/xrecorder.h"

namespace {
// 实际建目录 + 写一个探测文件确认真的可写——不能只看 mkpath() 的返回值，
// macOS 上从浏览器下载、还没脱离隔离属性(quarantine)的未公证 App 会被
// Gatekeeper "App Translocation" 挪到一个只读的临时挂载点运行，这种情况下
// mkpath() 在某些 Qt/系统组合下仍可能返回 true，但目录其实没法真正写入。
bool TryUseAsWritableDir(const QString& path) {
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

// RecordingDir()/ScreenshotDir() 共用：都是"跟可执行程序放一层，写不进去
// 就退到 Movies 目录"这同一套逻辑，只有子文件夹名字不一样。
QString ResolveDataDir(const QString& subfolder) {
    // 跟可执行程序放在同一层目录，方便查找——不是 QStandardPaths 那种
    // 用户目录深处的固定位置。applicationDirPath() 在 macOS .app 包里
    // 是 xxx.app/Contents/MacOS，要往上退三级到 .app 包本身所在的目录，
    // 数据文件夹才会跟 .app 平级（在 Finder 里和应用程序本身一样好找），
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
    QString path = dir.absoluteFilePath(subfolder);
    if (TryUseAsWritableDir(path)) {
        return path;
    }

    // 首选位置写不进去——最常见的原因是 macOS 上从浏览器下载、还没被
    // Gatekeeper 信任(未公证的临时签名 App 常见)的应用被 App Translocation
    // 挪到了一个只读的临时挂载点运行，这时"跟 App 同级"的目录天生不可能
    // 写进去。退到一个保证可写的用户目录，并把实际用的位置打进日志，不能
    // 让录像/截图功能在这种情况下悄悄失效却没有任何提示。
    QString fallback =
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/XPlayer/" + subfolder;
    qWarning() << "ResolveDataDir: cannot write to" << path << "-- falling back to" << fallback;
    QDir().mkpath(fallback);
    return fallback;
}

// 摄像头名字是用户随便填的自由文本，直接拼进文件名有两个问题：可能
// 带 /\:*?"<>| 这类文件系统里有特殊含义的字符（轻则拼出来的路径跟预期
// 不一样，重则 XMux 直接建不了文件），也可能很长。这里统一过滤成 '_'
// 再截到固定长度——按 QChar（UTF-16 code unit）截断，不是按字节，不会
// 把一个多字节字符从中间切断。过滤用最严格的 Windows 非法字符集合，
// 就算文件是在 macOS/Linux 上录的，之后要挪到 Windows 上也还能用。
QString SanitizeCameraNameForFilename(const QString& name) {
    static const QString kIllegal = QStringLiteral("/\\:*?\"<>|");
    constexpr int kMaxLength = 8;
    QString sanitized = name.trimmed();
    for (QChar& c : sanitized) {
        if (kIllegal.contains(c)) {
            c = QChar('_');
        }
    }
    return sanitized.left(kMaxLength);
}
}  // namespace

RecordingManager* RecordingManager::Instance() {
    static RecordingManager instance;
    return &instance;
}

RecordingManager::~RecordingManager() { StopAll(); }

QString RecordingManager::RecordingDir() { return ResolveDataDir("recordings"); }

QString RecordingManager::ScreenshotDir() { return ResolveDataDir("screenshots"); }

QString RecordingManager::BuildScreenshotPath(int window_index, const QString& camera_name) {
    // 跟 XRecorder 那边的录像文件名同一套时间戳格式/摄像头名处理规则
    // （见 xrecorder.cpp 的 NextSegmentPath），只是截图是一次性的，不需要
    // "part 分段序号"这一节——但时间戳精度只到秒，同一秒内对同一格再截一次
    // 图，不加区分的话文件名会完全一样、后一次会把前一次悄悄覆盖掉。所以
    // 这里补一个"文件已存在就往后加 _2/_3.."的消歧循环。
    QString sanitized_name = SanitizeCameraNameForFilename(camera_name);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString name_part = sanitized_name.isEmpty() ? QString() : sanitized_name + "_";
    QString base = QString("%1/%2_%3win%4")
                       .arg(ScreenshotDir(), timestamp, name_part, QString::number(window_index));
    QString path = base + ".png";
    for (int suffix = 2; QFileInfo::exists(path); ++suffix) {
        path = QString("%1_%2.png").arg(base).arg(suffix);
    }
    return path;
}

XRecordFormat RecordingManager::Format() {
    QSettings settings;
    return static_cast<XRecordFormat>(
        settings.value("recording/format", static_cast<int>(XRecordFormat::kMp4)).toInt());
}

void RecordingManager::SetFormat(XRecordFormat format) {
    QSettings settings;
    settings.setValue("recording/format", static_cast<int>(format));
}

bool RecordingManager::Start(int window_index, const QString& url, const QString& camera_name) {
    if (url.isEmpty()) {
        return false;
    }
    auto it = recorders_.find(window_index);
    if (it != recorders_.end()) {
        return true;  // 已经在录了，不重开
    }
    QString sanitized_name = SanitizeCameraNameForFilename(camera_name);
    auto* recorder = new XRecorder();
    recorder->Start(url.toUtf8().constData(), RecordingDir().toUtf8().constData(), window_index,
                     sanitized_name.toUtf8().constData(), Format());
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
