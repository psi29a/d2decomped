# SPDX-License-Identifier: GPL-3.0-or-later
"""A missile's frame direction: game.exe aims it as it makes it (FUN_0059fa30,
the 64-step direction at path +0x64), and the draw reads the frame of an
n-direction sprite from it (FUN_00620100 -> FUN_006487f0, then
FUN_00600c90: the table at 0x6e45a0, [log2(n) + 1][dir64]).

    uv run python missile_dirs.py [cases] [seed]          # "dx<TAB>dy" jobs, then game.exe's answers
    uv run python missile_dirs.py table                  # the table's rows, 1 to 32 directions

Each case puts a Quill Rat at a random free Blood Moor subtile (missiles.py's
game) and fires Fire Bolt at a random point. It prints, a line a case,
"dx dy: d8 d16 d32" (the target less the shooter, in subtiles), the form
tools/missile-dir-dump prints d2d's (d2d::game::missile_facing, then
missile_direction). Diff the two:

    uv run python missile_dirs.py 2000 > game.txt
    cut -d: -f1 game.txt | tr ' ' '\\t' | ../../build/tools/missile-dir-dump/missile-dir-dump > port.txt
    diff game.txt port.txt
"""
import random
import sys

import missiles

FIREBOLT = 58


def frame(e, dir64, dirs):
    """FUN_00600c90(dir64, dirs): stdcall, the DCC direction."""
    return e.call(0x600c90, dir64, dirs)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    game = missiles.Game()
    e = game.e
    if args and args[0] == "table":
        for dirs in (1, 2, 4, 8, 16, 32):
            print(f"{dirs}: " + " ".join(str(frame(e, d, dirs)) for d in range(64)))
        return
    cases = int(args[0]) if args else 1000
    rng = random.Random(int(args[1]) if len(args) > 1 else 1)
    done = 0
    while done < cases:
        case = missiles.make_case(game, rng)
        shooter = game.unit(63, *case["from"])
        if not shooter: continue
        src, sx, sy, _ = shooter
        span = rng.choice((2, 5, 12, 30))
        tx, ty = sx + rng.randint(-span, span), sy + rng.randint(-span, span)
        if (tx, ty) == (sx, sy): continue
        p = e.alloc(0x60)
        for i, v in {0: 0x21, 1: src, 4: FIREBOLT, 5: sx, 6: sy, 7: tx, 8: ty, 0xc: 1}.items(): e.w32(p + 4 * i, v)
        m = e.call(0x59fa30, ecx=game.g, edx=p)
        if not m: continue
        dir64 = e.call(0x620100, m) & 0xff                  # FUN_006487f0 returns the byte in AL
        print(f"{tx - sx} {ty - sy}: {frame(e, dir64, 8)} {frame(e, dir64, 16)} {frame(e, dir64, 32)}")
        done += 1


if __name__ == "__main__":
    main()
