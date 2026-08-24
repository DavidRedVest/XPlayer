#ifndef XAUDIO_PLAY_H_
#define XAUDIO_PLAY_H_

#include <list>
#include <mutex>
#include <vector>

struct AVFrame;
struct SwrContext;

struct XAudioSpec {
    int freq = 44100;
    unsigned short format = 0x8010;  // AUDIO_S16LSB
    unsigned char channels = 2;
    unsigned short samples = 1024;
};

struct XData {
    std::vector<unsigned char> data;
    int offset = 0;
    long long pts = 0;
};

// 音频播放（SDL 音频设备），单例：一路直播同一时间只有一份系统音频输出。
//
// 不信任容器/SDP 里预先声明的采样格式：RTSP 音频常常在真正解码出第一帧
// 之前就拿不到准确的采样格式（AVCodecParameters::format 可能是未知值），
// 而且 HE-AAC 之类带 SBR 的编码解码后的真实采样率会跟编码声明的不一样。
// 统一策略：只认解码出来的 AVFrame 里的真实 format/sample_rate/channels，
// 用 libswresample 转换成固定的 S16 交错格式再喂给 SDL，从源头避免"按
// 错误格式解释字节"导致的噪音。
class XAudioPlay {
public:
    static XAudioPlay* Instance();
    virtual ~XAudioPlay();

    // 送入解码出来的一帧音频；首次调用或格式变化时会自动（重新）配置
    // 重采样器和播放设备。
    void Push(AVFrame* frame);

    // 只清掉排队里还没播出去的 PCM，不关设备——暂停/seek/单帧步进时用：
    // 停止/跳转喂新数据只能防止"以后"还有声音继续进来，已经排在队列里
    // 的那几百毫秒缓冲还是会被 SDL 回调正常播完，表现出来就是"点了暂停
    // 画面停了，但声音还在响一小段"。Close() 那种整个关设备的方式又太重、
    // 快速恢复播放/连续 seek 时代价不必要。
    void Clear();

    virtual bool Open(XAudioSpec& spec) = 0;
    virtual void Close() = 0;

    void set_volume(int v) { volume_ = v; }

protected:
    XAudioPlay() = default;

    virtual void Callback(unsigned char* stream, int len) = 0;
    static void AudioCallback(void* userdata, unsigned char* stream, int len) {
        static_cast<XAudioPlay*>(userdata)->Callback(stream, len);
    }

    // Close() 的公共部分：释放重采样器状态，子类的 Close() 需要额外调用它。
    void ResetResampler();

    std::list<XData> audio_datas_;
    std::mutex mux_;
    unsigned char volume_ = 128;

    // 设备真正打开后给到的采样格式（SDL_AudioFormat 值，比如
    // AUDIO_S16SYS/AUDIO_F32SYS）——子类 Callback() 混音时需要知道这个
    // 才能正确处理非 S16 的情况（SDL_MixAudio 只认 S16，别的格式要自己
    // 按格式缩放音量）。
    unsigned short device_format_ = 0x8010;  // AUDIO_S16LSB，Open() 之后回填成真实值

private:
    void PushPcm(const unsigned char* data, int size, long long pts);

    SwrContext* swr_ = nullptr;
    int src_format_ = -1;
    int src_rate_ = 0;
    int src_channels_ = 0;
    bool device_open_ = false;

    // 设备真正打开后的参数（不是我们请求的那个）——不同机器/输出设备的
    // 原生采样率差异很大（比如外接显示器或某些内建输出，实测原生是
    // 96000Hz，不是常见的 44100/48000），如果只按自己请求的规格重采样、
    // 又让 SDL/系统在背后按设备原生率再转一次，这第二层转换质量没法控制，
    // 实测会导致放不出声音。所以 Open() 成功后要把 SDL 实际给到的规格回填
    // 到这里，重采样目标改成跟设备完全一致，避免任何"隐藏"的二次转换。
    int device_rate_ = 0;
    int device_channels_ = 0;
};

#endif  // XAUDIO_PLAY_H_
