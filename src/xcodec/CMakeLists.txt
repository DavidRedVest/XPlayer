set(XCODEC_PUBLIC_HEADERS
    include/xcodec/xcodec_api.h
    include/xcodec/xvideo_frame.h
    include/xcodec/xlive_stream.h
    include/xcodec/xrecorder.h
    include/xcodec/xplayback.h
    include/xcodec/xaudio_control.h
    include/xcodec/xsnapshot.h
)

set(XCODEC_SOURCES
    src/xtools.h
    src/xtools.cpp
    src/xformat.h
    src/xformat.cpp
    src/xdemux.h
    src/xdemux.cpp
    src/xdemux_task.h
    src/xdemux_task.cpp
    src/xmux.h
    src/xmux.cpp
    src/xmux_task.h
    src/xmux_task.cpp
    src/xcodec_engine.h
    src/xcodec_engine.cpp
    src/xdecode.h
    src/xdecode.cpp
    src/xdecode_task.h
    src/xdecode_task.cpp
    src/xaudio_play.h
    src/xaudio_play.cpp
    src/xaudio_tempo.h
    src/xaudio_tempo.cpp
    src/xaudio_control.cpp
    src/xlive_stream.cpp
    src/xrecorder.cpp
    src/xplayback.cpp
    src/xsnapshot.cpp
)

add_library(xcodec SHARED ${XCODEC_SOURCES} ${XCODEC_PUBLIC_HEADERS})

target_compile_definitions(xcodec PRIVATE XCODEC_EXPORTS)

target_include_directories(xcodec
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${FFMPEG_ROOT}/include
        ${COMMON_LIBS_ROOT}/include
)

find_library(AVFORMAT_LIB avformat PATHS ${FFMPEG_ROOT}/lib REQUIRED NO_DEFAULT_PATH)
find_library(AVCODEC_LIB avcodec PATHS ${FFMPEG_ROOT}/lib REQUIRED NO_DEFAULT_PATH)
find_library(AVUTIL_LIB avutil PATHS ${FFMPEG_ROOT}/lib REQUIRED NO_DEFAULT_PATH)
find_library(AVFILTER_LIB avfilter PATHS ${FFMPEG_ROOT}/lib REQUIRED NO_DEFAULT_PATH)
find_library(SWRESAMPLE_LIB swresample PATHS ${FFMPEG_ROOT}/lib REQUIRED NO_DEFAULT_PATH)
find_library(SWSCALE_LIB swscale PATHS ${FFMPEG_ROOT}/lib REQUIRED NO_DEFAULT_PATH)
find_library(SDL2_LIB SDL2 PATHS ${COMMON_LIBS_ROOT}/lib REQUIRED NO_DEFAULT_PATH)

target_link_libraries(xcodec PRIVATE ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${AVFILTER_LIB} ${SWRESAMPLE_LIB} ${SWSCALE_LIB} ${SDL2_LIB})

set_target_properties(xcodec PROPERTIES
    CXX_VISIBILITY_PRESET default
    INSTALL_RPATH "${FFMPEG_ROOT}/lib;${COMMON_LIBS_ROOT}/lib"
)
