"""Diff our level generator against game.exe's over a range of map seeds.

    uv run python diff_drlg.py 1-3000 [level] [tiles|units|seeds|monsters]     # level defaults to 2 (the Blood Moor)

Runs build/tools/drlg-dump for the range, game.exe's generator in the
emulator for the same seeds, and prints how many match plus the first
differing lines of the first few mismatches.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import drlg
import emu
import monsters


def main():
    first, last = (int(v, 0) for v in sys.argv[1].split("-"))
    lid = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    tiles = sys.argv[-1] in ("tiles", "units", "seeds", "monsters")
    what = sys.argv[-1]
    env = dict(os.environ)
    env.setdefault("D2_PATCH_INSTALLER", str(Path.home() / "Downloads/Diablo II + LoD/patch/LODPatch_114d.exe"))
    with tempfile.TemporaryDirectory() as out:
        subprocess.run([str(emu.ROOT / "build/tools/drlg-dump/drlg-dump"), emu.data_path(), f"{first}-{last}", str(lid), out] + ([what] if tiles else []),
                       env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        e = drlg.boot()
        bad = []
        rooms = [0, 0]                                  # monsters: rooms alike, rooms populated by either
        for seed in range(first, last + 1):
            if e.brk - emu.HEAP > emu.HEAP_SIZE * 7 // 10: e = drlg.boot(fresh=True)   # the bump heap never frees
            if what == "monsters":                      # monsters.py's population, room by room ($DIFFICULTY)
                game = monsters.dump(e, seed, lid, int(os.environ.get("DIFFICULTY", 0))).splitlines()
                g, o = ({l.split(":")[0]: l for l in lines} for lines in (game, Path(out, f"{seed}.txt").read_text().splitlines()))
                rooms[0] += sum(g[k] == o.get(k) for k in g)
                rooms[1] += len(g.keys() | o.keys())
            else:
                game = drlg.level_dump(e, seed, lid).splitlines()
            if tiles and what != "monsters":
                t = drlg.tiles_dump(e, drlg._last_level)
                game += (t if what == "tiles" else drlg.units_dump(e, drlg._last_level) if what == "units" else drlg.seeds_dump(e, drlg._last_level)).splitlines()
            ours = Path(out, f"{seed}.txt").read_text().splitlines()
            if game != ours:
                bad.append(seed)
                if len(bad) <= 3:
                    diff = [(i, g, o) for i, (g, o) in enumerate(zip(game, ours)) if g != o][:4]
                    print(f"seed {seed}: {len(game)} vs {len(ours)} lines; first diffs:")
                    for i, g, o in diff: print(f"  line {i + 1}\n    game: {g}\n    ours: {o}")
    n = last - first + 1
    if what == "monsters": print(f"level {lid}: {rooms[0]}/{rooms[1]} populated rooms match game.exe")
    print(f"level {lid}: {n - len(bad)}/{n} seeds match game.exe" + (f"; mismatches {bad[:20]}" if bad else ""))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
