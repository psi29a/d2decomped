# SPDX-License-Identifier: GPL-3.0-or-later
"""The life / mana / stamina value the HUD shows, as the oracle: FUN_00496dd0.

    uv run python globe_glide.py gen 2000 1 > cases.tsv
    uv run python globe_glide.py game cases.tsv > g.txt     # each case line + game.exe's shown value

FUN_00496dd0 (EAX = stat: 6 life, 8 mana, 10 stamina) is what the life globe
(FUN_00496f80), the mana globe (FUN_00497110), the stamina bar (FUN_004975b0)
and the globe text (FUN_00498120) read instead of the stat itself: the value
glides over 7..15 frames to a change smaller than max / 8, and a life loss
shows at once. Its state is three 16-byte slots at 0x7bef30 (+0 the last
value, +4 its frame, +8 the glide's start value, +0xc its frame), one a stat.

A case line is `seq stat frame cur max` (cur and max in 256ths, as the stat
reads). The lines of one seq are one run of calls in order: the slots are
zeroed when a new seq starts. `game` hooks the player (0x463dd0), the stat
(0x625480 -> cur), its max (0x625d10 / 0x625d60 / 0x625db0 -> max) and the
frame (0x44da90 -> frame), runs FUN_00496dd0 for real (its _ftol 0x682fd0
too) and prints the case line with the shown value after it.

The x87 runs at Windows' starting control word, 0x27f (53-bit precision, as
a double). Unicorn's x87 otherwise runs at 64 bits (it honours the precision
bits: 0x37f misses the same 12 calls as no write), and then 1 call in ~6000
is one off (5 / 7 * -175 is -125.0 in a double, -124.99... at 64 bits, then
_ftol truncates). game.exe only flips the rounding bits (or 0xc00 around a
fistp); none of its fldcw sets the precision.
So the glide is (double)(frame - t1 + 1) / dur * (V - F), truncated toward 0.

What the run shows (a Python model of the queue's steps, in doubles, matched
69703 of 69703 calls, gen 2000 1): the steps as char-panel.md has them.
"""
import random
import sys

from unicorn.x86_const import UC_X86_REG_FPCW

import emu

SLOTS, SLOTS_SIZE = 0x7BEF30, 0x30
STATS = (6, 8, 10)


def gen(count, seed):
    rng = random.Random(seed)
    for seq in range(count):
        stat = rng.choice(STATS)
        top = rng.choice((1, 7, 40, 255, 1000, 5000)) * 256 + rng.choice((0, 0, rng.randrange(256)))
        cur = rng.randrange(top + 1)
        frame = rng.choice((0, 1, 2, rng.randrange(3, 100000)))
        for _ in range(rng.randint(10, 60)):
            kind = rng.random()
            if kind < 0.35:
                pass                                                   # steady
            elif kind < 0.55:
                cur += rng.randint(-max(1, top // 9), max(1, top // 9))  # under max / 8
            elif kind < 0.7:
                cur += rng.randint(-top, top)                          # a big step
            elif kind < 0.8:
                cur = top                                              # topped up
            elif kind < 0.85:
                cur = rng.choice((0, -rng.randrange(1, 512)))          # empty, or below
            elif kind < 0.9:
                top = max(256, top + rng.randint(-top // 4, top // 4)) # max moves
            else:
                cur += rng.randint(-8, 8)                              # a nudge
            cur = min(cur, top + rng.choice((0, 0, 0, 256)))
            print(seq, stat, frame & 0xFFFFFFFF, cur, top)
            frame += rng.choice((1, 1, 1, 1, 0, 2, 3, rng.randint(4, 40)))


def game(path):
    e = emu.Emu()
    e.mu.reg_write(UC_X86_REG_FPCW, 0x27F)        # Windows' starting x87 state: 53-bit precision
    player = e.alloc(0x200)
    now = {"frame": 0, "cur": 0, "max": 0}
    e.hook(0x463DD0, lambda e: player, 0)
    e.hook(0x625480, lambda e: now["cur"], 3)
    for addr in (0x625D10, 0x625D60, 0x625DB0):
        e.hook(addr, lambda e: now["max"], 1)
    e.hook(0x44DA90, lambda e: now["frame"], 0)
    last = None
    for line in open(path):
        seq, stat, frame, cur, top = map(int, line.split())
        if seq != last:
            e.mu.mem_write(SLOTS, bytes(SLOTS_SIZE))
            last = seq
        now.update(frame=frame, cur=cur, max=top)
        shown = e.call(0x496DD0, regs={"eax": stat})
        print(seq, stat, frame, cur, top, shown - (1 << 32) if shown >> 31 else shown)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    else:
        sys.exit(__doc__)
