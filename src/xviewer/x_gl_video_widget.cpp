#include "x_gl_video_widget.h"

#include <QMetaObject>
#include <cstring>

namespace {

const int kVertexAttr = 0;
const int kTexCoordAttr = 1;

const char* kVertexShader = R"(
attribute vec4 vertexIn;
attribute vec2 textureIn;
varying vec2 textureOut;
void main(void) {
    gl_Position = vertexIn;
    textureOut = textureIn;
}
)";

const char* kFragmentShader = R"(
varying vec2 textureOut;
uniform sampler2D tex_y;
uniform sampler2D tex_u;
uniform sampler2D tex_v;
void main(void) {
    vec3 yuv;
    vec3 rgb;
    yuv.x = texture2D(tex_y, textureOut).r;
    yuv.y = texture2D(tex_u, textureOut).r - 0.5;
    yuv.z = texture2D(tex_v, textureOut).r - 0.5;
    rgb = mat3(1.0,      1.0,      1.0,
               0.0,     -0.39465,  2.03211,
               1.13983, -0.58060,  0.0) * yuv;
    gl_FragColor = vec4(rgb, 1.0);
}
)";

}  // namespace

XGLVideoWidget::XGLVideoWidget(QWidget* parent) : QOpenGLWidget(parent) {}

XGLVideoWidget::~XGLVideoWidget() {
    makeCurrent();
    if (tex_y_) glDeleteTextures(1, &tex_y_);
    if (tex_u_) glDeleteTextures(1, &tex_u_);
    if (tex_v_) glDeleteTextures(1, &tex_v_);
    doneCurrent();
}

void XGLVideoWidget::initializeGL() {
    initializeOpenGLFunctions();

    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    program_.bindAttributeLocation("vertexIn", kVertexAttr);
    program_.bindAttributeLocation("textureIn", kTexCoordAttr);
    program_.link();
    program_.bind();

    static const GLfloat kVertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };
    static const GLfloat kTexCoords[] = {
        0.0f, 1.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f,
    };

    vao_.create();
    vao_.bind();
    glVertexAttribPointer(kVertexAttr, 2, GL_FLOAT, GL_FALSE, 0, kVertices);
    glEnableVertexAttribArray(kVertexAttr);
    glVertexAttribPointer(kTexCoordAttr, 2, GL_FLOAT, GL_FALSE, 0, kTexCoords);
    glEnableVertexAttribArray(kTexCoordAttr);
    vao_.release();

    uni_y_ = program_.uniformLocation("tex_y");
    uni_u_ = program_.uniformLocation("tex_u");
    uni_v_ = program_.uniformLocation("tex_v");

    glGenTextures(1, &tex_y_);
    glGenTextures(1, &tex_u_);
    glGenTextures(1, &tex_v_);
    for (GLuint tex : {tex_y_, tex_u_, tex_v_}) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void XGLVideoWidget::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

void XGLVideoWidget::EnsureTextures(int width, int height) {
    if (width == tex_width_ && height == tex_height_) {
        return;
    }
    tex_width_ = width;
    tex_height_ = height;

    glBindTexture(GL_TEXTURE_2D, tex_y_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, tex_u_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width / 2, height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, tex_v_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width / 2, height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
}

void XGLVideoWidget::UploadPlane(GLuint tex, int unit, const unsigned char* data,
                                  int stride, int width, int height) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void XGLVideoWidget::paintGL() {
    std::vector<unsigned char> y, u, v;
    int width = 0, height = 0, ys = 0, us = 0, vs = 0;
    bool has_frame = false;
    bool do_clear = false;

    {
        std::unique_lock<std::mutex> lock(frame_mutex_);
        do_clear = clear_requested_;
        clear_requested_ = false;
        if (has_pending_frame_) {
            y.swap(pending_y_);
            u.swap(pending_u_);
            v.swap(pending_v_);
            width = pending_width_;
            height = pending_height_;
            ys = pending_y_stride_;
            us = pending_u_stride_;
            vs = pending_v_stride_;
            has_pending_frame_ = false;
            has_frame = true;
        }
    }

    if (do_clear) {
        tex_width_ = tex_height_ = 0;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    // paintGL 之间不保证 shader program 还是当前绑定的（Qt 自己合成/
    // 混合这个 widget 的 FBO 时会切换 GL 状态），每帧都要重新 bind。
    program_.bind();

    if (has_frame && width > 0 && height > 0) {
        EnsureTextures(width, height);
        UploadPlane(tex_y_, 0, y.data(), ys, width, height);
        UploadPlane(tex_u_, 1, u.data(), us, width / 2, height / 2);
        UploadPlane(tex_v_, 2, v.data(), vs, width / 2, height / 2);
    } else if (tex_width_ > 0 && tex_height_ > 0) {
        // 这次 paintGL 没有新帧（比如同一窗口里另一路视频更新、悬浮的分
        // 辨率/录像状态文字每秒刷新一次，都会触发一次跟这一格视频帧无关
        // 的重绘）——沿用已经上传过的纹理重画上一帧，不要直接清空。之前
        // 这里在没有新帧时直接 return（画布已经被 glClear 清成黑色），
        // 每一次无关重绘都会让这一格闪一下黑屏；多路同时播放、或者加了
        // 每秒刷新的悬浮信息之后，这种无关重绘变得很频繁，看起来就是
        // 持续闪烁。同一个 GL 上下文里纹理内容在两次 paintGL 之间是保留
        // 的，只要重新绑定到对应纹理单元，不用重新上传像素数据。
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_y_);
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, tex_u_);
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, tex_v_);
    } else {
        return;  // 还没收到过任何帧（或者刚 Clear() 过），画布保持清空
    }

    program_.setUniformValue(uni_y_, 0);
    program_.setUniformValue(uni_u_, 1);
    program_.setUniformValue(uni_v_, 2);

    vao_.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    vao_.release();
}

void XGLVideoWidget::OnVideoFrame(const XVideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || !frame.y || !frame.u || !frame.v) {
        return;
    }

    {
        std::unique_lock<std::mutex> lock(frame_mutex_);
        size_t y_size = static_cast<size_t>(frame.y_stride) * frame.height;
        size_t uv_size = static_cast<size_t>(frame.u_stride) * (frame.height / 2);
        pending_y_.assign(frame.y, frame.y + y_size);
        pending_u_.assign(frame.u, frame.u + uv_size);
        pending_v_.assign(frame.v, frame.v + uv_size);
        pending_width_ = frame.width;
        pending_height_ = frame.height;
        pending_y_stride_ = frame.y_stride;
        pending_u_stride_ = frame.u_stride;
        pending_v_stride_ = frame.v_stride;
        has_pending_frame_ = true;
    }

    QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
}

void XGLVideoWidget::Clear() {
    {
        std::unique_lock<std::mutex> lock(frame_mutex_);
        clear_requested_ = true;
        has_pending_frame_ = false;
    }
    QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
}
