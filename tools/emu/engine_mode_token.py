# SPDX-License-Identifier: GPL-3.0-or-later
"""Check GfxUtil base-token selection FUN_004daa00 in stock game.exe 1.14d.

Run: uv run python engine_mode_token.py [--mutation-check]
Player/object mode lookups are hooked; the synthetic MonStats table is read
by game.exe. No claim is made about the hooked lookup implementations.
"""
import argparse
import itertools

import _dungeon_rig as rig

MUT = set()


def suite(test):
    """Check each type, class boundary and null/non-null lookup result."""
    choice = {}
    test._callee("player", 0x65b480, 2, (), lambda _emu: choice["record"])
    test._callee("object", 0x65b570, 2, (), lambda _emu: choice["record"])
    header, rows, record = test.arena + 0x1000, test.arena + 0x2000, test.slot(1)
    test.m.w32(0x744304, header)
    cases = itertools.cycle(itertools.product((0, 1, 2, 3), (0xffffffff, 0, 1, 2, 3), (0, 1)))

    def put(img, offset, value):
        """Store one dword at an arena offset."""
        img[offset:offset + 4] = value.to_bytes(4, "little")

    def build(img, _rng):
        """Set table bounds and distinct row values for every case."""
        kind, index, present = next(cases)
        choice["record"] = record if present else 0
        put(img, 0x1a78, rows)
        put(img, 0x1a80, 2)
        put(img, 0x120, 0x12345678)
        put(img, 0x2010, 0x87654321)
        put(img, 0x21b8, 0xabcdef01)
        return {"edx": kind, "stack": [index]}

    def model(m, args):
        """Select the player, monster or object mode token."""
        kind, index = args["edx"], args["stack"][0]
        if kind in (0, 2):
            name = "player" if kind == 0 else "object"
            pointer = m.call(name, index, 1 if "argument" in MUT else 0)
            return m.r(pointer + 0x20) if pointer else 0
        if kind == 1 and 0 <= rig.s32(index) < (3 if "limit" in MUT else 2):
            return m.r(rows + index * 0x1a8 + (0x14 if "offset" in MUT else 0x10))
        return 0

    test.run("FUN_004daa00", 0x4daa00, build, model, 1)
    return test.cases


def main():
    """Run stock comparisons or intentional model defects."""
    rig.ROUNDS = 120
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("argument", "limit", "offset")}
    rig.run_suite(suite, mutations, "ENGINE mode token", args.mutation_check)


if __name__ == "__main__":
    main()
