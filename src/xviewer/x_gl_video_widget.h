#ifndef XVIEWER_X_GL_VIDEO_WIDGET_H_
#define XVIEWER_X_GL_VIDEO_WIDGET_H_

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <mutex>
#include <vector>

#include "xcodec/xvideo_frame.h"

// YUV420P 视频渲染控件：实现 IXVideoSink，在解码线程的回调里只做一次内存
// 拷贝（快、不阻塞），真正的纹理上传/绘制留到 Qt 主线程的 paintGL() 里做，
// 通过 QMetaObject::invokeMethod 排队触发，不在解码线程上碰任何 GL 状态。
class XGLVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions, public IXVideoSink {
    Q_OBJECT

public:
    explicit XGLVideoWidget(QWidget* parent = nullptr);
    ~XGLVideoWidget() override;

    void OnVideoFrame(const XVideoFrame& frame) override;

    // 清空画面（比如断开连接后），下一帧 paintGL 会画黑屏。
    void Clear();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    void EnsureTextures(int width, int height);
    void UploadPlane(GLuint tex, int unit, const unsigned char* data, int stride,
                      int width, int height);

    QOpenGLShaderProgram program_;
    // macOS 的 GL 实现即使在"legacy"2.1 上下文里，用 glVertexAttribPointer
    // 画图时也要求绑定一个 VAO，否则 glDrawArrays 会报 GL_INVALID_OPERATION
    // （Windows/Linux 上不需要）。
    QOpenGLVertexArrayObject vao_;
    GLuint tex_y_ = 0, tex_u_ = 0, tex_v_ = 0;
    GLint uni_y_ = -1, uni_u_ = -1, uni_v_ = -1;
    int tex_width_ = 0, tex_height_ = 0;

    std::mutex frame_mutex_;
    std::vector<unsigned char> pending_y_, pending_u_, pending_v_;
    int pending_width_ = 0, pending_height_ = 0;
    int pending_y_stride_ = 0, pending_u_stride_ = 0, pending_v_stride_ = 0;
    bool has_pending_frame_ = false;
    bool clear_requested_ = false;
    bool logged_first_frame_ = false;             // 排查 Windows 崩溃用的一次性诊断埋点
    bool logged_first_frame_error_check_ = false;
};

#endif  // XVIEWER_X_GL_VIDEO_WIDGET_H_
