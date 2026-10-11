#!/usr/bin/env bash
# Cross-build the native libraries ani-c3d needs for Android (run inside localhost/ani-c3d-android).
# Usage: build_deps.sh <abi> <prefix>   abi: arm64-v8a | armeabi-v7a
set -euo pipefail
ABI=$1; PREFIX=$2; API=24
TC=$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64
case $ABI in
  arm64-v8a)   TRIPLE=aarch64-linux-android;   SSL=android-arm64 ;;
  armeabi-v7a) TRIPLE=armv7a-linux-androideabi; SSL="android-arm no-asm" ;;  # (asm needs non-PIC relocations)
  x86_64)      TRIPLE=x86_64-linux-android;    SSL=android-x86_64 ;;  # only for the emulator
esac
SRC=${SRC_CACHE:-/tmp/src}; mkdir -p $SRC $PREFIX; cd $SRC
CM=(cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$API
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_PREFIX_PATH=$PREFIX -DCMAKE_FIND_ROOT_PATH=$PREFIX -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
fetch() { [ -f "$2" ] || curl -fsSLo "$2" "$1"; }

# --- OpenSSL (curl needs TLS 1.3: MangaDex refuses older)
if [ ! -f $PREFIX/lib/libssl.a ]; then
fetch https://github.com/openssl/openssl/releases/download/openssl-3.3.2/openssl-3.3.2.tar.gz openssl.tgz
rm -rf openssl-3.3.2 && tar xzf openssl.tgz && cd openssl-3.3.2
PATH=$TC/bin:$PATH ANDROID_NDK_ROOT=$ANDROID_NDK ./Configure $SSL -D__ANDROID_API__=$API -fPIC no-shared no-tests no-apps no-docs --prefix=$PREFIX --openssldir=/system/etc/security/cacerts >/dev/null
PATH=$TC/bin:$PATH make -j$(nproc) build_libs >/dev/null && PATH=$TC/bin:$PATH make install_dev >/dev/null
cd $SRC
fi

# --- curl on OpenSSL
fetch https://curl.se/download/curl-8.11.1.tar.xz curl.txz
rm -rf curl-8.11.1 && tar xJf curl.txz
"${CM[@]}" -S curl-8.11.1 -B curl-b -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DCURL_USE_OPENSSL=ON -DOPENSSL_ROOT_DIR=$PREFIX -DOPENSSL_USE_STATIC_LIBS=ON \
   -DCURL_USE_LIBPSL=OFF -DUSE_LIBIDN2=OFF -DUSE_NGHTTP2=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_DISABLE_LDAP=ON \
   -DCURL_ZLIB=ON -DENABLE_UNIX_SOCKETS=ON >/dev/null
cmake --build curl-b >/dev/null && cmake --install curl-b >/dev/null

# --- pcre2, libwebp, sqlite, SDL2
fetch https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.44/pcre2-10.44.tar.gz pcre2.tgz
rm -rf pcre2-10.44 && tar xzf pcre2.tgz
"${CM[@]}" -S pcre2-10.44 -B pcre2-b -DPCRE2_BUILD_PCRE2GREP=OFF -DPCRE2_BUILD_TESTS=OFF >/dev/null && cmake --build pcre2-b >/dev/null && cmake --install pcre2-b >/dev/null

fetch https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.4.0.tar.gz webp.tgz
rm -rf libwebp-1.4.0 && tar xzf webp.tgz
"${CM[@]}" -S libwebp-1.4.0 -B webp-b -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF \
   -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF >/dev/null
cmake --build webp-b >/dev/null && cmake --install webp-b >/dev/null

fetch https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip sqlite.zip
rm -rf sqlite-amalgamation-3460100 && unzip -q sqlite.zip
$TC/bin/${TRIPLE}$API-clang -O2 -fPIC -DSQLITE_THREADSAFE=1 -c sqlite-amalgamation-3460100/sqlite3.c -o sqlite3.o
$TC/bin/llvm-ar rcs $PREFIX/lib/libsqlite3.a sqlite3.o
cp sqlite-amalgamation-3460100/sqlite3.h sqlite-amalgamation-3460100/sqlite3ext.h $PREFIX/include/

fetch https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz sdl2.tgz
rm -rf SDL2-2.30.12 && tar xzf sdl2.tgz
"${CM[@]}" -S SDL2-2.30.12 -B sdl-b -DBUILD_SHARED_LIBS=ON -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF >/dev/null && cmake --build sdl-b >/dev/null && cmake --install sdl-b >/dev/null
echo "deps for $ABI done in $PREFIX"
