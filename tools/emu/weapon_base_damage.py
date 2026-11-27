# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a loaded weapon's base damage stats (FUN_0062cbe0) in stock game.exe 1.14d.

Runs FUN_0062cbe0's own block 0x62d495..0x62d6f9 (ESI the item, EDI its Items
record): low quality's x 3 >> 2 with floors 1 / 2, the throw pair only with a
max byte, then ethereal's x 3 / 2. The quality (FUN_00627e70), ethereal flag
(FUN_0062a8d0) and the stat list (FUN_00627260 set, FUN_006253b0 get) are
hooked. Each case is diffed against a line-for-line copy of d2d's
weapon_base_damage (components/rules/combat/combat.cpp), which
test_weapon_base_damage checks against the frozen cases.

Run: .venv/bin/python weapon_base_damage.py [--mutation-check] [check [cases [seed]]]
     .venv/bin/python weapon_base_damage.py inc --out ../../tests/weapon_base_damage_cases.inc
"""
import argparse
import hashlib
import random
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_ESP

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
START, END = 0x62D495, 0x62D6F9
QUALITY, SET_STAT, ETHEREAL, GET_STAT = 0x627E70, 0x627260, 0x62A8D0, 0x6253B0
STATS = (0x15, 0x16, 0x17, 0x18, 0x9F, 0xA0)
mutation = None


class Mismatch(Exception):
    """Stock execution disagreed with d2d's weapon_base_damage."""


def require(condition, message):
    """Raise Mismatch with `message` unless `condition` holds."""
    if not condition:
        raise Mismatch(message)


def model(min_byte, max_byte, low, ethereal, thrown):
    """d2d's weapon_base_damage: the (min, max) a pair of record bytes gives."""
    if thrown and max_byte == 0 and mutation != "throw_without_max":
        return 0, 0
    low_end = high_end = 0
    if low:
        if min_byte != 0 or thrown:
            low_end = max(min_byte * 3 >> 2, 2 if mutation == "min_floor_two" else 1)
        if max_byte != 0:
            high_end = max(max_byte * 3 >> 2, 2)
    else:
        low_end, high_end = min_byte, max_byte
    if ethereal and mutation != "no_ethereal":
        low_end, high_end = low_end * 3 // 2, high_end * 3 // 2
    if mutation == "low_by_percent" and low:
        low_end = max(min_byte * 75 // 100 + 1, 1) if low_end else 0
    return low_end, high_end


class Runner:
    """game.exe's block on one emulator, its callees hooked onto a dict of stats."""

    def __init__(self, exe):
        """Load game.exe, hook the block's four callees and make the item and record."""
        self.machine = emu.Emu(exe=exe)
        self.stats, self.quality, self.ethereal = {}, 2, 0
        machine = self.machine

        def set_stat(current):
            """FUN_00627260(item, stat, value, param): into the dict."""
            self.stats[current.arg(1)] = current.arg(2) & 0xFFFFFFFF
            return 0

        def get_stat(current):
            """FUN_006253b0(item, stat, param): from the dict, 0 unset."""
            return self.stats.get(current.arg(1), 0)

        machine.hook(SET_STAT, set_stat, 4)
        machine.hook(GET_STAT, get_stat, 3)
        machine.hook(QUALITY, lambda current: self.quality, 1)
        machine.hook(ETHEREAL, lambda current: self.ethereal, 1)
        self.record = machine.alloc(0x200)
        self.item = machine.alloc(0x100)

    def run(self, record_bytes, low, ethereal):
        """The block on bytes (+0xfe, +0xff, +0x102, +0x103, +0x100, +0x101); returns stats 0x15..0x18, 0x9f, 0xa0."""
        machine = self.machine
        for offset, value in zip((0xFE, 0xFF, 0x102, 0x103, 0x100, 0x101), record_bytes):
            machine.mu.mem_write(self.record + offset, bytes((value,)))
        self.stats, self.quality, self.ethereal = {}, 1 if low else 2, 1 if ethereal else 0
        machine.mu.reg_write(UC_X86_REG_ESP, emu.STACK + emu.STACK_SIZE - 0x1000)
        machine.mu.reg_write(UC_X86_REG_ESI, self.item)
        machine.mu.reg_write(UC_X86_REG_EDI, self.record)
        machine.mu.emu_start(START, END)
        return tuple(self.stats.get(stat, 0) for stat in STATS)


def cases(count, seed):
    """Random record bytes (min 1h, max 1h, min 2h, max 2h, min throw, max throw) with each quality and flag."""
    rng = random.Random(seed)
    picks = (0, 0, 1, 2, 3, 4, 5, 7, 10, 30, 63, 127, 128, 200, 255)
    for _ in range(count):
        bytes_ = tuple(rng.choice(picks) if rng.random() < 0.6 else rng.randrange(256) for _ in range(6))
        yield bytes_, rng.random() < 0.5, rng.random() < 0.5


def want(record_bytes, low, ethereal):
    """The model's six stats for one case."""
    min1, max1, min2, max2, min_throw, max_throw = record_bytes
    return (*model(min1, max1, low, ethereal, False), *model(min2, max2, low, ethereal, False),
            *model(min_throw, max_throw, low, ethereal, True))


def check(exe, count, seed):
    """Diff `count` random cases; returns the count."""
    runner = Runner(exe)
    for record_bytes, low, ethereal in cases(count, seed):
        got = runner.run(record_bytes, low, ethereal)
        expected = want(record_bytes, low, ethereal)
        require(got == expected, f"bytes {record_bytes} low {low} ethereal {ethereal}: game.exe {got}, d2d {expected}")
    return count


def write_inc(exe, out, count, seed):
    """Freeze game.exe's answers as tests/weapon_base_damage_cases.inc, after a passing check."""
    runner = Runner(exe)
    lines = ["// Stock game.exe 1.14d, FUN_0062cbe0 0x62d495..0x62d6f9; generated by tools/emu/weapon_base_damage.py inc.",
             "// bytes +0xfe, +0xff, +0x102, +0x103, +0x100, +0x101, low, ethereal; stats 0x15, 0x16, 0x17, 0x18, 0x9f, 0xa0"]
    for record_bytes, low, ethereal in cases(count, seed):
        got = runner.run(record_bytes, low, ethereal)
        lines.append("    { " + ", ".join(str(v) for v in record_bytes) + f", {int(low)}, {int(ethereal)}, "
                     + ", ".join(str(v) for v in got) + " },")
    Path(out).write_text("\n".join(lines) + "\n")


def main():
    """Check the block against the model, write the fixture, or (asked) show each planted defect is caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", default="check")
    parser.add_argument("cases", nargs="?", type=int, default=20000)
    parser.add_argument("seed", nargs="?", type=int, default=1)
    parser.add_argument("--exe")
    parser.add_argument("--out")
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    exe = options.exe or str(Path(emu.data_path()) / "bin" / "game.exe")
    require(hashlib.sha256(Path(exe).read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    if options.mode == "inc":
        check(exe, options.cases, options.seed)
        write_inc(exe, options.out, 400, 7)
        print(f"wrote {options.out}")
        return
    print(f"ok: {check(exe, options.cases, options.seed)} FUN_0062cbe0 base damage cases")
    if options.mutation_check:
        global mutation
        for defect in ("throw_without_max", "min_floor_two", "no_ethereal", "low_by_percent"):
            mutation = defect
            try:
                check(exe, options.cases, options.seed)
            except Mismatch:
                print(f"mutation {defect}: failed as required")
            else:
                raise RuntimeError(f"mutation {defect} was not detected")
        mutation = None


if __name__ == "__main__":
    main()
