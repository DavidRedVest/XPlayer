#include "xaudio_play.h"

#include <SDL2/SDL.h>
#include <cstring>
#include <iostream>

#include "xtools.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

using namespace std;

namespace {
constexpr unsigned short kAudioS16 = 0x8010;  // AUDIO_S16LSB

// 只是给 SDL_OpenAudio 的初始"请求"，不代表最终会用这个规格——实际规格
// 以 Open() 回填到 XAudioSpec 里的值为准（见 xaudio_play.h 里的说明）。
constexpr int kRequestRate = 44100;
constexpr int kRequestChannels = 2;
}  // namespace

class CXAudioPlay : public XAudioPlay {
public:
    bool Open(XAudioSpec& spec) override {
        // 先整个退出再重新初始化子系统，确保每次都是全新状态，不会残留上一
        // 次打开的设备参数。SDL_OpenAudio（老 API）内部会在需要时自动
        // InitSubSystem，换成 SDL_OpenAudioDevice 之后这一步不再是隐式的，
        // 漏掉这行会导致每次 Open() 都以 "Audio subsystem is not
        // initialized" 失败（本地实测复现）。
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        SDL_InitSubSystem(SDL_INIT_AUDIO);

        SDL_AudioSpec desired, obtained;
        SDL_zero(desired);
        desired.freq = spec.freq;
        desired.format = spec.format;
        desired.channels = spec.channels;
        desired.samples = spec.samples;
        desired.userdata = this;
        desired.callback = AudioCallback;
        // 用 SDL_OpenAudioDevice（不是老的 SDL_OpenAudio）+ 只允许频率/
        // 声道变化、不允许格式变化：下游 SDL_MixAudio() 只认 S16、重采样器
        // 也写死输出 S16，采样格式绝对不能被 SDL 静默换掉——SDL_OpenAudio
        // 传非空 obtained 时会把格式变更权完全交给 SDL，Windows 上 WASAPI
        // 常见默认给 F32（实测复现：请求 S16，拿到的是 33056=AUDIO_F32），
        // 拿到 S16 字节却被设备当 F32 解释，声音基本等于没有。限制
        // allowed_changes 之后 SDL 自己的音频子系统会在内部做好格式转换，
        // 交给我们的回调看到的还是稳定的 S16，不用在这个类里另外适配。
        device_id_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained,
                                          SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                                              SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
        if (device_id_ == 0) {
            cerr << "SDL_OpenAudioDevice failed: " << SDL_GetError() << endl;
            return false;
        }
        spec.freq = obtained.freq;
        spec.channels = obtained.channels;
        spec.format = obtained.format;
        SDL_PauseAudioDevice(device_id_, 0);
        return true;
    }

