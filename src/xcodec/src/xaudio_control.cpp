#include "xcodec/xaudio_control.h"

#include <algorithm>

#include "xaudio_play.h"

namespace {
// SDL_MIX_MAXVOLUME，XAudioPlay::volume_ 内部就是按这个范围存的（见
// xaudio_play.h/.cpp）——0-100 的百分比只在这个公开接口的边界上出现，
// 换算成内部范围之后就不再关心这个常量了。
constexpr int kSdlMaxVolume = 128;
}  // namespace

void SetAudioVolume(int volume_percent) {
    volume_percent = std::clamp(volume_percent, 0, 100);
    int sdl_volume = (volume_percent * kSdlMaxVolume + 50) / 100;  // 四舍五入
    XAudioPlay::Instance()->set_volume(sdl_volume);
}

int GetAudioVolume() {
    int sdl_volume = XAudioPlay::Instance()->volume();
    return (sdl_volume * 100 + kSdlMaxVolume / 2) / kSdlMaxVolume;  // 四舍五入
}
