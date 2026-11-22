# SPDX-License-Identifier: GPL-3.0-or-later
"""The missile fan in game.exe as the oracle: srvdofunc 8 (FUN_005db410: Multiple Shot, Teeth, Shock Wave).

    uv run python missile_fan.py gen 100000 1 > cases.txt     # `sx sy tx ty count mid`
    uv run python missile_fan.py game < cases.txt > game.txt   # each case and `| x,y,side ...` (or `-`)
    ../../build/tests/test_missile_fan --missile-fan < cases.txt | grep ' | ' > d2d.txt
    uv run python missile_fan.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the caster's subtile (FUN_006488c0 / FUN_00648900 of its path), the skill's point (FUN_0056d2c0), calc1 (the
count) and calc3 (the middle ones; 0: all of them). FUN_005db410(ECX game, EDX unit; skill, level) runs for real, with
FUN_0056d370 and FUN_0056d3b0: the step is the line to the point scaled up while short and turned a right angle and
halved to at most 3 subtiles squared; the first point is the skill's point less trunc(step x count / 2); the first
(count - mid) / 2 missiles carry flag 0x10000, then mid without it, then the rest with it, each aimed one step on
(0x5db4e8..0x5db678). Each line: the case, then each missile's aim and whether it has 0x10000 (`side` 1), in order.
Hooked (inputs and side effects, not the claim): the point FUN_0056d2c0, the calcs FUN_00646ca0 (by the Skills row's
calc1 / calc2 / calc3 words: Teeth's row of game.exe's own table, drlg.boot), the path's subtile FUN_006488c0 /
FUN_00648900, the missile maker FUN_0059fa30 (recorded: the aim at the request's +0x1c / +0x20, its flags at +0).
"""
import random
import sys

USAGE = "usage: missile_fan.py gen N SEED | game < CASES | compare GAME D2D"
TEETH = 67
M32 = 0xffffffff


def gen(count, seed):
    rng = random.Random(seed)
    for dx, dy in ((0, 0), (1, 0), (0, 1), (-1, 0), (0, -1), (1, 1), (2, 1), (-3, 2), (10, 0), (0, -10), (7, -7), (40, 3)):
        for number, mid in ((1, 0), (2, 0), (3, 0), (5, 0), (24, 0), (5, 2), (6, 2), (2, 5), (0, 0), (4, 1)):
            print(1000, 1000, 1000 + dx, 1000 + dy, number, mid)
    for sx, tx in ((-30000, 30000), (30000, -30000), (0, 46341), (0, 65536), (-0x7fff0000, 0x7fff0000)):
        for number in (1, 3, 5):
            print(sx, 100, tx, 107, number, 2)
            print(100, sx, 107, tx, number, 0)
    for _ in range(count):
        sx, sy = rng.randint(100, 30000), rng.randint(100, 30000)
        reach = rng.choice((rng.randint(0, 3), rng.randint(0, 12), rng.randint(0, 60)))
        print(sx, sy, sx + rng.randint(-reach, reach), sy + rng.randint(-reach, reach), rng.choice((rng.randint(1, 24), rng.randint(0, 30), 5, 3)),
              rng.choice((0, 0, 0, 2, rng.randint(0, 8))))


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


class Rig:
    def __init__(self):
        import drlg
        e = self.e = drlg.boot()
        self.game, self.unit, self.path = e.alloc(0x200), e.alloc(0x200), e.alloc(0x200)
        tables = e.r32(0x744304)
        row = e.r32(tables + 0xb98) + TEETH * 0x23c
        self.calc1, self.calc2, self.calc3 = (e.r32(row + offset) for offset in (0x138, 0x13c, 0x140))
        self.case, self.shots = {}, []
        hooks = ((0x56d2c0, self.point, 2), (0x646ca0, self.calc, 4), (0x6488c0, lambda e: self.case["sx"] & M32, 1),
                 (0x648900, lambda e: self.case["sy"] & M32, 1), (0x59fa30, self.missile, 0))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def point(self, e):
        e.w32(e.arg(0), self.case["tx"])
        e.w32(e.arg(1), self.case["ty"])
        return 1

    def calc(self, e):
        which = e.arg(1)
        if which == self.calc1:
            return self.case["count"] & M32
        if which == self.calc3:
            return self.case["mid"] & M32
        return 0

    def missile(self, e):
        from unicorn.x86_const import UC_X86_REG_EDX
        request = e.mu.reg_read(UC_X86_REG_EDX)
        self.shots.append((signed(e.r32(request + 0x1c)), signed(e.r32(request + 0x20)), int(bool(e.r32(request) & 0x10000))))
        return 1

    def run(self, sx, sy, tx, ty, count, mid):
        e = self.e
        self.case, self.shots = {"sx": sx, "sy": sy, "tx": tx, "ty": ty, "count": count, "mid": mid}, []
        e.mu.mem_write(self.unit, bytes(0x200))
        unit = e.view("unit", self.unit)
        unit.type = 0
        unit.path = self.path
        e.call(0x5db410, TEETH, 1, ecx=self.game, edx=self.unit)
        return " ".join(f"{x},{y},{side}" for x, y, side in self.shots) or "-"


def game():
    rig = Rig()
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:6])))


def integer(field):
    try:
        int(field)
    except ValueError:
        return False
    return True


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            if "|" not in line:
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            case, result = line.split("|", 1)
            case, result = case.split(), result.split()
            shots_ok = result == ["-"] or all(len(shot.split(",")) == 3 and all(integer(part) for part in shot.split(",")) for shot in result)
            if len(case) != 6 or not result or not shots_ok or not all(integer(field) for field in case):
                sys.exit(f"malformed line in {path}: {line.strip()!r} (6 case fields, then `-` or x,y,side shots)")
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
