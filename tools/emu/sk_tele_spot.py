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
    if cmd == "gen":
        units = "--no-units" not in sys.argv
        args = [arg for arg in sys.argv[2:] if arg != "--no-units"]
        print("\n".join(gen(int(args[0]), int(args[1]), units)))
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
