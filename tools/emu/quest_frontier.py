# SPDX-License-Identifier: GPL-3.0-or-later
"""Check nine quest frontier accessors against stock game.exe 1.14d.

Run: python quest_frontier.py [--mutation-check]
The quest lookup is hooked and its arguments checked; the record and data
are synthetic. The comparison covers return values, call order, all arena
writes, and stack cleanup. It does not check the lookup's implementation.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
REC, DATA, GAME = 0x100, 0x200, 0x400


def put(img, offset, value, size=4):
    """Store a little-endian integer at an arena offset."""
    img[offset:offset + size] = value.to_bytes(size, "little")


def suite(rig):
    """Run accessors with absent and present quest records."""
    base = rig.arena
    rig.callee("lookup", 0x543640, 0, ("ecx", "edx"))
    pick = {}
    rig.custom["lookup"] = lambda emu: pick["record"]

    def build(img, rng):
        """Prepare a record and vary its data bytes and lookup result."""
        index = rig.cases % 24
        pick["record"] = 0 if index < 8 else base + REC
        put(img, REC + 0x18, base + DATA)
        put(img, DATA, rng.choice((0, 1, 0x80, 0xff)), 1)
        put(img, DATA + 0xc, rng.choice((0, 1, 0x80, 0xff)), 1)
        for offset in (6, 0xf, 0x14):
            put(img, DATA + offset, (0, 1, 2, 0x80)[index % 4], 1)
        put(img, DATA + 0x13, (0, 1, 2, 0x80)[(index // 2) % 4], 1)
        put(img, DATA + 0x10, (0, 1, 2, 0x80)[(index // 4) % 4], 1)
        put(img, DATA + 0x84, (0, 1, 0x80000000)[index % 3])
        put(img, DATA + 0x88, rng.randrange(1 << 32))
        put(img, GAME + 0x70, (index // 2) % 2)
        return {"ecx": base + GAME, "edx": base + 0x600}

    def has_flag(model, args):
        """Quest 0x12 has a nonzero first data byte."""
        record = model.call("lookup", args["ecx"], 0x13 if "quest" in MUTATIONS else 0x12)
        return int(bool(record and model.r(model.r(record + 0x18), 1)))

    def clear_flag(model, args):
        """Clear that byte when quest 0x12 exists."""
        record = model.call("lookup", args["ecx"], 0x12)
        if record:
            model.w(model.r(record + 0x18), 1 if "clear" in MUTATIONS else 0, 1)

    def absent_flag(model, args):
        """Quest 0x13 absent, or its byte at data +0xc zero."""
        record = model.call("lookup", args["ecx"], 0x13)
        if not record:
            return 0 if "missing" in MUTATIONS else 1
        return int(model.r(model.r(record + 0x18) + 0xc, 1) == 0)

    def build_getter(img, rng):
        """Set a data pointer and arbitrary dword at offset 0x88."""
        put(img, REC + 0x18, base + DATA)
        put(img, DATA + 0x88, rng.randrange(1 << 32))
        return {"ecx": base + REC}

    def getter(model, args):
        """Read the dword at record's data pointer plus 0x88."""
        return model.r(model.r(args["ecx"] + 0x18) + (0x84 if "offset" in MUTATIONS else 0x88))

    def summit(model, args):
        """Quest 0x21 exists and its data dword +0x84 is zero."""
        record = model.call("lookup", args["ecx"], 0x21)
        return int(bool(record) and model.r(model.r(record + 0x18) + 0x84) == (1 if "summit" in MUTATIONS else 0))

    def seal_byte(model, args):
        """Return quest 0x17 data byte +0x13, or zero if absent."""
        record = model.call("lookup", args["ecx"], 0x17)
        return model.r(model.r(record + 0x18) + (0x14 if "seal-byte" in MUTATIONS else 0x13), 1) if record else 0

    def seal_gate(model, args):
        """Game +0x70 must be zero; absent quest or nonzero byte +0x14 allows it."""
        if model.r(args["ecx"] + 0x70) and "seal-gate" not in MUTATIONS:
            return 0
        record = model.call("lookup", args["ecx"], 0x17)
        return int(not record or model.r(model.r(record + 0x18) + 0x14, 1) != 0)

    def coords(model, args):
        """Copy two quest 0x11 data dwords when data byte +6 is nonzero."""
        record = model.call("lookup", args["ecx"], 0x11)
        if not record:
            return 0
        data = model.r(record + 0x18)
        if not model.r(data + 6, 1):
            return 0
        model.w(args["edx"], model.r(data + 0x10))
        model.w(args["edx"] + 4, model.r(data + (0x10 if "coords" in MUTATIONS else 0x14)))
        return 1

    def taint(model, args):
        """Set quest 0xb byte +0x10 once when byte +0xf equals one."""
        record = model.call("lookup", args["ecx"], 0xb)
        if not record:
            return 0
        data = model.r(record + 0x18)
        if model.r(data + 0xf, 1) != 1 or model.r(data + 0x10, 1):
            return 0
        model.w(data + 0x10, 2 if "taint" in MUTATIONS else 1, 1)
        return 1

    rig.run("FUN_005bad20", 0x5bad20, build, has_flag, 0)
    rig.run("FUN_005bad40", 0x5bad40, build, clear_flag, 0, ret=False)
    rig.run("FUN_005bbf80", 0x5bbf80, build, absent_flag, 0, mask=0xff)
    rig.run("FUN_005901e0", 0x5901e0, build_getter, getter, 0)
    rig.run("FUN_0058a9f0", 0x58a9f0, build, summit, 0)
    rig.run("FUN_005b5210", 0x5b5210, build, seal_byte, 0)
    rig.run("FUN_005b5810", 0x5b5810, build, seal_gate, 0)
    rig.run("FUN_005b9ca0", 0x5b9ca0, build, coords, 0)
    rig.run("FUN_0059b6e0", 0x59b6e0, build, taint, 0)
    return rig.cases


def mutation(name):
    """Set or clear a named deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run the oracle or verify its checks catch deliberate defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in (
        "quest", "clear", "missing", "offset", "summit", "seal-byte", "seal-gate", "coords", "taint")},
                      "quest frontier", args.mutation_check)


if __name__ == "__main__":
    main()
