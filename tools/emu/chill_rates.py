# SPDX-License-Identifier: GPL-3.0-or-later
"""A chilled monster's rates in game.exe as the oracle: FUN_0057af80, the chill, on a monster of every MonStats row.

    uv run python chill_rates.py gen > cases.txt               # `class difficulty`, every row on each difficulty
    uv run python chill_rates.py game < cases.txt > game.txt     # each case and `| put velocity attack other`
    ../../build/tests/test_mode_rates --chill-rates < cases.txt | grep ' | ' > d2d.txt
    uv run python chill_rates.py compare game.txt d2d.txt        # `N cases, M differ`; exit 1 on a difference or a bad input

FUN_0057af80 (EDI the unit, stack the source unit and the length) runs for real on game.exe's own loaded tables
(drlg.boot): a monster's coldeffect is its MonStats row's byte at +0x168 + the game's difficulty (0x57afe2); 0 puts no
chill; otherwise a new state 0xb list gets stats 0x43 (velocitypercent), 0x44 (attackrate) and 0x45 (other_animrate),
each the coldeffect (0x57b0a0..0x57b0bf). Each line: the case, then `1 velocity attack other` as set, or `0 0 0 0`.
Hooked (inputs and side effects, not the claim): the state-list lookup FUN_006256b0 (none on yet), the game
FUN_00554010 (its difficulty byte +0x6d), the state set FUN_00639db0, the list maker FUN_006251f0 (returns a list),
the timer FUN_005417d0, FUN_006252d0, FUN_00625ce0, FUN_00626e10, the stat setter FUN_006270b0 (recorded) and the
rate refresh FUN_00623f50.
"""
import sys

USAGE = "usage: chill_rates.py gen | game < CASES | compare GAME D2D"
M32 = 0xffffffff


def classes():
    from emu import mpq_cache
    lines = mpq_cache().read("data\\global\\excel\\monstats.txt").decode("latin1").split("\r\n")
    return sum(1 for line in lines[1:] if line.split("\t")[0] not in ("", "Expansion"))


def gen():
    for cls in range(classes()):
        for difficulty in range(3):
            print(cls, difficulty)


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


class Rig:
    def __init__(self):
        import drlg
        e = self.e = drlg.boot()
        self.unit, self.game, self.list = e.alloc(0x200), e.alloc(0x200), e.alloc(0x40)
        self.stats = {}
        hooks = ((0x6256b0, lambda e: 0, 2), (0x554010, lambda e: self.game, 0), (0x639db0, lambda e: 0, 3),
                 (0x6251f0, lambda e: self.list, 5), (0x5417d0, lambda e: 0, 4), (0x6252d0, lambda e: 0, 2),
                 (0x625ce0, lambda e: 0, 2), (0x626e10, lambda e: 0, 3), (0x6270b0, self.set_stat, 4),
                 (0x623f50, lambda e: 0, 3))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def set_stat(self, e):
        if e.arg(0) == self.list and e.arg(3) == 0:
            self.stats[e.arg(1)] = signed(e.arg(2))
        return 0

    def run(self, cls, difficulty):
        e = self.e
        self.stats = {}
        e.mu.mem_write(self.unit, bytes(0x200))
        unit = e.view("unit", self.unit)
        unit.type = 1
        setattr(unit, "class", cls)   # a keyword in Python
        e.mu.mem_write(self.game + 0x6d, bytes([difficulty]))
        e.call(0x57af80, 0, 10, regs={"edi": self.unit})
        if not self.stats:
            return "0 0 0 0"
        return f"1 {self.stats.get(0x43, 0)} {self.stats.get(0x44, 0)} {self.stats.get(0x45, 0)}"


def game():
    rig = Rig()
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:2])))


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
            if len(case) != 2 or len(result) != 4 or not all(integer(field) for field in case + result):
                sys.exit(f"malformed line in {path}: {line.strip()!r} (2 case fields and 4 results, integers)")
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
    if not a or (a[0], len(a)) not in {("gen", 1), ("game", 1), ("compare", 3)}:
        sys.exit(USAGE)
    if a[0] == "gen":
        gen()
    elif a[0] == "game":
        game()
    else:
        compare(a[1], a[2])


if __name__ == "__main__":
    main()
