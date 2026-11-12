# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's player Teleport destination rule as the oracle: srvdofunc 27
(FUN_005ca360) and the warp it calls (FUN_00554ea0: the target room
FUN_00463740, the nearest free spot FUN_0064e7b0 -> FUN_0064dea0, the
collision test FUN_0064d9b0), run natively on synthetic rooms.

    uv run python sk_tele_spot.py gen 20000 1 --no-units > cases.tsv
    uv run python sk_tele_spot.py game cases.tsv > game.tsv
    ../../build/tests/test_tele_spot --tele-spot cases.tsv | grep -E '^[0-9]+ ' > d2d.tsv    # tests/game/test_tele_spot.cpp
    uv run python sk_tele_spot.py diff game.tsv d2d.tsv cases.tsv
    uv run python sk_tele_spot.py probe          # where the snap lands round a blocked target

The same script is the oracle of a level warp's arrival spot (FUN_005550b0):

    uv run python sk_tele_spot.py gen --warp 20000 1 > cases.tsv
    uv run python sk_tele_spot.py warp cases.tsv > game.tsv
    ../../build/tests/test_warp_arrival --warp-arrival cases.tsv | grep -E '^[0-9]+ ' > d2d.tsv    # tests/game/test_warp_arrival.cpp
    uv run python sk_tele_spot.py diff game.tsv d2d.tsv cases.tsv

A warp case is `id 1 x y x y n {x y bits}...`: the other side's warp unit's
subtile (twice, so the lines parse as above) and the collision words round
it. `warp` runs FUN_0064e7b0 with FUN_005550b0's arguments (0x555144 to
0x555157: size 2, mask 0x1c09, flag 1: no spot returns the unit's own
subtile) and then FUN_00554ea0 from that subtile (0x5551bc: flag 0, a unit
that is typed a monster so it takes the short tail), as the warp does; the
answer is `id 1 x y` where FUN_00650be0 put the unit, `id 0 - -` when
FUN_00554ea0 finds no spot (the warp is not taken). The world is one room
for the whole 200 x 200 window (U: a ring that crosses room edges is not
modelled). `gen --warp` blocks a patch round the unit; a few cases fill every ring to 44 to 50 so that some
are refused.

The world: 5 x 5 rooms of 40 x 40 subtiles (outdoor rooms are 8 x 8 tiles);
the caster stands in the centre room (2, 2). A room's near list is the room
and the up to 8 rooms touching it. A case is
`id tele cx cy tx ty n {x y bits}...`: the level's Teleport column, the
caster's and the target's subtile, then the non-zero collision words.
`game` prints `id do x y`: 1 when FUN_005ca360 put the unit somewhere
(FUN_00650be0 recorded the subtile), else 0 and `- -`. d2d's test prints the
same lines. The units' bit 0x1000 is not in the cases `gen` makes for d2d:
d2d keeps units in the Crowd, not the grid, so use `gen --no-units`.

