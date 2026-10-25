#!/bin/sh
# Cross-builds ijl11.dll for 32-bit game.exe (brew install mingw-w64).
set -e
cd "$(dirname "$0")"
i686-w64-mingw32-g++ -std=c++20 -O2 -DWHISPER_VERSION="\"$(git describe --tags --always --dirty) $(date +%Y-%m-%d)\"" -shared -static -s -o ijl11.dll whisper.cpp ijl11.def -lws2_32 -lgdi32
