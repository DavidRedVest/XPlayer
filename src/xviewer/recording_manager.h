#ifndef XVIEWER_RECORDING_MANAGER_H_
#define XVIEWER_RECORDING_MANAGER_H_

#include <QMap>
#include <QString>

#include "xcodec/xrecorder.h"

class XRecorder;

// 后台录像的调度：完全独立于摄像头配置，按"网格窗口编号"（左到右、上到下）
// 管理——由 MainWindow 上的"录制当前画面/录制所有/关闭当前录制/关闭所有"
// 四个按钮手动触发，不再跟着 CameraConfig 的开关自动增删。录像文件统一写
// 到固定目录，不再依赖每个摄像头单独配置存储路径。
class RecordingManager {
public:
    static RecordingManager* Instance();

    // 所有录像文件的固定目录（不存在会自动创建）。
    static QString RecordingDir();

    // 所有截图文件的固定目录（不存在会自动创建），跟 RecordingDir() 同一套
    // "跟 App 同级、写不进去就退到 Movies 目录"逻辑，只是子文件夹不同。
    static QString ScreenshotDir();

    // 拼一个截图文件的完整保存路径（含目录），命名规则跟录像文件名对齐，
    // 只是没有 part 分段序号。camera_name 可以传空字符串（比如回放网格里
    // 的格子没有"摄像头"这个概念）。
    static QString BuildScreenshotPath(int window_index, const QString& camera_name);

    // 当前选定的录像封装格式（TS/MKV/MP4），跨进程重启用 QSettings 持久化，
    // 默认 MP4。只影响之后新开始的录像——已经在录的窗口沿用它开始时的格式，
    // 录到当前段结束才会用新格式（不会打断正在写的文件）。
    static XRecordFormat Format();
    static void SetFormat(XRecordFormat format);

    // 开始录这一个窗口：url 是这一格当前摄像头的主码流地址，camera_name
    // 拼进录像文件名方便事后按名字找文件（这里负责过滤非法字符+截断，
    // XRecorder 收到的已经是处理好的），可以传空字符串。如果这个窗口
    // 已经在录了，直接返回 true、不做任何事（不会打断正在写的文件重开）。
    bool Start(int window_index, const QString& url, const QString& camera_name);

    void Stop(int window_index);

    // 应用退出前调用，停掉所有还在跑的录像，等它们的当前文件收尾写完。
    void StopAll();

    bool IsRecording(int window_index) const;

    // 从这次开始录像到现在经过的毫秒数，没在录的返回 0。
    long long ElapsedMs(int window_index) const;

private:
    RecordingManager() = default;
    ~RecordingManager();

    QMap<int, XRecorder*> recorders_;  // 窗口编号 -> 对应的后台录像
};

#endif  // XVIEWER_RECORDING_MANAGER_H_
