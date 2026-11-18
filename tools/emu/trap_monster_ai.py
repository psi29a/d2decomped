# SPDX-License-Identifier: GPL-3.0-or-later
"""Check chest-trap first-think timing and the three trap AIs in stock 1.14d.

The real FUN_00573780, FUN_005fb5b0, FUN_005fb900 and FUN_005fb9b0 run
against synthetic game, unit, AI-control, MonStats and think-context records.
Mode changes, event scheduling, attacks, idles and death are hooked and
compared with a small model; no MPQs are read.

Run: python trap_monster_ai.py [--mutation-check]
"""
import argparse
import struct

import emu


AI_FUNCTIONS = ((0x5FB5B0, "missile"), (0x5FB900, "skill"), (0x5FB9B0, "skill"))


class Oracle:
    """One emulator with the unclaimed AI actions recorded."""

    def __init__(self):
        self.machine = emu.Emu()
        self.game = self.machine.alloc(0x200)
        self.unit = self.machine.alloc(0x200)
        self.target = self.machine.alloc(0x200)
        self.control = self.machine.alloc(0x40)
        self.row = self.machine.alloc(0x220)
        self.context = self.machine.alloc(0x40)
        self.calls = []
        self.machine.hook(0x540E60, self.action("clear", 2), 2)
        self.machine.hook(0x5417D0, self.action("event", 4), 4)
        self.machine.hook(0x5DDFC0, self.action("death", 3), 3)
        self.machine.hook(0x5DDF90, self.action("missile", 2), 2)
        self.machine.hook(0x5DE080, self.action("idle", 1), 1)
        self.machine.hook(0x5DEAD0, self.action("skill", 5), 5)
        self.machine.hook(0x463860, lambda current: 0, 0)

    def action(self, name, count):
        """Return a hook that records `count` stack arguments."""
        def callback(current):
            self.calls.append((name, *(current.arg(index) for index in range(count))))
            return 1
        return callback

    def word(self, address, value):
        """Write one signed 16-bit table cell."""
        self.machine.mu.mem_write(address, struct.pack("<h", value))

    def first_think(self, mode):
        """Run FUN_00573780 for one unit mode and return its recorded calls."""
        self.machine.w32(self.game + 0xA8, 100)
        self.machine.w32(self.unit + 0x10, mode)
        self.calls.clear()
        self.machine.call(0x573780, ecx=self.game, edx=self.unit)
        return list(self.calls)

    def think(self, address, kind, difficulty, present, distance, count, toggle, skill=7):
        """Run one Trap-* AI case and return calls plus its written state."""
        self.machine.mu.mem_write(self.game + 0x6D, bytes([difficulty]))
        self.machine.w32(self.unit + 0xC4, 0x100)
        self.machine.w32(self.control + 0x14, count)
        self.machine.w32(self.control + 0x18, toggle)
        self.machine.w32(self.context, self.control)
        self.machine.w32(self.context + 8, self.target if present else 0)
        self.machine.w32(self.context + 0x14, distance)
        self.machine.w32(self.context + 0x1C, self.row)
        for index, value in enumerate((5, 10, 20)):
            self.word(self.row + 0x56 + index * 2, value)
        for index, value in enumerate((1, 2, 3)):
            self.word(self.row + 0x5C + index * 2, value)
        for index, value in enumerate((7, 8, 9)):
            self.word(self.row + 0x62 + index * 2, value)
        self.word(self.row + 0x170, skill)
        self.machine.mu.mem_write(self.row + 0x180, b"\x0b")
        self.calls.clear()
        self.machine.call(address, self.context, ecx=self.game, edx=self.unit)
        return (list(self.calls), self.machine.r32(self.unit + 0xC4),
                self.machine.r32(self.control + 0x14), self.machine.r32(self.control + 0x18), kind)


def model(kind, difficulty, present, distance, count, toggle, skill=7, target=0, mutations=frozenset()):
    """Model the Trap-* branch and its writes for one synthetic case."""
    reaches = (5, 10, 20)[difficulty]
    acts = (1, 2, 3)[difficulty]
    in_range = distance < reaches if "exclusive" in mutations else distance <= reaches
    under_count = count <= acts if "count" in mutations else count < acts
    has_target = True if "target" in mutations else present
    calls = []
    flags = 0x100
    if has_target and in_range and under_count:
        if kind == "missile" and toggle == 0:
            calls.append(("missile", 4, target))
            count += 1
            toggle = 1
        elif kind == "skill" and skill >= 0 and toggle == 0:
            calls.append(("skill", 11, skill, target, 0, 0))
            count += 1
            toggle = 1
        else:
            toggle = 0
            calls.append(("idle", (7, 8, 9)[difficulty]))
    else:
        flags |= 0x20000
        calls.append(("death", 0, 0, 0))
    return calls, flags, count, toggle, kind


def run(mutations=frozenset()):
    """Compare the stock routines with the model over their branch boundaries."""
    oracle = Oracle()
    expected_delay = 103 if "delay" in mutations else 102
    first_cases = ((1, [("clear", 2, 0), ("clear", 2, 0), ("event", 2, expected_delay, 0, 0)]),
                   (0, [("clear", 2, 0)]), (12, [("clear", 2, 0)]))
    checked = 0
    for mode, expected in first_cases:
        actual = oracle.first_think(mode)
        if actual != expected:
            raise AssertionError(f"FUN_00573780 mode {mode}: {actual!r} != {expected!r}")
        checked += 1
    for address, kind in AI_FUNCTIONS:
        for difficulty in range(3):
            reach = (5, 10, 20)[difficulty]
            act_limit = (1, 2, 3)[difficulty]
            cases = ((False, 0, 0, 0, 7), (True, reach + 1, 0, 0, 7),
                     (True, reach, act_limit, 0, 7), (True, reach, 0, 0, 7),
                     (True, reach, 0, 1, 7))
            if kind == "skill":
                cases += ((True, reach, 0, 0, -1),)
            for present, distance, count, toggle, skill in cases:
                actual = oracle.think(address, kind, difficulty, present, distance, count, toggle, skill)
                expected = model(kind, difficulty, present, distance, count, toggle, skill, oracle.target, mutations)
                if actual != expected:
                    raise AssertionError(f"{address:08x} difficulty {difficulty}: {actual!r} != {expected!r}")
                checked += 1
    return checked


def main():
    """Run the comparison, or prove four model defects are detected."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    if not args.mutation_check:
        print(f"ok: {run()} trap-monster timing and AI cases")
        return
    for mutation in ("delay", "exclusive", "count", "target"):
        try:
            run(frozenset({mutation}))
        except AssertionError:
            print(f"mutation {mutation}: caught")
        else:
            raise SystemExit(f"mutation {mutation}: NOT caught")
    print("mutation check ok (4 ways)")


if __name__ == "__main__":
    main()
