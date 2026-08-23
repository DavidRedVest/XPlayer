#ifndef XDECODE_H_
#define XDECODE_H_

#include <vector>

#include "xcodec_engine.h"

struct AVPacket;
struct AVFrame;

class XDecode : public XCodec {
public:
    bool Send(const AVPacket* pkt);
    bool Recv(AVFrame* frame);
    std::vector<AVFrame*> End();
};

#endif  // XDECODE_H_