    void Close() override {
        if (device_id_ != 0) {
            SDL_CloseAudioDevice(device_id_);
            device_id_ = 0;
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        {
            unique_lock<mutex> lock(mux_);
            audio_datas_.clear();
        }
        ResetResampler();
    }

    void Callback(unsigned char* stream, int len) override {
        SDL_memset(stream, 0, len);
        unique_lock<mutex> lock(mux_);

        int mixed_size = 0;
        while (mixed_size < len && !audio_datas_.empty()) {
            // 必须用引用：`auto buf = audio_datas_.front()` 是拷贝的话，
            // 下面 buf.offset 的更新只会改到拷贝上，队列里真正那条数据的
            // offset 永远停在 0——只要一条数据没能在一次回调里正好用完，
            // 下次回调就会从头重新读它，同一段声音反复播放，听起来就是
            // 回声/卡顿循环。
            auto& buf = audio_datas_.front();
            int size = static_cast<int>(buf.data.size()) - buf.offset;
            int need_size = len - mixed_size;
            if (size > need_size) {
                size = need_size;
            }
            SDL_MixAudio(stream + mixed_size, buf.data.data() + buf.offset, size, volume_);
            buf.offset += size;
            mixed_size += size;
            if (buf.offset >= static_cast<int>(buf.data.size())) {
                audio_datas_.pop_front();
            }
        }
    }

private:
    SDL_AudioDeviceID device_id_ = 0;
};

XAudioPlay* XAudioPlay::Instance() {
    static CXAudioPlay instance;
    static bool inited = (SDL_Init(SDL_INIT_AUDIO) == 0);
    (void)inited;
    return &instance;
}

XAudioPlay::~XAudioPlay() { ResetResampler(); }

void XAudioPlay::Clear() {
    unique_lock<mutex> lock(mux_);
    audio_datas_.clear();
}

void XAudioPlay::ResetResampler() {
    if (swr_) {
        swr_free(&swr_);
    }
    src_format_ = -1;
    src_rate_ = 0;
    src_channels_ = 0;
    device_open_ = false;
    device_rate_ = 0;
    device_channels_ = 0;
}

void XAudioPlay::PushPcm(const unsigned char* data, int size, long long pts) {
    unique_lock<mutex> lock(mux_);
    audio_datas_.push_back(XData());
    audio_datas_.back().pts = pts;
    audio_datas_.back().data.assign(data, data + size);
}

void XAudioPlay::Push(AVFrame* frame) {
    if (!frame || !frame->data[0] || frame->format < 0 || frame->channels <= 0) {
        return;
    }

    if (!device_open_) {
        XAudioSpec spec;
        spec.freq = kRequestRate;
        spec.channels = kRequestChannels;
        spec.format = kAudioS16;
        if (!Open(spec)) {
            LOGERROR("XAudioPlay::Push: Open device failed");
            return;
        }
        device_open_ = true;
        // 用设备真正给到的规格，不是我们请求的那个。
        device_rate_ = spec.freq;
        device_channels_ = spec.channels;
        if (spec.format != kAudioS16) {
            // 极少见：驱动不支持我们请求的 S16。下面 swr 的输出固定写死
            // S16，跟设备实际格式不一致又会回到"按错误格式解释字节"的
            // 老问题，这里先明确报出来，不要静默播放噪音。
            LOGERROR("XAudioPlay: device did not grant AUDIO_S16 (got format="
                     << spec.format << "), playback will likely be corrupted");
        }
        LOGINFO("XAudioPlay: device opened at rate=" << device_rate_
                << " channels=" << device_channels_);
    }

    bool changed = frame->format != src_format_ || frame->sample_rate != src_rate_ ||
                    frame->channels != src_channels_;
    if (changed) {
        if (swr_) {
            swr_free(&swr_);
        }
        src_format_ = frame->format;
        src_rate_ = frame->sample_rate;
        src_channels_ = frame->channels;

        auto in_layout = av_get_default_channel_layout(src_channels_);
        auto out_layout = av_get_default_channel_layout(device_channels_);
        swr_ = swr_alloc_set_opts(nullptr, out_layout, AV_SAMPLE_FMT_S16, device_rate_, in_layout,
                                   static_cast<AVSampleFormat>(src_format_), src_rate_, 0, nullptr);
        if (!swr_ || swr_init(swr_) < 0) {
            LOGERROR("XAudioPlay::Push: swr_init failed, format=" << src_format_
                     << " rate=" << src_rate_ << " channels=" << src_channels_);
            if (swr_) {
                swr_free(&swr_);
            }
            return;
        }
        LOGINFO("XAudioPlay: source format=" << src_format_ << " rate=" << src_rate_
                << " channels=" << src_channels_ << " -> device rate=" << device_rate_
                << " channels=" << device_channels_);
    }

    if (!swr_) {
        return;
    }

    int out_samples = static_cast<int>(av_rescale_rnd(
        swr_get_delay(swr_, src_rate_) + frame->nb_samples, device_rate_, src_rate_, AV_ROUND_UP));
    vector<unsigned char> out_buf(static_cast<size_t>(out_samples) * device_channels_ * 2);
    unsigned char* out_ptr = out_buf.data();
    int converted = swr_convert(swr_, &out_ptr, out_samples,
                                 const_cast<const unsigned char**>(frame->data), frame->nb_samples);
    if (converted <= 0) {
        return;
    }
    int out_size = converted * device_channels_ * 2;
    PushPcm(out_buf.data(), out_size, frame->pts);
}
