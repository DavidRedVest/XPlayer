#ifndef XCODEC_ENGINE_H_
#define XCODEC_ENGINE_H_

#include <mutex>

struct AVCodecContext;
struct AVFrame;

// AVCodecContext 的线程安全包装基类，编解码任务共用。
class XCodec {
public:
    virtual ~XCodec() = default;

    static AVCodecContext* Create(int codec_id, bool is_encode, int thread_count = 4);

    // c 的所有权转移给 this；如果 c_ 已经有值，先释放旧的。
    void set_c(AVCodecContext* c);

    bool SetOpt(const char* key, const char* val);
    bool SetOpt(const char* key, int val);

    bool Open();

    AVFrame* CreateFrame();

    // 丢弃解码器内部缓存的所有已解码但未输出的帧，用于 seek 之后清空状态。
    void Clear();

protected:
    AVCodecContext* c_ = nullptr;
    std::mutex mux_;
};

#endif  // XCODEC_ENGINE_H_
