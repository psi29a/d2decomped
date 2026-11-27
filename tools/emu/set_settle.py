# SPDX-License-Identifier: GPL-3.0-or-later
"""The requirements pass's set lists, and a break's, in stock game.exe 1.14d.

Run: export D2_MPQ_DIR=/path/to/the/local/game/data
     .venv/bin/python set_settle.py [--mutation-check]
     .venv/bin/python set_settle.py inc --out ../../tests/set_settle_cases.inc

FUN_0055dbc0 (ItemMode.cpp's requirements pass; ECX game, EDX a player, one stack word 0) runs for real
over ten body slots of random items (quality, flags 0x100 / 0x4000), every requirement met
(FUN_0055db00 hooked to 1). Hooked: the slot lookup FUN_0063bde0, the flag test / set FUN_006280a0 /
FUN_006280d0 (a model of each item's flags), the item's stats on FUN_00625820 (on unless broken or
flagged), the quality FUN_00627e70, FUN_00627d40 (0), FUN_00621190 (1, a body item), the inventory
walk FUN_0063b2c0 (none), and the side effects FUN_0055d9d0, FUN_0063d2b0, FUN_0063d1d0, FUN_0055da70,
FUN_00621000; FUN_0055c730 (stats off) and FUN_0055c2c0 (stats on: a set piece rebuilds its set's
list with itself, FUN_00663cc0(.., 0, 0); set_list.py) are recorded. The claim, d2d's
rules::settle_requirements:
  - a flagged unbroken item that meets its requirements loses 0x4000 and its stats go on, in slot order;
  - then every body slot 1..10 in order holding a set item (quality 5) neither broken nor flagged has
    its stats taken off and put on again (0x55de7c / 0x55de88), so each set's list is last built with
    its piece in the highest slot.
Then FUN_0055f850 (a break; ECX game, EDX the owner, the item on the stack) runs with every callee
hooked and recorded: flag 0x100 on, FUN_0055c730(.., 1, 1), FUN_0055dbc0, and last FUN_00663cc0(owner,
item, 1, 1), whose FUN_00663b40 with 1 frees the item's set's list (set_list.py): the pass rebuilt it,
the break frees it, so the set's other pieces give nothing until the next stats on.
`inc` writes the pass's cases for tests/test_set_settle.cpp.
"""
import argparse
import random
import sys
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX

import emu

PASS, BREAK = 0x55DBC0, 0x55F850
MUT = set()


def model(slots):
    """d2d's settle_requirements with every requirement met: the slots stamped, in stamp order (last wins)."""
    flags = [item[1] if item else 0 for item in slots]
    order = []

    def stamp(slot):
        """A slot's stats go on: it moves to the end of the order."""
        if slot in order:
            order.remove(slot)
        order.append(slot)

    changed = True
    while changed:
        changed = False
        for slot, item in enumerate(slots):
            if item and not flags[slot] & 0x100 and flags[slot] & 0x4000:
                flags[slot] &= ~0x4000
                stamp(slot)
                changed = True
    if "no_final" not in MUT:
        walk = range(9, -1, -1) if "reverse" in MUT else range(10)
        for slot in walk:
            item = slots[slot]
            broken_ok = "final_broken" in MUT
            if item and (item[0] == 5 or "any_quality" in MUT) and (broken_ok or not flags[slot] & 0x100) and not flags[slot] & 0x4000:
                stamp(slot)
    return order


def build(rng):
    """Ten body slots: None or (quality, flags)."""
    return [None if rng.random() < 0.3 else (rng.choice((2, 4, 5, 5, 5, 7)), rng.choice((0, 0, 0, 0x100, 0x4000, 0x4100)))
            for _ in range(10)]


