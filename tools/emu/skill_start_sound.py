# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's skill start sounds (FUN_004c6140), run by its own code.

    uv run python skill_start_sound.py gen 300 1 > cases.tsv
    uv run python skill_start_sound.py game cases.tsv > g.txt
    ../../build/tests/test_skill_start_sound cases.tsv | diff g.txt -
    uv run python skill_start_sound.py inc g.txt > ../../tests/skill_start_sound_cases.inc

FUN_004c6140(EAX skill, ECX unit; item guid or -1, level) starts a skill on
any unit, player or monster: S->C 0x4c / 0x4d reach it through unit
commands 0x16 / 0x15 (FUN_004c6eb0 / FUN_004c6f40). Every call it makes
before its sounds (the skill node, the item, the mode, the town test) is
hooked to let the start through; the cltstfunc (+0xf2) is cltstfunc 17's
FUN_004e39a0, hooked to answer 1 or 0, or none (-1); FUN_00623c20 (the
unit's hit class) is hooked to the case's. FUN_004ca500 and FUN_004ca4c0
(the swing table at 0x727ed8, the unit's rate +0x4c) run for real. Each
FUN_004b9a00 call (ECX sound, EDX unit, the delay in ticks) is a line's
`sound:ticks`, in call order. d2d's rules::sound_ids::skill_start
(components/rules/sound_ids.hpp) gives the same for the same line
(tests/test_skill_start_sound.cpp).

A case line: unit type, unit class, the skill's charclass (+0xc, 255 none),
its flags byte +5 (bit 4 stsuccessonly, 5 stsounddelay, 6 weaponsnd), its
flags byte +7, stsound (+0xfc), stsoundclass (+0xfe), ItemCastSound
(+0x122), from an item (1) or not, the cltstfunc (0 none, 1 gives 1, 2
gives 0), the hit class, the unit's rate.
"""
import random
import sys

from emu import Emu

FUNCTION = 0x4c6140
DATA_TABLE = 0x744304
SKILL_ROW = 0x23c
CLTST_17 = 0x4e39a0
PLAY = 0x4b9a00
HIT_CLASS = 0x623c20
NODE = 0x5000
# What else FUN_004c6140 calls on the way: {address: (stack args, answer)}.
HOOKS = {
    0x4c58a0: (0, 1),      # the skill may start
    0x620250: (1, 0),      # no item held for it
    0x620210: (2, 0),
    0x463dd0: (0, 0),      # the local player: not this unit
    0x4aa8b0: (0, 0),
    0x647280: (6, 0),
    0x647aa0: (3, 0),
    0x6442a0: (3, 0),
    0x4648f0: (0, 0),      # no target
    0x620bb0: (1, 0),
    0x61ab00: (1, 0),      # not refused in town
    0x4c5710: (0, 0),
    0x643860: (2, 0),
    0x639df0: (2, 0),
    0x480e70: (0, 0),
    0x46e100: (0, 0),
    0x470390: (6, 0),
    0x4c5680: (0, 0),
}


def gen(count, seed):
    rng = random.Random(seed)
    for _ in range(count):
        unit_type = rng.choice((0, 0, 1, 1, 2))
        print(unit_type, rng.choice((0, 1, 2, 6)), rng.choice((255, 0, 2, 6)), rng.randrange(256), rng.randrange(256),
              rng.choice((0, 0x2a, 0x3f0)), rng.choice((0, 0x3f1)), rng.choice((0, 0x3f2)), rng.randrange(2), rng.randrange(3),
              rng.choice((0, 1, 2, 4, 6, 9, 10, 13, 0x21, 0x5c)), rng.choice((0, 128, 256, 300, 512)))


def game(path):
    emulator = Emu()
    table, skills, unit = emulator.alloc(0x1000), emulator.alloc(2 * SKILL_ROW), emulator.alloc(0x200)
    emulator.w32(DATA_TABLE, table)
    emulator.w32(table + 0xb98, skills)
    emulator.w32(table + 0xba0, 2)
    row = skills + SKILL_ROW                              # skill 1
    for address, (args, answer) in HOOKS.items():
        emulator.hook(address, lambda _emu, value=answer: value, args)
    emulator.hook(0x6439b0, lambda _emu: NODE, 1)
    played, case = [], {}

    def item_node(emu):
        emu.w32(emu.arg(4), 1)                            # its charges: some left
        return 1
    emulator.hook(0x643b00, item_node, 5)
    emulator.hook(CLTST_17, lambda _emu: case["cltst"] == 1, 1)
    emulator.hook(HIT_CLASS, lambda _emu: case["hit_class"], 0)

    def play(emu):
        from unicorn.x86_const import UC_X86_REG_ECX
        delay = emu.arg(0)
        played.append(f"{emu.mu.reg_read(UC_X86_REG_ECX) & 0xffff}:{delay - (1 << 32) if delay >> 31 else delay}")
        return 0
    emulator.hook(PLAY, play, 3)
    with open(path) as lines:
        for line in lines:
            fields = [int(field) for field in line.split()]
            if len(fields) != 12:
                continue
            unit_type, unit_class, char_class, flags5, flags7, st_sound, st_class, item_sound, from_item, cltst, hit_class, rate = fields
            case.update(cltst=cltst, hit_class=hit_class)
            emulator.mu.mem_write(row, bytes(SKILL_ROW))
            emulator.mu.mem_write(row + 5, bytes((flags5,)))
            emulator.mu.mem_write(row + 7, bytes((flags7,)))
            emulator.mu.mem_write(row + 0xc, bytes((char_class,)))
            emulator.mu.mem_write(row + 0xf2, (17 if cltst else 0xffff).to_bytes(2, "little"))
            emulator.mu.mem_write(row + 0xfc, st_sound.to_bytes(2, "little"))
            emulator.mu.mem_write(row + 0xfe, st_class.to_bytes(2, "little"))
            emulator.mu.mem_write(row + 0x122, item_sound.to_bytes(2, "little"))
            emulator.mu.mem_write(unit, bytes(0x200))
            emulator.w32(unit, unit_type)
            emulator.w32(unit + 4, unit_class)
            emulator.mu.mem_write(unit + 0x4c, rate.to_bytes(2, "little"))
            played.clear()
            emulator.call(FUNCTION, 5 if from_item else 0xffffffff, 1, ecx=unit, regs={"eax": 1})
            print(f"{line.strip()} =" + "".join(" " + cue for cue in played))


def inc(path):
    print("// Stock game.exe 1.14d, FUN_004c6140's sounds;")
    print("// generated by tools/emu/skill_start_sound.py (gen 300 1, game, inc).")
    print('// { "the case line", "each sound:ticks it plays, in order" }')
    with open(path) as lines:
        for line in lines:
            case, answers = line.split("=")
            print(f'    {{ "{case.strip()}", "{answers.strip()}" }},')


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "inc":
        inc(sys.argv[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
