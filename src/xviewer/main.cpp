#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>

#include "main_window.h"

int main(int argc, char** argv) {
    // XGLVideoWidget 的着色器用的是旧式 GLSL（attribute/varying/gl_FragColor），
    // 之前从没显式设置过 QSurfaceFormat，完全依赖各平台 Qt 的默认 profile/
    // 版本——macOS 和 Windows 的默认值不一定一致，如果拿到一个不兼容旧语法
    // 的 core profile，着色器编译/链接会静默失败，后续的绘制调用在某些
    // Windows 驱动上可能直接崩溃。显式钉死成 2.1 兼容 profile，两个平台
    // 用同一个、明确兼容这套旧语法的上下文，不再各自猜测默认值。
    QSurfaceFormat format;
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setOrganizationName("xviewer");
    QApplication::setApplicationName("xviewer");
    app.setWindowIcon(QIcon(":/player.svg"));

    MainWindow window;
    window.show();

    return app.exec();
}
