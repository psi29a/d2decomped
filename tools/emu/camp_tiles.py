# SPDX-License-Identifier: GPL-3.0-or-later
"""The Rogue Encampment's picked tiles, game.exe's against d2d's.

    uv run python camp_tiles.py [FIRST-LAST] [--mutation-check]     # map seeds, default 1-6

game.exe: the camp (level 1) built from the map seed (FUN_006194a0), every room brought up
(FUN_0061b730, drlg.tiles_dump): each room's walls, floors and shadows, the tile each is
(DT1 and index) and its orientation, room-relative, made level-relative here. d2d:
`drlg-dump <mpq dir> <seed> 1 <out dir> camptiles` (GameData::town's Level::picks, laid by
lay_camp_picks). Both are sets of `layer x,y oN dt1:index`. Prints `ok: N camp tile dumps
match` when all agree. `--mutation-check` also demands that d2d's tiles for the next map seed
differ from game.exe's, so a pick that ignored the seed would be seen.
Needs build/tools/drlg-dump and mpq-cat ($D2_BUILD), $D2_MPQ_DIR, $D2_PATCH_INSTALLER.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import drlg
import emu


def game_dump(e, seed):
    """game.exe's camp tiles for `seed`: sorted `layer x,y oN dt1:index` lines, level-relative."""
    act = drlg.alloc_act(e, 0, seed, 1)
    lvl = drlg.find_level(e, act, 1)
    tiles, room_x, room_y = set(), 0, 0
    for line in drlg.tiles_dump(e, lvl).splitlines():
        if room := re.match(r"room (\d+),(\d+)", line):
            room_x, room_y = int(room[1]), int(room[2])
        elif tile := re.match(r" (wall|floor|shadow) (-?\d+),(-?\d+) (o\d+) (\S+)", line):
            tiles.add(f" {tile[1]} {int(tile[2]) + room_x},{int(tile[3]) + room_y} {tile[4]} {tile[5]}")
    return sorted(tiles)


def our_dump(first, last):
    """d2d's, per seed, for seeds first..last (drlg-dump camptiles)."""
    with tempfile.TemporaryDirectory() as out:
        subprocess.run([str(emu.BUILD / "tools/drlg-dump/drlg-dump"), emu.data_path(), f"{first}-{last}", "1", out, "camptiles"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return {seed: sorted(set(Path(out, f"{seed}.txt").read_text().splitlines())) for seed in range(first, last + 1)}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    first, last = (int(v, 0) for v in (args[0] if args else "1-6").split("-"))
    if last < first:
        sys.exit(f"seeds {first}-{last} run backwards")
    mutation = "--mutation-check" in sys.argv
    e = drlg.boot()
    ours = our_dump(first, last + (1 if mutation else 0))
    bad = []
    for seed in range(first, last + 1):
        game = game_dump(e, seed)
        if game != ours[seed]:
            bad.append((seed, [(g, o) for g, o in zip(game, ours[seed], strict=False) if g != o][:3]))
    if bad:
        for seed, diff in bad[:5]: print(f"seed {seed}: {diff}")
        sys.exit(1)
    if mutation:
        if ours[last + 1] == game_dump(e, last):
            sys.exit("mutation check: another seed's tiles came out the same, so the check doesn't see the seed")
        print("mutation check: d2d's next map seed differs from game.exe's, as it must")
    print(f"ok: {last - first + 1} camp tile dumps match")


if __name__ == "__main__":
    main()
