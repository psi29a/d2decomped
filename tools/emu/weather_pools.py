# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's rain splash and mud bubble pools, run by its own code.

    uv run --project tools/emu python tools/emu/weather_pools.py

FUN_00472320 makes the pools (FUN_006bca10: splashes 0x200 x 0x18 at
0x7a89fc, bubbles 0x80 x 0x18 at 0x7a8a00). FUN_00472da0 (a splash) and
FUN_00472ec0 (a bubble) take a slot first (FUN_006bcb40) and return
without a roll when it answers -1. Each is called past its capacity on a
fake player's seed (+0x20, through FUN_00463dd0); a call that moves the
seed made one. Then one slot is freed (FUN_006bccd0) and the next call
makes one again. d2d's rules::Rain caps them at Rain::kSplashPool and
kBubblePool; prints what game.exe made.
"""
import sys

import emu

SPLASHES, BUBBLES = 0x7A89FC, 0x7A8A00
POOLS = ((0x472DA0, SPLASHES, 0x200, "splashes"), (0x472EC0, BUBBLES, 0x80, "bubbles"))
FREE = 0x6BCCD0


def rolled(emulator, player, function):
    """One spawn call at (100, 200): did it move the player's seed?"""
    before = emulator.read(player + 0x20, 8)
    emulator.call(function, 100, 200)
    return emulator.read(player + 0x20, 8) != before


def main():
    emulator = emu.Emu()
    emulator.crt_init()
    player = emulator.alloc(0x100)
    emulator.hook(0x463DD0, lambda _emu: player, 0)    # the player unit: its seed at +0x20 / +0x24
    emulator.hook(0x4F51D0, lambda _emu: 0, 0)         # the other draw mode's test: off
    emulator.call(0x472320)
    bad = 0
    for function, pool, capacity, name in POOLS:
        emulator.w32(player + 0x20, 1234)
        emulator.w32(player + 0x24, 666)

        made = sum(rolled(emulator, player, function) for _ in range(capacity + 5))
        emulator.call(FREE, ecx=emulator.r32(pool), edx=7 << 16)
        again = [rolled(emulator, player, function) for _ in range(2)]
        print(f"{name}: game.exe made {made} of {capacity + 5}, then {sum(again)} of 2 after a slot freed")
        bad += made != capacity or again != [True, False]
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
