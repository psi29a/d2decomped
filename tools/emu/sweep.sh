#!/bin/sh
# Every Act 1 level against game.exe, 8 at a time: sweep.sh [seeds] [kind...]
# e.g. sweep.sh 1-50 monsters objgroups   (kinds: monsters, units, tiles, seeds, objgroups, drops)
cd "$(dirname "$0")"
seeds=${1:-1-50}; shift; kinds=${*:-monsters objgroups}
for k in $kinds; do for l in $(seq 2 39); do echo "$l $k"; done; done |
    xargs -P "${JOBS:-8}" -n 2 sh -c 'uv run python diff_drlg.py '"$seeds"' $0 $1 2>&1 | grep "seeds match" | sed "s/^/$1 /"' | sort -k3 -n
