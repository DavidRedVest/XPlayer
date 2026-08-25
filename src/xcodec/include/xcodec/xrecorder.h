#ifndef XCODEC_XRECORDER_H_
#define XCODEC_XRECORDER_H_

#include "xcodec_api.h"

// 录像文件的封装格式。三种格式在这份代码里的处理路径完全一样（FFmpeg 按
// 文件名后缀猜格式，见 xmux.cpp），只有后缀不同，所以用枚举+小配置表，不
// 单独抽象成类层级。TS/MKV 都不强依赖"干净收尾才能播放"，异常中断（断电/
// 进程被杀）时前面已经写出去的部分仍然可读；MP4 的索引（moov）是收尾时才
// 一次性写完的，异常中断这一段基本报废，是唯一保留 MP4 作为默认值只是为了
// 兼容性最好、大多数播放器认得。
enum class XRecordFormat { kMp4, kTs, kMkv };

// 单路摄像头的后台录像：独立于 XLiveStream 的预览连接，自己开一路 demux，
// 按时长滚动切分写 mp4 文件到 save_dir 下。跟预览完全解耦——不受预览开关/
// 切换分屏影响，也不复用预览端已经在解码的数据。
class XCODEC_API XRecorder {
public:
    XRecorder();
    ~XRecorder();

    XRecorder(const XRecorder&) = delete;
    XRecorder& operator=(const XRecorder&) = delete;

    // 开始录像。跟 XLiveStream::Open 一样，真正的连接在后台线程里做，
    // Start() 立刻返回、不阻塞调用者；返回值只表示参数合法、请求已受理，
    // 不代表已经连上。
    // window_index：触发这次录像的画面窗口编号（从左到右、从上到下），
    // 会拼进文件名，方便事后知道这段录像是哪个窗口触发的。
    // camera_name：可选，拼进文件名方便事后按摄像头找文件；调用方负责
    // 传进来之前先做好合法字符过滤/长度截断（这里不做任何处理，原样
    // 使用），传空字符串/nullptr 就不在文件名里加这一段。
    // segment_seconds：每隔多久滚动一个新文件（默认 45 分钟一段，避免
    // 单个文件太大）；文件名格式固定为
    // "<日期>_<时间>_[<camera_name>_]win<window_index>_part<该窗口这次录像里的分段序号>.<format 对应后缀>"。
    bool Start(const char* url, const char* save_dir, int window_index, const char* camera_name,
               XRecordFormat format = XRecordFormat::kMp4, int segment_seconds = 2700);

    // 幂等、阻塞直到内部线程完全退出、当前文件收尾写完才返回。
    void Stop();

    bool IsRecording() const;

    // 从这次开始录像到现在经过的毫秒数（跨分段滚动持续累计，不会在切分新
    // 文件时被重置）；没在录像时返回 0。
    long long ElapsedMs() const;

private:
    struct Impl;
    Impl* impl_;
};

#endif  // XCODEC_XRECORDER_H_
