#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

#include "xcodec/xlive_stream.h"

namespace {
std::atomic<bool> g_stop{false};
void OnSigInt(int) { g_stop = true; }
}  // namespace

class PrintingSink : public IXVideoSink {
public:
    void OnVideoFrame(const XVideoFrame& frame) override {
        auto n = ++count_;
        if (n == 1 || n % 50 == 0) {
            std::printf("[frame %lld] %dx%d pts_ms=%lld\n",
                        static_cast<long long>(n), frame.width, frame.height,
                        frame.pts_ms);
        }
    }

    long long count() const { return count_; }

private:
    std::atomic<long long> count_{0};
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <rtsp-or-file-url> [seconds] [cycles]\n", argv[0]);
        return 1;
    }
    const char* url = argv[1];
    int seconds = argc > 2 ? std::atoi(argv[2]) : 10;
    int cycles = argc > 3 ? std::atoi(argv[3]) : 1;

    std::signal(SIGINT, OnSigInt);

    XLiveStream stream;

    for (int cycle = 1; cycle <= cycles && !g_stop; ++cycle) {
        PrintingSink sink;
        std::printf("=== cycle %d: opening %s ===\n", cycle, url);
        if (!stream.Open(url, &sink)) {
            std::fprintf(stderr, "Open failed\n");
            return 1;
        }
        std::printf("opened, running for %d seconds (Ctrl+C to stop early)\n", seconds);

        for (int i = 0; i < seconds && !g_stop; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::printf("closing ...\n");
        stream.Close();
        std::printf("=== cycle %d closed cleanly, total frames = %lld ===\n", cycle,
                    static_cast<long long>(sink.count()));
    }
    return 0;
}
