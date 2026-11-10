# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the Gfx.cpp mode loaders and preload walkers in stock 1.14d.

Run: uv run python engine_cof_preload.py [--mutation-check]
FUN_0046f9e0 (monster mode load, EAX mode, ECX unit), FUN_0046fa80 (load by
unit type, then the node), FUN_0046fda0 (a monster mode's slot in the table at 0x7a80fc),
FUN_0046feb0 (every mode the monster has, through FUN_0046fda0) and FUN_0046ff50 (a player's mode slot at 0x7a7490 / 0x7a74e0,
through FUN_0046fb60 and FUN_005ff300). The transform, mode getters, the
MonStats2 mode check, list clear, flag set, the loaders, the node lookup, the
preload workers and the queue call are hooked and return this case's synthetic
values. The claim: the mode-0xe replacement, the 0xc clear-first path, the
dispatch by unit type and its lookups, the table walks and bit tests, the
slot / pending-flag state machine and the bytes popped.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32, s32

MUT = set()
FILE = 0x6D6C1C
SLOTS, PENDING = 0x7A7490, 0x7A74E0


def suite(test):
    """Run the four functions."""
    sel = {}
    unit = test.slot(0)

    def transform(emu):
        """Remember the locals, then write this case's type / class / mode."""
        sel["seen"] = (emu.r32(emu.arg(1)), emu.r32(emu.arg(2)), emu.r32(emu.arg(3)))
        sel["site"] = (emu.arg(4), emu.arg(5))
        emu.w32(emu.arg(1), sel["type"])
        emu.w32(emu.arg(2), sel["cls"])
        emu.w32(emu.arg(3), sel["mode"])
        return 0

    def seen_ok(m, line):
        """Check the transform call site and the locals it was handed."""
        want = (6 if not unit else m.r(unit), m.r(unit + 4), 0x55)
        rig.require(sel["site"] == (FILE, line), f"transform site {sel['site']}")
        rig.require(sel["seen"] == want, f"transform locals {sel['seen']} vs {want}")

    test._callee("mode", 0x621190, 1, (), lambda _emu: 0x55)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)

    def rnd_case(rng):
        """Common random values."""
        sel.update(type=rng.randrange(4), cls=rng.randrange(1 << 32), mode=rng.choice((0xC, 0xE, 0x1, rng.randrange(0x20))))

    # FUN_0046f9e0 ---------------------------------------------------------
    test._callee("monflag", 0x46C140, 0, ("ecx", "edx"), lambda _emu: sel["monflag"])
    test.callee("clear", 0x46EC80, 0, ("ecx",))
    test.callee("setflags", 0x46F020, 0, ("ecx", "edx"))
    test.callee("monload", 0x46E8D0, 0, ("esi", "edi"))

    def build_mon(img, rng):
        """EAX mode 0xc / 0xe / other, class lookup pass or fail."""
        rnd_case(rng)
        sel["monflag"] = rng.randrange(2)
        return {"ecx": unit, "regs": {"eax": rng.choice((0xC, 0xE, 0xC, rng.randrange(0x20)))}}

    def mon(m, args):
        """Replace 0xe by the unit's mode; mode 0xc clears the list and flags first."""
        mode = args["regs"]["eax"]
        m.call("mode", unit)
        m.call("transform", unit)
        seen_ok(m, 0x498)
        edi = sel["mode"] if mode == 0xE else mode
        if not m.call("monflag", sel["cls"], edi):
            return
        if edi == 0xC:
            m.call("clear", unit)
            m.call("setflags", unit, 0)
        m.call("monload", unit, edi if "mode" not in MUT else mode)
    test.run("FUN_0046f9e0", 0x46F9E0, build_mon, mon, 0, ret=False)

    # FUN_0046fa80 ---------------------------------------------------------
    test.callee("mlist", 0x46F9E0, 0, ("eax", "ecx"))
    test.callee("objload", 0x46E980, 0, ("esi", "edi"))
    test._callee("loadcof", 0x46E740, 2, ("eax",), lambda emu: sel.__setitem__("slot", emu.arg(0)) or sel["loaded"], nlog=2)
    test.callee("setflag", 0x4DB1C0, 1, ("ecx", "edx"))
    test.callee("node", 0x46E5A0, 1, ("ecx", "edx"))

    def build_load(img, rng):
        """Unit type 0 / 1 / 2 / other, mode token -1 or a value."""
        rnd_case(rng)
        sel["loaded"] = rng.randrange(2)
        return {"ecx": unit, "edx": rng.randrange(1 << 32), "stack": [rng.choice((0xFFFFFFFF, rng.randrange(1 << 32)))]}

    def load(m, args):
        """Load by unit type, then return the node for (mode, token)."""
        mode, tok = args["edx"], args["stack"][0]
        m.call("mode", unit)
        m.call("transform", unit)
        seen_ok(m, 0x502)
        edi = sel["mode"] if tok == M32 else tok
        kind = sel["type"]
        if kind == 1:
            m.call("mlist", edi, unit)
            return m.call("node", unit, mode, edi)
        if kind == 0:
            if m.call("loadcof", unit, sel["slot"], edi):
                m.call("setflag", unit, edi, 1)
            return m.call("node", unit, mode, 0xFFFFFFFF)
        if kind == 2:
            m.call("objload", unit, edi)
            return m.call("node", unit, mode, 0xFFFFFFFF)
        return 0
    test.run("FUN_0046fa80", 0x46FA80, build_load, load, 1)

    # FUN_0046fda0: the monster mode once, slots in the table at 0x7a80fc ----
    test.callee("blocking", 0x46FB60, 1, ("eax",))
    test._callee("queue", 0x5FF300, 3, (), lambda _emu: sel["queue"])
    rig_blocking = [0]
    test.custom["blocking"] = lambda _emu: rig_blocking[0]
    table = test.arena + 0x1800
    test.m.w32(0x744304, test.arena + 0x1000)

    def build_once(img, rng):
        """A monster class / mode slot (class -1 reaches before the table, still inside the arena)."""
        rnd_case(rng)
        sel.update(cls=rng.choice((0xFFFFFFFF, 0, 1, 2, 3, 4, 5)), type=rng.randrange(4), queue=rng.randrange(2))
        count = rng.choice((0, 3, 6))
        img[0x1000 + 0xA80:0x1000 + 0xA84] = count.to_bytes(4, "little")
        test.m.w32(0x7A80FC, table)
        sel["blocking"] = rig_blocking[0] = rng.choice((1, 1, 0, 0xFFFFFFFF))
        sel["count"] = count
        sel["once_mode"] = rng.randrange(20)
        sel["at"] = (sel["once_mode"] + 40 * s32(sel["cls"])) * 4
        img[table - test.arena + sel["at"]:table - test.arena + sel["at"] + 4] = rng.choice((0, 0, 1)).to_bytes(4, "little")
        img[table - test.arena + sel["at"] + 0x50:table - test.arena + sel["at"] + 0x54] = rng.choice((0, 0, 1)).to_bytes(4, "little")
        return {"regs": {"eax": sel["once_mode"], "esi": unit}, "stack": [rng.randrange(1 << 32)]}

    def once(m, args):
        """Slot done: stop; fresh: preload, mark pending when 1; pending: queue, done when it returns 0."""
        m.call("mode", unit)
        m.call("transform", unit)
        seen_ok(m, 0x70A)
        if s32(sel["cls"]) >= sel["count"]:
            return
        at = table + sel["at"]
        if m.r(at):
            return
        if not m.r(at + 0x50):
            if m.call("blocking", unit, args["stack"][0]) == 1:
                m.w(at + 0x50, 1)
            else:
                m.w(at + 0x50, 0)
                m.w(at, 0)
        elif not m.call("queue", sel["type"], sel["once_mode"], sel["cls"]):
            m.w(at, 0 if "once" in MUT else 1)
    test.run("FUN_0046fda0", 0x46FDA0, build_once, once, 1, ret=False)

    # FUN_0046feb0 ---------------------------------------------------------
    hdr, rows1, rows2 = test.arena + 0x1000, test.arena + 0x2000, test.arena + 0x2800
    test.m.w32(0x744304, hdr)
    test._callee("class", 0x46DAC0, 0, ("ecx",), lambda _emu: sel["cls"])
    test.callee("worker", 0x46FDA0, 1, ("eax", "ebx", "esi"))

    def build_walk(img, rng):
        """MonStats / MonStats2 rows in the header, a mode list at 0x712ac4."""
        def put(off, value, size=4):
            """Store an arena value."""
            img[off:off + size] = (value & ((1 << 8 * size) - 1)).to_bytes(size, "little")
        sel["cls"] = rng.choice((0xFFFFFFFF, 0, 1, 2, 3, 4, 5))
        put(0x1000 + 0xA80, 4)
        put(0x1000 + 0xA78, rows1)
        put(0x1000 + 0xA98, 3)
        put(0x1000 + 0xA90, rows2)
        for i in range(4):
            put(0x2000 + i * 0x1A8 + 0x18, rng.choice((0, 1, 2, 0xFFFF, 0x8000, 3)), 2)
        count = rng.randrange(0, 6)
        test.m.w32(0x712B00, count)
        for i in range(5):
            test.m.w32(0x712AC4 + 4 * i, rng.randrange(0x40))
        return {"regs": {"eax": unit}, "stack": [rng.randrange(1 << 32)]}

    def walk(m, args):
        """Every mode the monster's MonStats2 row has goes to the worker."""
        owner = args["regs"]["eax"]
        cls = m.call("class", owner)
        if s32(cls) < 0 or s32(cls) >= m.r(hdr + 0xA80):
            return
        index = m.r(m.r(hdr + 0xA78) + cls * 0x1A8 + 0x18, 2)
        index = index - 0x10000 if index & 0x8000 else index
        if index < 0 or index >= m.r(hdr + 0xA98):
            return
        row = m.r(hdr + 0xA90) + index * 0x134
        for i in range(test.m.r32(0x712B00)):
            mode = test.m.r32(0x712AC4 + 4 * i)
            if m.r(row + (mode >> (2 if "walk" in MUT else 3)) + 0xF0, 1) & (1 << (mode & 7)):
                m.call("worker", mode, row, owner, args["stack"][0])
    test.run("FUN_0046feb0", 0x46FEB0, build_walk, walk, 1, ret=False)

    # FUN_0046ff50 ---------------------------------------------------------
    def build_slot(img, rng):
        """A player class / mode slot with random state."""
        rnd_case(rng)
        sel.update(cls=rng.randrange(7), mode=rng.randrange(20), type=rng.randrange(3), queue=rng.randrange(2))
        sel["offset"] = (sel["mode"] + 40 * sel["cls"]) * 4
        sel["done"] = rng.choice((0, 0, 1))
        sel["pend"] = rng.choice((0, 0, 1))
        test.m.w32(SLOTS + sel["offset"], sel["done"])
        test.m.w32(PENDING + sel["offset"], sel["pend"])
        sel["blocking"] = rng.choice((1, 1, 0, 0xFFFFFFFF))
        rig_blocking[0] = sel["blocking"]
        return {"regs": {"eax": sel["mode"], "esi": unit}, "stack": [rng.randrange(1 << 32)]}

    def slot(m, args):
        """Done slots stop; pending ones queue; fresh ones preload and mark pending on success."""
        m.call("mode", unit)
        m.call("transform", unit)
        seen_ok(m, 0x751)
        at = sel["offset"]
        done, pend = None, None
        if sel["done"]:
            done, pend = 1, sel["pend"]
        elif not sel["pend"]:
            if m.call("blocking", unit, args["stack"][0]) == 1:
                done, pend = 0, 1
            else:
                done, pend = 0, 0
        else:
            if not m.call("queue", sel["type"], args["regs"]["eax"], sel["cls"]):
                done, pend = 1, 1
            else:
                done, pend = 0, 1
        rig.require(test.m.r32(SLOTS + at) == done, f"slot {test.m.r32(SLOTS + at)} vs {done}")
        rig.require(test.m.r32(PENDING + at) == (0 if "pend" in MUT else pend), "pending flag")
    test.run("FUN_0046ff50", 0x46FF50, build_slot, slot, 1, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 400
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("mode", "pend", "walk", "once")}
    rig.run_suite(suite, mutations, "ENGINE COF preload", args.mutation_check)


if __name__ == "__main__":
    main()
