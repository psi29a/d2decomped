# SPDX-License-Identifier: GPL-3.0-or-later
"""A monster's animation rate in a mode that isn't moving or a cast, as the
oracle: FUN_00623f50 from 0x6241d7 (its mode and class resolved) to its end.

    uv run python skill_attack_rate.py gen 20000 1 > cases.tsv
    uv run python skill_attack_rate.py game cases.tsv > g.txt     # `id rate` (unit +0x4c)

FUN_00621580 (mode, unit type 1, class < 0x19a: the row at 0x6e8b90 + mode x
0x14) says whether the mode is an attack: +0xc set (A1, A2) always; +8 set
(S1..S4, SQ) when the unit's current skill (FUN_00620250 -> FUN_00643ce0 ->
FUN_0045c4b0, the Skills row) has byte +6 & DAT_006ce274 (8): the flag the
loader sets from Skills.txt UseAttackRate. An attack: rate = effective IAS
(FUN_00621740 row 0, stat 93) + stat 68 attackrate, clamped 15..175. Else
FUN_00621630 (+0x10: DT, DD) plays the AnimData speed as it is, any other mode
stat 69 clamped 15..175. +0x4c = rate x speed / 100 (at most 0x7fff).

A case line is `id mode skill ias pct speed`: the monster mode's code, the
Skills.txt row id of the current skill (game.exe's own loaded table: the
UseAttackRate flag is the claim), stat 93 (raw, before the diminishing),
`pct` = stats 68 and 69 (d2d: 100 + monster.anim_pct), the AnimData speed.

Hooked (not the claim): FUN_00620250 / FUN_00643ce0 (the current skill: the
case's id), the stat getters FUN_00625500 (stat 93 -> ias) and FUN_00625480
(68 / 69 -> pct), FUN_006235a0 (dual wield: no, player only), FUN_00623c80
(a mode-start call, nothing), FUN_0063a7b0 (state 0x26: off). The data tables
are game.exe's own (drlg.boot).
"""
import random
import sys

MODES = ("DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN")
CASE_MODES = ("DT", "NU", "GH", "A1", "A2", "BL", "S1", "S2", "S3", "S4", "DD", "KB", "SQ")


def skills():
    """(row id, name, UseAttackRate) of every Skills.txt row with no charclass (the monster skills)."""
    from emu import mpq_cache
    lines = mpq_cache().read("data\\global\\excel\\skills.txt").decode("latin1").split("\r\n")
    head = lines[0].split("\t")
    out = []
    for line in lines[1:]:
        row = line.split("\t")
        if len(row) < len(head) or row[0] == "Expansion":
            continue
        if not row[head.index("charclass")]:
            out.append((int(row[head.index("Id")]), row[0], int(row[head.index("UseAttackRate")] or 0)))
    return out


def gen(count, seed):
    rows = skills()
    with_flag = [row for row in rows if row[2]]
    without = [row for row in rows if not row[2]]
    rng = random.Random(seed)
    for index in range(count):
        skill = rng.choice(with_flag if rng.random() < 0.5 else without)[0]
        mode = rng.choice(CASE_MODES)
        ias = rng.choice((0, 0, 20, -20, rng.randint(-100, 200)))
        pct = rng.choice((100, 100, 150, 50, rng.randint(0, 300)))
        speed = rng.choice((256, 256, 128, 200, rng.randint(1, 512)))
        print(index, mode, skill, ias, pct, speed)


def game(path):
    import drlg
    e = drlg.boot()
    now = {}
    e.hook(0x620250, lambda e: 0x1000, 1)
    e.hook(0x643CE0, lambda e: now["skill"], 3)
    e.hook(0x625500, lambda e: now["ias"] if e.arg(1) == 93 else 0, 3)
    e.hook(0x625480, lambda e: now["pct"] if e.arg(1) in (68, 69) else 0, 3)
    e.hook(0x6235A0, lambda e: 0, 1)
    e.hook(0x623C80, lambda e: 0, 2)
    e.hook(0x63A7B0, lambda e: 0, 2)
    unit, anim = e.alloc(0x100), e.alloc(0x40)
    for line in open(path, encoding="utf-8"):
        word = line.split()
        now.update(skill=int(word[2]), ias=int(word[3]) & 0xFFFFFFFF, pct=int(word[4]) & 0xFFFFFFFF)
        print(word[0], run_mode(e, unit, anim, 1, 5, MODES.index(word[1]), int(word[5])))


def run_mode(e, unit, anim, unit_type, unit_class, mode, speed):
    """FUN_00623f50 from 0x6241d7 on `unit` (type, class and mode resolved), its AnimData speed; returns +0x4c."""
    import emu
    mu = e.mu
    frame = emu.STACK + emu.STACK_SIZE - 0x2000
    e.w32(anim + 0xC, speed)
    mu.mem_write(unit, bytes(0x100))
    e.w32(unit, unit_type)
    e.w32(unit + 0x10, mode)
    e.w32(frame, 0)
    e.w32(frame + 4, emu.RET_MAGIC)
    e.w32(frame + 8, unit_type)                # [ebp+8]: the unit type FUN_00645270 left
    e.w32(frame - 4, unit_class)               # [ebp-4]: its class
    e.w32(frame - 8, mode)                     # [ebp-8]: the mode
    e.w32(frame - 0xC, anim)                   # [ebp-0xc]: the AnimData record
    mu.reg_write(emu.UC_X86_REG_EBP, frame)
    mu.reg_write(emu.UC_X86_REG_ESP, frame - 0x1C)
    mu.reg_write(emu.UC_X86_REG_EDI, unit)
    mu.emu_start(0x6241D7, emu.RET_MAGIC)
    return e.r16(unit + 0x4C)

if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    else:
        sys.exit(__doc__)