class Rig:
    """FUN_0055dbc0 with its callees hooked: the items, their flags and quality, and the stats off / on calls."""

    def __init__(self):
        """A fresh emulator, the game, a player with an inventory, and every hook of the pass."""
        e = self.e = emu.Emu()
        self.game, self.unit, self.inv = e.alloc(0x200), e.alloc(0x200), e.alloc(0x100)
        e.w32(self.unit, 0)
        e.w32(self.unit + 0x60, self.inv)
        self.items, self.flags, self.quality, self.calls = {}, {}, {}, []
        e.hook(0x55D9D0, lambda _e: 0, 0)
        e.hook(0x63B2C0, lambda _e: 0, 1)
        e.hook(0x63BDE0, lambda _e: self.items.get(_e.arg(1), 0), 2)
        e.hook(0x6280A0, lambda _e: int(self.flags[_e.arg(0)] & _e.arg(1) != 0), 4)
        e.hook(0x6280D0, self.set_flag, 3)
        e.hook(0x625820, lambda _e: int(not self.flags[_e.arg(0)] & 0x4100), 2)
        e.hook(0x55DB00, lambda _e: 1, 0)
        e.hook(0x627E70, lambda _e: self.quality[_e.arg(0)], 1)
        e.hook(0x627D40, lambda _e: 0, 1)
        e.hook(0x621190, lambda _e: 1, 1)
        for addr, nstack in ((0x63D2B0, 2), (0x63D1D0, 2), (0x55DA70, 0), (0x621000, 2)):
            e.hook(addr, lambda _e: 0, nstack)
        e.hook(0x55C730, lambda _e: self.calls.append(("off", self.slot_of(_e))) or 0, 3)
        e.hook(0x55C2C0, lambda _e: self.calls.append(("on", self.slot_of(_e))) or 0, 2)

    def set_flag(self, e):
        """FUN_006280d0(item, flag, on): sets or clears the flag in the model."""
        item, flag, on = e.arg(0), e.arg(1), e.arg(2)
        self.flags[item] = self.flags[item] | flag if on else self.flags[item] & ~flag
        return 0

    def slot_of(self, e):
        """The body slot (0-based) of the item in EDX, for FUN_0055c730 / FUN_0055c2c0."""
        item = e.mu.reg_read(UC_X86_REG_EDX)
        return next(slot for slot, each in self.items.items() if each == item) - 1

    def settle(self, slots):
        """FUN_0055dbc0 on these slots: the slots whose stats went on, in last-on order, and the off / on pairs."""
        self.items, self.flags, self.quality, self.calls = {}, {}, {}, []
        for slot, item in enumerate(slots):
            if item:
                unit = self.e.alloc(0x100)
                self.e.w32(unit, 4)
                self.items[slot + 1], self.flags[unit], self.quality[unit] = unit, item[1], item[0]
        self.e.call(PASS, 0, ecx=self.game, edx=self.unit)
        order = []
        for kind, slot in self.calls:
            if kind == "on":
                if slot in order:
                    order.remove(slot)
                order.append(slot)
        return order

    def break_order(self):
        """FUN_0055f850 with its callees recorded: the calls that touch the stats and the set lists."""
        e = emu.Emu()
        calls = []
        item, owner, game = e.alloc(0x100), e.alloc(0x200), e.alloc(0x100)
        e.w32(item, 4)
        e.w32(owner + 0x60, e.alloc(0x40))
        e.hook(0x6280D0, lambda _e: calls.append(("flag", hex(_e.arg(1)), _e.arg(2))) or 0, 3)
        e.hook(0x621190, lambda _e: 1, 1)
        e.hook(0x55C730, lambda _e: calls.append(("stats off", _e.arg(1), _e.arg(2))) or 0, 3)
        e.hook(0x55DBC0, lambda _e: calls.append(("requirements pass",)) or 0, 1)
        e.hook(0x663CC0, lambda _e: calls.append(("set list", _e.arg(2), _e.arg(3))) or 0, 4)
        for addr, nstack in ((0x63D2B0, 2), (0x63CC70, 2), (0x621000, 2), (0x627260, 4), (0x5531C0, 1), (0x53D130, 4), (0x553380, 1)):
            e.hook(addr, lambda _e: 0, nstack)
        e.call(BREAK, item, ecx=game, edx=owner)
        return calls


def run(rig, mutation, cases=3000, collect=None):
    """Each case through game.exe and the model; the failure count."""
    MUT.clear()
    MUT.update(mutation)
    rng = random.Random(20261126)
    bad = 0
    for index in range(cases):
        slots = build(rng)
        got, want = rig.settle(slots), model(slots)
        if got != want:
            bad += 1
            if bad <= 3:
                print(f"case {index}: {slots}: game {got} model {want}")
        if collect is not None:
            collect.append((slots, got))
    return bad


def main():
    """Run the oracle, with --mutation-check prove each mutant of the model is caught, or write the fixture."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", choices=("inc",))
    parser.add_argument("--out")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig = Rig()
    collect = []
    bad = run(rig, (), collect=collect)
    want_break = [("flag", "0x100", 1), ("stats off", 1, 1), ("requirements pass",), ("set list", 1, 1)]
    got_break = rig.break_order()
    if bad or got_break != want_break:
        print(f"FAIL: {bad} cases differ; the break's calls {got_break}", file=sys.stderr)
        sys.exit(1)
    if args.mode == "inc":
        lines = ["// Generated by tools/emu/set_settle.py inc: FUN_0055dbc0's answers from game.exe 1.14d.",
                 "// { ten body slots' quality (-1 empty), their flags, the slots whose stats went on in last-on order (-1 ends) }"]
        for slots, order in collect[:500]:
            quality = ", ".join(str(item[0] if item else -1) for item in slots)
            flags = ", ".join(str(item[1] if item else 0) for item in slots)
            ranks = ", ".join(str(slot) for slot in order + [-1] * (10 - len(order)))
            lines.append(f"{{ {{ {quality} }}, {{ {flags} }}, {{ {ranks} }} }},")
        text = "\n".join(lines) + "\n"
        if args.out:
            Path(args.out).write_text(text)
        else:
            sys.stdout.write(text)
        return
    print(f"ok: {len(collect)} FUN_0055dbc0 cases; FUN_0055f850 frees the set's list after the pass: {got_break}")
    if args.mutation_check:
        for name in ("no_final", "reverse", "final_broken", "any_quality"):
            caught = run(rig, (name,))
            print(f"mutant {name}: {'caught' if caught else 'NOT CAUGHT'} ({caught})")
            if not caught:
                sys.exit(1)
        print("mutation check ok")


if __name__ == "__main__":
    main()
