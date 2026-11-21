# SPDX-License-Identifier: GPL-3.0-or-later
"""item_slow's event function in game.exe as the oracle (FUN_005c0310, slot 19 of 0x7325b0): ItemStatCost's
domeleedamage / domissiledamage for item_slow, which FUN_005c4470 hooks on a summon whose aurastat it is (0x5c4686..
0x5c46f2: Clay Golem's) and gear on its wearer. FUN_0057c6c0 raises domeleedamage on the striker (event 5,
0x57c7d8..0x57c7e3) once its melee hit has landed (record +4 & 1, not & 0x20), the struck unit as the other.

    uv run python item_slow.py gen 100000 1 > cases.txt     # `slow type kind boss hireling`
    uv run python item_slow.py game < cases.txt > game.txt   # each case and `| put velocity rate length state`
    ../../build/tests/test_clay_slow --item-slow < cases.txt | grep ' | ' > d2d.txt
    uv run python item_slow.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the wearer's item_slow (stat 0x96; the node's key 0x96 << 16, read through FUN_00625500 / FUN_00625420 as
stat key >> 16 at layer key & 0xffff = 0), the struck unit's type (0 a player, 1 a monster, 2..5 the others) and, for a
monster, its kind (0 plain, 1 champion 1 | 4 | 8, 2 unique 1 | 8, 3 superunique 2 | 8, 4 minion 0x10, 5 the
superunique flag 2 alone), whether it's a MonStats boss (FUN_0063e9f0(0, unit)) and a hireling (FUN_0063ee90).
FUN_005c0310(ECX game, EDX event; wearer, struck, hit, key, 0) runs for real, FUN_005a0180 too: a 0 does nothing; a
player's is held to 50; a monster's to 50 when +0x16 & 0xc, a MonStats boss or a hireling, else to 75 when +0x16 & 2,
else to 90; another type's nothing; then state 0x18 on the struck unit as Clay Golem's (FUN_005c6760).
Each line: the case, then `1 velocity attackrate length state`, or `0 0 0 0 0` when nothing is put.
Hooked (inputs and side effects, not the claim): the stat getter FUN_00625500 (the wearer's item_slow), the MonStats
boss test FUN_0063e9f0, the hireling test FUN_0063ee90, the state put FUN_0056e970 (recorded, returns a list), the stat
setter FUN_006270b0 (recorded) and the pace refresh FUN_00623f50.
"""
import random
import sys

USAGE = "usage: item_slow.py gen N SEED | game < CASES | compare GAME D2D"
KIND_FLAGS = (0, 1 | 4 | 8, 1 | 8, 2 | 8, 0x10, 2)   # plain, champion, unique, superunique, minion, flag 2 alone


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
        self.slow, self.put, self.stats, self.boss, self.hireling = 0, None, {}, 0, 0
        hooks = ((0x625500, self.get_stat, 3), (0x56e970, self.put_state, 0), (0x6270b0, self.set_stat, 4),
                 (0x623f50, lambda e: 0, 3), (0x63e9f0, self.is_boss, 2), (0x63ee90, self.is_hireling, 1))
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def get_stat(self, e):
        return self.slow & 0xffffffff if e.arg(0) == self.golem and e.arg(1) == 0x96 and e.arg(2) & 0xffff == 0 else 0

    def is_boss(self, e):
        return self.boss if e.arg(0) == 0 and e.arg(1) == self.striker else 0

    def is_hireling(self, e):
        return self.hireling if e.arg(0) == self.striker else 0

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

    def run(self, slow, unit_type, kind, boss, hireling):
        e = self.e
        self.slow, self.put, self.stats, self.boss, self.hireling = slow, None, {}, boss, hireling
        e.w32(self.golem, 1)
        e.w32(self.striker, unit_type)
        e.w32(self.striker + 0x14, self.data)
        e.mu.mem_write(self.data + 0x16, (KIND_FLAGS[kind] if 0 <= kind < len(KIND_FLAGS) else 0).to_bytes(2, "little"))
        e.call(0x5c0310, self.golem, self.striker, self.hit, 0x96 << 16, 0, ecx=self.game, edx=5)
        if self.put is None:
            return "0 0 0 0 0"
        velocity, length, state = self.put
        rate = self.stats.get(0x44)
        if velocity is None or rate is None or self.stats.get(0x45) != rate:
            sys.exit(f"unexpected stats {self.put} {self.stats}")
        return f"1 {velocity} {rate} {length} {state}"


def gen(count, seed):
    rng = random.Random(seed)
    for slow in (0, 1, 49, 50, 51, 74, 75, 76, 89, 90, 91, 200, -1, -50, -200):
        for unit_type in range(6):
            for kind in range(6) if unit_type == 1 else (0,):
                for boss, hireling in ((0, 0), (1, 0), (0, 1), (1, 1)) if unit_type == 1 else ((0, 0),):
                    print(slow, unit_type, kind, boss, hireling)
    for _ in range(count):
        slow = rng.choice((rng.randint(1, 75), rng.randint(0, 120), rng.randint(-100, 300), rng.randint(-(1 << 31), (1 << 31) - 1)))
        unit_type = rng.choice((1, 1, 1, 0, rng.randint(0, 5)))
        monster = unit_type == 1
        print(slow, unit_type, rng.randint(0, 5) if monster else 0, int(monster and rng.random() < 0.2), int(monster and rng.random() < 0.1))


def game():
    rig = Rig(boot())
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        print(line.strip(), "|", rig.run(*(int(f) for f in field[:5])))


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
            if len(case) != 5 or len(result) != 5 or not all(field.lstrip("-").isdigit() for field in case + result):
                sys.exit(f"malformed line in {path}: {line.strip()!r} (5 case fields and 5 results, integers)")
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
