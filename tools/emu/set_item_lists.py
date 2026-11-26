# SPDX-License-Identifier: GPL-3.0-or-later
"""Check which of a set item's own bonus lists FUN_00663a20 switches on, in stock game.exe 1.14d.

Run: export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
     /home/gui/Projects/d2decomped/tools/emu/.venv/bin/python set_item_lists.py [--mutation-check] [--cases FILE]

FUN_00663a20 (ECX item, EDX the worn-member mask FUN_00663cc0 got from
FUN_0062a370) runs for real, and so do FUN_006279a0 (the toggle) and
FUN_00625650 (the list by state) on a fake item: unit +0x5c an extended stat
list (+0x10 flag 0x80000000) whose +0x3c chain (next at +0x2c) holds the
five SetItems lists, states 0xa5..0xa9 (+0x14), each made with flags 0x2040
(+0x10, 0x2000 = off) as FUN_0065fec0 kind 4 makes them (drops.md). Hooked:
FUN_00627e70 (quality), FUN_0062a490 (the SetItems record, +0x87 the add
func), FUN_0062b3a0 (its +0x2e, the member index), FUN_006269f0 / FUN_00626e10
(unlink / relink, logged). Swept: add func 0..3, own index -1..6, mask
0..0x7f and four large masks, plus the gates (not an item, not quality 5, no
record). A list is on when its 0x2000 is clear afterwards.

The model is d2d's game::set_bonus_props (character.cpp): add func 1, list i
on while the member in place i (i, or i + 1 from its own index on) is in the
mask; add func 2, list i on with popcount(mask) >= i + 2; else none.
--cases FILE writes the cases d2d's data can hold (own 0..5, mask below
0x40) for tests/game/test_set_item_lists.cpp (tests/game/set_item_lists_cases.inc).
"""
import argparse
import sys

import emu

TARGET = 0x663A20
STATES = (0xA5, 0xA6, 0xA7, 0xA8, 0xA9)
MUT = set()


def model_on(add_func, own, mask):
    """d2d's set_bonus_props: the 5-bit mask of the item's own lists that are on."""
    others = mask & ~(1 << own) if own >= 0 else mask
    out = 0
    for bit in range(5):
        place = bit if bit < own or "noskip" in MUT else bit + 1
        if add_func == 1:
            on = others >> place & 1
        elif add_func == 2:
            count = bin(others if "noself" in MUT else mask).count("1")
            on = count >= bit + (1 if "count1" in MUT else 2)
        else:
            on = False
        out |= int(bool(on)) << bit
    return out


class Lists:
    """FUN_00663a20 over a fake item with the five SetItems lists."""

    def __init__(self):
        """Allocates the fake item, its stat list and five lists; hooks FUN_00663a20's lookups and relink."""
        e = self.e = emu.Emu()
        self.item, self.stats, self.record = e.alloc(0x100), e.alloc(0x100), e.alloc(0x100)
        self.lists = [e.alloc(0x60) for _ in STATES]
        self.quality, self.own, self.has_record = 5, 0, True
        self.relinks = []
        e.hook(0x627E70, lambda h: self.quality, 1)
        e.hook(0x62A490, lambda h: self.record if self.has_record else 0, 1)
        e.hook(0x62B3A0, lambda h: self.own & 0xFFFFFFFF, 1)
        e.hook(0x6269F0, lambda h: 0, 1)

        def relink(h):
            """FUN_00626e10 stand-in: logs (list, flag) and returns 0."""
            self.relinks.append((h.arg(1), h.arg(2)))
            return 0
        e.hook(0x626E10, relink, 3)

    def run(self, add_func, own, mask, unit_type=4):
        """Return (eax, on-mask of the five lists, relink count)."""
        e = self.e
        e.w32(self.item, unit_type)
        e.w32(self.item + 0x5C, self.stats)
        e.w32(self.stats + 0x10, 0x80000000)
        e.w32(self.stats + 0x3C, self.lists[0])
        e.w32(self.stats + 0x40, 0)
        for index, (lst, state) in enumerate(zip(self.lists, STATES)):
            e.w32(lst + 0x10, 0x2040)
            e.w32(lst + 0x14, state)
            e.w32(lst + 0x2C, self.lists[index + 1] if index + 1 < len(self.lists) else 0)
        e.mu.mem_write(self.record + 0x87, bytes((add_func,)))
        self.own, self.relinks = own, []
        eax = e.call(TARGET, ecx=self.item, edx=mask & 0xFFFFFFFF, limit=20000)
        on = sum(1 << i for i, lst in enumerate(self.lists) if not e.r32(lst + 0x10) & 0x2000)
        return eax, on, len(self.relinks)


def run(mutation, cases_out=None):
    """Return (in-domain differences, cases, out-of-domain differences)."""
    MUT.clear()
    MUT.update(mutation)
    game = Lists()
    bad = cases = outside = 0
    masks = list(range(0x80)) + [0x80, 0x100, 0x80000000, 0xFFFFFFFF]
    for add_func in range(4):
        for own in range(-1, 7):
            for mask in masks:
                eax, on, relinks = game.run(add_func, own, mask)
                want = model_on(add_func, own, mask)
                if eax != int(add_func != 0) or relinks != bin(on).count("1"):
                    bad += 1
                    print(f"add {add_func} own {own} mask {mask:#x}: eax {eax} relinks {relinks}")
                cases += 1
                in_domain = 0 <= own <= 5 and mask < 0x40
                if on != want:
                    if in_domain:
                        bad += 1
                        if bad < 6:
                            print(f"add {add_func} own {own} mask {mask:#x}: game {on:05b} d2d {want:05b}")
                    else:
                        outside += 1
                if in_domain and cases_out is not None:
                    cases_out.append(f"{{ {add_func}, {own}, {mask:#04x}, {on:#04x} }},")
    for gate in ("type", "quality", "record"):
        game.quality, game.has_record = (3 if gate == "quality" else 5), gate != "record"
        eax, on, _ = game.run(1, 0, 0x3F, unit_type=0 if gate == "type" else 4)
        cases += 1
        if eax != 0 or on != 0:
            bad += 1
            print(f"gate {gate}: eax {eax} on {on:05b}")
    game.quality, game.has_record = 5, True
    return bad, cases, outside


def main():
    """Run the oracle; --mutation-check proves a wrong list rule is caught."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true")
    parser.add_argument("--cases", help="write tests/game/set_item_lists_cases.inc here")
    args = parser.parse_args()
    lines = [] if args.cases else None
    bad, cases, outside = run((), lines)
    if bad:
        print(f"FAIL: {bad} of {cases} differ", file=sys.stderr)
        sys.exit(1)
    print(f"ok: {cases} FUN_00663a20 cases (add func 0..3, own index -1..6, masks); "
          f"{outside} outside d2d's data (own -1 / 6, mask >= 0x40) differ")
    if lines is not None:
        with open(args.cases, "w", encoding="ascii") as out:
            out.write("// tools/emu/set_item_lists.py --cases: { add func, own index, mask, lists on } from FUN_00663a20\n")
            out.write("\n".join(lines) + "\n")
    if args.mutation_check:
        for name in ("noskip", "noself", "count1"):
            bad, _, _ = run((name,))
            print(f"mutant {name}: {'caught' if bad else 'NOT CAUGHT'} ({bad})")
            if not bad:
                sys.exit(1)
        print("mutation check ok")


if __name__ == "__main__":
    main()
