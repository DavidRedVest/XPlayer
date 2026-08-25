#ifndef XVIEWER_FILE_UTILS_H_
#define XVIEWER_FILE_UTILS_H_

#include <QString>

// 在系统文件管理器里定位到这个文件。macOS/Windows 都有"打开文件夹并选中
// 这个文件"的原生方式；Linux 没有统一标准，退化成只打开所在文件夹。
void RevealInFileManager(const QString& path);

#endif  // XVIEWER_FILE_UTILS_H_
