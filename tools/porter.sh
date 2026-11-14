#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# d2d alone, for the ports: D2Vita (docs/d2vita.md), the Switch
# (docs/switch.md) and Android (docs/android.md), into build/ports/<target>/:
#   tools/porter.sh win32   Game.exe, SDL3.dll, OpenAL32.dll (MinGW i686)
#   tools/porter.sh vita    eboot.bin, d2d.vpk (VitaSDK)
#   tools/porter.sh switch  d2d.nro (devkitA64)
#   tools/porter.sh android d2d.apk (Android NDK, arm64; docs/android.md)
# Runs where its toolchain is: CI's ports jobs, or the Makefile's Docker.
#   win32  i686-w64-mingw32-g++-posix and a MinGW zlib (Debian/Ubuntu:
#          g++-mingw-w64-i686-posix, libz-mingw-w64-dev), gcc and make for
#          FFmpeg's configure and build; $SDL3_MINGW and
#          $OPENAL_MINGW, the unpacked SDL3-devel-*-mingw and
#          openal-soft-*-bin releases
#   vita   $VITASDK with its sdl3, openal-soft, zlib and bzip2 packages
#          (the vitasdk/vitasdk image has them)
#   switch $DEVKITPRO with libnx, mesa, zlib and bzip2 (the devkitpro/devkita64
#          image has them); it fetches SDL3 and OpenAL Soft's source itself
#   android $ANDROID_HOME (default ~/Library/Android/sdk) with an ndk/, a
#          build-tools/ and a platforms/ in it, and a JDK; it fetches SDL3 and
#          OpenAL Soft's source itself. No Gradle: aapt2, javac, d8, apksigner.
set -euo pipefail
target=${1:?usage: tools/porter.sh win32|vita|switch|android}
build=build/port-$target
out=build/ports/$target
mkdir -p "$out"
common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DD2D_GAME_ONLY=ON -DD2D_BUNDLED_STORMLIB=ON)

fetch() {   # name url sha256: the tarball unpacked into $build/name
    [[ -d $build/$1 ]] && return
    mkdir -p "$build"
    curl -fsSLo "$build/$1.tar.gz" "$2"
    # CMake's, not sha256sum -c: macOS's own takes no list on stdin.
    local sum
    sum=$(cmake -E sha256sum "$build/$1.tar.gz")
    [[ ${sum%% *} == "$3" ]] || { echo "tools/porter.sh: $1.tar.gz: SHA-256 ${sum%% *}, not $3" >&2; exit 1; }
    # Into .part, then renamed: an interrupted tar leaves no tree a rerun takes as done.
    rm -rf "$build/$1.part" && mkdir "$build/$1.part"
    tar -xzf "$build/$1.tar.gz" -C "$build/$1.part" --strip-components=1 && mv "$build/$1.part" "$build/$1"
}

case $target in
win32)
    : "${SDL3_MINGW:?the unpacked SDL3-devel-*-mingw release}" "${OPENAL_MINGW:?the unpacked openal-soft-*-bin release}"
    # -static and zlib's archive: libstdc++, libgcc, winpthread and zlib in
    # Game.exe (stripped), so it imports Windows' DLLs and the two beside it.
    cmake -S . -B "$build" "${common[@]}" \
        -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86 \
        -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc-posix \
        -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++-posix \
        -DCMAKE_RC_COMPILER=i686-w64-mingw32-windres \
        -DCMAKE_FIND_ROOT_PATH="/usr/i686-w64-mingw32;$SDL3_MINGW/i686-w64-mingw32" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
        -DCMAKE_EXE_LINKER_FLAGS="-static -s" -DZLIB_USE_STATIC_LIBS=ON \
        -DOPENAL_INCLUDE_DIR="$OPENAL_MINGW/include" \
        -DOPENAL_LIBRARY="$OPENAL_MINGW/libs/Win32/libOpenAL32.dll.a"
    cmake --build "$build" --target d2d
    cp "$build/apps/d2d/d2d.exe" "$out/Game.exe"
    cp "$SDL3_MINGW/i686-w64-mingw32/bin/SDL3.dll" "$out/"
    cp "$OPENAL_MINGW/bin/Win32/soft_oal.dll" "$out/OpenAL32.dll"
    ;;
