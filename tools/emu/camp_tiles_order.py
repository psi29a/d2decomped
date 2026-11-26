# SPDX-License-Identifier: GPL-3.0-or-later
"""The Rogue Encampment's picked tiles with its rooms brought up in a given order, game.exe's against d2d's.

    for kind in list reverse shuffle; do mkdir -p OUT/$kind
        ORDER=$kind $D2_BUILD/tools/drlg-dump/drlg-dump $D2_MPQ_DIR FIRST-LAST 1 OUT/$kind camptiles; done
    uv run python camp_tiles_order.py FIRST-LAST OUT [--mutation-check]

game.exe: the camp (level 1) built from the map seed (FUN_006194a0), its rooms brought up with
FUN_0061b730 in each of ORDERS (list, reverse, shuffle: drlg.walk_order), each laying its tiles as
it comes up (FUN_0061b190), so a shared edge's re-pick rolls on whichever room came up later; then
every room's tiles (drlg.tiles_dump). d2d: OUT/<kind>/<seed>.txt, written by `ORDER=<kind>
drlg-dump ... camptiles` above (lay_camp_picks, the same order); this script runs no other program.
Both are sets of `layer x,y oN dt1:index`, as camp_tiles.py's. Prints `ok: N camp tile dumps match`
when all agree. `--mutation-check` also demands that d2d's reverse order differs from game.exe's
list order, so the match above is known to depend on the order the rooms came up in.
Needs $D2_MPQ_DIR and $D2_PATCH_INSTALLER (drlg.boot).
"""
import re
import sys
from pathlib import Path

import drlg

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


def our_dump(out, seed, kind):
    """d2d's for `seed`, rooms up in `kind` order: OUT/<kind>/<seed>.txt as a sorted set of lines."""
    return sorted(set(Path(out, kind, f"{seed}.txt").read_text().splitlines()))


def main():
    """Diff every seed in every order; exit 1 on a mismatch."""
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        sys.exit(__doc__)
    first, last = (int(v, 0) for v in args[0].split("-"))
    if last < first:
        sys.exit(f"seeds {first}-{last} run backwards")
    e = drlg.boot()
    bad, cases = [], 0
    for kind in ORDERS:
        for seed in range(first, last + 1):
            cases += 1
            game, ours = game_dump(e, seed, kind), our_dump(args[1], seed, kind)
            if game != ours:
                only_game, only_ours = sorted(set(game) - set(ours)), sorted(set(ours) - set(game))
                bad.append((seed, kind, len(only_game), len(only_ours), only_game[:2], only_ours[:2]))
    if bad:
        for seed, kind, n_game, n_ours, game, ours in bad[:6]:
            print(f"seed {seed} order {kind}: {n_game} only game.exe's {game}, {n_ours} only d2d's {ours}")
        sys.exit(1)
    if "--mutation-check" in sys.argv:
        wrong = our_dump(args[1], first, "reverse")          # d2d's rooms up in another order than game.exe's list order
        if wrong == game_dump(e, first, "list"):
            sys.exit("mutation check: another order gave the same tiles, so the check doesn't see the order")
        print("mutation check: d2d's reverse order differs from game.exe's list order, as it must")
    print(f"ok: {cases} camp tile dumps match")


if __name__ == "__main__":
    main()
