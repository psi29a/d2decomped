# SPDX-License-Identifier: GPL-3.0-or-later
"""A new character's start items as game.exe gives them (docs/research/re/drops.md "A new character's items").

    uv run python new_character_stacks.py           # each class's CharStats items
    uv run python new_character_stacks.py quivers   # the Amazon's row with arrows in a hand and with no loc, bolts in a hand

FUN_00534f10 runs for real on a fake player (unit +4 the class), over its
CharStats row's ten item slots (+0x5c, 8 bytes each). FUN_00534d60 and
FUN_00534c70 run for real too: the code to its index (FUN_00633640), the
stack check (FUN_006289f0), maxstack (FUN_006295b0), the quiver check
(FUN_00629bb0, ItemTypes row 5) and belt check (FUN_0062bad0). Stubbed:
making the unit (FUN_00559ce0, a fake item of that index), its ilvl
(FUN_00558200), its flags and body spot (FUN_006280d0, FUN_00627d70),
putting it in a hand, the inventory or the belt (FUN_00534bb0,
FUN_00534b30, FUN_0055e9b0) and max durability (FUN_00625e00).
FUN_00627260 (set stat) is hooked to record each item's stats. A line per
item: its code, then stat 70 (quantity), or `-` when nothing set it, and
the StartSkill stat FUN_00627150 sets on the first slot's item: its param
(the skill) and value. Should print `jav 60` for the Amazon, `stat 0x6b 36
= 1` (Fire Bolt) for the Sorceress and `stat 0x6b 70 = 1` (Raise Skeleton)
for the Necromancer, and `aqv 250, aqv 100, cqv 250` for quivers: arrows
250 in a hand, 100 elsewhere; bolts (maxstack 250) full.
"""
import sys

from unicorn.x86_const import UC_X86_REG_EDX

import drlg

SLOTS = 0x5c                                           # CharStats item1 .. item10: code, loc, count


def main():
    e = drlg.boot()
    rows = e.r32(e.r32(0x744304) + 0xbc4)
    classes = range(7)
    if sys.argv[1:] == ["quivers"]:
        row = rows                                     # the Amazon's: arrows (bowq) in the left hand and with no loc, bolts (xboq) in the left hand
        for slot in range(10): e.mu.mem_write(row + SLOTS + 8 * slot, b"\0" * 8)
        for slot, (code, loc) in enumerate(((b"aqv ", 5), (b"aqv ", 0), (b"cqv ", 5))):
            e.mu.mem_write(row + SLOTS + 8 * slot, code + bytes([loc, 1, 0, 0]))
        classes = range(1)

    items, stats = [], {}

    def make(e):                                       # FUN_00559ce0(ECX owner, EDX item index; game, ...)
        item, data, path = e.alloc(0x100), e.alloc(0x100), e.alloc(0x40)
        e.w32(item, 4)
        e.w32(item + 4, e.mu.reg_read(UC_X86_REG_EDX))
        e.w32(item + 0xc, 0x100 + len(items))
        e.w32(item + 0x14, data)
        e.w32(item + 0x2c, path)                       # the belt's FUN_0055e9b0 call reads path +0xc
        items.append(item)
        stats[item] = {}
        return item
    e.hook(0x559ce0, make, 9)
    e.hook(0x627260, lambda e: stats.setdefault(e.arg(0), {}).__setitem__(e.arg(1), e.arg(2)), 4)
    e.hook(0x625480, lambda e: stats.get(e.arg(0), {}).get(e.arg(1), 0), 3)   # stat 0xfe (extra stack): none
    e.hook(0x558200, lambda e: 1, 0)
    e.stub(0, {0x6280d0: 3, 0x627d70: 2, 0x625e00: 1, 0x6257d0: 3, 0x626e10: 3})
    skills = []                                       # the first slot's StartSkill (stat 0x6b on its list)
    e.hook(0x6251f0, lambda e: 0x5157, 5)
    e.hook(0x627150, lambda e: skills.append((e.arg(1), e.arg(3), e.arg(2))), 4)   # (list, stat, value, param)
    e.stub(1, {0x534bb0: 1, 0x534b30: 2, 0x55e9b0: 4})

    codes = {}
    game, player = e.alloc(0x2000), e.alloc(0x200)
    for cls in classes:
        items.clear(); skills.clear()
        e.w32(player + 4, cls)
        e.call(0x534f10, 0, ecx=player, edx=game)
        line = []
        for item in items:
            index = e.r32(item + 4)
            if index not in codes: codes[index] = e.read(e.call(0x6335f0, index) + 0x80, 4).decode("latin-1").rstrip(" \0")
            quantity = stats[item].get(0x46)
            line.append(f"{codes[index]} {'-' if quantity is None else quantity}")
        print(f"class {cls}: " + ", ".join(line) + "".join(f" | stat {stat:#x} {param} = {value}" for stat, param, value in skills))


if __name__ == "__main__":
    main()
