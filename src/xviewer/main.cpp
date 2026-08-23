#include <QApplication>
#include <QIcon>

#include "main_window.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("xviewer");
    QApplication::setApplicationName("xviewer");
    app.setWindowIcon(QIcon(":/player.svg"));

    MainWindow window;
    window.show();

    return app.exec();
}
