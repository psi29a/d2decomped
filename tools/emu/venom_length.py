# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_0057b7d0's poison-length choice on synthetic unit stats.

Run: .venv/bin/python venom_length.py [--mutation-check]

The stock function runs from its entry through the poison block. Combat
subsystems outside the claim are hooked; the stat getter is scripted. The
check observes record +0x2c: positive stat 101 overrides stat 59, otherwise
stat 59 is divided by stat 326 when that count is greater than one.
"""
import argparse
import random
import sys

import emu

FUNCTION = 0x57B7D0
MASK = 0xFFFFFFFF
mutation = None


def signed(value):
    """Interpret a 32-bit word as signed."""
    value &= MASK
    return value - 0x100000000 if value & 0x80000000 else value


def model(override, length, count):
    """The poison-length branch at 0x57bbbf..0x57bc0c."""
    if mutation == "ignore_override":
        override = 0
    if override > 0:
        return override
    if mutation != "never_divide" and count > 1:
        return int(length / count) if count else length
    return length


class Game:
    """A synthetic player, target and damage record around FUN_0057b7d0."""

    def __init__(self):
        self.machine = machine = emu.Emu()
        self.unit = machine.alloc(0x200)
        self.target = machine.alloc(0x200)
        self.record = machine.alloc(0x200)
        machine.w32(self.unit, 0)
        machine.w32(self.target, 1)
        self.stats = {}

        def stat(current):
            """Return the scripted layer-0 unit stat."""
            return self.stats.get(current.arg(1), 0) & MASK

        def poison_roll(_current):
            """Make the poison field nonzero; its roll is outside this claim."""
            return 256

        for address in (0x625480, 0x625500):
            machine.hook(address, stat, 3)
        machine.hook(0x57A880, poison_roll, 3)
        machine.stub(0, {
            0x472280: 1, 0x483360: 3, 0x535BC0: 0, 0x535D10: 0,
            0x535E20: 0, 0x57A8E0: 5, 0x57B420: 6, 0x57B660: 0,
            0x57B6A0: 2, 0x5A5560: 1, 0x5C0C30: 3, 0x6259B0: 1,
            0x627260: 4, 0x63E940: 1, 0x63EE90: 1, 0x645830: 4,
        })

    def run(self, override, length, count):
        """Return the poison length written to record +0x2c."""
        self.stats = {0x39: 256, 0x3A: 512, 0x3B: length,
                      0x65: override, 0x146: count}
        self.machine.mu.mem_write(self.record, bytes(0x200))
        self.machine.call(FUNCTION, self.target, self.record, 0, 0x80,
                          ecx=0, edx=self.unit, limit=300000)
        return signed(self.machine.r32(self.record + 0x2C))


def cases():
    """Deterministic boundaries plus random positive and signed inputs."""
    out = [(25, 100, 1), (25, 100, 4), (1, 0, 0), (0, 100, 1),
           (0, 100, 4), (-1, 100, 4), (0, -100, 4)]
    rng = random.Random(FUNCTION)
    for _ in range(600):
        out.append((rng.choice((-20, -1, 0, 1, 10, rng.randint(1, 500))),
                    rng.randint(-500, 1000), rng.choice((0, 1, 2, 4, 10))))
    return out


def check(defect=None):
    """Compare every case, returning the number of differences."""
    global mutation
    mutation = defect
    game = Game()
    bad = 0
    for case in cases():
        actual = game.run(*case)
        expected = model(*case)
        if actual != expected:
            bad += 1
            if defect is None and bad <= 10:
                print(f"{case}: game.exe {actual}, model {expected}", file=sys.stderr)
    mutation = None
    return bad


def main():
    """Run the comparison and optional deliberate model defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    bad = check()
    if bad:
        raise SystemExit(f"FAIL: {bad} poison-length cases differ")
    print(f"ok: {len(cases())} FUN_0057b7d0 poison-length cases")
    if options.mutation_check:
        for defect in ("ignore_override", "never_divide"):
            caught = check(defect)
            if not caught:
                raise SystemExit(f"mutation {defect} was not caught")
            print(f"caught: {defect} ({caught} cases)")
        print("mutation check ok")


if __name__ == "__main__":
    main()
