# SPDX-License-Identifier: GPL-3.0-or-later
"""Bone Wall's cast (srvdofunc 60, FUN_005c58b0) as the oracle: the first
piece, then its two makers' start, target and count.

    uv run python bone_wall.py gen 20000 1 > cases.tsv
    uv run python bone_wall.py game cases.tsv > g.txt

A case line is `id cx cy px py calc2 town`: the caster's subtile, the
subtile the first piece landed on (FUN_0045adf0 / FUN_0045ae20 on it), the
skill's calc2 (row +0x13c through FUN_00646ca0) and whether the cast is in
a town (FUN_0061ab00). `game` prints `id count` and, when it launched the
makers, each one's `sx sy tx ty` (the record FUN_0059fa30 gets: -0x88 / -0x84
start, -0x80 / -0x7c target) and the count FUN_0064a760 hands it.

As run: no makers in a town or when calc2 / 2 (C division) is 1 or less.
Else dx = cx - px, dy = cy - py (dx = 1 when both are 0); maker A from P to
(px - dy, py + dx), maker B from P to (px + dy, py - dx), both with count
calc2 / 2 and the first piece's id (FUN_0064a710).

Hooked (not the claim): the target point FUN_0056d2c0 (P), the rooms
FUN_00620bb0 / FUN_00463740, the town test FUN_0061ab00, the summon row
FUN_0056e620 (0), the calcs FUN_00646ca0 (calc2 for row +0x13c, 1 else),
the piece's spawn FUN_0056d940 (a unit at P) and its set-up FUN_0058f030 /
FUN_005a4850 / FUN_005c4470 / FUN_005b1990, the unit's x / y getters, the
missile maker FUN_0059fa30 (recorded). A fake data block holds one Skills row
(+0x48 the maker's missile row, 7).
"""
import random
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

SKILL, CALC1, CALC2, MAKER = 1, 111, 222, 7


def gen(count, seed):
    rng = random.Random(seed)
    for index in range(count):
        px, py = rng.randint(1000, 30000), rng.randint(1000, 30000)
        near = rng.choice((0, 1, 2, 5, 20, 60))
        offset = rng.choice(((0, 0), (near, 0), (0, near), (-near, 0), (0, -near), (near, near), (-near, near),
                             (rng.randint(-near, near), rng.randint(-near, near))))
        calc2 = rng.choice((0, 1, 2, 3, 4, 5, 8, 9, rng.randint(2, 60), -rng.randint(1, 5)))
        town = int(rng.random() < 0.05)
        print(index, px + offset[0], py + offset[1], px, py, calc2, town)


def game(path):
    e = emu.Emu()
    now, got = {}, {}
    data = e.alloc(0x1000)
    skills = e.alloc(0x23C * 4)
    e.w32(0x744304, data)
    e.w32(data + 0xB98, skills)
    e.w32(data + 0xBA0, 4)
    e.w32(data + 0xBF0, 0x100)
    e.w32(data + 0xB6C, 0x100)
    row = skills + SKILL * 0x23C
    e.w32(row + 0xC0, CALC1)
    e.w32(row + 0x13C, CALC2)
    e.mu.mem_write(row + 0x48, MAKER.to_bytes(2, "little"))
    caster, piece = e.alloc(0x200), e.alloc(0x200)
    e.w32(caster, 0)
    e.w32(caster + 0xC, 1)
    e.w32(piece, 1)
    e.w32(piece + 0xC, 0x4242)

    def point(e):
        e.w32(e.arg(0), now["px"])
        e.w32(e.arg(1), now["py"])
        return 1

    def unit_x(e):
        return now["cx"] if e.mu.reg_read(UC_X86_REG_ECX) == caster else now["px"]

    def unit_y(e):
        return now["cy"] if e.mu.reg_read(UC_X86_REG_ECX) == caster else now["py"]

    def maker(e):
        record = e.mu.reg_read(UC_X86_REG_EDX)
        got.setdefault("makers", []).append([e.s32(record + off) for off in (0x14, 0x18, 0x1C, 0x20)])
        got["kind"], got["missile"] = e.r32(record), e.r32(record + 0x10)
        return 0x5000 + len(got["makers"])

    e.hook(0x56D2C0, point, 2)
    e.hook(0x620BB0, lambda e: 0x1000, 1)
    e.hook(0x463740, lambda e: 0x2000, 1)
    e.hook(0x61AB00, lambda e: now["town"], 1)
    e.hook(0x56E620, lambda e: 0, 5)
    e.hook(0x646CA0, lambda e: now["calc2"] & 0xFFFFFFFF if e.arg(1) == CALC2 else 1, 4)
    e.hook(0x56D940, lambda e: piece, 0)
    e.stub(0, {0x58F030: 4, 0x5A4850: 2, 0x5C4470: 4, 0x5B1990: 2, 0x64A710: 2})
    e.hook(0x45ADF0, unit_x, 0)
    e.hook(0x45AE20, unit_y, 0)
    e.hook(0x59FA30, maker, 0)
    e.hook(0x64A760, lambda e: got.setdefault("counts", []).append(e.arg(1) - (e.arg(1) >> 31 << 32)) or 0, 2)
    game_addr = e.alloc(0x1000)
    for line in open(path, encoding="utf-8"):
        index, cx, cy, px, py, calc2, town = map(int, line.split())
        now.update(cx=cx, cy=cy, px=px, py=py, calc2=calc2, town=town)
        got.clear()
        e.call(0x5C58B0, SKILL, 1, ecx=game_addr, edx=caster)
        makers, counts = got.get("makers", []), got.get("counts", [])
        assert len(makers) == len(counts) and (not makers or (got["kind"] == 0x21 and got["missile"] == MAKER)), line
        print(index, counts[0] if counts else 0, *(value for pair in makers for value in pair))


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    else:
        sys.exit(__doc__)
