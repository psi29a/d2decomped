# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's waypoint refusal window (C->S 0x49) as the oracle.

The server's 0x49 path runs natively: the dispatcher FUN_0054d750 (its busy
test FUN_0057eec0), the handler FUN_0054c5d0, its timer read FUN_0055b6c0
(player data +0x160; refused while that + 10000 > GetTickCount: voice event
0x13 and FUN_00554190 ends the interaction) and FUN_00554190. A hostile
declaration runs game.exe's own setter FUN_0055b720, as FUN_005a5e50 calls
it; it is the only code that writes player data +0x160. A level change runs
nothing: no code on that path writes the field.

Hooked (not the claim): GetTickCount (the case's clock); FUN_006221a0 (the
player data: a zeroed block); FUN_00549570 (the target check: 0 = in range,
same act, waypoint active; 2 = not active); FUN_00584f60 (the travel:
recorded); FUN_00553380 (the voice: recorded); FUN_00554d00 / FUN_00552f60
(the error path's unit lookups: 0, so it only returns); FUN_00410a80 (the
dispatcher's timing log, off with game +0x1dc4 = 0).

    uv run python wp_refuse.py gen 100000 7 > cases.txt          # --solo: no hostile declarations
    uv run python wp_refuse.py game cases.txt > g.txt
    ../../build/tests/test_game --waypoint-refuse cases.txt | grep -E '^[0-9]+ ' | diff g.txt -

A case is `id now active L l1 .. H h1 ..`: the GetTickCount of the 0x49,
whether the target waypoint is active, then the count and times of the
level changes and of the player's hostile declarations before it. `game`
prints `id refused` (1: no travel). GetTickCount stays in 70,000 .. 2^31:
past the first 10 s of uptime (zeroed player data refuses there) and the
wrap.
"""
import random
import sys

import emu

T_MIN, T_MAX = 70_000, 1 << 31


def gen(n_random, seed, solo):
    rng = random.Random(seed)
    cases = []
    now = 1_000_000
    hostile_grid = [None] if solo else [None] + list(range(0, 20001, 250))
    level_grid = range(0, 20001, 1 if solo else 100)
    for dl in level_grid:                    # ms since the last level change
        for dh in hostile_grid:              # ms since the last hostile declaration (None: never)
            for active in (1, 0):
                cases.append((now, active, [now - dl], [] if dh is None else [now - dh]))
    for _ in range(n_random):
        now = rng.randrange(T_MIN, T_MAX)
        levels = [now - rng.choice((rng.randrange(0, 12000), rng.randrange(0, 600000))) for _ in range(rng.randrange(1, 4))]
        hostiles = [] if solo else [now - rng.choice((rng.randrange(0, 12000), rng.randrange(0, 60000))) for _ in range(rng.choice((0, 0, 0, 1, 2)))]
        cases.append((now, int(rng.random() < 0.9), sorted(levels), sorted(hostiles)))
    for i, (now, active, levels, hostiles) in enumerate(cases):
        print(i, now, active, len(levels), *levels, len(hostiles), *hostiles)


class Oracle:
    def __init__(self):
        self.cur = {"now": 0, "active": 1}
        emu.Emu.w_GetTickCount = lambda _self: (self.cur["now"], 0)
        self.e = e = emu.Emu()
        self.out = {}
        self.game, self.player, self.other = e.alloc(0x2000), e.alloc(0x200), e.alloc(0x200)
        self.pcdata, self.packet = e.alloc(0x400), e.alloc(16)
        e.w32(self.player, 0); e.w32(self.player + 0xc, 1); e.w32(self.player + 0x10, 1)   # a player, id 1, mode NU
        e.w32(self.other, 0); e.w32(self.other + 0xc, 2)
        e.w32(self.game + 0x1dc4, 0)
        e.hook(0x6221a0, lambda _e: self.pcdata, 1)
        e.hook(0x549570, lambda _e: 0 if self.cur["active"] else 2, 2)
        e.hook(0x554d00, lambda _e: 0, 0)
        e.hook(0x552f60, lambda _e: 0, 1)
        e.hook(0x584f60, lambda _e: self.out.__setitem__("travel", 1) or 0, 2)
        e.hook(0x553380, lambda _e: 0, 1)
        e.hook(0x410a80, lambda _e: 0, 0)

    def refused(self, now, active, hostiles):
        e = self.e
        self.cur["active"] = active
        self.out.clear()
        e.mu.mem_write(self.pcdata, b"\0" * 0x400)
        for t in hostiles:
            self.cur["now"] = t
            e.call(0x55b720, (t + 60000) & 0xffffffff, ecx=self.player, edx=self.other)
        self.cur["now"] = now
        e.mu.mem_write(self.packet, bytes([0x49]) + (900).to_bytes(4, "little") + (3).to_bytes(2, "little") + b"\0\0")
        e.w32(self.player + 0x64, 0); e.w32(self.player + 0x68, 0)
        e.call(0x54d750, self.packet, 9, ecx=self.game, edx=self.player)
        return 0 if self.out.get("travel") else 1


def parse(line):
    p = [int(x) for x in line.split()]
    nl = p[3]
    levels = p[4:4 + nl]
    nh = p[4 + nl]
    return p[0], p[1], p[2], levels, p[5 + nl:5 + nl + nh]


def main():
    if len(sys.argv) > 3 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]), "--solo" in sys.argv[4:])
    elif len(sys.argv) > 2 and sys.argv[1] == "game":
        o = Oracle()
        with open(sys.argv[2]) as f:
            for line in f:
                if line.strip():
                    i, now, active, _levels, hostiles = parse(line)
                    print(i, o.refused(now, active, hostiles))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
