# SPDX-License-Identifier: GPL-3.0-or-later
"""The Rogue Encampment's picked tiles with its rooms brought up in a given order, game.exe's against d2d's.

    uv run python camp_tiles_order.py [FIRST-LAST] [--mutation-check]     # map seeds, default 1-4

game.exe: the camp (level 1) built from the map seed (FUN_006194a0), its rooms brought up with
FUN_0061b730 in each of $ORDERS (list, reverse, shuffle: drlg.walk_order), each laying its tiles as
it comes up (FUN_0061b190), so a shared edge's re-pick rolls on whichever room came up later; then
every room's tiles (drlg.tiles_dump). d2d: `ORDER=<kind> drlg-dump <mpq dir> <seed> 1 <out dir>
camptiles` (lay_camp_picks, the same order). Both are sets of `layer x,y oN dt1:index`, as
camp_tiles.py's. Prints `ok: N camp tile dumps match` when all agree. `--mutation-check` also runs
d2d in reverse order against game.exe's list order and demands a difference, so the match above is
known to depend on the order the rooms came up in.
Needs build/tools/drlg-dump and mpq-cat ($D2_BUILD), $D2_MPQ_DIR, $D2_PATCH_INSTALLER.
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import drlg
import emu

ORDERS = ("list", "reverse", "shuffle")


def game_dump(e, seed, kind):
    """game.exe's camp tiles for `seed`, rooms up in `kind` order: sorted `layer x,y oN dt1:index` lines, level-relative."""
    act = drlg.alloc_act(e, 0, seed, 1)
    lvl = drlg.find_level(e, act, 1)
    drlg.bring_up(e, lvl, seed, kind)
    tiles, room_x, room_y = set(), 0, 0
    for line in drlg.tiles_dump(e, lvl).splitlines():
        if room := re.match(r"room (\d+),(\d+)", line):
            room_x, room_y = int(room[1]), int(room[2])
        elif tile := re.match(r" (wall|floor|shadow) (-?\d+),(-?\d+) (o\d+) (\S+)", line):
            tiles.add(f" {tile[1]} {int(tile[2]) + room_x},{int(tile[3]) + room_y} {tile[4]} {tile[5]}")
    return sorted(tiles)


def our_dump(first, last, kind):
    """d2d's, per seed, for seeds first..last (ORDER=kind drlg-dump camptiles)."""
    with tempfile.TemporaryDirectory() as out:
        env = dict(os.environ, ORDER=kind)
        subprocess.run([str(emu.BUILD / "tools/drlg-dump/drlg-dump"), emu.data_path(), f"{first}-{last}", "1", out, "camptiles"],
                       env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return {seed: sorted(set(Path(out, f"{seed}.txt").read_text().splitlines())) for seed in range(first, last + 1)}


def main():
    """Diff every seed in every order; exit 1 on a mismatch."""
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    first, last = (int(v, 0) for v in (args[0] if args else "1-4").split("-"))
    if last < first:
        sys.exit(f"seeds {first}-{last} run backwards")
    e = drlg.boot()
    bad, cases = [], 0
    for kind in ORDERS:
        ours = our_dump(first, last, kind)
        for seed in range(first, last + 1):
            cases += 1
            game = game_dump(e, seed, kind)
            if game != ours[seed]:
                only_game, only_ours = sorted(set(game) - set(ours[seed])), sorted(set(ours[seed]) - set(game))
                bad.append((seed, kind, len(only_game), len(only_ours), only_game[:2], only_ours[:2]))
    if bad:
        for seed, kind, n_game, n_ours, game, ours in bad[:6]:
            print(f"seed {seed} order {kind}: {n_game} only game.exe's {game}, {n_ours} only d2d's {ours}")
        sys.exit(1)
    if "--mutation-check" in sys.argv:
        wrong = our_dump(first, first, "reverse")[first]          # d2d's rooms up in another order than game.exe's list order
        if wrong == game_dump(e, first, "list"):
            sys.exit("mutation check: another order gave the same tiles, so the check doesn't see the order")
        print("mutation check: d2d's reverse order differs from game.exe's list order, as it must")
    print(f"ok: {cases} camp tile dumps match")


if __name__ == "__main__":
    main()
