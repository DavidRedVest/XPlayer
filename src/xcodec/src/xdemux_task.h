#ifndef XDEMUX_TASK_H_
#define XDEMUX_TASK_H_

#include <string>

#include "xdemux.h"
#include "xtools.h"

// 解封装线程：不断读包、通过 XThread 处理链 Next() 转发给下一级（解码任务）。
// 只服务直播场景，不做倍速播放的节流（那是本地文件回放专属，不在这里）。
class XDemuxTask : public XThread {
public:
    int audio_index() { return demux_.audio_index(); }
    int video_index() { return demux_.video_index(); }

    void Main() override;

    bool Open(std::string url, int timeout_ms = 5000);

    std::shared_ptr<XPara> CopyVideoPara() { return demux_.CopyVideoPara(); }
    std::shared_ptr<XPara> CopyAudioPara() { return demux_.CopyAudioPara(); }

    void Exit() override;
    void Stop() override;

private:
    XDemux demux_;
    std::string url_;
    int timeout_ms_ = 0;
};

#endif  // XDEMUX_TASK_H_
