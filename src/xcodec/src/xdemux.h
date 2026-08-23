#ifndef XDEMUX_H_
#define XDEMUX_H_

#include "xformat.h"

class XDemux : public XFormat {
public:
    // url 支持 rtsp:// 及本地文件路径。调用前需要先 set_time_out_ms()（如果
    // 想要超时保护），因为中断回调是在分配 AVFormatContext 时就装好的——
    // 这样连接阶段本身卡住也能被 RequestAbort()/超时打断，不用等连接成功
    // 之后才生效。
    bool Open(const char* url);

    bool Read(AVPacket* pkt);
    bool Seek(long long pts, int stream_index);
};

#endif  // XDEMUX_H_
