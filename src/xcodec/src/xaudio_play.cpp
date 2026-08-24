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
constexpr unsigned short kAudioF32 = 0x8120;  // AUDIO_F32LSB / AUDIO_F32SYS（小端机器上等价）
constexpr unsigned short kAudioS32 = 0x8020;  // AUDIO_S32LSB
constexpr unsigned short kAudioU8 = 0x0008;   // AUDIO_U8

// 只是给 SDL_OpenAudio 的初始"请求"，不代表最终会用这个规格——实际规格
// 以 Open() 回填到 XAudioSpec 里的值为准（见 xaudio_play.h 里的说明）。
constexpr int kRequestRate = 44100;
constexpr int kRequestChannels = 2;

// 把设备实际给到的 SDL_AudioFormat 换算成重采样器要用的目标格式——不能
// 像以前那样固定写死 AV_SAMPLE_FMT_S16：Windows 上 WASAPI 常见只肯给
// AUDIO_F32，写死 S16 意味着重采样器吐出来的字节会被设备当浮点数解释，
// 基本等于没声音。未识别的格式退回 S16（至少不会比以前更差）。
AVSampleFormat SdlFormatToAvSampleFormat(unsigned short sdl_format) {
    switch (sdl_format) {
        case kAudioF32:
            return AV_SAMPLE_FMT_FLT;
        case kAudioS32:
            return AV_SAMPLE_FMT_S32;
        case kAudioU8:
            return AV_SAMPLE_FMT_U8;
        case kAudioS16:
        default:
            return AV_SAMPLE_FMT_S16;
    }
}
}  // namespace

class CXAudioPlay : public XAudioPlay {
public:
    bool Open(XAudioSpec& spec) override {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);

        SDL_AudioSpec desired, obtained;
        SDL_zero(desired);
        desired.freq = spec.freq;
        desired.format = spec.format;
        desired.channels = spec.channels;
        desired.samples = spec.samples;
        desired.userdata = this;
        desired.callback = AudioCallback;
        // 传非空的 obtained：不让 SDL 在背后按自己猜的方式把数据转换成
        // 设备原生格式——那一步的转换质量不可控。而是拿到设备真正的规格，
        // 让上层用 libswresample 一次性、可控地转换到位。
        //
        // 曾经改成 SDL_OpenAudioDevice + 限制 allowed_changes 强制要 S16，
        // 结果 Windows 和 macOS 上都变成"设备打开成功、日志也正常，但实际
        // 放不出声音"——两边都不可控地更糟，已经改回这个已经验证过能用的
        // SDL_OpenAudio 写法。设备实际给到的格式（不一定是 S16）现在改成
        // 在 Push() 里让重采样器直接按这个真实格式转换到位，而不是像以前
        // 那样重采样器写死输出 S16、格式对不上就放弃治疗。
        if (SDL_OpenAudio(&desired, &obtained) < 0) {
            cerr << "SDL_OpenAudio failed: " << SDL_GetError() << endl;
            return false;
        }
        spec.freq = obtained.freq;
        spec.channels = obtained.channels;
        spec.format = obtained.format;
        SDL_PauseAudio(0);
        return true;
    }

    void Close() override {
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

        // SDL_MixAudio() 只认 S16——如果设备实际给的是别的格式（比如
        // Windows WASAPI 常见的 AUDIO_F32），Push() 里的重采样器现在已经
        // 直接按这个真实格式转换好了，这里只需要按格式做"乘音量再复制"，
        // 不需要真正意义上的多路混音（同一时刻只有一路在真正播放，dst 在
        // 这之前已经清零，逐段写入互不重叠）。
        bool is_s16 = (device_format_ == kAudioS16);
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
            if (is_s16) {
                SDL_MixAudio(stream + mixed_size, buf.data.data() + buf.offset, size, volume_);
            } else {
                MixScaleCopyF32(stream + mixed_size, buf.data.data() + buf.offset, size, volume_);
            }
            buf.offset += size;
            mixed_size += size;
            if (buf.offset >= static_cast<int>(buf.data.size())) {
                audio_datas_.pop_front();
            }
        }
    }

private:
    // 按 32 位浮点样本做音量缩放再拷贝——SDL_MixAudio 不支持这个格式，
    // 自己实现。dst 已经清零过、每次调用写入的区间互不重叠，直接赋值
    // 等价于叠加。size 按字节算，调用方保证是 4 的倍数（一帧样本的整数倍）。
    static void MixScaleCopyF32(unsigned char* dst, const unsigned char* src, int size,
                                 unsigned char volume) {
        float scale = volume / 128.0f;
        int n = size / static_cast<int>(sizeof(float));
        const float* s = reinterpret_cast<const float*>(src);
        float* d = reinterpret_cast<float*>(dst);
        for (int i = 0; i < n; ++i) {
            d[i] = s[i] * scale;
        }
    }
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
    device_format_ = kAudioS16;
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
        device_format_ = spec.format;
        if (spec.format != kAudioS16) {
            // 不再是"报错然后放弃治疗"——下面 Push() 里重采样器的输出目标
            // 已经跟着 device_format_ 走，这里只是留个记录方便确认设备到底
            // 给了什么格式。
            LOGINFO("XAudioPlay: device granted format=" << spec.format
                    << " (not AUDIO_S16, resampler target adjusted accordingly)");
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
        // 目标格式跟着设备实际给到的走（device_format_），不是写死 S16——
        // 这样不管设备给的是 S16 还是 F32，重采样器吐出来的字节都能直接
        // 按那个格式解释，Callback() 混音时再按同一个格式处理。
        swr_ = swr_alloc_set_opts(nullptr, out_layout, SdlFormatToAvSampleFormat(device_format_),
                                   device_rate_, in_layout, static_cast<AVSampleFormat>(src_format_),
                                   src_rate_, 0, nullptr);
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

    int bytes_per_sample = av_get_bytes_per_sample(SdlFormatToAvSampleFormat(device_format_));
    int out_samples = static_cast<int>(av_rescale_rnd(
        swr_get_delay(swr_, src_rate_) + frame->nb_samples, device_rate_, src_rate_, AV_ROUND_UP));
    vector<unsigned char> out_buf(static_cast<size_t>(out_samples) * device_channels_ * bytes_per_sample);
    unsigned char* out_ptr = out_buf.data();
    int converted = swr_convert(swr_, &out_ptr, out_samples,
                                 const_cast<const unsigned char**>(frame->data), frame->nb_samples);
    if (converted <= 0) {
        return;
    }
    int out_size = converted * device_channels_ * bytes_per_sample;
    PushPcm(out_buf.data(), out_size, frame->pts);
}
