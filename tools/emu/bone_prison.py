# SPDX-License-Identifier: GPL-3.0-or-later
"""Bone Prison's cast (srvdofunc 62, FUN_005c5d00) as the oracle: which of its twelve pieces are made, where, and the
way each one faces.

    uv run python bone_prison.py gen 20000 1 > cases.txt     # `px py town spot near (dx dy) x 12`
    uv run python bone_prison.py game < cases.txt > game.txt   # each case and `| x,y,dir ...` (`-` for a piece not made)
    ../../build/tests/test_bone_prison --bone-prison < cases.txt | grep ' | ' > d2d.txt
    uv run python bone_prison.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the skill's point (FUN_0056d2c0, subtiles; at least 8 on each axis, so every spot and its neighbours are at 0
or more as a unit's subtile is), then three 12-bit masks in the pieces' order (bit k the k-th piece's spot): `town`,
the spots in a town room (FUN_0061ab00); `spot`, those whose own subtile the spawn finds free (FUN_005b2f20 at radius
-1); `near`, those it finds room for within 4 when it isn't (radius 4), each then at its own (dx, dy) off the spot
(the case's twelve pairs, -4..4). FUN_005c5d00, its piece maker FUN_005c5bc0 and the spawn FUN_0056d940 run for real:
twelve times, the point plus the table at 0x6e304c (x) / 0x6e307c (y); a spot in a town room is skipped before the
spawn; FUN_0056d940 tries radius -1, then radius 4 (its request has no flag 4, 0x56d9bb); a made piece then faces the
point from the subtile it got (FUN_006488c0 / FUN_00648900 on its path, FUN_0064fdc0 run for real, to FUN_006488a0).
Each line: the case, then each piece's `x,y,dir` (its subtile, its direction 0..63) or `-`.
Hooked (inputs and side effects, not the claim): the point FUN_0056d2c0, the rooms FUN_00620bb0 / FUN_00463740, the
town test FUN_0061ab00 (`town`), the summon row FUN_0056e620, the calcs FUN_00646ca0, the spawn's search FUN_005b2f20
(`spot` / `near`: a unit at the subtile the case gives, else none), FUN_0056d940's set-up after it (FUN_0056d8d0,
FUN_00639df0, FUN_005b0e00, FUN_00573780, FUN_00540e60, FUN_005417d0), the piece's FUN_005c4470 / FUN_005a4850 /
FUN_005b1990, the AI group FUN_0058f030 / FUN_0058f100 / FUN_0044be50 / FUN_00451f50, the owner FUN_00621ce0, the
path's subtile FUN_006488c0 / FUN_00648900 and FUN_006488a0 (recorded). A fake data block holds one Skills row.
"""
import random
import sys

import emu

USAGE = "usage: bone_prison.py gen N SEED | game < CASES | compare GAME D2D"
SKILL = 1
ALL = 0xfff


def gen(count, seed):
    rng = random.Random(seed)
    offsets = lambda: [rng.randint(-4, 4) for _ in range(24)]
    for px, py in ((1000, 1000), (8, 8), (8, 30000), (30000, 8)):   # every spot and its neighbours at 0 or more
        for town, spot, near in ((0, ALL, 0), (ALL, ALL, ALL), (0, 0, 0), (0, 0, ALL), (0x555, ALL, 0), (0, 0xaaa, 0x0f0), (0x0f0, 0xf0f, ALL)):
            print(px, py, town, spot, near, *offsets())
    for _ in range(count):
        px, py = rng.choice((rng.randint(8, 60), rng.randint(8, 30000))), rng.choice((rng.randint(8, 60), rng.randint(8, 30000)))
        town = rng.choice((0, 0, 0, ALL, rng.randint(0, ALL)))
        spot = rng.choice((ALL, ALL, rng.randint(0, ALL), ALL & ~(1 << rng.randint(0, 11)), 0))
        near = rng.choice((ALL, 0, rng.randint(0, ALL)))
        print(px, py, town, spot, near, *offsets())


