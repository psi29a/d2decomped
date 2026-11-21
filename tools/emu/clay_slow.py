# SPDX-License-Identifier: GPL-3.0-or-later
"""Clay Golem's slow in game.exe as the oracle: its damagedinmelee event function (FUN_005c6760, slot 27 of 0x7325b0),
which FUN_0057c6c0 raises on the golem (event 1, 0x57c7e8..0x57c7f3) once a melee hit on it has landed (record +4
word: bit 1 set, bit 0x20 clear), the striker as the other unit.

    uv run python clay_slow.py gen 100000 1 > cases.txt     # `slow type kind`
    uv run python clay_slow.py game < cases.txt > game.txt   # each case and `| put velocity rate length state`
    ../../build/tests/test_clay_slow --clay-slow < cases.txt | grep ' | ' > d2d.txt
    uv run python clay_slow.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the golem's item_slow (stat 0x96 at layer 0, FUN_00625500: the aurastat dm34 Clay Golem's FUN_005c4470 puts
on it), the striker's unit type (0 a player, 1 a monster, 2..5 the others) and, for a monster, its kind: 0 plain,
1 champion (monster data +0x16 = 1 | 4 | 8), 2 unique (1 | 8), 3 superunique (2 | 8), 4 minion (0x10).
FUN_005c6760(ECX game, EDX event; golem, striker, hit, skill, level) runs for real, FUN_005a0180 (the +0x16 & 0xc
test) too: a value of 0 does nothing; a player's is held to 50, a champion's, unique's or superunique's to 50, any
other monster's to 90, another type's nothing; then state 0x18 on the striker through FUN_0056e970 {golem, striker,
skill 0, level 1, length 0x2ee frames, stat 0x43 = -value}, and stats 0x44 / 0x45 = -value on its list (FUN_006270b0).
Each line: the case, then `1 velocity attackrate length state` with the stats as set (other_animrate is checked equal
to attackrate), or `0 0 0 0 0` when nothing is put.
Hooked (inputs and side effects, not the claim): the stat getter FUN_00625500 (the golem's item_slow), the state put
FUN_0056e970 (recorded, returns a list), the stat setter FUN_006270b0 (recorded) and the pace refresh FUN_00623f50.
"""
import random
import sys

USAGE = "usage: clay_slow.py gen N SEED | game < CASES | compare GAME D2D"
CLAY_GOLEM = 75
KIND_FLAGS = (0, 1 | 4 | 8, 1 | 8, 2 | 8, 0x10)   # plain, champion, unique, superunique, minion


def boot():
    import emu
    return emu.Emu()


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


class Rig:
    def __init__(self, e):
        self.e = e
        self.game, self.golem, self.striker, self.data = (e.alloc(0x200) for _ in range(4))
        self.hit, self.list = e.alloc(0x80), e.alloc(0x40)
        self.slow, self.put, self.stats = 0, None, {}
        hooks = ((0x625500, self.get_stat, 3), (0x56e970, self.put_state, 0), (0x6270b0, self.set_stat, 4),
                 (0x623f50, lambda e: 0, 3))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def get_stat(self, e):
        return self.slow & 0xffffffff if e.arg(0) == self.golem and e.arg(1) == 0x96 and e.arg(2) == 0 else 0

    def put_state(self, e):
        from unicorn.x86_const import UC_X86_REG_ECX
        desc = e.mu.reg_read(UC_X86_REG_ECX)
        field = [e.r32(desc + 4 * k) for k in range(9)]
        if field[0] != self.golem or field[1] != self.striker or field[2] != 0 or field[3] != 1:
            sys.exit(f"unexpected put descriptor {field}")
        self.put = (signed(field[6]) if field[5] == 0x43 else None, field[4], field[7])
        return self.list

    def set_stat(self, e):
        if e.arg(0) == self.list and e.arg(3) == 0:
            self.stats[e.arg(1)] = signed(e.arg(2))
        return 0

    def run(self, slow, unit_type, kind):
        e = self.e
        self.slow, self.put, self.stats = slow, None, {}
        e.view("unit", self.golem).type = 1
        striker = e.view("unit", self.striker)
        striker.type = unit_type
        striker.data = self.data
        e.view("monster_data", self.data).flags = KIND_FLAGS[kind] if 0 <= kind < len(KIND_FLAGS) else 0
        e.call(0x5c6760, self.golem, self.striker, self.hit, CLAY_GOLEM, 1, ecx=self.game, edx=1)
        if self.put is None:
            return "0 0 0 0 0"
        velocity, length, state = self.put
        rate = self.stats.get(0x44)
        if velocity is None or rate is None or self.stats.get(0x45) != rate:
            sys.exit(f"unexpected stats {self.put} {self.stats}")
        return f"1 {velocity} {rate} {length} {state}"


def gen(count, seed):
    rng = random.Random(seed)
    for slow in (0, 1, 49, 50, 51, 75, 89, 90, 91, 200, -1, -50, -200):
        for unit_type in range(6):
            for kind in range(5) if unit_type == 1 else (0,):
                print(slow, unit_type, kind)
    for _ in range(count):
        slow = rng.choice((rng.randint(1, 75), rng.randint(0, 120), rng.randint(-100, 300), rng.randint(-(1 << 31), (1 << 31) - 1)))
        unit_type = rng.choice((1, 1, 1, 0, rng.randint(0, 5)))
        print(slow, unit_type, rng.randint(0, 4) if unit_type == 1 else 0)


def game():
    rig = Rig(boot())
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:3])))


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
            if len(case) != 3 or len(result) != 5 or not all(field.lstrip("-").isdigit() for field in case + result):
                sys.exit(f"malformed line in {path}: {line.strip()!r} (3 case fields and 5 results, integers)")
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
