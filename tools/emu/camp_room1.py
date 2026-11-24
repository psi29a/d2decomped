# SPDX-License-Identifier: GPL-3.0-or-later
"""The Rogue Encampment's rooms' room1 seeds (room1 +0x6c), game.exe's against d2d's.

    uv run python camp_room1.py [FIRST-LAST] [--mutation-check]     # map seeds, default 1-6

game.exe: the camp (level 1) built from the map seed (FUN_006194a0), its rooms brought
up with FUN_0061b730 in each of $ORDERS (list, reverse, shuffle: drlg.walk_order), then every
room's room1 seed (drlg.seeds_dump). d2d: `drlg-dump <mpq dir> <seed> 1 <out dir> camp`
(preset_room1_seeds, the same order). Prints `ok: N camp room1 seed dumps match` when all
agree. `--mutation-check` also runs d2d with the order game.exe did not use and demands a
difference, so the match above is known to depend on the order the rooms came up in.
Needs build/tools/drlg-dump and mpq-cat ($D2_BUILD), $D2_MPQ_DIR, $D2_PATCH_INSTALLER.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import drlg
import emu

ORDERS = ("list", "reverse", "shuffle")


def game_dump(e, seed, kind):
    """game.exe's camp room1 seeds for `seed`, rooms up in `kind` order."""
    act = drlg.alloc_act(e, 0, seed, 1)
    lvl = drlg.find_level(e, act, 1)
    drlg.bring_up(e, lvl, seed, kind)
    return drlg.seeds_dump(e, lvl).splitlines()


def our_dump(first, last, kind):
    """d2d's, per seed, for seeds first..last (drlg-dump camp)."""
    with tempfile.TemporaryDirectory() as out:
        env = dict(os.environ, ORDER=kind)
        subprocess.run([str(emu.BUILD / "tools/drlg-dump/drlg-dump"), emu.data_path(), f"{first}-{last}", "1", out, "camp"],
                       env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return {seed: Path(out, f"{seed}.txt").read_text().splitlines() for seed in range(first, last + 1)}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    first, last = (int(v, 0) for v in (args[0] if args else "1-6").split("-"))
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
                bad.append((seed, kind, [(g, o) for g, o in zip(game, ours[seed], strict=False) if g != o][:3]))
    if bad:
        for seed, kind, diff in bad[:5]: print(f"seed {seed} order {kind}: {diff}")
        sys.exit(1)
    if "--mutation-check" in sys.argv:
        wrong = our_dump(first, first, "reverse")[first]          # d2d's rooms up in another order than game.exe's list order
        if wrong == game_dump(e, first, "list"):
            sys.exit("mutation check: another order gave the same seeds, so the check doesn't see the order")
        print("mutation check: d2d's reverse order differs from game.exe's list order, as it must")
    print(f"ok: {cases} camp room1 seed dumps match")


if __name__ == "__main__":
    main()
