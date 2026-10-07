#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run the CI Linux build (configure, build, clang-tidy, ctest) in a docker
# image with the deps baked in, against the current tree. So a push doesn't
# fail a check the local mac clang doesn't cover (identifier-length,
# include-cleaner). Builds in its own build-linux/ (a docker volume) so it doesn't
# clash with the host build. The image is (re)built only when the
# Dockerfile changes.
set -euo pipefail
cd "$(dirname "$0")/.."
tag="d2d-linux-ci:$(sha1sum tools/validate-linux.Dockerfile | cut -c1-12)"
docker image inspect "$tag" >/dev/null 2>&1 || docker build -t "$tag" -f tools/validate-linux.Dockerfile tools/
# build-linux/ is a docker volume, not the bind mount: Docker Desktop's
# macOS mount throws bus errors and torn files at FFmpeg's make.
docker run --rm -t \
    -v "$PWD:/src" -v d2d-build-linux:/src/build-linux -w /src \
    "$tag" bash -euxc '
# Extra warnings as proxies for MSVC checks that only fire on the Windows
# leg: -Wshadow (C4458 hides class member), -Wconversion for the narrowing
# family (C4244 / C4267). -Wno-sign-conversion because the codebase mixes
# int and size_t in vector ops throughout; the narrowing checks catch the
# high-value MSVC diagnostics without a codebase-wide cleanup.
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-Wshadow -Wconversion -Wno-sign-conversion -Werror"
cmake --build build-linux -- -j 8 -k 0
python3 -c "import json; print(\"\n\".join(sorted({e[\"file\"] for e in json.load(open(\"build-linux/compile_commands.json\")) if \"/_deps/\" not in e[\"file\"] and \"/build\" not in e[\"file\"]})))" > build-linux/tidy_files.txt
xargs -P 4 -n 1 clang-tidy -p build-linux --quiet --extra-arg=-Wno-unknown-warning-option < build-linux/tidy_files.txt
ctest --test-dir build-linux --output-on-failure -C Release
'
