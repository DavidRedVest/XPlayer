#ifndef XVIEWER_CAMERA_CONFIG_H_
#define XVIEWER_CAMERA_CONFIG_H_

#include <QList>
#include <QString>

// 摄像头信息：地址字段不假设协议——现在测的是 RTSP，以后可能是 TS 流或者
// 自定义 UDP 数据，XLiveStream::Open() 内部走 avformat_open_input，本来就
// 不挑协议，这里不用为此做任何改动。
struct CameraInfo {
    int id = -1;  // 由 CameraConfig::Add 分配，跨编辑/排序/删除保持不变。
    QString name;
    QString url;       // 主码流（录像用这个，画质高）
    QString sub_url;    // 辅码流（网格预览用这个，分辨率低更合适）
};

// 摄像头列表，单例，持久化到 QSettings（跨启动保留，不用像 player_v1
// 那样固定存一个 test.db 在当前工作目录）。
class CameraConfig {
public:
    static CameraConfig* Instance();

    void Add(const CameraInfo& info);
    CameraInfo Get(int index) const;
    void Set(int index, const CameraInfo& info);
    void Remove(int index);
    int Count() const;

private:
    CameraConfig();

    void Load();
    void Save();

    QList<CameraInfo> cams_;
    int next_id_ = 1;
};

#endif  // XVIEWER_CAMERA_CONFIG_H_
