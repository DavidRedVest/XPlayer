# XPlayer (player_v2)

XPlayer is an NVR-style multi-camera RTSP viewer and recorder — a from-scratch, cross-platform rebuild (CMake + Qt6) of a prior Windows-only (MSVC/Qt5) application. It decodes and renders multiple live RTSP streams simultaneously, records them to disk, and plays back recordings with seek/step/variable-speed controls.

Currently developed and verified on **macOS**. The codebase itself has no OS-specific code paths (pure Qt6 Widgets + FFmpeg + SDL2), so Windows and Linux builds are expected to work once each platform's dependencies are wired up — see [Platform status](#platform-status).

## Features

- **Multi-camera live grid** — 1/4/9/16-way split-screen preview, drag a camera from the list onto any tile to open it (sub-stream, for preview bandwidth).
- **Recording** — manual per-tile or per-grid start/stop, main-stream quality, automatic segment rotation (default 45 min/segment), written next to the executable.
- **Recording playback** — a second, independent grid page for browsing and playing back recorded files: play/pause, ±1 frame step, ±10s skip, seek slider, and variable playback speed (0.25x–4x) with **pitch-preserving audio** (FFmpeg's `atempo` filter, WSOLA-based — same technique VLC/mpv use — so sped-up audio doesn't sound high-pitched/chipmunked).
- **Live and playback are fully independent pages** — switching between them never stops streams or recordings running in the background.
- **Per-tile audio selection** — click a tile to make it the audible one; live audio always follows the current single-click selection.
- **Fullscreen** — double-click a tile to fill the grid with it, or use the real OS-level fullscreen toggle (excludes the camera list panel).
- **Camera list management** — add/edit/delete cameras (name + main/sub RTSP URLs), persisted via `QSettings`.
- **Per-tile overlays** — live resolution/fps readout, recording indicator with elapsed time.
- **Collapsible side panel** — hide/show the camera or recordings list independently of fullscreen.

## Architecture

Two CMake targets in one repo:

- **`xcodec`** (`src/xcodec/`) — the decode/encode engine, built as a shared library, no Qt dependency. Wraps FFmpeg (demux/decode/encode/mux/filter) and SDL2 (video render + audio output) behind a small public C++ interface (`include/xcodec/`: `XLiveStream`, `XPlayback`, `XRecorder`, `XVideoFrame`/`IXVideoSink`). Internally built around threaded pipeline stages (demux → decode → render/audio) chained via a producer/consumer pattern, mirroring the predecessor's `XThread`-based design but rewritten from scratch.
- **`xviewer`** (`src/xviewer/`) — the Qt6 Widgets GUI application. `MainWindow` owns two independent `TileGridView` pages (live / playback), each managing its own array of `CameraTileWidget` tiles (a `QOpenGLWidget`-based YUV renderer that also owns an `XLiveStream` or `XPlayback` instance per tile).

A small CLI tool, `tools/xcodec_probe`, exercises `xcodec` directly (open a stream, print frame stats, exit) without building the GUI — useful for isolating decode-engine issues from GUI issues.

## Building

### Requirements

- CMake 3.20+
- A C++17 compiler
- Qt 6.x, components: `Widgets`, `OpenGLWidgets`, `Svg`
- FFmpeg (built/tested against 4.3.7) with `avformat`, `avcodec`, `avutil`, `avfilter`, `swresample`
- SDL2

FFmpeg and SDL2 are consumed as pre-built dev packages (headers + import libs), not vendored or fetched by the build.

### Configure & build

```sh
cmake -B build \
  -DFFMPEG_ROOT=/path/to/ffmpeg-install \
  -DCOMMON_LIBS_ROOT=/path/to/sdl2-install \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<kit>/lib/cmake \
  -DPLAYER_V2_BUILD_XVIEWER=ON
cmake --build build
```

- `FFMPEG_ROOT` — install root containing FFmpeg's `include/` and `lib/` (headers + `avformat`/`avcodec`/`avutil`/`avfilter`/`swresample`).
- `COMMON_LIBS_ROOT` — install root containing SDL2's `include/`/`lib/`.
- `CMAKE_PREFIX_PATH` — Qt6's CMake package directory, so `find_package(Qt6 ...)` can locate it.
- `PLAYER_V2_BUILD_XVIEWER` (default `OFF`) — build the Qt6 GUI app. Leave off to build just `xcodec` + `xcodec_probe`.
- `PLAYER_V2_BUILD_TOOLS` (default `ON`) — build `xcodec_probe`.

Build output (the `xviewer` app bundle/binary, `xcodec` shared library, and `xcodec_probe`) lands under `build/`.

## Repository layout

```
CMakeLists.txt          top-level: dependency paths, subdirectory wiring
src/
  xcodec/                decode/encode engine (shared library, no Qt)
    include/xcodec/       public API headers
    src/                  implementation
  xviewer/                Qt6 GUI application
tools/
  xcodec_probe/           CLI smoke-test tool for the xcodec engine
```

## Platform status

| Platform | Status |
|---|---|
| macOS | Actively developed and verified |
| Windows | Not yet built/verified — no known platform-specific blockers in source, needs a Windows FFmpeg/SDL2/Qt6 toolchain wired up |
| Linux | Not yet built/verified — no known platform-specific blockers in source, needs a Linux FFmpeg/SDL2/Qt6 toolchain wired up |

## License

Not yet specified.
