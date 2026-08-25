#ifndef XVIEWER_X_GL_VIDEO_WIDGET_H_
#define XVIEWER_X_GL_VIDEO_WIDGET_H_

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QString>
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

    // 请求把当前画面存成 PNG。真正的读取/转换/写文件发生在下一次 paintGL()
    // 里（必须在持有 GL 上下文的线程上做 glGetTexImage），所以是异步的——
    // 通过 SnapshotSaved 信号通知调用方成不成功。截的是纹理本身的分辨率
    // （也就是视频源分辨率），不是这一格在屏幕上显示的大小；从没收到过
    // 帧（黑屏）时直接以失败通知，不用等。
    void RequestSnapshot(const QString& path);

signals:
    void SnapshotSaved(const QString& path, bool ok);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    void EnsureTextures(int width, int height);
    void UploadPlane(GLuint tex, int unit, const unsigned char* data, int stride,
                      int width, int height);
    void SaveSnapshot(const QString& path);

    QOpenGLShaderProgram program_;
    // macOS 的 GL 实现即使在"legacy"2.1 上下文里，用 glVertexAttribPointer
    // 画图时也要求绑定一个 VAO，否则 glDrawArrays 会报 GL_INVALID_OPERATION
    // （Windows/Linux 上不需要，但绑了也没坏处）。
    QOpenGLVertexArrayObject vao_;
    // 顶点/纹理坐标数据必须放在真正的显存缓冲区（VBO）里，不能让
    // glVertexAttribPointer 直接指向 CPU 内存里的静态数组——绑了 VAO 之后
    // 用裸指针在 Windows 的 NVIDIA 驱动上会崩（驱动把这个 CPU 地址当成显存
    // 偏移量去解释，glDrawArrays 取顶点数据时访问越界）。macOS 的 Metal 后端
    // 容忍这种写法，掩盖了这个问题，Windows 上是真崩溃，不只是"未定义行为
    // 但凑合能跑"。
    QOpenGLBuffer vertex_buffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer texcoord_buffer_{QOpenGLBuffer::VertexBuffer};
    GLuint tex_y_ = 0, tex_u_ = 0, tex_v_ = 0;
    GLint uni_y_ = -1, uni_u_ = -1, uni_v_ = -1;
    int tex_width_ = 0, tex_height_ = 0;

    std::mutex frame_mutex_;
    std::vector<unsigned char> pending_y_, pending_u_, pending_v_;
    int pending_width_ = 0, pending_height_ = 0;
    int pending_y_stride_ = 0, pending_u_stride_ = 0, pending_v_stride_ = 0;
    bool has_pending_frame_ = false;
    bool clear_requested_ = false;

    // 只在 GUI 线程上碰（RequestSnapshot 由 GUI 线程调用，paintGL 也跑在
    // GUI 线程），不需要跟上面那些解码线程/GUI 线程共享的字段一样加锁。
    QString pending_snapshot_path_;
};

#endif  // XVIEWER_X_GL_VIDEO_WIDGET_H_
