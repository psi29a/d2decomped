#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run the CI Linux build (configure, build, clang-tidy on what changed, ctest) in a docker
# image with the deps baked in, against the current tree. So a push doesn't
# fail a check the local mac clang doesn't cover (identifier-length,
# include-cleaner). Builds in its own build-linux/ (a docker volume) so it doesn't
# clash with the host build. The image is (re)built only when the
# Dockerfile changes.
set -euo pipefail
cd "$(dirname "$0")/.."
tag="d2d-linux-ci:$(sha1sum tools/validate-linux.Dockerfile | cut -c1-12)"
docker image inspect "$tag" >/dev/null 2>&1 || docker build -t "$tag" -f tools/validate-linux.Dockerfile tools/
# JOBS: parallel compiles and clang-tidy runs (default: the machine's threads
# less 2 when it has more than 4, capped at 8, else all of them); D2D_DOCKER_MEMORY: a container memory
# limit such as 10g. A C++ job at -O3 takes up to ~2 GB, so on a small machine
# an unlimited -j can run the host out of memory; with a limit, the OOM
# killer stops the container instead. D2D_BUILD_VOLUME: the build's docker
# volume (default d2d-build-linux); give each worktree its own, since all of
# them mount at /src and ninja would take another tree's newer objects.
threads=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
default_jobs=$(( threads > 4 ? threads - 2 : threads ))
(( default_jobs > 8 )) && default_jobs=8
jobs=${JOBS:-$default_jobs}
volume=${D2D_BUILD_VOLUME:-d2d-build-linux}
limits=()
[[ -n "${D2D_DOCKER_MEMORY:-}" ]] && limits+=(--memory="$D2D_DOCKER_MEMORY")
# --asan: a Debug build with AddressSanitizer + UBSan on glibc (where the
# v0.4.0 exit-time heap abort was reported), in its own volume, then ctest.
# Game data for the real-data tests (smoke, quests) when these are set on
# the host: D2_MPQ_DIR, D2_PATCH_INSTALLER, D2_SAVES_DIR, D2_GAME_EXE (mounted read-only).
# --all: clang-tidy on every file, not only what the branch changed.
# Both builds compile through ccache (the image has it, CMake finds it), its
# cache a docker volume of its own (d2d-ccache) that every worktree shares.
asan=0 tidy_all=0
for arg in "$@"; do
    case "$arg" in
        --asan) asan=1 ;;
        --all) tidy_all=1 ;;
        *) echo "validate-linux.sh: unknown option $arg (--asan, --all)" >&2; exit 2 ;;
    esac
done
cache=(-v d2d-ccache:/ccache -e CCACHE_DIR=/ccache)
if [[ "$asan" == 1 ]]; then
    mounts=() envs=()
    for var in D2_MPQ_DIR D2_PATCH_INSTALLER D2_SAVES_DIR D2_GAME_EXE; do
        [[ -n "${!var:-}" ]] || continue
        mounts+=(-v "${!var}:/data/$var:ro") envs+=(-e "$var=/data/$var")
    done
    exec docker run --rm -t ${limits[@]+"${limits[@]}"} -e JOBS="$jobs" "${cache[@]}" ${mounts[@]+"${mounts[@]}"} ${envs[@]+"${envs[@]}"} \
        -e ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 -e UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
        -v "$PWD:/src" -v "$volume-asan:/src/build-linux-asan" -w /src \
        "$tag" bash -euxc '
cmake -S . -B build-linux-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DD2D_BUILD_TOOLS=OFF -DD2D_CHECK_HEADERS=OFF \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-linux-asan -- -j "$JOBS"
ctest --test-dir build-linux-asan --output-on-failure
'
fi
# What clang-tidy looks at: the files changed since this branch left
# origin/main (committed, staged, not yet added), comment-only headers
# marked, picked as CI picks a pull request's (tools/tidy-pr-select.py).
# Worked out here, where git is; no merge base (no origin/main) means --all.
changed=""
if [[ "$tidy_all" == 0 ]]; then
    if base=$(git merge-base origin/main HEAD 2>/dev/null); then
        changed=$( { git diff --name-only "$base"; git ls-files --others --exclude-standard; } | grep -E '\.(cpp|hpp|h|inc)$' | sort -u \
            | python3 tools/tidy-pr-select.py --annotate --base "$base" --changed - ) || true
    else
        tidy_all=1
    fi
fi
# build-linux/ is a docker volume, not the bind mount: Docker Desktop's
# macOS mount throws bus errors and torn files at FFmpeg's make.
docker run --rm -t ${limits[@]+"${limits[@]}"} -e JOBS="$jobs" "${cache[@]}" -e TIDY_ALL="$tidy_all" -e TIDY_CHANGED="$changed" \
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
if [ "$TIDY_ALL" = 1 ]; then
    python3 -c "import json; print(\"\n\".join(sorted({e[\"file\"] for e in json.load(open(\"build-linux/compile_commands.json\")) if \"/_deps/\" not in e[\"file\"] and \"/build\" not in e[\"file\"]})))" > build-linux/tidy_files.txt
else
    printf "%s\n" "$TIDY_CHANGED" > build-linux/tidy_changed.txt
    BUILD_DIR=build-linux python3 tools/tidy-pr-select.py --changed build-linux/tidy_changed.txt > build-linux/tidy_files.txt
fi
echo "clang-tidy: $(grep -c . build-linux/tidy_files.txt || true) files"
if [ -s build-linux/tidy_files.txt ]; then
    xargs -P "$JOBS" -n 1 clang-tidy -p build-linux --quiet --extra-arg=-Wno-unknown-warning-option < build-linux/tidy_files.txt
fi
ctest --test-dir build-linux --output-on-failure -C Release
'
