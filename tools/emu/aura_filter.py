# SPDX-License-Identifier: GPL-3.0-or-later
"""A skill's aurafilter on a monster, by game.exe's own FUN_0056b3e0.

    uv run python aura_filter.py gen 200000 1 > cases.tsv
    uv run python aura_filter.py [--exe PATH] game cases.tsv > g.txt   # each case line + game.exe's answer
    ../../build/tests/test_aura_filter cases.tsv | diff g.txt -
    uv run python aura_filter.py inc g.txt > ../../tests/aura_filter_cases.inc   # the test's fixture (gen 2000 1)

FUN_0056b3e0 (ECX the caster, EDX the unit tested, the flags on the stack)
runs for real on a monster (type 1) and a player caster (type 0): its
switch on the unit type, the mode tests (0x1000: mode 0xc only; clear:
neither 0xc nor 0), the unit flags +0xc4 bit 2 (0x80) and bit 3 (0x400),
the MonStats +0xd test under 0x10000 against game.exe's own mask byte at
0x6ce268, and the order of the rest. d2d's rules::aura_filter_takes is the
port.

Hooked, each answering from the case: FUN_0063e990 (undead), FUN_0063e9f0
(boss), FUN_0063edc0 (prime evil), FUN_00620bb0 (the unit's room),
FUN_0061ab00 (the room's level a town: 0x100), FUN_00554010 (the game),
FUN_00554de0 / FUN_00554200 (same side / other side: the one `ally`
answer and its opposite, as both read FUN_00650d70), FUN_00451f80 (the
MonStats row: +0xd bit 0 is the case's npc), FUN_00622c40 (0x20000),
FUN_00639df0 (state 0x56), FUN_0045adf0 / FUN_0045ae20 (positions) and
FUN_0064e260 (a blocked line, mask 4, from the caster's room: 0x200). A
hook that's asked for a mask, a state or an argument the claim doesn't
expect stops the run.

A case line is `seq filter mode undead boss prime ally npc flag4 flag8 town
melee justhit line`: mode 0 living (the walk's 1, here), 1 dying (mode 0),
2 a corpse (mode 0xc); then 0/1 for each answer. To stderr: how many cases
a line-for-line copy of d2d's old aura_filter_takes (upstream before this
change: a filter of 0 as 0x583, dying taken as dead, no npc / unit-flag /
town / melee / state / line bits) gets wrong.
"""
import random
import sys

import emu

FILTER = 0x56B3E0
NPC_MASK = 0x6CE268
KNOWN_ROWS = (0x2, 0x3, 0x1102, 0x8383, 0x8583, 0x870B, 0x8783, 0xA583, 0xA587, 0xA783, 0xC583, 0xE003, 0xE786,
              0x10103, 0x12001, 0x12003, 0x18000, 0x4A283, 0x2A783, 0x583)   # Skills.txt 1.14d, in_reach's, the walk's default
BITS = (0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x80, 0x100, 0x200, 0x400, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000, 0x20000, 0x40000, 0x80000)
FIELDS = ("mode", "undead", "boss", "prime", "ally", "npc", "flag4", "flag8", "town", "melee", "justhit", "line")


def gen(count, seed):
    rng = random.Random(seed)
    for seq in range(count):
        shape = rng.random()
        if shape < 0.5:
            flt = rng.choice(KNOWN_ROWS)
        elif shape < 0.9:                                         # a known row with bits flipped
            flt = rng.choice(KNOWN_ROWS)
            for _ in range(rng.randint(1, 4)): flt ^= rng.choice(BITS)
        else:                                                     # any 20 bits, 0 among them
            flt = rng.choice((0, rng.randrange(1 << 20), rng.randrange(1 << 32)))
        mode = rng.choice((0, 0, 0, 1, 2))
        flags = [int(rng.random() < chance) for chance in (0.3, 0.2, 0.1, 0.3, 0.1, 0.9, 0.9, 0.2, 0.5, 0.2, 0.8)]
        print(seq, flt, mode, *flags)


