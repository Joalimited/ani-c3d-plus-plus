#!/usr/bin/env bash
# Build the ani-c3d APK for Fire TV / Android (run inside localhost/ani-c3d-android, /work = the repo).
# Needs the native deps from build_deps.sh in build/android/deps/<abi>. Output: dist/ani-c3d-firetv.apk
set -euo pipefail
cd /work
SDK=$ANDROID_HOME; BT=$SDK/build-tools/34.0.0; JAR=$SDK/platforms/android-34/android.jar
OUT=${APK_WORK:-build/android/apk}; rm -rf $OUT; mkdir -p $OUT/lib $OUT/classes $OUT/res-c dist
ABIS=${ABIS:-"arm64-v8a armeabi-v7a"}
for abi in $ABIS; do
  cmake -S . -B build/android/$abi -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=$abi \
        -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release -DANDROID_STL=c++_shared >/dev/null
  cmake --build build/android/$abi
  mkdir -p $OUT/lib/$abi
  cp build/android/$abi/libmain.so build/android/deps/$abi/lib/libSDL2.so $OUT/lib/$abi/
  case $abi in arm64-v8a) T=aarch64-linux-android ;; armeabi-v7a) T=arm-linux-androideabi ;; x86_64) T=x86_64-linux-android ;; esac
  STL=$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/$T/libc++_shared.so
  cp "$STL" $OUT/lib/$abi/
done
# Java: SDL's activity glue + our one-line subclass
SDLJ=$OUT/sdl-java
rm -rf $SDLJ && cp -r build/android/src/SDL2-2.30.12/android-project/app/src/main/java $SDLJ
# SDL hands the volume keys to Android; the TV build wants them (in-app volume with the remote's +/-)
sed -i 's/keyCode == KeyEvent.KEYCODE_VOLUME_DOWN ||/false ||/; s/keyCode == KeyEvent.KEYCODE_VOLUME_UP ||/false ||/' $(find $SDLJ -name '*.java')
javac -encoding UTF-8 --release 11 -g:none -Xlint:-options -cp $JAR -d $OUT/classes $(find $SDLJ -name '*.java') $(find tools/android/java -name '*.java')
$BT/d8 --release --min-api 24 --lib $JAR --output $OUT $(find $OUT/classes -name '*.class')
# resources + manifest + assets (the font), then add the .so files and the dex
$BT/aapt2 compile --dir tools/android/res -o $OUT/res-c/res.zip
mkdir -p $OUT/assets/fonts && cp assets/fonts/NotoSansMono-Bold.ttf $OUT/assets/fonts/ && cp assets/cacert.pem $OUT/assets/
$BT/aapt2 link -o $OUT/base.apk -I $JAR --manifest tools/android/AndroidManifest.xml --min-sdk-version 24 --target-sdk-version 34 \
    -A $OUT/assets $OUT/res-c/res.zip
(cd $OUT && zip -q -r base.apk lib classes.dex)
$BT/zipalign -f -p 4 $OUT/base.apk $OUT/aligned.apk
[ -f build/android/debug.keystore ] || keytool -genkeypair -keystore build/android/debug.keystore -storepass android -keypass android \
    -alias ani -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=ani-c3d" >/dev/null 2>&1
$BT/apksigner sign --ks build/android/debug.keystore --ks-pass pass:android --key-pass pass:android --out ${APK_OUT:-dist/ani-c3d-firetv.apk} $OUT/aligned.apk
ls -la ${APK_OUT:-dist/ani-c3d-firetv.apk}
