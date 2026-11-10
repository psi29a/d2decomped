# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the kind-8 overlay frame helpers FUN_0046def0 and FUN_0046df40 in stock 1.14d.

Run: uv run python engine_overlay_frames.py [--mutation-check]
FUN_0046def0 (EAX graphics record, ECX overlay node): looks through the
record's overlay list (+0x2c, next at +0xa4) for another kind-8 node with the
same +0x38 (copies its +0x3c and +8 to the node and stops) or with +0x3c
set (clears the node's +0x3c and stops); with no other node it sets +0x3c to
1 and clears +8. FUN_0046df40 (EAX node, ECX unit, stack: graphics record)
turns the unit's state for the node's +0x38 off and on around a FUN_0063a250
test (when the state was set it is switched on twice when that test is zero),
clears the kind-8 nodes of that value (+0x3c = 0, +8 = -1), then switches the
nodes with the next larger value (or the smallest when none is larger; zero
counts as unset) on (+0x3c = 1, +8 = 0). The state test / set and the
FUN_0063a250 test are hooked. The claim: the walk order, the min / next-larger
search with signed compares, the double state set, the writes and the bytes popped.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import s32

MUT = set()


def suite(test):
    """Run both helpers over random kind-8 overlay lists."""
    sel = {}
    unit, gfx = test.slot(0), test.slot(1)
    slots = (2, 3, 4, 5)
    extra = test.slot(6)
    test._callee("hasstate", 0x639DF0, 2, (), lambda _e: sel["has"])
    test.callee("setstate", 0x639DB0, 3, ())
    test._callee("prop", 0x63A250, 1, (), lambda _e: sel["prop"])

    def nodes(img, rng):
        """A random-length overlay chain from the record's +0x2c; returns the node addresses."""
        count = rng.randrange(len(slots) + 1)
        used = [test.slot(i) for i in slots[:count]]
        img[gfx - test.arena + 0x2C:gfx - test.arena + 0x30] = (used[0] if used else 0).to_bytes(4, "little")
        for i, n in enumerate(used):
            off = n - test.arena
            img[off + 0xA4:off + 0xA8] = (used[i + 1] if i + 1 < len(used) else 0).to_bytes(4, "little")
            img[off:off + 4] = rng.choice((8, 8, 8, 1)).to_bytes(4, "little")
            img[off + 0x38:off + 0x3C] = rng.choice((0, 1, 2, 3, 0xFFFFFFFF, 0x80000000)).to_bytes(4, "little")
            img[off + 0x3C:off + 0x40] = rng.choice((0, 0, 1, 5)).to_bytes(4, "little")
        return used

    def build_pair(img, rng):
        """FUN_0046def0: the node is in the chain or not."""
        used = nodes(img, rng)
        node = rng.choice(used) if used and rng.randrange(2) else extra
        off = extra - test.arena
        img[off + 0x38:off + 0x3C] = rng.choice((0, 1, 2, 3, 0xFFFFFFFF)).to_bytes(4, "little")
        return {"regs": {"eax": gfx}, "ecx": node}

    def pair(m, args):
        """Copy a matching kind-8 node's state, or note that this node is the only one."""
        node = args["ecx"]
        cur = m.r(gfx + 0x2C)
        while cur:
            if m.r(cur) == 8 and cur != node:
                if m.r(cur + 0x38) == m.r(node + 0x38):
                    m.w(node + 0x3C, m.r(cur + 0x3C))
                    m.w(node + 8, m.r(cur + 8))
                    return
                if m.r(cur + 0x3C) != 0 and "second" not in MUT:
                    m.w(node + 0x3C, 0)
                    return
            cur = m.r(cur + 0xA4)
        m.w(node + 0x3C, 1)
        m.w(node + 8, 0)
    test.run("FUN_0046def0", 0x46DEF0, build_pair, pair, 0, ret=False)

    def build_cycle(img, rng):
        """FUN_0046df40: a node (maybe one of the list) and the unit's state flags."""
        used = nodes(img, rng)
        node = rng.choice(used) if used and rng.randrange(2) else extra
        off = extra - test.arena
        img[off + 0x38:off + 0x3C] = rng.choice((0, 1, 2, 3, 0xFFFFFFFF)).to_bytes(4, "little")
        sel.update(has=rng.choice((0, 0, 1, 0x100)), prop=rng.randrange(3))
        return {"regs": {"eax": node}, "ecx": unit, "stack": [gfx]}

    def cycle(m, args):
        """The state off / on, the clear of the node's value, the switch to the next one."""
        node = args["regs"]["eax"]
        v = m.r(node + 0x38)
        has = m.call("hasstate", unit, v)
        if has:
            m.call("setstate", unit, v, 0)
        if not m.call("prop", unit):
            m.w(node + 8, 0)
            if has and "twice" not in MUT:
                m.call("setstate", unit, v, 1)
        if has:
            m.call("setstate", unit, v, 1)
        head = m.r(gfx + 0x2C)
        cur = head
        while cur:
            if m.r(cur + 0x38) == v and m.r(cur) == 8:
                m.w(cur + 0x3C, 0)
                m.w(cur + 8, 0xFFFFFFFF)
            cur = m.r(cur + 0xA4)
        nxt = low = 0
        cur = head
        while cur:
            if m.r(cur) == 8:
                value = m.r(cur + 0x38)
                if not nxt or s32(value) < s32(nxt):
                    if s32(value) > s32(v):
                        nxt = value
                if not low or s32(value) < s32(low):
                    low = value
            cur = m.r(cur + 0xA4)
        nxt = nxt or (0 if "wrap" in MUT else low)
        cur = head
        while cur:
            if m.r(cur + 0x38) == nxt and m.r(cur) == 8:
                m.w(cur + 0x3C, 1)
                m.w(cur + 8, 0)
            cur = m.r(cur + 0xA4)
    test.run("FUN_0046df40", 0x46DF40, build_cycle, cycle, 1, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 800
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("second", "twice", "wrap")}
    rig.run_suite(suite, mutations, "ENGINE overlay frames", args.mutation_check)


if __name__ == "__main__":
    main()
