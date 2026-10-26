# SPDX-License-Identifier: GPL-3.0-or-later
"""Check every level's collision against jaenster/libd2's game.exe captures.

    D2_BUILD=build-release uv run python libd2_crc.py [first-last] [normal,nightmare,hell]

libd2 (MIT) captured, from the real 1.14d engine, one checksum per level
for map seeds 1..200 at each difficulty (packages/drlg/src/golden,
coll_crc_verify.zig). drlg-dump <dir> <seeds> 0 crc prints ours in the
same form. A second opinion only: a mismatch is traced in game.exe
(diff_drlg.py ... collision) before anything changes. Prints, per
difficulty, how many levels match and each failing level's seed count.
Exit status 1 when any level differs.
"""
import gzip
import json
import os
import subprocess
import sys
import urllib.request
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import emu

URL = "https://raw.githubusercontent.com/jaenster/libd2/{rev}/packages/drlg/src/golden/coll_crc_200_{name}.jsonl.gz"
REV = "main"                                      # ponytail: pin a commit if their goldens ever change under us
CACHE = Path(__file__).parent / ".cache" / "libd2"
DIFFICULTIES = {"normal": 0, "nightmare": 1, "hell": 2}


def golden(name):
    path = CACHE / f"coll_crc_200_{name}.jsonl.gz"
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(URL.format(rev=REV, name=name), path)
    with gzip.open(path, "rt") as lines:
        return {(r["seed"], r["levelId"]): r for r in map(json.loads, lines) if r.get("evt") == "drlg_coll_crc"}


def ours(first, last, difficulty):
    env = dict(os.environ, DIFFICULTY=str(difficulty))
    env.setdefault("D2_PATCH_INSTALLER", str(Path.home() / "Downloads/Diablo II + LoD/patch/LODPatch_114d.exe"))

    def run(seeds):
        out = subprocess.run([str(emu.BUILD / "tools/drlg-dump/drlg-dump"), emu.data_path(), seeds, "0", "crc"],
                             env=env, capture_output=True, text=True, check=True).stdout
        return [json.loads(line) for line in out.splitlines() if line.startswith("{")]

    chunk = max(1, (last - first + 1) // (os.cpu_count() or 4) + 1)
    ranges = [f"{s}-{min(s + chunk - 1, last)}" for s in range(first, last + 1, chunk)]
    with ThreadPoolExecutor(len(ranges)) as pool:
        return {(r["seed"], r["levelId"]): r for part in pool.map(run, ranges) for r in part}


def main():
    first, last = (int(v) for v in (sys.argv[1] if len(sys.argv) > 1 else "1-200").split("-"))
    names = sys.argv[2].split(",") if len(sys.argv) > 2 else list(DIFFICULTIES)
    failed = False
    for name in names:
        want = {key: r for key, r in golden(name).items() if first <= key[0] <= last}
        if not want: sys.exit(f"{name}: libd2 has no seeds in {first}-{last} (it covers 1-200)")
        got = ours(first, last, DIFFICULTIES[name])
        bad = Counter(level for (seed, level), r in want.items() if got.get((seed, level), {}).get("crc") != r["crc"])
        levels = {level for _, level in want}
        print(f"{name}: {len(levels) - len(bad)}/{len(levels)} levels match on all {last - first + 1} seeds")
        for level, count in sorted(bad.items()):
            seeds = sorted(seed for (seed, lvl), r in want.items() if lvl == level and got.get((seed, lvl), {}).get("crc") != r["crc"])
            print(f"  level {level}: {count} seeds differ (first {seeds[:5]})")
        failed |= bool(bad)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
