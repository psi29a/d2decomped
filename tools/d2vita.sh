#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# d2d alone, for D2Vita (docs/d2vita.md) and the Switch (docs/switch.md),
# into build/d2vita/<target>/:
#   tools/d2vita.sh win32   Game.exe, SDL3.dll, OpenAL32.dll (MinGW i686)
#   tools/d2vita.sh vita    eboot.bin, d2d.vpk (VitaSDK)
#   tools/d2vita.sh switch  d2d.nro (devkitA64)
# Runs where its toolchain is: CI's d2vita jobs, or the Makefile's Docker.
#   win32  i686-w64-mingw32-g++-posix and a MinGW zlib (Debian/Ubuntu:
#          g++-mingw-w64-i686-posix, libz-mingw-w64-dev), gcc and make for
#          FFmpeg's configure and build; $SDL3_MINGW and
#          $OPENAL_MINGW, the unpacked SDL3-devel-*-mingw and
#          openal-soft-*-bin releases
#   vita   $VITASDK with its sdl3, openal-soft, zlib and bzip2 packages
#          (the vitasdk/vitasdk image has them)
#   switch $DEVKITPRO with libnx, mesa, zlib and bzip2 (the devkitpro/devkita64
#          image has them); it fetches SDL3 and OpenAL Soft's source itself
set -euo pipefail
target=${1:?usage: tools/d2vita.sh win32|vita|switch}
build=build/d2vita-$target
out=build/d2vita/$target
mkdir -p "$out"
common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DD2D_GAME_ONLY=ON -DD2D_BUNDLED_STORMLIB=ON)

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
    fetch() {   # name url sha256: the tarball unpacked into $build/name
        [[ -d $build/$1 ]] && return
        mkdir -p "$build"
        curl -fsSLo "$build/$1.tar.gz" "$2"
        echo "$3  $build/$1.tar.gz" | sha256sum -c --quiet
        mkdir "$build/$1" && tar -xzf "$build/$1.tar.gz" -C "$build/$1" --strip-components=1
    }
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
*)
    echo "tools/d2vita.sh: $target: win32, vita or switch" >&2
    exit 2
    ;;
esac
ls -l "$out"