Hooked (not the claim): FUN_0056d2c0 (the target point: the case's), the
caster's room FUN_00620bb0, FUN_0061a1b0 / FUN_0061db70 (a fake Levels row:
byte +4 = Teleport), FUN_006229f0 (the clear-line test for Teleport 2:
recorded, answers "clear"), FUN_00619790 (a room's near list),
FUN_0061a010 (a room's collision grid: {x0, y0, w, h, ..., +0x20 words}),
FUN_00620510 (the unit's size: 2, game.exe's answer for a player, read in
its jump table at 0x62057c), FUN_00650be0 (the position set: recorded),
FUN_0064c040 and FUN_00554670 (the room move bookkeeping). The unit is
typed a monster (dword 0 = 1) so FUN_00554ea0 takes its short tail; the
player's tail (client packets, the 20-step ring, the 50-frame warp spot,
the pets' warp FUN_005754b0) sets no position. Runs natively: FUN_005ca360,
FUN_00554ea0, FUN_00463740, FUN_0064e7b0, FUN_0064dea0, FUN_0064d9b0 and
the footprint test under it.

Tags: V (the oracle's own run): the room rule and the ring search come out
of game.exe's code, not a model. U: the near list is the 3 x 3 touching rooms
(a hook); an irregular level's rooms are not covered. Teleport 2's clear-line
test is hooked to "clear", so it is not claimed.
"""
import random
import sys

ROOM, GRID = 40, 5
SIDE = ROOM * GRID
MASK = 0x1c09
UNITS = 0x1000


class Oracle:
    def __init__(self):
        from unicorn.x86_const import UC_X86_REG_ECX

        import emu
        self.e = e = emu.Emu()
        self.rooms, self.coll = {}, {}
        for i in range(GRID):
            for j in range(GRID):
                room, coll = e.alloc(0x100), e.alloc(0x40)
                rect = e.view("room", room)
                rect.x, rect.y, rect.w, rect.h = i * ROOM, j * ROOM, ROOM, ROOM
                grid = e.view("grid", coll)
                grid.x, grid.y, grid.w, grid.h = i * ROOM, j * ROOM, ROOM, ROOM
                grid.x_copy, grid.y_copy, grid.w_copy, grid.h_copy = i * ROOM, j * ROOM, ROOM, ROOM
                grid.cells = e.alloc(ROOM * ROOM * 2)
                self.rooms[(i, j)], self.coll[room] = room, (i, j, coll)
        self.near = {}
        for (i, j), room in self.rooms.items():
            near = [self.rooms[(a, b)] for a in range(i - 1, i + 2) for b in range(j - 1, j + 2) if (a, b) in self.rooms]
            array = e.alloc(4 * len(near))
            for k, other in enumerate(near):
                e.w32(array + 4 * k, other)
            self.near[room] = (array, len(near))
        self.unit, self.path, self.levelrow = e.alloc(0x200), e.alloc(0x100), e.alloc(0x100)
        unit = e.view("unit", self.unit)
        unit.type = 1                                            # a monster: FUN_00554ea0's short tail
        unit.path = self.path
        self.xy = (0, 0)
        self.here = self.rooms[(2, 2)]
        self.moved = None
        self.line = None
        self.xy_buf = e.alloc(8)
        e.hook(0x619790, self.near_list, 3)
        e.hook(0x61a010, lambda e: self.coll[e.mu.reg_read(UC_X86_REG_ECX)][2], 0)
        e.hook(0x620bb0, lambda e: self.here, 1)
        e.stub(2, {0x620510: 1})
        e.hook(0x650be0, self.set_pos, 5)
        e.stub(0, {0x64c040: 1, 0x554670: 1})
        e.hook(0x56d2c0, self.target, 2)
        e.stub(0x1000, {0x61a1b0: 1})          # a field level's id (U: only that it is not a town)
        e.stub(self.levelrow, {0x61db70: 1})
        e.hook(0x6229f0, self.line_test, 4)

    def near_list(self, e):
        array, count = self.near[e.arg(0)]
        e.w32(e.arg(1), array)
        e.w32(e.arg(2), count)
        return 0

    def set_pos(self, e):
        self.moved = (e.arg(3), e.arg(4))
        return 1

    def target(self, e):
        e.w32(e.arg(0), self.xy[0])
        e.w32(e.arg(1), self.xy[1])
        return 1

    def line_test(self, e):
        self.line = (e.arg(1), e.arg(2), e.arg(3))
        return 0

    def load(self, words):
        for (i, j), room in self.rooms.items():
            buf = bytearray(ROOM * ROOM * 2)
            for (x, y), value in words.items():
                if i * ROOM <= x < (i + 1) * ROOM and j * ROOM <= y < (j + 1) * ROOM:
                    at = ((y - j * ROOM) * ROOM + (x - i * ROOM)) * 2
                    buf[at:at + 2] = value.to_bytes(2, "little")
            self.e.mu.mem_write(self.e.view("grid", self.coll[room][2]).cells, bytes(buf))

    def run(self, tele, cx, cy, tx, ty, words):
        self.load(words)
        self.e.view("levels", self.levelrow).teleport = tele
        self.here = self.rooms[(cx // ROOM, cy // ROOM)]
        self.xy, self.moved, self.line = (tx & 0xFFFFFFFF, ty & 0xFFFFFFFF), None, None
        ret = self.e.call(0x5ca360, 0, 0, ecx=0x2000, edx=self.unit) & 0xFFFFFFFF
        return ret, self.moved


class WarpOracle(Oracle):
    """One room for the whole 200 x 200 window, as d2d's collision is one array: the ring search never crosses a room
    edge. (FUN_0064dea0 looks each subtile's room up from the room it last found, so on a level of several rooms a
    ring can lose a neighbour; the real-level runs cover that, this world does not model it: U.)"""

    def __init__(self):
        super().__init__()
        e = self.e
        room, coll = e.alloc(0x100), e.alloc(0x40)
        rect = e.view("room", room)
        rect.x, rect.y, rect.w, rect.h = 0, 0, SIDE, SIDE
        grid = e.view("grid", coll)
        grid.x, grid.y, grid.w, grid.h = 0, 0, SIDE, SIDE
        grid.x_copy, grid.y_copy, grid.w_copy, grid.h_copy = 0, 0, SIDE, SIDE
        grid.cells = e.alloc(SIDE * SIDE * 2)
        array = e.alloc(4)
        e.w32(array, room)
        self.rooms, self.coll, self.near = {(0, 0): room}, {room: (0, 0, coll)}, {room: (array, 1)}
        self.here = room

    def load(self, words):
        buf = bytearray(SIDE * SIDE * 2)
        for (x, y), value in words.items():
            at = (y * SIDE + x) * 2
            buf[at:at + 2] = value.to_bytes(2, "little")
        self.e.mu.mem_write(self.e.view("grid", self.coll[self.here][2]).cells, bytes(buf))

    def warp(self, ux, uy, words):
        """FUN_005550b0's two spot searches for a unit on subtile (ux, uy): (did, (x, y) or None)."""
        e = self.e
        self.load(words)
        self.moved = None
        e.w32(self.xy_buf, ux)
        e.w32(self.xy_buf + 4, uy)
        room = e.call(0x64e7b0, self.here, self.xy_buf, 2, MASK, 1) & 0xFFFFFFFF          # 0x555144..0x555157
        if not room:
            return 0, None
        x, y = e.s32(self.xy_buf), e.s32(self.xy_buf + 4)
        ret = e.call(0x554ea0, room, x, y, 0, 0, ecx=0x2000, edx=self.unit) & 0xFFFFFFFF     # 0x5551bc
        return ret, self.moved


def make_warp_case(rng):
    """One warp case: a warp unit in the middle of the window with a patch of walls round it, or (5 percent) every ring to 44..50 filled."""
    ux, uy = rng.randint(70, 130), rng.randint(70, 130)
    words = {}
    if rng.random() < 0.05:
        radius = rng.choice((44, 45, 46, 47, 48, 49, 50))
        for y in range(uy - radius, uy + radius + 1):
            for x in range(ux - radius, ux + radius + 1):
                words[(x, y)] = 1
    else:
        radius = rng.choice((1, 2, 3, 4, 6, 9, 14))
        density = rng.choice((0.3, 0.5, 0.7, 0.85, 0.95, 1.0))
        for y in range(uy - radius, uy + radius + 1):
            for x in range(ux - radius, ux + radius + 1):
                if rng.random() < density:
                    words[(x, y)] = rng.choice((0x1, 0x1, 0x9, 0x400, 0x800, 0x4))
        for _ in range(rng.randint(0, 12)):
            at = (ux + rng.randint(-radius - 4, radius + 4), uy + rng.randint(-radius - 4, radius + 4))
            words[at] = words.get(at, 0) | rng.choice((0x1, 0x8, 0x400))
        if rng.random() < 0.15:
            words.pop((ux, uy), None)
    return ux, uy, {at: value for at, value in words.items() if 0 <= at[0] < SIDE and 0 <= at[1] < SIDE}


def gen_warp(count, seed):
    rng = random.Random(seed)
    lines = []
    for cid in range(count):
        ux, uy, words = make_warp_case(rng)
        flat = " ".join(f"{x} {y} {value}" for (x, y), value in sorted(words.items()))
        lines.append(f"{cid} 1 {ux} {uy} {ux} {uy} {len(words)} {flat}".rstrip())
    return lines


def make_case(rng, units):
    """One case: walls, doors and objects as rectangles of collision bits, a few unit bits, a caster and a target."""
    words = {}
    for _ in range(rng.randint(0, 40)):
        x, y = rng.randrange(SIDE), rng.randrange(SIDE)
        width, height = rng.randint(1, 12), rng.randint(1, 12)
        bits = rng.choice((0x1, 0x9, 0x5, 0x400, 0x800, 0x4, 0x8))
        for a in range(x, min(x + width, SIDE)):
            for b in range(y, min(y + height, SIDE)):
                words[(a, b)] = words.get((a, b), 0) | bits
    for _ in range(rng.randint(0, 12)):
        x, y = rng.randrange(SIDE), rng.randrange(SIDE)
        words[(x, y)] = words.get((x, y), 0) | UNITS
    cx, cy = rng.randrange(2 * ROOM, 3 * ROOM), rng.randrange(2 * ROOM, 3 * ROOM)
    words.pop((cx, cy), None)
    mode = rng.random()
    if mode < 0.4:                                               # an ordinary hop: up to 40 away
        tx, ty = cx + rng.randint(-40, 40), cy + rng.randint(-40, 40)
    elif mode < 0.7:                                             # anywhere on the map
        tx, ty = rng.randrange(SIDE), rng.randrange(SIDE)
    elif mode < 0.85:                                            # at a monster
        spots = [at for at, value in words.items() if value & UNITS] or [(cx + 5, cy)]
        tx, ty = rng.choice(spots)
    else:                                                        # past the map
        tx, ty = rng.randint(-30, SIDE + 30), rng.choice((-5, SIDE + 5))
    tele = 0 if rng.random() < 0.05 else 1
    if not units:
        words = {at: value & ~UNITS for at, value in words.items() if value & ~UNITS}
    return tele, cx, cy, tx, ty, words


def gen(count, seed, units=True):
    rng = random.Random(seed)
    lines = []
    for cid in range(count):
        tele, cx, cy, tx, ty, words = make_case(rng, units)
        flat = " ".join(f"{x} {y} {value}" for (x, y), value in sorted(words.items()))
        lines.append(f"{cid} {tele} {cx} {cy} {tx} {ty} {len(words)} {flat}".rstrip())
    return lines


def parse(line):
    field = list(map(int, line.split()))
    cid, tele, cx, cy, tx, ty, count = field[:7]
    words = {(field[7 + 3 * k], field[8 + 3 * k]): field[9 + 3 * k] for k in range(count)}
    return cid, tele, cx, cy, tx, ty, words


def show(cid, ret, pos):
    return f"{cid} {int(ret != 0)} " + " ".join(map(str, pos if pos else ("-", "-")))


def diff(game, d2d, cases=()):
    """{kind: count} of the answers that differ, from line lists; with the cases, a class of case on each kind."""
    game, d2d = ([line for line in lines if line.split()[:1] and line.split()[0].isdigit()] for lines in (game, d2d))   # d2d's log lines are not answers
    by_id = {parse(line)[0]: parse(line) for line in cases}
    if not game or len(game) != len(d2d):
        raise ValueError(f"{len(game)} game answers against {len(d2d)} from d2d")   # zip would stop at the shorter file
    kinds = {}
    for ours, theirs in zip(game, d2d, strict=True):
        a, b = ours.split(), theirs.split()
        if a == b:
            kind = "same"
        elif a[1] == "1" and b[1] == "0":
            kind = "game lands, d2d refuses"
        elif a[1] == "0" and b[1] == "1":
            kind = "game refuses, d2d lands"
        else:
            kind = "both land, other spot"
        if kind != "same" and by_id:
            _, tele, cx, cy, tx, ty, words = by_id[int(a[0])]
            if max(abs(tx // ROOM - cx // ROOM), abs(ty // ROOM - cy // ROOM)) > 1:
                kind += " (target room not near)"
            elif tele == 0:
                kind += " (Teleport 0)"
            elif a[1] == "1" and b[1] == "0":
                kind += " (target blocked)"
        kinds[kind] = kinds.get(kind, 0) + 1
    return kinds


def probe(oracle):
    """Lines describing where the snap lands round a blocked target, and the refusals."""
    cx, cy = 100, 100
    out = []
    for radius in (0, 1, 2, 3, 5):
        words = {(110 + dx, 100 + dy): 1 for dx in range(-radius, radius + 1) for dy in range(-radius, radius + 1)}
        ret, pos = oracle.run(1, cx, cy, 110, 100, words)
        out.append(f"target (110,100), blocked square radius {radius}: do {ret} landed {pos}")
    for bits in (0x4, 0x8, 0x400, 0x800, 0x2, 0x10, 0x100, 0x2000):
        ret, pos = oracle.run(1, cx, cy, 110, 100, {(110, 100): bits})
        out.append(f"target word {bits:#x}: do {ret} landed {pos}")
    for target in ((199, 100), (120, 100), (79, 100), (40, 40), (39, 39), (159, 159), (160, 160), (-1, 100)):
        ret, pos = oracle.run(1, cx, cy, *target, {})
        out.append(f"open map, target {target}: do {ret} landed {pos}")
    ret, pos = oracle.run(0, cx, cy, 110, 100, {})
    out.append(f"Teleport 0: do {ret} landed {pos}")
    ret, pos = oracle.run(2, cx, cy, 110, 100, {})
    out.append(f"Teleport 2: do {ret} landed {pos}, line test args {oracle.line and tuple(hex(v) for v in oracle.line)}")
    return out


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read().splitlines()


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "gen" and "--warp" in sys.argv:
        args = [arg for arg in sys.argv[2:] if arg != "--warp"]
        print("\n".join(gen_warp(int(args[0]), int(args[1]))))
    elif cmd == "gen":
        units = "--no-units" not in sys.argv
        args = [arg for arg in sys.argv[2:] if arg != "--no-units"]
        print("\n".join(gen(int(args[0]), int(args[1]), units)))
    elif cmd == "warp":
        oracle = WarpOracle()
        for line in read(sys.argv[2]):
            cid, _, ux, uy, _, _, words = parse(line)
            ret, pos = oracle.warp(ux, uy, words)
            print(show(cid, ret, pos))
    elif cmd == "game":
        oracle = Oracle()
        for line in read(sys.argv[2]):
            cid, *rest = parse(line)
            ret, pos = oracle.run(*rest)
            print(show(cid, ret, pos))
    elif cmd == "diff":
        game, d2d = read(sys.argv[2]), read(sys.argv[3])
        kinds = diff(game, d2d, read(sys.argv[4]) if len(sys.argv) > 4 else ())
        print(f"{len(game)} cases")
        for kind, count in sorted(kinds.items(), key=lambda item: -item[1]):
            print(f"  {count:7d}  {kind}")
        sys.exit(1 if set(kinds) - {"same"} else 0)
    elif cmd == "probe":
        print("\n".join(probe(Oracle())))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
