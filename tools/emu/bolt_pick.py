# SPDX-License-Identifier: GPL-3.0-or-later
"""Which missile a missile fan shoots by the caster's weapon, game.exe as the oracle (FUN_005db410, srvdofunc 8).

    uv run python bolt_pick.py cases > cases.txt             # `skill wclass`: every do-8 skill row, weapon classes 0..13
    uv run python bolt_pick.py game < cases.txt > game.txt   # each case and `| row` (or `-`)
    ../../build/tests/test_bolt_pick --bolt-pick < cases.txt | grep ' | ' > d2d.txt
    uv run python bolt_pick.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: a Skills.txt row whose srvdofunc is 8 (Multiple Shot, Teeth, PrimePoisonball, Shock Wave) and the class
FUN_00623c60 gives the caster's weapon (FUN_00629fe0: the weapon's ItemsTxt wclass through the map at 0x7446a0:
bow 1 .. xbw 7, ht1 12; 0 without one). FUN_005db410(ECX game, EDX unit; skill, level) runs for real on the skill's
row of game.exe's own table (drlg.boot): srvmissilea (+0x48), or srvmissileb (+0x4a) when the class isn't 1 and
srvmissileb is set (0x5db51e..0x5db53d). Each line: the case, then the Missiles row of its first missile (the
request's +0x10 at FUN_0059fa30), or `-` with none.
Hooked (inputs and side effects, not the claim): the weapon class FUN_00623c60, the point FUN_0056d2c0, the calcs
FUN_00646ca0 (one missile, no middle), the path's subtile FUN_006488c0 / FUN_00648900, the missile maker FUN_0059fa30.
"""
import sys

USAGE = "usage: bolt_pick.py cases | game < CASES | compare GAME D2D"
M32 = 0xffffffff


def do8_skills(e):
    tables = e.r32(0x744304)
    count, base = e.r32(tables + 0xba0), e.r32(tables + 0xb98)
    return [skill for skill in range(count) if e.mu.mem_read(base + skill * 0x23c + 0x2e, 2) == (8).to_bytes(2, "little")]


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


class Rig:
    def __init__(self):
        import drlg
        e = self.e = drlg.boot()
        self.game, self.unit, self.path = e.alloc(0x200), e.alloc(0x200), e.alloc(0x200)
        self.wclass, self.rows = 0, []
        hooks = ((0x623c60, lambda e: self.wclass, 0), (0x56d2c0, self.point, 2), (0x646ca0, self.calc, 4),
                 (0x6488c0, lambda e: 1000, 1), (0x648900, lambda e: 1000, 1), (0x59fa30, self.missile, 0))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)
        self.calc1 = 0

    def point(self, e):
        e.w32(e.arg(0), 1010)
        e.w32(e.arg(1), 1003)
        return 1

    def calc(self, e):
        return 1 if e.arg(1) == self.calc1 else 0

    def missile(self, e):
        from unicorn.x86_const import UC_X86_REG_EDX
        self.rows.append(signed(e.r32(e.mu.reg_read(UC_X86_REG_EDX) + 0x10)))
        return 1

    def run(self, skill, wclass):
        e = self.e
        row = e.r32(e.r32(0x744304) + 0xb98) + skill * 0x23c
        self.calc1, self.wclass, self.rows = e.r32(row + 0x138), wclass & M32, []
        e.mu.mem_write(self.unit, bytes(0x200))
        unit = e.view("unit", self.unit)
        unit.type = 0
        unit.path = self.path
        e.call(0x5db410, skill, 1, ecx=self.game, edx=self.unit)
        return str(self.rows[0]) if self.rows else "-"


def cases():
    import drlg
    for skill in do8_skills(drlg.boot()):
        for wclass in range(14):
            print(skill, wclass)


def game():
    rig = Rig()
    for line in sys.stdin:
        field = line.split()
        if field:
            print(line.strip(), "|", rig.run(*(int(f) for f in field[:2])))


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            case, bar, result = line.partition("|")
            case, result = case.split(), result.split()
            if not bar or len(case) != 2 or len(result) != 1 or not all(f.lstrip("-").isdigit() for f in case) \
                    or not (result[0] == "-" or result[0].lstrip("-").isdigit()):
                sys.exit(f"malformed line in {path}: {line.strip()!r} (2 case fields, then a row or `-`)")
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
    if not a or (a[0], len(a)) not in {("cases", 1), ("game", 1), ("compare", 3)}:
        sys.exit(USAGE)
    if a[0] == "cases":
        cases()
    elif a[0] == "game":
        game()
    else:
        compare(a[1], a[2])


if __name__ == "__main__":
    main()
