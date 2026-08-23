#!/usr/bin/env bash
# Bundle the built xviewer.app into a self-contained, ad-hoc-signed .app and zip it.
# Usage: COMMON_LIBS_ROOT=<sdl2 prefix> package-macos.sh <build-dir> <output-zip>
set -euo pipefail

BUILD_DIR="$(cd "$1" && pwd)"
OUTPUT_ZIP="$2"
: "${COMMON_LIBS_ROOT:?COMMON_LIBS_ROOT env var (SDL2 install prefix) must be set}"

APP="$BUILD_DIR/src/xviewer/xviewer.app"
if [ ! -d "$APP" ]; then
    echo "error: $APP not found" >&2
    exit 1
fi

STAGE_DIR="$(mktemp -d)"
STAGED_APP="$STAGE_DIR/XPlayer.app"
cp -R "$APP" "$STAGED_APP"

# Bundle Qt's own libs/plugins.
macdeployqt "$STAGED_APP"

# Bundle the remaining non-system dylibs (FFmpeg, SDL2, xcodec) that macdeployqt
# doesn't know about, rewriting their load paths to @executable_path/../Frameworks.
# dylibbundler does NOT read the binary's own LC_RPATH entries to resolve
# "@rpath/..." references (a known dylibbundler limitation) -- every location
# that's only reachable via @rpath (xcodec's own build output, SDL2) has to be
# passed explicitly via -s, or dylibbundler drops into an interactive prompt
# and hangs non-interactive CI runs.
# -cd/-of (create-if-missing, overwrite files) rather than -od
# (overwrite-dir): -od unconditionally *erases* Contents/Frameworks first,
# which would wipe out the Qt frameworks macdeployqt just placed there.
dylibbundler -cd -of -b \
    -x "$STAGED_APP/Contents/MacOS/xviewer" \
    -d "$STAGED_APP/Contents/Frameworks" \
    -p "@executable_path/../Frameworks" \
    -s "$BUILD_DIR/src/xcodec" \
    -s "$COMMON_LIBS_ROOT/lib"

# dylibbundler can end up writing two identical @executable_path/../Frameworks
# LC_RPATH entries onto a binary that already had one from macdeployqt (e.g.
# it rewrote two distinct absolute FFmpeg-lib rpaths down to the same
# relative value) -- dyld refuses to load a binary with a literal duplicate
# LC_RPATH, so collapse any duplicates back down to one entry.
dedupe_rpaths() {
    local bin="$1"
    local paths
    paths="$(otool -l "$bin" | awk '/LC_RPATH/{getline; getline; print $2}' | sort | uniq -d)"
    while IFS= read -r p; do
        [ -z "$p" ] && continue
        local count
        count="$(otool -l "$bin" | awk -v p="$p" '/LC_RPATH/{getline; getline; if ($2==p) print}' | wc -l | tr -d ' ')"
        for _ in $(seq 2 "$count"); do
            install_name_tool -delete_rpath "$p" "$bin"
        done
    done <<< "$paths"
}
dedupe_rpaths "$STAGED_APP/Contents/MacOS/xviewer"
for dylib in "$STAGED_APP"/Contents/Frameworks/*.dylib; do
    [ -f "$dylib" ] && dedupe_rpaths "$dylib"
done

# Homebrew's "sdl2" formula is an alias for sdl2-compat: an SDL2-ABI shim
# that dlopen()s a real SDL3 dylib at runtime (never an LC_LOAD_DYLIB
# dependency) -- otool -L / dylibbundler can't see it at all, so it never
# gets bundled, and the shipped app fails at startup with "Failed loading
# SDL3 library". sdl2-compat's first dlopen candidate is literally
# "@loader_path/libSDL3.dylib" (i.e. right next to itself), so dropping a
# real copy there is enough; no rpath/install-name changes needed on it
# since nothing links against it directly.
sdl3_dylib="$(brew --prefix sdl3 2>/dev/null)/lib/libSDL3.dylib"
if [ -e "$sdl3_dylib" ]; then
    sdl3_real="$(readlink -f "$sdl3_dylib" 2>/dev/null || python3 -c "import os,sys; print(os.path.realpath(sys.argv[1]))" "$sdl3_dylib")"
    cp "$sdl3_real" "$STAGED_APP/Contents/Frameworks/$(basename "$sdl3_real")"
    ln -sf "$(basename "$sdl3_real")" "$STAGED_APP/Contents/Frameworks/libSDL3.dylib"
    install_name_tool -id "@executable_path/../Frameworks/$(basename "$sdl3_real")" \
        "$STAGED_APP/Contents/Frameworks/$(basename "$sdl3_real")"
fi

# Ad-hoc (temporary) signature: no developer certificate, no notarization.
codesign --force --deep -s - "$STAGED_APP"
codesign -dv --verbose=4 "$STAGED_APP" 2>&1 | grep -i "signature"

ditto -c -k --keepParent "$STAGED_APP" "$OUTPUT_ZIP"
rm -rf "$STAGE_DIR"

echo "packaged: $OUTPUT_ZIP"
