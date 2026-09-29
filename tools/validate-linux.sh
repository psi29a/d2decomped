#!/usr/bin/env bash
# Run the CI Linux build (deps, configure, build, clang-tidy, ctest) in an
# ubuntu:26.04 container against the current tree, so a push doesn't fail
# a check the local mac clang doesn't cover (identifier-length,
# include-cleaner). Uses a separate build/ dir (build-linux/) so it doesn't
# clash with the host build.
set -euo pipefail
cd "$(dirname "$0")/.."
image=ubuntu:26.04
docker run --rm -t \
    -v "$PWD:/src" -w /src \
    -e DEBIAN_FRONTEND=noninteractive \
    "$image" bash -euxc '
apt-get update -qq
apt-get install -y --no-install-recommends \
    ca-certificates cmake ninja-build build-essential pkg-config \
    python3 findutils \
    libsdl3-dev libopenal-dev zlib1g-dev libbz2-dev libstorm-dev \
    qt6-base-dev libgl1-mesa-dev \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
    clang-tidy
# Extra warnings as proxies for MSVC checks that only fire on the Windows
# leg: -Wshadow (C4458 hides class member), -Wconversion for the narrowing
# family (C4244 / C4267). -Wno-sign-conversion because the codebase mixes
# int and size_t in vector ops throughout; the narrowing checks catch the
# high-value MSVC diagnostics without a codebase-wide cleanup.
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-Wshadow -Wconversion -Wno-sign-conversion -Werror"
cmake --build build-linux -- -k 0
python3 -c "import json; print(\"\n\".join(sorted({e[\"file\"] for e in json.load(open(\"build-linux/compile_commands.json\")) if \"/_deps/\" not in e[\"file\"] and \"/build\" not in e[\"file\"]})))" > build-linux/tidy_files.txt
xargs -P 4 -n 1 clang-tidy -p build-linux --quiet --extra-arg=-Wno-unknown-warning-option < build-linux/tidy_files.txt
ctest --test-dir build-linux --output-on-failure -C Release
'