class Rig:
    def __init__(self):
        e = self.e = emu.Emu()
        data, skills = e.alloc(0x1000), e.alloc(0x23c * 4)
        e.w32(0x744304, data)
        e.w32(data + 0xb98, skills)
        e.w32(data + 0xba0, 4)
        e.w32(data + 0xbf0, 0x100)
        self.game, self.caster = e.alloc(0x1000), e.alloc(0x200)
        self.case, self.pieces, self.paths = {}, [], {}
        self.spot = -1
        hooks = ((0x56d2c0, self.point, 2), (0x620bb0, lambda e: 0x1000, 1), (0x463740, lambda e: 0x2000, 1),
                 (0x61ab00, self.town, 1), (0x56e620, lambda e: 0, 5), (0x646ca0, lambda e: 1, 4), (0x5b2f20, self.spawn, 6),
                 (0x6488c0, lambda e: self.paths[e.arg(0)][0] & 0xffffffff, 1),
                 (0x648900, lambda e: self.paths[e.arg(0)][1] & 0xffffffff, 1), (0x6488a0, self.face, 2))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)
        e.stub(0, {0x5c4470: 4, 0x5a4850: 2, 0x5b1990: 2, 0x58f030: 4, 0x58f100: 1, 0x44be50: 0, 0x451f50: 0, 0x621ce0: 2,
                   0x56d8d0: 1, 0x639df0: 2, 0x5b0e00: 2, 0x573780: 0, 0x540e60: 2, 0x5417d0: 4})

    def point(self, e):
        e.w32(e.arg(0), self.case["px"])
        e.w32(e.arg(1), self.case["py"])
        return 1

    def town(self, _e):
        self.spot += 1                       # one town test a piece, before its spawn
        return (self.case["town"] >> self.spot) & 1

    def spawn(self, e):
        signed = lambda value: value - (1 << 32) if value & 0x80000000 else value
        x, y, radius = signed(e.arg(0)), signed(e.arg(1)), signed(e.arg(4))
        k = self.spot
        if radius == -1 and (self.case["spot"] >> k) & 1:
            at = (x, y)
        elif radius == 4 and (self.case["near"] >> k) & 1:
            at = (x + self.case["offsets"][2 * k], y + self.case["offsets"][2 * k + 1])
        else:
            return 0
        unit, path = e.alloc(0x200), e.alloc(0x100)
        e.w32(unit, 1)                       # a monster
        e.w32(unit + 0x2c, path)
        self.paths[path] = (at[0], at[1], k)
        self.pieces[k] = [at[0], at[1], None]
        return unit

    def face(self, e):
        self.pieces[self.paths[e.arg(0)][2]][2] = e.arg(1)
        return 0

    def run(self, px, py, town, spot, near, offsets):
        self.case, self.spot, self.paths = {"px": px, "py": py, "town": town, "spot": spot, "near": near, "offsets": offsets}, -1, {}
        self.pieces = [None] * 12
        self.e.mu.mem_write(self.caster, bytes(0x200))
        self.e.call(0x5c5d00, SKILL, 1, ecx=self.game, edx=self.caster)
        if self.spot != 11:
            sys.exit(f"{self.spot + 1} town tests, not 12: {px} {py}")
        return " ".join("-" if piece is None else f"{piece[0]},{piece[1]},{piece[2]}" for piece in self.pieces)


def game():
    rig = Rig()
    for line in sys.stdin:
        field = line.split()
        if field:
            values = [int(f) for f in field[:29]]
            print(line.strip(), "|", rig.run(*values[:5], values[5:]))


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            case, bar, result = line.partition("|")
            case, result = case.split(), result.split()
            pieces_ok = len(result) == 12 and all(piece == "-" or (len(piece.split(",")) == 3 and all(part.lstrip("-").isdigit() for part in piece.split(",")))
                                                  for piece in result)
            if not bar or len(case) != 29 or not all(f.lstrip("-").isdigit() for f in case) or not pieces_ok:
                sys.exit(f"malformed line in {path}: {line.strip()!r} (29 case fields, then 12 pieces)")
            out.append((case, result))
    return out


def compare(game_path, d2d_path):
    game_rows, d2d_rows = rows(game_path), rows(d2d_path)
    if not game_rows or len(game_rows) != len(d2d_rows):
        sys.exit(f"empty or unequal: {len(game_rows)} game, {len(d2d_rows)} d2d (an empty file is not a pass)")
    differ = 0
    for (g_case, g), (d_case, d) in zip(game_rows, d2d_rows, strict=True):
        if g_case != d_case:
            sys.exit(f"cases out of step: game {' '.join(g_case)}, d2d {' '.join(d_case)}")
        if g != d:
            differ += 1
            if differ <= 20:
                print(f"DIFF {' '.join(g_case)}: game {' '.join(g)}, d2d {' '.join(d)}")
    print(f"{len(game_rows)} cases, {differ} differ")
    sys.exit(1 if differ else 0)


def main():
    a = sys.argv[1:]
    if not a or (a[0], len(a)) not in {("gen", 3), ("game", 1), ("compare", 3)}:
        sys.exit(USAGE)
    if a[0] == "gen":
        gen(int(a[1]), int(a[2]))
    elif a[0] == "game":
        game()
    else:
        compare(a[1], a[2])


if __name__ == "__main__":
    main()
