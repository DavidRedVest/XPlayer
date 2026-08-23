#include "xaudio_tempo.h"

#include <cstdio>
#include <string>
#include <vector>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

namespace {
// atempo 单级只认 [0.5, 2.0] 的倍速，超出范围就拆成多级、每级都落在合法
// 范围内（比如 4.0 拆成两级 2.0 * 2.0）。当前 UI 最多到 4x、最低到 0.5x，
// 实际只有 4x 会触发拆分，这里写成通用循环是为了以后倍速选项变化时也不用
// 再改这部分逻辑。
std::string BuildFilterChain(double speed) {
    std::vector<double> stages;
    double remaining = speed;
    while (remaining > 2.0) {
        stages.push_back(2.0);
        remaining /= 2.0;
    }
    while (remaining < 0.5) {
        stages.push_back(0.5);
        remaining /= 0.5;
    }
    stages.push_back(remaining);

    std::string desc;
    char buf[32];
    for (size_t i = 0; i < stages.size(); ++i) {
        if (i > 0) {
            desc += ",";
        }
        snprintf(buf, sizeof(buf), "atempo=%.6f", stages[i]);
        desc += buf;
    }
    return desc;
}
}  // namespace

XAudioTempo::~XAudioTempo() {
    std::lock_guard<std::mutex> lock(mux_);
    TeardownGraph();
}

void XAudioTempo::TeardownGraph() {
    if (graph_) {
        avfilter_graph_free(&graph_);
    }
    src_ctx_ = nullptr;
    sink_ctx_ = nullptr;
    cur_format_ = -1;
    cur_rate_ = 0;
    cur_channels_ = 0;
    cur_speed_ = 1.0;
}

bool XAudioTempo::EnsureGraph(AVFrame* frame, double speed) {
    TeardownGraph();

    graph_ = avfilter_graph_alloc();
    if (!graph_) {
        return false;
    }

    int64_t channel_layout = frame->channel_layout != 0
                                  ? static_cast<int64_t>(frame->channel_layout)
                                  : av_get_default_channel_layout(frame->channels);

    char args[256];
    snprintf(args, sizeof(args),
             "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%llx",
             frame->sample_rate, frame->sample_rate,
             av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame->format)),
             static_cast<unsigned long long>(channel_layout));

    const AVFilter* abuffer = avfilter_get_by_name("abuffer");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffer || !abuffersink) {
        TeardownGraph();
        return false;
    }

    if (avfilter_graph_create_filter(&src_ctx_, abuffer, "src", args, nullptr, graph_) < 0) {
        TeardownGraph();
        return false;
    }
    if (avfilter_graph_create_filter(&sink_ctx_, abuffersink, "sink", nullptr, nullptr, graph_) < 0) {
        TeardownGraph();
        return false;
    }

    // 跟 FFmpeg 官方 doc/examples/filtering_audio.c 里的写法一样：
    // "outputs"（命名成 "in"）挂在 abuffer 的输出端，代表滤镜字符串描述的
    // 那条链"从这里接输入"；"inputs"（命名成 "out"）挂在 abuffersink 的
    // 输入端，代表那条链"从这里接输出"——图里两个方向的命名是相对滤镜串
    // 本身来说的，不是相对 abuffer/abuffersink。
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    if (!outputs || !inputs) {
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        TeardownGraph();
        return false;
    }
    outputs->name = av_strdup("in");
    outputs->filter_ctx = src_ctx_;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = sink_ctx_;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    std::string filter_desc = BuildFilterChain(speed);
    int ret = avfilter_graph_parse_ptr(graph_, filter_desc.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0) {
        TeardownGraph();
        return false;
    }

    if (avfilter_graph_config(graph_, nullptr) < 0) {
        TeardownGraph();
        return false;
    }

    cur_format_ = frame->format;
    cur_rate_ = frame->sample_rate;
    cur_channels_ = frame->channels;
    cur_speed_ = speed;
    return true;
}

bool XAudioTempo::Push(AVFrame* frame, double speed) {
    if (!frame) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mux_);
    bool params_changed = frame->format != cur_format_ || frame->sample_rate != cur_rate_ ||
                          frame->channels != cur_channels_;
    if (!graph_ || params_changed || speed != cur_speed_) {
        if (!EnsureGraph(frame, speed)) {
            return false;
        }
    }
    return av_buffersrc_add_frame(src_ctx_, frame) >= 0;
}

AVFrame* XAudioTempo::PopFrame() {
    std::lock_guard<std::mutex> lock(mux_);
    if (!sink_ctx_) {
        return nullptr;
    }
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        return nullptr;
    }
    if (av_buffersink_get_frame(sink_ctx_, frame) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }
    return frame;
}

void XAudioTempo::Reset() {
    std::lock_guard<std::mutex> lock(mux_);
    TeardownGraph();
}
