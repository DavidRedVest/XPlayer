#include "file_utils.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

void RevealInFileManager(const QString& path) {
#if defined(Q_OS_MACOS)
    QProcess::startDetached("open", {"-R", path});
#elif defined(Q_OS_WIN)
    QProcess::startDetached("explorer", {"/select,", QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}
