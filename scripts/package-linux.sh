#!/usr/bin/env bash
# Bundle the built xviewer binary + its non-system shared library dependencies
# (Qt, FFmpeg, SDL2, xcodec) into a self-contained folder and tar.gz it.
# Usage: package-linux.sh <build-dir> <qt-install-dir> <output-tar-gz>
set -euo pipefail

BUILD_DIR="$(realpath "$1")"
QT_DIR="$(realpath "$2")"
OUTPUT_TAR="$3"

BIN="$BUILD_DIR/src/xviewer/xviewer"
XCODEC_SO="$BUILD_DIR/src/xcodec/libxcodec.so"
[ -f "$BIN" ] || { echo "error: $BIN not found" >&2; exit 1; }
[ -f "$XCODEC_SO" ] || { echo "error: $XCODEC_SO not found" >&2; exit 1; }

STAGE_DIR="$(mktemp -d)"
APP_DIR="$STAGE_DIR/XPlayer"
mkdir -p "$APP_DIR/lib/plugins/platforms"

cp "$BIN" "$APP_DIR/xviewer"
cp "$XCODEC_SO" "$APP_DIR/lib/"

# Any dependency resolved from one of these prefixes is "ours" (Qt, FFmpeg,
# SDL2, xcodec) and needs to travel with the app; anything else is assumed to
# be a base-OS library present on any reasonably modern glibc Linux.
NONSYSTEM_PREFIXES=("$QT_DIR" "$FFMPEG_ROOT" "$COMMON_LIBS_ROOT" "$BUILD_DIR")

is_nonsystem() {
    local path="$1"
    for prefix in "${NONSYSTEM_PREFIXES[@]}"; do
        [ -n "$prefix" ] && [[ "$path" == "$prefix"* ]] && return 0
    done
    return 1
}

collect_deps() {
    ldd "$1" 2>/dev/null | awk '{print $3}' | grep -E '^/'
}

copied=()
queue=("$APP_DIR/xviewer" "$APP_DIR/lib/libxcodec.so")
while [ ${#queue[@]} -gt 0 ]; do
    f="${queue[0]}"
    queue=("${queue[@]:1}")
    for dep in $(collect_deps "$f"); do
        base="$(basename "$dep")"
        already_copied=false
        for c in "${copied[@]:-}"; do
            [ "$c" = "$base" ] && already_copied=true && break
        done
        $already_copied && continue
        if is_nonsystem "$dep"; then
            cp -L "$dep" "$APP_DIR/lib/$base"
            copied+=("$base")
            queue+=("$APP_DIR/lib/$base")
        fi
    done
done

# Qt needs its xcb platform plugin to create a window at all. There should be
# exactly one match under a real Qt install root; -print -quit (rather than
# -exec cp on every match) also means a too-broad QT_DIR can't make this
# rediscover and try to copy the file it just wrote onto itself.
qxcb="$(find "$QT_DIR" -name "libqxcb.so" -print -quit)"
[ -n "$qxcb" ] || { echo "error: libqxcb.so not found under $QT_DIR" >&2; exit 1; }
cp "$qxcb" "$APP_DIR/lib/plugins/platforms/"

# Everything above still carries the CI build machine's absolute RPATH
# (pointing into the build/dependency dirs) since that's what let `ldd`
# resolve dependencies while walking the graph above. Now that every
# library the app needs lives together in this one bundle, rewrite RPATH
# to be relative so the bundle works on a machine that has none of those
# build-time paths.
patchelf --set-rpath '$ORIGIN/lib' "$APP_DIR/xviewer"
for so in "$APP_DIR"/lib/*.so*; do
    [ -f "$so" ] && patchelf --set-rpath '$ORIGIN' "$so"
done

cat > "$APP_DIR/xviewer.sh" <<'EOF'
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export QT_PLUGIN_PATH="$DIR/lib/plugins"
exec "$DIR/xviewer" "$@"
EOF
chmod +x "$APP_DIR/xviewer.sh" "$APP_DIR/xviewer"

tar -C "$STAGE_DIR" -czf "$OUTPUT_TAR" "XPlayer"
rm -rf "$STAGE_DIR"

echo "packaged: $OUTPUT_TAR"
