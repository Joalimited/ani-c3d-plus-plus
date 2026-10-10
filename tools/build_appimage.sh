#!/usr/bin/env bash
# Builds dist/ani-c3d-x86_64.AppImage from build/linux/ani-c3d (run tools/build.sh linux first).
# Bundles the libraries that are not part of every desktop (SDL, curl and its TLS stack, PCRE2, SQLite, WebP); the system
# provides glibc, libstdc++, OpenGL, X11/Wayland and audio. Like ani-3d it plays through the system's mpv.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
BIN="$ROOT/build/linux/ani-c3d"
[ -x "$BIN" ] || { echo "missing $BIN - run tools/build.sh linux first"; exit 1; }
TOOL="${APPIMAGETOOL:-$HOME/ani-3d-build/appimagetool}"
if [ ! -x "$TOOL" ]; then
  mkdir -p "$(dirname "$TOOL")"
  curl -L -s -o "$TOOL" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod +x "$TOOL"
fi
W="$ROOT/build/appimage"
rm -rf "$W"
mkdir -p "$W/AppDir/usr/bin/fonts" "$W/AppDir/usr/lib" "$ROOT/dist"
cp "$BIN" "$W/AppDir/usr/bin/ani-c3d"
cp "$ROOT"/assets/fonts/* "$W/AppDir/usr/bin/fonts/"

# libraries every Linux desktop already has (and must not be replaced: GPU drivers load against them)
skip='^(linux-vdso|ld-linux|libc\.|libm\.|libdl\.|libpthread\.|librt\.|libresolv\.|libstdc\+\+|libgcc_s|libGL|libOpenGL|libGLX|libGLdispatch|libEGL|libX|libxcb|libwayland|libdrm|libgbm|libasound|libpulse|libdbus|libudev|libsystemd|libz\.so)'
copy_deps() {
  ldd "$1" | awk '/=> \//{print $3}' | while read -r lib; do
    name="$(basename "$lib")"
    if ! echo "$name" | grep -Eq "$skip" && [ ! -e "$W/AppDir/usr/lib/$name" ]; then
      cp -L "$lib" "$W/AppDir/usr/lib/$name"
      copy_deps "$lib"
    fi
  done
}
copy_deps "$BIN"
# sdl2-compat loads SDL3 at run time (dlopen), so ldd cannot see it
for extra in /usr/lib64/libSDL3.so.0; do
  [ -e "$extra" ] && cp -L "$extra" "$W/AppDir/usr/lib/" && copy_deps "$extra"
done

cat > "$W/AppDir/AppRun" <<'RUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
command -v mpv >/dev/null 2>&1 || echo "ani-c3d: mpv is not installed - install it (e.g. sudo dnf install mpv / sudo apt install mpv) to play episodes" >&2
exec "$HERE/usr/bin/ani-c3d" "$@"
RUN
chmod +x "$W/AppDir/AppRun"
cat > "$W/AppDir/ani-c3d.desktop" <<'DESK'
[Desktop Entry]
Type=Application
Name=ani-c3d
Comment=3D file-system-navigator style anime and manga browser (C++)
Exec=ani-c3d
Icon=ani-c3d
Categories=AudioVideo;Video;
Terminal=false
DESK
cp "$ROOT/assets/ani-c3d.png" "$W/AppDir/ani-c3d.png"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$W/AppDir" "$ROOT/dist/ani-c3d-x86_64.AppImage" > "$W/appimagetool.log" 2>&1 || { tail -20 "$W/appimagetool.log"; exit 1; }
ls -la "$ROOT/dist/ani-c3d-x86_64.AppImage"
echo "bundled libraries: $(ls "$W/AppDir/usr/lib" | tr '\n' ' ')"
