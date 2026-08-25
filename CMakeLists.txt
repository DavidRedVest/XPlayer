cmake_minimum_required(VERSION 3.20)
project(player_v2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 第三方依赖路径（默认值只对本机 macOS 开发环境有效，其它机器/CI 用 -D 覆盖） ---
set(FFMPEG_ROOT "/Users/liszt/home/ffmpeg_dynamic_build/ffmpeg_4.3.7" CACHE PATH "FFmpeg 4.3.7 install root (contains include/ and lib/)")
set(COMMON_LIBS_ROOT "/Users/liszt/home/ffmpeg_dynamic_build/common_libs" CACHE PATH "SDL2/x264/x265/fdk-aac install root (contains include/ and lib/)")

# Qt6 官方安装器的 kit 路径，交给 find_package(Qt6) 用
list(APPEND CMAKE_PREFIX_PATH "/Users/liszt/Qt/6.8.3/macos/lib/cmake")

# 运行期直接从依赖库原地加载，不用每次手动设 DYLD_LIBRARY_PATH
set(CMAKE_BUILD_RPATH "${FFMPEG_ROOT}/lib" "${COMMON_LIBS_ROOT}/lib")
set(CMAKE_INSTALL_RPATH "${FFMPEG_ROOT}/lib" "${COMMON_LIBS_ROOT}/lib")
set(CMAKE_MACOSX_RPATH ON)

option(PLAYER_V2_BUILD_XVIEWER "Build the Qt6 xviewer application" OFF)
option(PLAYER_V2_BUILD_TOOLS "Build command-line dev tools (xcodec_probe)" ON)

add_subdirectory(src/xcodec)

if(PLAYER_V2_BUILD_TOOLS)
    add_subdirectory(tools/xcodec_probe)
    add_subdirectory(tools/xrecorder_probe)
endif()

if(PLAYER_V2_BUILD_XVIEWER)
    add_subdirectory(src/xviewer)
endif()
