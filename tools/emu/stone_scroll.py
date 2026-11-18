# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the deciphered scroll's symbols at FUN_0049fba0(1).

    .venv/bin/python stone_scroll.py [--exe PATH] [--mutation-check]

The stock draw runs for real over a scripted clock (GetTickCount), order
(0x7bf098) and step count (0x7bf243 / 0x7bf247). The scroll itself
(FUN_0049fa10), the font, the cel draw (FUN_004f6480) and the sound
(FUN_004b9a00) are hooked and logged; the cel files are already loaded
(0x7bf270, 0x7bf26c). The model is components/rules/stone_scroll.hpp's
StoneScrollClock and stone_symbol, as tests/test_stone_scroll.cpp checks.
No MPQ data is read.
"""
import argparse
import sys

from unicorn.x86_const import UC_X86_REG_ECX

import emu

ENTRY = 0x49FBA0
CLIENT_PLAYER = 0x463DD0
SCROLL = 0x49FA10
SET_FONT = 0x502EF0
CEL = 0x4F6480
SOUND = 0x4B9A00
SYMBOLS, GLOWS = 0x7BF270, 0x7BF26C
SINCE, STEPS, ORDER = 0x7BF243, 0x7BF247, 0x7BF098
PANEL_X, PANEL_Y = 0x7A2858, 0x7A285C
SYMBOL_SPOT = ((242, 104), (255, 222), (148, 310), (47, 222), (75, 104))
GLOW_SPOT = ((303, 161), (322, 242), (254, 290), (190, 238), (211, 162))
DELAY = (0, 12, 24, 36, 48)
MUTATIONS = ("no-step-gate", "delay-by-stone", "spot-by-place", "frame-by-stone", "fade-off-by-one", "glow-past-20", "no-sound")
MASK = 0xFFFFFFFF


class ScrollOracle:
    """FUN_0049fba0 with its draw boundaries scripted."""

    def __init__(self, exe=None):
        """Load game.exe and hook what FUN_0049fba0 calls."""
        self.machine = machine = emu.Emu(exe)
        self.player = machine.alloc(0x100)
        machine.hook(CLIENT_PLAYER, lambda _current: self.player, 0)
        machine.hook(SCROLL, lambda _current: self.log.append(("scroll",)) or 0, 0)
        machine.hook(SET_FONT, lambda _current: 0, 0)
        machine.hook(CEL, self.cel, 6)
        machine.hook(SOUND, lambda current: self.log.append(("sound", current.mu.reg_read(UC_X86_REG_ECX))) or 0, 3)
        for address, (name, _handler) in list(machine.slots.items()):
            if name == "GetTickCount":
                machine.slots[address] = (name, lambda _current: (self.now, 0))
        machine.w32(SYMBOLS, 0x5100)
        machine.w32(GLOWS, 0x5200)
        machine.w32(PANEL_X, 0x50)
        machine.w32(PANEL_Y, -0x3C & MASK)

    def cel(self, current):
        """FUN_004f6480(cel, x, y, -1, mode, 0): which file, its frame, where, the mode."""
        record = current.arg(0)
        signed = [value - (1 << 32) if value & 0x80000000 else value for value in (current.arg(1), current.arg(2))]
        self.log.append(("cel", current.r32(record + 0x34), current.r32(record), *signed, current.arg(4)))
        return 0

    def run(self, order, since, steps, now):
        """One draw: the order, the clock's state, the time; what it drew and the clock after."""
        machine = self.machine
        for place, stone in enumerate(order):
            machine.mu.mem_write(ORDER + 2 * place, stone.to_bytes(2, "little"))
        machine.mu.mem_write(SINCE, since.to_bytes(4, "little"))
        machine.mu.mem_write(STEPS, steps.to_bytes(4, "little"))
        self.now, self.log = now, []
        machine.call(ENTRY, 1, limit=20000)
        after = int.from_bytes(machine.mu.mem_read(SINCE, 4), "little"), int.from_bytes(machine.mu.mem_read(STEPS, 4), "little")
        return self.log, after


def model(order, since, steps, now, mutation=None):
    """StoneScrollClock::draw, then stone_symbol for each place."""
    log = [("scroll",)]
    if since == 0:
        since, steps = now, 0
    elif now > since + 0x32 or mutation == "no-step-gate":
        since, steps = now, steps + 1
    for place, stone in enumerate(order):
        delay = DELAY[stone] if mutation == "delay-by-stone" else DELAY[place]
        if steps <= delay:
            continue
        age = steps - delay
        if age == 1 and mutation != "no-sound":
            log.append(("sound", 0xA6F))
        fade = age + 1 if mutation == "fade-off-by-one" else age
        mode = 0 if fade < 5 else 1 if fade < 10 else 2 if fade < 15 else 5
        spot = SYMBOL_SPOT[place] if mutation == "spot-by-place" else SYMBOL_SPOT[stone]
        frame = stone if mutation == "frame-by-stone" else place
        log.append(("cel", 0x5100, frame, spot[0] + 0x50, spot[1] + 0x3C, mode))
        glow = age if age <= 20 or mutation == "glow-past-20" else 0
        log.append(("cel", 0x5200, glow, GLOW_SPOT[stone][0] + 0x50, GLOW_SPOT[stone][1] + 0x3C, 3))
    return log, (since, steps)


def cases():
    """The clock's start, its gate, and each symbol's fade, glow and sound."""
    order = (3, 0, 4, 1, 2)
    rows = [(order, 0, 0, 1000), (order, 1000, 0, 1050), (order, 1000, 0, 1051)]
    rows += [(order, 1000, steps, 5000) for steps in (4, 9, 11, 12, 14, 19, 20, 23, 25, 36, 48, 60, 80)]
    rows += [((4, 3, 2, 1, 0), 1000, 50, 5000), ((1, 2, 3, 4, 0), 1000, 30, 1010)]
    return rows


def check(oracle, mutation=None):
    """Diff the model against game.exe over every case."""
    bad = []
    for case in cases():
        got, want = oracle.run(*case), model(*case, mutation)
        if got != want:
            bad.append((case, got, want))
    return bad


def main():
    """Parse the arguments and run the check or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    oracle = ScrollOracle(args.exe)
    bad = check(oracle)
    if bad:
        for case, got, want in bad:
            print(f"case {case}:\n  game  {got}\n  model {want}")
        sys.exit(f"{len(bad)} scroll cases differ")
    print(f"ok: {len(cases())} FUN_0049fba0 scroll cases")
    if args.mutation_check:
        for name in MUTATIONS:
            if not check(oracle, name):
                sys.exit(f"mutation {name} was not caught")
            print(f"mutation {name}: failed as required")
        print(f"mutation check ok ({len(MUTATIONS)} ways)")


if __name__ == "__main__":
    main()