vita)
    : "${VITASDK:?the VitaSDK install dir}"
    cmake -S . -B "$build" "${common[@]}" -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
    cmake --build "$build" --target d2d.vpk-vpk   # vita_create_vpk names it so (CMake 3.20+)
    cp "$build/apps/d2d/eboot.bin" "$build/apps/d2d/d2d.vpk" "$out/"
    ;;
switch)
    : "${DEVKITPRO:?the devkitPro install dir}"
    # devkitPro packages SDL2 alone, and its openal-soft plays through SDL2,
    # whose SDL_ names clash with SDL3's in one binary. So SDL3 from
    # devkitPro's switch-sdl-3.4 branch (unreleased) and OpenAL Soft on SDL3,
    # built once into $deps. ponytail: pinned by hand; bump with the SHA-256.
    nx=(-DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake")
    deps=$PWD/$build/nx-deps
    if [[ ! -f $deps/lib/libSDL3.a ]]; then
        fetch nx-sdl3-src https://github.com/devkitPro/SDL/archive/c329016c0e338ab4397ad930f83330c9fe058348.tar.gz \
            6047e4787f105ab4a0c6293ee3090ce5d1ebad112fcbe2a3e5a8874b0cc6f709
        cmake -S "$build/nx-sdl3-src" -B "$build/nx-sdl3" -G Ninja -DCMAKE_BUILD_TYPE=Release "${nx[@]}" \
            -DCMAKE_INSTALL_PREFIX="$deps" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
        cmake --build "$build/nx-sdl3" --target install
    fi
    if [[ ! -f $deps/lib/libopenal.a ]]; then
        fetch nx-openal-src https://github.com/kcat/openal-soft/archive/refs/tags/1.25.2.tar.gz \
            fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894
        cmake -S "$build/nx-openal-src" -B "$build/nx-openal" -G Ninja -DCMAKE_BUILD_TYPE=Release "${nx[@]}" \
            -DCMAKE_INSTALL_PREFIX="$deps" -DSDL3_DIR="$deps/lib/cmake/SDL3" -DLIBTYPE=STATIC \
            -DALSOFT_BACKEND_SDL3=ON -DALSOFT_REQUIRE_SDL3=ON -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF \
            -DALSOFT_ENABLE_MODULES=OFF   # devkitPro names logging.cpp and logging.cppm both logging.o
        cmake --build "$build/nx-openal" --target install
    fi
    cmake -S . -B "$build" "${common[@]}" "${nx[@]}" -DSDL3_DIR="$deps/lib/cmake/SDL3" \
        -DOPENAL_INCLUDE_DIR="$deps/include" -DOPENAL_LIBRARY="$deps/lib/libopenal.a"
    cmake --build "$build" --target d2d_nro
    cp "$build/apps/d2d/d2d.nro" "$out/"
    ;;
android)
    # arm64 only, Android 8 (API 26) up, pages of 4 or 16 KB.
    # ponytail: a debug-signed APK from the SDK's own tools; a Gradle project
    # when there's a release to sign or a second ABI to carry.
    sdk=${ANDROID_HOME:-$HOME/Library/Android/sdk}
    ndk=$(ls -d "$sdk"/ndk/* | sort -V | tail -1)
    tools=$(ls -d "$sdk"/build-tools/* | sort -V | tail -1)
    android_jar=$(ls -d "$sdk"/platforms/* | sort -V | tail -1)/android.jar
    # Before anything is fetched or compiled: an older NDK ignores the 16 KB
    # flag below and says nothing, an older zipalign has no -P.
    at_least() {   # dir major what: the version a folder is named for
        local version=${1##*/}
        [[ ${version%%.*} -ge $2 ]] || { echo "tools/porter.sh: android needs $3 $2 or later, found $1" >&2; exit 1; }
    }
    at_least "$ndk" 27 "NDK"
    at_least "$tools" 35 "build-tools"
    ndk_args=(-DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a
        -DANDROID_PLATFORM=android-26 -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON)
    deps=$PWD/$build/deps
    if [[ ! -f $deps/lib/libSDL3.so ]]; then
        fetch sdl3-src https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz \
            7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
        cmake -S "$build/sdl3-src" -B "$build/sdl3" -G Ninja -DCMAKE_BUILD_TYPE=Release "${ndk_args[@]}" \
            -DCMAKE_INSTALL_PREFIX="$deps" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
        cmake --build "$build/sdl3" --target install
    fi
    if [[ ! -f $deps/lib/libopenal.a ]]; then   # plays through OpenSL ES
        fetch openal-src https://github.com/kcat/openal-soft/archive/refs/tags/1.25.2.tar.gz \
            fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894
        cmake -S "$build/openal-src" -B "$build/openal" -G Ninja -DCMAKE_BUILD_TYPE=Release "${ndk_args[@]}" \
            -DCMAKE_INSTALL_PREFIX="$deps" -DLIBTYPE=STATIC -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF \
            -DALSOFT_ENABLE_MODULES=OFF -DALSOFT_BACKEND_WAVE=OFF
        cmake --build "$build/openal" --target install
    fi
    cmake -S . -B "$build/d2d" "${common[@]}" "${ndk_args[@]}" -DCMAKE_FIND_ROOT_PATH="$deps" \
        -DSDL3_DIR="$deps/lib/cmake/SDL3" -DOPENAL_INCLUDE_DIR="$deps/include" -DOPENAL_LIBRARY="$deps/lib/libopenal.a"
    cmake --build "$build/d2d" --target d2d
    # The APK: the icon and manifest, SDL's Java activity, the two libraries
    # (stripped, stored unpacked and page-aligned so Android maps them in place).
    apk=$build/apk
    rm -rf "$apk" && mkdir -p "$apk/res/mipmap" "$apk/classes" "$apk/lib/arm64-v8a"
    cp assets/icon/d2d.png "$apk/res/mipmap/ic_launcher.png"
    "$tools/aapt2" compile --dir "$apk/res" -o "$apk/res.zip"
    "$tools/aapt2" link -o "$apk/d2d.apk" -I "$android_jar" --manifest apps/d2d/android/AndroidManifest.xml \
        --version-code 1 --version-name "$(git describe --tags --always --dirty)" "$apk/res.zip"
    find "$build/sdl3-src/android-project/app/src/main/java" -name '*.java' > "$apk/sources"
    javac -nowarn --release 11 -cp "$android_jar" -d "$apk/classes" @"$apk/sources"
    find "$apk/classes" -name '*.class' > "$apk/class-files"
    "$tools/d8" --release --min-api 26 --lib "$android_jar" --output "$apk" @"$apk/class-files"
    strip=$(ls "$ndk"/toolchains/llvm/prebuilt/*/bin/llvm-strip)
    "$strip" -o "$apk/lib/arm64-v8a/libmain.so" "$build/d2d/apps/d2d/libmain.so"
    "$strip" -o "$apk/lib/arm64-v8a/libSDL3.so" "$deps/lib/libSDL3.so"
    (cd "$apk" && zip -q -0 d2d.apk classes.dex lib/arm64-v8a/*.so)
    "$tools/zipalign" -f -P 16 4 "$apk/d2d.apk" "$apk/aligned.apk"
    # A debug key of this build's own: adb installs it, no store takes it.
    [[ -f $build/debug.keystore ]] || keytool -genkeypair -keystore "$build/debug.keystore" -storepass android \
        -keypass android -alias debug -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=d2d debug"
    "$tools/apksigner" sign --ks "$build/debug.keystore" --ks-pass pass:android --out "$out/d2d.apk" "$apk/aligned.apk"
    rm -f "$out/d2d.apk.idsig"
    ;;
*)
    echo "tools/porter.sh: $target: win32, vita, switch or android" >&2
    exit 2
    ;;
esac
ls -l "$out"
