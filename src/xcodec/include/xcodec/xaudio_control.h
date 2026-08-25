#ifndef XCODEC_XAUDIO_CONTROL_H_
#define XCODEC_XAUDIO_CONTROL_H_

#include "xcodec_api.h"

// 全局音频音量控制——XAudioPlay 是进程内单例（同一时刻只有一路真正在
// 发声，见 XLiveStream/XPlayback 的静音切换设计），音量天然也是个全局
// 概念，不需要挂在某一路直播/回放实例上。这里只是把内部实现细节
// （xaudio_play.h 里的 XAudioPlay::set_volume/volume，SDL 的 0-128 范围）
// 转换成对外的公开接口，用更直观的 0-100 百分比。
XCODEC_API void SetAudioVolume(int volume_percent);
XCODEC_API int GetAudioVolume();

#endif  // XCODEC_XAUDIO_CONTROL_H_
