#!/usr/bin/env bash
# Build ani-c3d inside the toolchain container (tools/Containerfile -> image ani-c3d-build).
#   tools/build.sh linux     -> build/linux/ani-c3d
#   tools/build.sh windows   -> build/windows/ani-c3d.exe (single static exe; set ANI_MPV_ZIP to bundle mpv)
set -euo pipefail
cd "$(dirname "$0")/.."
target="${1:-linux}"
img=localhost/ani-c3d-build
mpv_zip="${ANI_MPV_ZIP:-$HOME/ani-3d-build/mpv.zip}"
mounts=(-v "$PWD:/src:Z")
[ -f "$mpv_zip" ] && mounts+=(-v "$mpv_zip:/mpv.zip:ro,Z")
case "$target" in
  linux)
    podman run --rm "${mounts[@]}" -w /src "$img" bash -c \
      'cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build/linux'
    ;;
  windows)
    podman run --rm "${mounts[@]}" -w /src -e ANI_MPV_ZIP=/mpv.zip -e ANI_MESA_ZIP=/opt/mesa.zip "$img" bash -c \
      'cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake >/dev/null && cmake --build build/windows'
    ;;
  *) echo "usage: $0 linux|windows"; exit 2 ;;
esac
