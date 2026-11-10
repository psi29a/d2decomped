# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the Gfx.cpp list / cache frees and FUN_0046e150 in stock 1.14d.

Run: uv run python engine_gfx_free.py [--mutation-check]
FUN_0046eb70 frees a unit's overlay list, FUN_0046ec10 the whole graphics
record, FUN_0046f370 a COF cache's loaded entries, FUN_0046f3f0 / FUN_0046f440
the monster / player cache, FUN_0046f310 loads a bundle path into the player
cache, FUN_0046e150 sets the value of a unit's overlays of one row.
The pool free, the graphics-record getter (FUN_00463e10), the overlay sound
free (FUN_004743d0), FUN_0046eb70 (when FUN_0046ec10 runs) and the bundle
loader FUN_0046ddf0 are hooked; the cache getters run the real FUN_0046f370.
The claim: which nodes are freed, in what order, with which line numbers,
the byte counter at 0x7a8918, the cache zeroing, the bundle path, the fatal
lines (0x814, 0x1fe, 0x205).
"""
import argparse

import _dungeon_rig as rig

MUT = set()
FILE = 0x6D6C1C
CACHES = {"mon": (0x7A8100, 0xE50), "ply": (0x7A78F0, 0xE61)}
COUNTER = 0x7A8918


def chain(test, img, rng, head_off, slots, next_off, size=0x100):
    """Link a random-length run of `slots` from the dword at `head_off`; returns the nodes used."""
    count = rng.randrange(len(slots) + 1)
    used = slots[:count]
    ptrs = [test.slot(i, size) for i in used]
    img[head_off:head_off + 4] = (ptrs[0] if ptrs else 0).to_bytes(4, "little")
    for i, ptr in enumerate(ptrs):
        nxt = ptrs[i + 1] if i + 1 < len(ptrs) else 0
        off = ptr - test.arena + next_off
        img[off:off + 4] = nxt.to_bytes(4, "little")
    return ptrs


def entries(m, cache, rd, counter):
    """Model of FUN_0046f370 over the buckets at cache+0xc; returns the new byte counter."""
    for i in range(0x200):
        node = rd(cache + 0xC + 4 * i)
        while node:
            nxt = m.r(node)
            if m.r(node + 4) & (2 if "flag" in MUT else 1):
                counter = (counter + (-0x1C - m.r(node + 8))) & rig.M32
                m.call("pool_free", m.r(node + 0x14), FILE, 0xE3C, 0)
                m.call("pool_free", node, FILE, 0xE3D, 0)
            node = nxt
    return counter


def suite(test):
    """Run each function over random lists / caches."""
    sel = {}
    test.callee("pool_free", 0x40B3C0, 2, ("ecx", "edx"))
    test._callee("gfxinfo", 0x463E10, 0, ("ecx",), lambda _emu: sel["gfx"])
    test.callee("sound", 0x4743D0, 0, ("ecx",))
    unit, gfx = test.slot(0), test.slot(1)
    slots = (2, 3, 4, 5)

    def overlays_build(img, rng):
        """FUN_0046eb70: an overlay chain, some with a sound handle."""
        chain(test, img, rng, gfx - test.arena + 0x2C, slots, 0xA4)
        for i in slots:
            if rng.randrange(2):
                o = test.slot(i) - test.arena + 0x40
                img[o:o + 4] = bytes(4)
        return {"regs": {"eax": gfx}}

    def overlays_model(m, args):
        """Free each overlay: sound first when +0x40 is set, then the node (line 0x690)."""
        node = m.r(gfx + 0x2C)
        while node:
            nxt = m.r(node + 0xA4)
            if m.r(node + 0x40) and "sound" not in MUT:
                m.call("sound", m.r(node + 0x40))
            m.call("pool_free", node, FILE, 0x690, 0)
            node = nxt
    test.run("FUN_0046eb70", 0x46EB70, overlays_build, overlays_model, 0, ret=False)

    test.callee("overlays", 0x46EB70, 0, ("eax",))     # from here FUN_0046eb70 is hooked away
    def record_build(img, rng):
        """FUN_0046ec10: no record, or a COF list of nodes."""
        sel["gfx"] = gfx if rng.randrange(4) else 0
        chain(test, img, rng, gfx - test.arena + 0x28, slots, 0x10)
        return {"ecx": unit}

    def record_model(m, args):
        """Fatal 0x814 without a record; else COF nodes (0x81a), the overlays, the record (0x820)."""
        m.call("gfxinfo", unit)
        if not sel["gfx"]:
            raise rig.Halt(0x814)
        node = m.r(gfx + 0x28)
        while node:
            nxt = m.r(node + 0x10)
            m.call("pool_free", node, FILE, 0x81A, 0)
            node = nxt
        m.call("overlays", gfx)
        m.call("pool_free", gfx, FILE, 0x821 if "line" in MUT else 0x820, 0)
    test.run("FUN_0046ec10", 0x46EC10, record_build, record_model, 0, ret=False)

    cache_at = test.arena + 0x1000

    def cache_fill(img, rng, at_arena):
        """Entries in the arena (0x2000..), bucket pointers for a few; returns the bucket dwords (0x200)."""
        buckets = [0] * 0x200
        for k in range(rng.randrange(7)):
            node = test.arena + 0x2000 + k * 0x20
            img[node - test.arena:node - test.arena + 4] = (0).to_bytes(4, "little")
            slot = rng.randrange(0x200)
            if buckets[slot] and rng.randrange(2):      # chain behind the head
                img[node - test.arena:node - test.arena + 4] = buckets[slot].to_bytes(4, "little")
            buckets[slot] = node
        return buckets

    def walk_build(img, rng):
        """FUN_0046f370: the cache sits in the arena."""
        buckets = cache_fill(img, rng, True)
        for i, b in enumerate(buckets):
            img[0x100C + 4 * i:0x1010 + 4 * i] = b.to_bytes(4, "little")
        sel["counter"] = rng.randrange(1 << 32)
        test.m.w32(COUNTER, sel["counter"])
        return {"regs": {"eax": cache_at}}

    def walk_model(m, args):
        """Free the entries whose flag bit 0 is set; counter -= 0x1c + size."""
        counter = entries(m, cache_at, m.r, sel["counter"])
        rig.require(test.m.r32(COUNTER) == counter, f"counter {test.m.r32(COUNTER):#x} vs {counter:#x}")
    test.run("FUN_0046f370", 0x46F370, walk_build, walk_model, 0, ret=False)

    for name, addr, key in (("FUN_0046f3f0", 0x46F3F0, "mon"), ("FUN_0046f440", 0x46F440, "ply")):
        base, line = CACHES[key]

        def cache_build(img, rng, base=base):
            """A cache in game memory: block, size, buckets pointing into the arena."""
            buckets = cache_fill(img, rng, False)
            blob = bytearray(rng.randbytes(0x80C))
            blob[4:8] = (rng.choice((0, test.arena + 0x3000))).to_bytes(4, "little")
            for i, b in enumerate(buckets):
                blob[0xC + 4 * i:0x10 + 4 * i] = b.to_bytes(4, "little")
            test.m.mu.mem_write(base, bytes(blob))
            sel.update(blob=bytes(blob), counter=rng.randrange(1 << 32))
            test.m.w32(COUNTER, sel["counter"])
            return {}

        def cache_model(m, args, base=base, line=line):
            """Free the entries, subtract +8 from the counter, free the block (if any), zero 0x80c bytes."""
            blob = sel["blob"]

            def rd(at, base=base, blob=blob):
                """A dword of the cache as it was before the run."""
                return int.from_bytes(blob[at - base:at - base + 4], "little")
            counter = entries(m, base, rd, sel["counter"])
            counter = (counter - rd(base + 8)) & rig.M32
            if rd(base + 4):
                m.call("pool_free", rd(base + 4), FILE, line, 0)
            rig.require(test.m.r32(COUNTER) == counter, f"counter {test.m.r32(COUNTER):#x} vs {counter:#x}")
            want = bytes(0x80B) + (b"\1" if "zero" in MUT else b"\0")
            rig.require(test.m.read(base, 0x80C) == want, "cache not zeroed")
        test.run(name, addr, cache_build, cache_model, 0, ret=False)

    # FUN_0046f310: the player cache gets the bundle path
    test._callee("loadcache", 0x46DDF0, 1, ("ebx",), lambda emu: sel.__setitem__("path", emu.cstr(emu.arg(0))) or 0, nlog=0)

    def path_build(img, rng):
        """CL picks a bundle name; the upper ECX bits are dead."""
        sel["cl"] = rng.randrange(5)
        return {"ecx": (rng.randrange(1 << 24) << 8) | sel["cl"]}

    def path_model(m, args):
        """The path is DATA\\GLOBAL\\<name>; EBX is the player cache."""
        name = test.m.cstr(test.m.r32(0x6D6C30 + 4 * sel["cl"]))
        m.call("loadcache", CACHES["mon" if "cache" in MUT else "ply"][0])
        rig.require(sel["path"] == "DATA\\GLOBAL\\" + name, f"path {sel['path']!r}")
    test.run("FUN_0046f310", 0x46F310, path_build, path_model, 0, ret=False)

    def frame_build(img, rng):
        """FUN_0046e150: overlays of mixed kinds / rows."""
        sel["gfx"] = gfx if rng.randrange(5) else 0
        nodes = chain(test, img, rng, gfx - test.arena + 0x2C, slots, 0xA4)
        for ptr in nodes:
            off = ptr - test.arena
            img[off:off + 4] = rng.choice((1, 1, 1, 6)).to_bytes(4, "little")
            img[off + 4:off + 8] = rng.randrange(3).to_bytes(4, "little")
        return {"ecx": unit, "edx": rng.randrange(3), "stack": [rng.randrange(1 << 32)]}

    def frame_model(m, args):
        """Set +8 of every kind-1 node of the row; a matching node of another kind is fatal 0x205."""
        m.call("gfxinfo", unit)
        if not sel["gfx"]:
            raise rig.Halt(0x1FE)
        node = m.r(gfx + 0x2C)
        while node:
            if m.r(node + 4) == args["edx"]:
                if m.r(node) != 1:
                    raise rig.Halt(0x205)
                m.w(node + 8, args["stack"][0])
                if "stop" in MUT:
                    return
            node = m.r(node + 0xA4)
    test.run("FUN_0046e150", 0x46E150, frame_build, frame_model, 1, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 96
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("sound", "line", "flag", "zero", "cache", "stop")}
    rig.run_suite(suite, mutations, "ENGINE graphics free", args.mutation_check)


if __name__ == "__main__":
    main()