def game(path, exe=None):
    e = emu.Emu(exe=exe)
    caster, unit, game_struct, row = e.alloc(0x100), e.alloc(0x100), e.alloc(0x100), e.alloc(0x1A8)
    caster_room, unit_room = e.alloc(0x10), e.alloc(0x10)
    e.w32(caster, 0)                                              # a player
    e.w32(unit, 1)                                                # a monster
    e.w32(unit + 4, 77)                                           # its class, FUN_00451f80's ECX
    now = {}

    def ecx(): return e.mu.reg_read(emu.UC_X86_REG_ECX)
    def edx(): return e.mu.reg_read(emu.UC_X86_REG_EDX)

    def only(cond, what):
        if not cond: raise SystemExit(f"aura_filter: unexpected {what}")

    def of_unit(name):
        def answer(e):
            only(e.arg(0) == unit, f"{name} on {e.arg(0):x}")
            return now[name]
        return answer

    def boss(e):
        only(e.arg(0) == 0 and e.arg(1) == unit, "FUN_0063e9f0 args")
        return now["boss"]

    def room(e):
        return {caster: caster_room, unit: unit_room}[e.arg(0)]

    def town(e):
        only(e.arg(0) == unit_room, "FUN_0061ab00 on the caster's room")
        return now["town"]

    def side(same):
        def answer(e):
            only(ecx() == game_struct and edx() == caster and e.arg(0) == unit, "side args")
            return now["ally"] if same else 1 - now["ally"]
        return answer

    def mon_row(e):
        only(ecx() == 77, "FUN_00451f80 class")
        return row

    def melee(e):
        only((e.arg(0), e.arg(1), e.arg(2)) == (caster, unit, 0), "FUN_00622c40 args")
        return now["melee"]

    def state(e):
        only((e.arg(0), e.arg(1)) == (unit, 0x56), "FUN_00639df0 args")
        return now["justhit"]

    def line(e):
        only(e.arg(0) == caster_room and e.arg(3) == 4, "FUN_0064e260 room or mask")
        return 1 - now["line"]                                    # nonzero: blocked

    e.hook(0x63E990, of_unit("undead"), 1)
    e.hook(0x63E9F0, boss, 2)
    e.hook(0x63EDC0, of_unit("prime"), 1)
    e.hook(0x620BB0, room, 1)
    e.hook(0x61AB00, town, 1)
    e.hook(0x554010, lambda e: game_struct, 0)
    e.hook(0x554DE0, side(True), 1)
    e.hook(0x554200, side(False), 1)
    e.hook(0x451F80, mon_row, 0)
    e.hook(0x622C40, melee, 3)
    e.hook(0x639DF0, state, 2)
    e.hook(0x45ADF0, lambda e: 10, 0)
    e.hook(0x45AE20, lambda e: 20, 0)
    e.hook(0x64E260, line, 4)
    only(e.read(NPC_MASK, 1) == b"\x01", "npc mask byte")
    wrong = 0
    for text in open(path):
        seq, flt, *values = map(int, text.split())
        now.clear()
        now.update(zip(FIELDS, values))
        e.w32(unit + 0x10, (1, 0, 0xC)[now["mode"]])            # living: the walk's mode 1
        e.w32(unit + 0xC4, (now["flag4"] << 2) | (now["flag8"] << 3))
        e.mu.mem_write(row + 0xD, bytes((now["npc"],)))
        took = e.call(FILTER, flt & 0xFFFFFFFF, ecx=caster, edx=unit) & 0xFFFFFFFF
        wrong += old_d2d(flt, now) != bool(took)
        print(seq, flt, *values, int(bool(took)))
    print(f"aura_filter: the old aura_filter_takes gets {wrong} cases wrong", file=sys.stderr)


def old_d2d(flt, case):
    """d2d's aura_filter_takes before this change, line for line."""
    if flt == 0: flt = 0x583
    if flt & 0x2 == 0: return False
    if (case["mode"] != 0) != (flt & 0x1000 != 0): return False
    if flt & 0x4 and not case["undead"]: return False
    if flt & 0x4000 and case["boss"]: return False
    if flt & 0x40000 and case["prime"]: return False
    if flt & 0x8000 and case["ally"]: return False
    if flt & 0x10000 and not case["ally"]: return False
    return True


def inc(path):
    print("// Stock game.exe 1.14d, FUN_0056b3e0; generated by tools/emu/aura_filter.py (gen 2000 1, game, inc).")
    print("// filter, mode (0 living, 1 dying, 2 corpse), undead, boss, prime, ally, npc, flag4, flag8, town, melee, justhit, line, taken")
    for text in open(path):
        _, *fields = text.split()
        print("    { " + ", ".join(fields) + " },")


def main():
    args = sys.argv[1:]
    exe = None
    if "--exe" in args:
        at = args.index("--exe")
        exe = args[at + 1]
        del args[at:at + 2]
    if len(args) == 3 and args[0] == "gen":
        gen(int(args[1]), int(args[2]))
    elif len(args) == 2 and args[0] == "game":
        game(args[1], exe)
    elif len(args) == 2 and args[0] == "inc":
        inc(args[1])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
