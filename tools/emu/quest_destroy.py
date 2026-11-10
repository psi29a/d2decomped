# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_00543800 quest-state cleanup against stock game.exe 1.14d.

Run: python quest_destroy.py [--mutation-check]
The allocator and quest-buffer free routines are hooked and logged; the
synthetic lists are acyclic and allocator internals are outside this claim.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
GAME, HEADER, RECORDS, EXTRA, CHILD, POOL, BUFFER = 0, 0x1200, 0x1400, 0x2000, 0x2600, 0x3000, 0x3100
FILE = 0x6e0be4


def put(img, offset, value):
    """Store an unsigned dword at an arena offset."""
    img[offset:offset + 4] = value.to_bytes(4, "little")


def suite(rig):
    """Exercise missing state, multiple records and a second linked list."""
    base = rig.arena
    rig.callee("buffer", 0x65c490, 2, ())

    def build(img, rng):
        """Construct acyclic lists with optional record data blocks."""
        index = rig.cases % 24
        length, extra = index % 4, (index // 4) % 3
        put(img, GAME + 0x1c, base + POOL)
        put(img, GAME + 0x10f4, 0 if index >= 20 else base + HEADER)
        put(img, HEADER, base + RECORDS if length else 0)
        put(img, HEADER + 0xc, base + BUFFER)
        put(img, HEADER + 0x10, base + EXTRA if extra else 0)
        for entry in range(length):
            offset = RECORDS + entry * 0x100
            put(img, offset + 0x18, base + CHILD + entry * 0x10 if (entry + index) % 2 else 0)
            put(img, offset + 0xf4, base + offset + 0x100 if entry + 1 < length else 0)
        for entry in range(extra):
            offset = EXTRA + entry * 0x20
            put(img, offset + 0x10, base + offset + 0x20 if entry + 1 < extra else 0)
        return {"ecx": base + GAME}

    def model(m, args):
        """Free record data, records, extra list, quest buffer, then header."""
        game = args["ecx"]
        header, pool = m.r(game + 0x10f4), m.r(game + 0x1c)
        if not header:
            return
        record = m.r(header)
        while record:
            child, next_record = m.r(record + 0x18), m.r(record + 0xf4)
            if child and "child" not in MUTATIONS:
                m.free(pool, child, 0x1df, FILE)
            m.free(pool, record, 0x1e1, FILE)
            record = next_record
        extra = m.r(header + 0x10)
        while extra:
            next_extra = m.r(extra + 0x10)
            m.free(pool, extra, 0x1e9 if "line" in MUTATIONS else 0x1e8, FILE)
            extra = next_extra
        m.call("buffer", pool, m.r(header + 0xc))
        if "header" not in MUTATIONS:
            m.free(pool, header, 0x1ec, FILE)

    rig.run("FUN_00543800", 0x543800, build, model, 0, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run the stock comparisons or demonstrate model defect detection."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("child", "line", "header")},
                      "quest state cleanup", args.mutation_check)


if __name__ == "__main__":
    main()
