# SPDX-License-Identifier: GPL-3.0-or-later
"""Iron Golem's item in game.exe as the oracle: its start function (FUN_005c32a0, srvstfunc 20), which takes the
caster's target only when it is an item fit to make the golem of, and the slots its do (FUN_005c5250, srvdofunc 57)
wears it in on the golem (FUN_0062ea80, then FUN_005606b0 with the first).

    uv run python iron_golem.py gen 100000 1 > cases.txt     # `type mode pick flags held`
    uv run python iron_golem.py game < cases.txt > game.txt   # each case and `| takes body1 body2`
    ../../build/tests/test_iron_golem --iron-golem < cases.txt | grep ' | ' > d2d.txt
    uv run python iron_golem.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the target unit's type (+0, 4 an item), its mode (+0x10, 3 on the ground), which item it is (`pick` modulo
the Items rows: its class, +4), its item flags (the item data's +0x18; 0x10 identified) and whether something holds
it (1: the +0x5c record's +0x34 node has bit 31 of +0x10 set and a unit at +0x44, FUN_00625820). FUN_005c32a0(ECX
game, EDX caster; skill 90, level 1) runs for real: the type and the mode, FUN_00629cc0 (Items +0xdc & 2: bitfield1
bit 1, a metal base), FUN_006280a0 (flags & 0x10) and FUN_00625820 (none holding it). FUN_0062ea80 then runs for
real on the same item: the BodyLoc1 / BodyLoc2 of its ItemTypes row (+0xa / +0xb), 0 / 0 for a unit not an item.
Each line: the case, then `takes body1 body2` (takes 1 or 0).
Hooked (inputs, not the claim): FUN_00553540, the caster's target (the case's unit).
"""
import random
import sys

USAGE = "usage: iron_golem.py gen N SEED | game < CASES | compare GAME D2D"
IRON_GOLEM = 90


def boot():
    import drlg
    return drlg.boot()


class Rig:
    def __init__(self, e):
        self.e = e
        self.game, self.caster, self.unit, self.data = (e.alloc(0x200) for _ in range(4))
        self.record, self.node, self.holder = (e.alloc(0x100) for _ in range(3))
        self.items = 0
        while e.call(0x6335f0, self.items):
            self.items += 1
        e.hook(0x553540, lambda e: self.unit, 2)

    def run(self, kind, mode, pick, flags, held):
        e = self.e
        e.mu.mem_write(self.unit, bytes(0x200))
        e.mu.mem_write(self.data, bytes(0x200))
        e.w32(self.unit, kind)
        e.w32(self.unit + 4, pick % self.items)
        e.w32(self.unit + 0x10, mode)
        e.w32(self.unit + 0x14, self.data)
        e.w32(self.data + 0x18, flags & 0xffffffff)
        if held:
            e.w32(self.unit + 0x5c, self.record)
            e.w32(self.record + 0x34, self.node)
            e.w32(self.node + 0x10, 0x80000000)
            e.w32(self.node + 0x44, self.holder)
        takes = 1 if e.call(0x5c32a0, IRON_GOLEM, 1, ecx=self.game, edx=self.caster) else 0
        body = e.alloc(8)
        e.call(0x62ea80, self.unit, body, body + 4)
        return f"{takes} {e.mu.mem_read(body, 1)[0]} {e.mu.mem_read(body + 4, 1)[0]}"


def gen(count, seed):
    rng = random.Random(seed)
    for pick in range(1000):
        for flags, held in ((0x10, 0), (0, 0), (0x10, 1), (0xffffffff, 0), (0xffffffef, 0)):
            print(4, 3, pick, flags, held)
    for kind, mode in ((0, 3), (1, 3), (2, 3), (3, 3), (5, 3), (4, 0), (4, 1), (4, 2), (4, 4), (4, 5), (4, 6)):
        for pick in range(0, 1000, 7):
            print(kind, mode, pick, 0x10, 0)
    for _ in range(count):
        print(rng.choice((4, 4, 4, 4, 0, 1, 2)), rng.choice((3, 3, 3, 0, 1, 2, 4, 5)), rng.randrange(1 << 16),
              rng.choice((0x10, 0x10, rng.getrandbits(32))), rng.choice((0, 0, 0, 1)))


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
            if len(case) != 5 or len(result) != 3 or not all(f.lstrip("-").isdigit() for f in case + result):
                sys.exit(f"malformed row in {path}: {line.strip()!r}")
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
