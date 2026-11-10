# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_00591cd0's quest item and party check against stock 1.14d.

Run: python quest_horadric_party.py [--mutation-check]
Item search, item check, party lookup and member iteration are hooked; the
iteration hook simulates the callback's write to quest data byte +0xa0.
"""
import argparse

import _dungeon_rig as rig_mod

MUTATIONS = set()
REC, DATA, GAME, PLAYER, ITEM = 0x100, 0x200, 0x400, 0x500, 0x600


def put(img, offset, value):
    """Store an unsigned little-endian dword at an arena offset."""
    img[offset:offset + 4] = value.to_bytes(4, "little")


def suite(rig):
    """Cover immediate item success, no-party and callback result cases."""
    base = rig.arena
    pick = {}
    for name, addr, nstack, regs in (("find", 0x554010, 1, ("ecx",)), ("check", 0x558110, 0, ("ecx", "edx")),
                                     ("party", 0x554630, 0, ("ecx",)), ("iterate", 0x540510, 2, ("ecx", "edx"))):
        rig.callee(name, addr, nstack, regs)
    rig.custom["find"] = lambda emu: base + ITEM
    rig.custom["check"] = lambda emu: pick["found"]
    rig.custom["party"] = lambda emu: pick["party"]

    def iterate(emu):
        """Simulate a party callback writing its result to quest data."""
        rig_mod.require(emu.read(base + DATA + 0xa0, 1) == b"\0", "quest flag not cleared before iteration")
        emu.mu.mem_write(base + DATA + 0xa0, bytes([pick["callback"]]))
        return 0

    rig.custom["iterate"] = iterate

    def build(img, rng):
        """Cover each branch and nonboolean callback and party values."""
        index = rig.cases
        pick["found"] = (0, 1, 0x80000000)[index % 3]
        pick["party"] = (0xffff, 0x1234ffff, 0, 3)[(index // 3) % 4]
        pick["callback"] = (0, 1, 0x80)[(index // 12) % 3]
        put(img, REC + 0x18, base + DATA)
        put(img, REC + 4, base + GAME)
        return {"regs": {"ebx": base + REC, "edi": base + PLAYER}}

    def model(m, args):
        """Test the item first; otherwise ask the party callback to set a flag."""
        rec, player = args["regs"]["ebx"], args["regs"]["edi"]
        item = m.call("find", player, 0x206d6468 if "item" not in MUTATIONS else 0x206d6469)
        if m.call("check", item, player):
            return 1
        party = m.call("party", player) & 0xffff
        if party == 0xffff:
            return 0
        data = m.r(rec + 0x18)
        m.w(data + 0xa0, 0, 1)
        m.call("iterate", m.r(rec + 4), party, 0x591ca1 if "callback" in MUTATIONS else 0x591ca0, 0)
        m.w(data + 0xa0, pick["callback"], 1)
        return int(m.r(data + 0xa0, 1) == 0 if "invert" in MUTATIONS else m.r(data + 0xa0, 1) != 0)

    for _ in range(2):
        rig.run("FUN_00591cd0", 0x591cd0, build, model, 0)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run stock comparisons or demonstrate detection of model defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("item", "callback", "invert")},
                      "quest item party", args.mutation_check)


if __name__ == "__main__":
    main()
