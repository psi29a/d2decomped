#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Every Act 1 level against game.exe, a job per core but two ($JOBS): sweep.sh [seeds] [kind...]
# e.g. sweep.sh 1-50 monsters objgroups   (kinds: monsters, units, tiles, seeds, objgroups, drops, collision)
# Our side runs from a Release build (build-release, configured and brought
# up to date here; same output as Debug, ~2x faster). Progress goes to
# stderr as each check finishes; the sorted results to stdout at the end.
cd "$(dirname "$0")"
root=../..
[ -f "$root/build-release/build.ninja" ] || cmake -S "$root" -B "$root/build-release" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DD2_PATCH_INSTALLER="${D2_PATCH_INSTALLER:-$HOME/Downloads/Diablo II + LoD/patch/LODPatch_114d.exe}" >/dev/null || exit 1
cmake --build "$root/build-release" --target drlg-dump mpq-cat >/dev/null || { echo "build-release failed" >&2; exit 1; }
export D2_BUILD=build-release
seeds=${1:-1-50}; shift; kinds=${*:-monsters objgroups}
total=$(( $(echo $kinds | wc -w) * 38 )); count=$(echo "$seeds" | awk -F- '{print $2 - $1 + 1}')
start=$(date +%s)
for k in $kinds; do for l in $(seq 2 39); do echo "$l $k"; done; done |
    xargs -P "${JOBS:-$(( $(sysctl -n hw.ncpu 2>/dev/null || nproc) - 2 ))}" -n 2 sh -c 'uv run python diff_drlg.py '"$seeds"' $0 $1 2>&1 | grep "seeds match" | sed "s/^/$1 /"' |
    while read -r line; do
        finished=$((${finished:-0} + 1)); now=$(date +%s); spent=$((now - start))
        case $line in *" $count/$count seeds match"*) mark=ok ;; *) mark=FAIL ;; esac
        printf '[%d/%d %dm%02ds eta %dm%02ds] %s %s\n' "$finished" "$total" $((spent / 60)) $((spent % 60)) \
            $(( spent * (total - finished) / finished / 60 )) $(( spent * (total - finished) / finished % 60 )) "$mark" "$line" >&2
        echo "$line"
    done | sort -k3 -n
