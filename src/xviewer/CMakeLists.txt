set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Svg)
find_package(OpenGL REQUIRED)

qt_add_executable(xviewer
    main.cpp
    main_window.h
    main_window.cpp
    x_gl_video_widget.h
    x_gl_video_widget.cpp
    camera_tile_widget.h
    camera_tile_widget.cpp
    camera_config.h
    camera_config.cpp
    camera_edit_dialog.h
    camera_edit_dialog.cpp
    file_utils.h
    file_utils.cpp
    recording_manager.h
    recording_manager.cpp
    tile_grid_view.h
    tile_grid_view.cpp
    playback_control_bar.h
    playback_control_bar.cpp
    resources/resources.qrc
    resources/player.icns
)

target_link_libraries(xviewer PRIVATE
    xcodec
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Svg
    # QOpenGLFunctions only wraps the GLES2-compatible subset -- glGetTexImage
    # (used by XGLVideoWidget::SaveSnapshot() to read a tile's texture back
    # for screenshots) is desktop-only and isn't in it, so it's called as a
    # plain GL symbol. That resolves fine on macOS/Linux via Qt6::OpenGLWidgets'
    # own transitive linkage, but Windows needs opengl32.lib linked explicitly
    # or it's an unresolved external at link time.
    OpenGL::GL
)

if(APPLE)
    set_source_files_properties(resources/player.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(xviewer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE player.icns
        INSTALL_RPATH "${FFMPEG_ROOT}/lib;${COMMON_LIBS_ROOT}/lib"
    )
endif()
# Linux: deliberately no custom RPATH here — leave CMake's default build-tree
# RPATH (absolute paths into the CI build/dependency dirs) in place so the
# packaging script's `ldd`-based dependency walk can still resolve everything
# while still on the build machine. scripts/package-linux.sh uses `patchelf`
# to rewrite the RPATH to a relative "$ORIGIN/lib" only in the staged copy.

if(WIN32)
    # Without this, Windows links xviewer as a console-subsystem app, so a
    # console/terminal window pops up alongside the GUI on every launch even
    # in a Release build. qt_add_executable already conditionally links the
    # Qt entry-point shim that supplies WinMain and forwards to main(), so
    # this alone is enough -- no source changes needed.
    set_target_properties(xviewer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
