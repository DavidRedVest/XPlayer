#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "xcodec/xrecorder.h"

namespace {
XRecordFormat ParseFormat(const char* s) {
    if (std::strcmp(s, "ts") == 0) return XRecordFormat::kTs;
    if (std::strcmp(s, "mkv") == 0) return XRecordFormat::kMkv;
    return XRecordFormat::kMp4;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                      "usage: %s <rtsp-url> <save-dir> <ts|mkv|mp4> [seconds] [camera-name]\n",
                      argv[0]);
        return 1;
    }
    const char* url = argv[1];
    const char* save_dir = argv[2];
    XRecordFormat format = ParseFormat(argv[3]);
    int seconds = argc > 4 ? std::atoi(argv[4]) : 5;
    const char* camera_name = argc > 5 ? argv[5] : "";

    XRecorder recorder;
    if (!recorder.Start(url, save_dir, 1, camera_name, format)) {
        std::fprintf(stderr, "Start failed\n");
        return 1;
    }
    std::printf("recording for %d seconds ...\n", seconds);
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    recorder.Stop();
    std::printf("done\n");
    return 0;
}
