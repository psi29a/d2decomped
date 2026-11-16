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
# JOBS: parallel compiles (default 8); D2D_DOCKER_MEMORY: a container memory
# limit such as 10g. A C++ job at -O3 takes up to ~2 GB, so on a small machine
# an unlimited -j 8 can run the host out of memory; with a limit, the OOM
# killer stops the container instead. D2D_BUILD_VOLUME: the build's docker
# volume (default d2d-build-linux); give each worktree its own, since all of
# them mount at /src and ninja would take another tree's newer objects.
jobs=${JOBS:-8}
volume=${D2D_BUILD_VOLUME:-d2d-build-linux}
limits=()
[[ -n "${D2D_DOCKER_MEMORY:-}" ]] && limits+=(--memory="$D2D_DOCKER_MEMORY")
# --asan: a Debug build with AddressSanitizer + UBSan on glibc (where the
# v0.4.0 exit-time heap abort was reported), in its own volume, then ctest.
# Game data for the real-data tests (smoke, quests) when these are set on
# the host: D2_MPQ_DIR, D2_PATCH_INSTALLER, D2_SAVES_DIR, D2_GAME_EXE (mounted read-only).
if [[ "${1:-}" == "--asan" ]]; then
    mounts=() envs=()
    for var in D2_MPQ_DIR D2_PATCH_INSTALLER D2_SAVES_DIR D2_GAME_EXE; do
        [[ -n "${!var:-}" ]] || continue
        mounts+=(-v "${!var}:/data/$var:ro") envs+=(-e "$var=/data/$var")
    done
    exec docker run --rm -t ${limits[@]+"${limits[@]}"} -e JOBS="$jobs" ${mounts[@]+"${mounts[@]}"} ${envs[@]+"${envs[@]}"} \
        -e ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 -e UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
        -v "$PWD:/src" -v "$volume-asan:/src/build-linux-asan" -w /src \
        "$tag" bash -euxc '
cmake -S . -B build-linux-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DD2D_BUILD_TOOLS=OFF -DD2D_CHECK_HEADERS=OFF \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-linux-asan -- -j "$JOBS"
ctest --test-dir build-linux-asan --output-on-failure
'
fi
# build-linux/ is a docker volume, not the bind mount: Docker Desktop's
# macOS mount throws bus errors and torn files at FFmpeg's make.
docker run --rm -t ${limits[@]+"${limits[@]}"} -e JOBS="$jobs" \
    -v "$PWD:/src" -v "$volume:/src/build-linux" -w /src \
    "$tag" bash -euxc '
# Extra warnings as proxies for MSVC checks that only fire on the Windows
# leg: -Wshadow (C4458 hides class member), -Wconversion for the narrowing
# family (C4244 / C4267). -Wno-sign-conversion because the codebase mixes
# int and size_t in vector ops throughout; the narrowing checks catch the
# high-value MSVC diagnostics without a codebase-wide cleanup.
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-Wshadow -Wconversion -Wno-sign-conversion -Werror"
cmake --build build-linux -- -j "$JOBS" -k 0
python3 -c "import json; print(\"\n\".join(sorted({e[\"file\"] for e in json.load(open(\"build-linux/compile_commands.json\")) if \"/_deps/\" not in e[\"file\"] and \"/build\" not in e[\"file\"]})))" > build-linux/tidy_files.txt
xargs -P 4 -n 1 clang-tidy -p build-linux --quiet --extra-arg=-Wno-unknown-warning-option < build-linux/tidy_files.txt
ctest --test-dir build-linux --output-on-failure -C Release
'
