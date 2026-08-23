**中文** | [English](README.en.md)

# XPlayer (player_v2)

XPlayer 是一个 NVR 风格的多摄像头 RTSP 预览/录像播放器——对一个早期 Windows-only（MSVC/Qt5）应用的从零跨平台重写（CMake + Qt6）。它可以同时解码渲染多路实时 RTSP 流、录像到磁盘，并支持带 seek/单帧步进/倍速的录像回放。

目前在 **macOS** 上开发和验证。代码本身没有任何平台专属代码路径（纯 Qt6 Widgets + FFmpeg + SDL2），配好各平台的依赖后 Windows/Linux 预期也能构建——见下方 [平台状态](#平台状态)。

## 功能特性

- **多摄像头实时网格** ——1/4/9/16 画面分屏预览，从摄像头列表拖一项到任意格子即可打开（用辅码流，节省预览带宽）。
- **录像** ——按格子或整体手动开始/停止，用主码流保证画质，自动按段轮转（默认每段 45 分钟），写在可执行程序旁边。
- **录像回放** ——独立的第二个网格页面，用于浏览和播放录像文件：播放/暂停、±1 帧步进、±10 秒跳转、进度条拖动、可变倍速播放（0.25x–4x），并且**倍速播放时音频保调**（基于 FFmpeg 的 `atempo` 滤镜，WSOLA 算法——跟 VLC/mpv 用的是同一技术路线，所以加速播放时声音不会变尖变"忽悠"）。
- **直播和回放是完全独立的两个页面** ——互相切换不会停止后台正在跑的流或录像。
- **每格独立选择声音** ——单击某一格让它出声音；实时声音永远跟随当前单击选中的格子。
- **全屏** ——双击某一格让它填满整个网格区域，也支持真正的操作系统级全屏切换（不含摄像头列表面板）。
- **摄像头列表管理** ——增/改/删摄像头（名称 + 主/辅码流 RTSP 地址），通过 `QSettings` 持久化。
- **每格叠加信息** ——实时分辨率/帧率显示，录像状态及已录制时长提示。
- **可折叠侧边栏** ——可独立于全屏状态显示/隐藏摄像头或录像列表。

## 架构

一个仓库里两个 CMake 目标：

- **`xcodec`**（`src/xcodec/`）——解码/编码引擎，编译成共享库，不依赖 Qt。把 FFmpeg（解复用/解码/编码/复用/滤镜）和 SDL2（视频渲染 + 音频输出）包装在一个小巧的公开 C++ 接口后面（`include/xcodec/`：`XLiveStream`、`XPlayback`、`XRecorder`、`XVideoFrame`/`IXVideoSink`）。内部由多个线程化的流水线阶段（解复用 → 解码 → 渲染/音频）通过生产者/消费者模式串联而成，设计思路上承接前代的 `XThread` 结构，但代码是重新写的。
- **`xviewer`**（`src/xviewer/`）——Qt6 Widgets GUI 应用。`MainWindow` 持有两个独立的 `TileGridView` 页面（直播/回放），各自管理一组 `CameraTileWidget` 格子（基于 `QOpenGLWidget` 的 YUV 渲染器，每格还各自持有一个 `XLiveStream` 或 `XPlayback` 实例）。

另外还有一个小型命令行工具 `tools/xcodec_probe`，可以在不构建 GUI 的情况下直接跑 `xcodec`（打开一路流、打印帧统计、退出）——方便把解码引擎的问题和 GUI 的问题分开排查。

## 构建

### 依赖要求

- CMake 3.20+
- 支持 C++17 的编译器
- Qt 6.x，需要 `Widgets`、`OpenGLWidgets`、`Svg` 三个组件
- FFmpeg（开发/测试用的是 4.3.7），需要 `avformat`、`avcodec`、`avutil`、`avfilter`、`swresample`
- SDL2

FFmpeg 和 SDL2 都是以预编译开发包（头文件 + 导入库）的形式使用，构建脚本本身不会去拉取或打包它们。

### 配置与构建

```sh
cmake -B build \
  -DFFMPEG_ROOT=/path/to/ffmpeg-install \
  -DCOMMON_LIBS_ROOT=/path/to/sdl2-install \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<kit>/lib/cmake \
  -DPLAYER_V2_BUILD_XVIEWER=ON
cmake --build build
```

- `FFMPEG_ROOT` ——FFmpeg 安装根目录，需要包含 `include/` 和 `lib/`（头文件 + `avformat`/`avcodec`/`avutil`/`avfilter`/`swresample`）。
- `COMMON_LIBS_ROOT` ——SDL2 安装根目录，需要包含 `include/`/`lib/`。
- `CMAKE_PREFIX_PATH` ——Qt6 的 CMake 包目录，供 `find_package(Qt6 ...)` 查找。
- `PLAYER_V2_BUILD_XVIEWER`（默认 `OFF`）——是否构建 Qt6 GUI 应用；不开的话只会构建 `xcodec` + `xcodec_probe`。
- `PLAYER_V2_BUILD_TOOLS`（默认 `ON`）——是否构建 `xcodec_probe`。

构建产物（`xviewer` 应用包/可执行文件、`xcodec` 共享库、`xcodec_probe`）都在 `build/` 目录下。

## 下载预编译版本

每个版本 tag（如 `v0.1.0`）都会通过 GitHub Actions 自动构建 Windows/Linux/macOS 三个平台的可执行程序并发布到本仓库的 [Releases](../../releases) 页面，解压即可运行，不需要自己配置依赖。macOS 版本使用临时（ad-hoc）签名，首次打开需要在"系统设置 → 隐私与安全性"里手动放行一次。

## 目录结构

```
CMakeLists.txt          顶层：依赖路径、子目录接入
src/
  xcodec/                解码/编码引擎（共享库，不依赖 Qt）
    include/xcodec/       公开 API 头文件
    src/                  实现
  xviewer/                Qt6 GUI 应用
tools/
  xcodec_probe/           xcodec 引擎的命令行冒烟测试工具
```

## 平台状态

| 平台 | 状态 |
|---|---|
| macOS | 持续开发和验证中 |
| Windows | 尚未在真机上构建/验证过——源码里没有发现平台专属的阻碍，CI 流水线已配置好 Windows 构建 |
| Linux | 尚未在真机上构建/验证过——源码里没有发现平台专属的阻碍，CI 流水线已配置好 Linux 构建 |

## 许可证

尚未指定。
