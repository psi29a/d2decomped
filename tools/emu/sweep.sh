#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Every level of an act ($ACT: 1, the default, to 5; towns left out) against
# game.exe, a job per core but two ($JOBS): sweep.sh [seeds] [kind...]
# e.g. ACT=2 sweep.sh 1-50 monsters objgroups   (kinds: monsters, units, tiles, seeds, objgroups, drops, collision)
# Our side runs from a Release build (build-release, configured and brought
# up to date here; same output as Debug, ~2x faster). Progress goes to
# stderr as each check finishes; the sorted results to stdout at the end.
set -eu
cd "$(dirname "$0")"
root=../..
[ -f "$root/build-release/build.ninja" ] || cmake -S "$root" -B "$root/build-release" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DD2_PATCH_INSTALLER="${D2_PATCH_INSTALLER:-$HOME/Downloads/Diablo II + LoD/patch/LODPatch_114d.exe}" >/dev/null || exit 1
cmake --build "$root/build-release" --target drlg-dump mpq-cat >/dev/null || { echo "build-release failed" >&2; exit 1; }
export D2_BUILD=build-release
uv sync -q || exit 1   # once, before the jobs: parallel first-time syncs race
seeds=${1:-1-50}
[ "$#" -eq 0 ] || shift
kinds=${*:-monsters objgroups}
case $seeds in *[!0-9-]*|*-*-*|''|-*|*-) echo "seeds must be FIRST-LAST" >&2; exit 2 ;; esac
case $seeds in *-*) ;; *) echo "seeds must be FIRST-LAST" >&2; exit 2 ;; esac
first_seed=${seeds%-*}; last_seed=${seeds#*-}
[ "$first_seed" -le "$last_seed" ] || { echo "empty seed range" >&2; exit 2; }
for kind in $kinds; do
    case $kind in tiles|units|seeds|monsters|objgroups|drops|collision|game|npcs|layout) ;;
        *) echo "unknown comparison: $kind" >&2; exit 2 ;;
    esac
done
act=${ACT:-1}; case $act in [1-5]) ;; *) echo "ACT is 1 to 5" >&2; exit 1 ;; esac
towns="1 40 75 103 109 137"   # each act's town (drlg.TOWNS), then one past Act 5's last level
first=$(( $(echo $towns | cut -d' ' -f"$act") + 1 )); last=$(( $(echo $towns | cut -d' ' -f$((act + 1))) - 1 ))
total=$(( $(echo "$kinds" | wc -w) * (last - first + 1) ))
count=$((last_seed - first_seed + 1))
cores=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
jobs=${JOBS:-$((cores > 3 ? cores - 2 : 1))}
case $jobs in *[!0-9]*|'') echo "JOBS must be a positive integer" >&2; exit 2 ;; esac
[ "$jobs" -ge 1 ] || { echo "JOBS must be at least 1" >&2; exit 2; }
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
export SWEEP_SEEDS="$seeds" SWEEP_KINDS="$kinds" SWEEP_OUT="$out" SWEEP_COUNT="$count" SWEEP_LEVELS=$((last - first + 1)) SWEEP_START=$(date +%s)
# A job per level, all kinds in it: game.exe loads once per level; the
# later levels first (the slow ones), so none starts last.
# In the background, so a signal stops the jobs and the sweep at once
# (the EXIT trap then cleans up), not after the last level. A background
# job ignores Ctrl-C, so stop() ends its whole tree.
sweep=
stop_tree() {   # the parent first, so xargs starts no level meanwhile
    children=$(pgrep -P "$1" || true); kill "$1" 2>/dev/null || true
    for child in $children; do stop_tree "$child"; done
}
stop() { [ -z "$sweep" ] || stop_tree "$sweep"; exit "$1"; }
trap 'stop 129' HUP; trap 'stop 130' INT; trap 'stop 143' TERM
seq "$last" -1 "$first" | xargs -P "$jobs" -n 1 sh -c '
    uv run python diff_drlg.py "$SWEEP_SEEDS" "$1" $SWEEP_KINDS > "$SWEEP_OUT/$1.log" 2>&1
    status=$?
    echo "$status" > "$SWEEP_OUT/$1.status"
    finished=$(ls "$SWEEP_OUT" | grep -c "\.status$")
    spent=$(($(date +%s) - SWEEP_START))
    left=$((spent * (SWEEP_LEVELS - finished) / finished))
    results=$(grep "seeds match game.exe" "$SWEEP_OUT/$1.log")
    if [ "$status" -eq 0 ] && [ -n "$results" ] && ! printf "%s\n" "$results" | grep -q -v " $SWEEP_COUNT/$SWEEP_COUNT seeds match"; then mark=ok; else mark=FAIL; fi
    printf "[%d/%d levels %dm%02ds eta %dm%02ds] %s level %s\n" "$finished" "$SWEEP_LEVELS" $((spent / 60)) $((spent % 60)) \
        $((left / 60)) $((left % 60)) "$mark" "$1" >&2
' sh &
sweep=$!
wait "$sweep"
failed=0
seen=0
for level in $(seq "$first" "$last"); do
    [ -f "$out/$level.status" ] || { echo "level $level did not finish" >&2; failed=1; continue; }
    status=$(cat "$out/$level.status")
    [ "$status" -eq 0 ] || { echo "level $level exited $status" >&2; cat "$out/$level.log" >&2; failed=1; }
    lines=$(grep 'seeds match game.exe' "$out/$level.log" || true)
    found=$(printf '%s\n' "$lines" | grep -c 'seeds match game.exe' || true)
    expected=$(echo "$kinds" | wc -w)
    [ "$found" -eq "$expected" ] || { echo "level $level: $found/$expected comparison results" >&2; failed=1; }
    printf '%s\n' "$lines" | sed '/^$/d'
    if printf '%s\n' "$lines" | grep 'seeds match game.exe' | grep -q -v " $count/$count seeds match game.exe"; then failed=1; fi
    seen=$((seen + found))
done
[ "$seen" -eq "$total" ] || failed=1
exit "$failed"
