#ifndef XVIEWER_RECORDING_MANAGER_H_
#define XVIEWER_RECORDING_MANAGER_H_

#include <QMap>
#include <QString>

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

    // 开始录这一个窗口：url 是这一格当前摄像头的主码流地址。如果这个窗口
    // 已经在录了，直接返回 true、不做任何事（不会打断正在写的文件重开）。
    bool Start(int window_index, const QString& url);

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
