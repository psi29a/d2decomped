#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# d2d alone, for D2Vita (docs/d2vita.md), into build/d2vita/<target>/:
#   tools/d2vita.sh win32   Game.exe, SDL3.dll, OpenAL32.dll (MinGW i686)
#   tools/d2vita.sh vita    eboot.bin, d2d.vpk (VitaSDK)
# Runs where its toolchain is: CI's d2vita jobs, or the Makefile's Docker.
#   win32  i686-w64-mingw32-g++-posix and a MinGW zlib (Debian/Ubuntu:
#          g++-mingw-w64-i686-posix, libz-mingw-w64-dev), gcc and make for
#          FFmpeg's configure and build; $SDL3_MINGW and
#          $OPENAL_MINGW, the unpacked SDL3-devel-*-mingw and
#          openal-soft-*-bin releases
#   vita   $VITASDK with its sdl3, openal-soft, zlib and bzip2 packages
#          (the vitasdk/vitasdk image has them)
set -euo pipefail
target=${1:?usage: tools/d2vita.sh win32|vita}
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
*)
    echo "tools/d2vita.sh: $target: win32 or vita" >&2
    exit 2
    ;;
esac
ls -l "$out"
