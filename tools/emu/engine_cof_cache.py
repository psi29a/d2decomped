# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the Gfx.cpp COF cache start, stop and preload timer in stock 1.14d.

Run: uv run python engine_cof_cache.py [--mutation-check]
FUN_00470200 (ECX = 0x7a891c) zeroes both caches and the 0x460-byte slot table,
allocates the per-class table at 0x7a80fc (0xa0 bytes a MonStats row, line
0x7e1), reads the d2char.mpq flag and tail-calls FUN_004da790. FUN_00470290
frees both caches (FUN_0046f370 runs for real) and the class table (line
0x7f9). FUN_00470350 (ECX) runs FUN_00470070 at most every 600 ms (0x258)
unless 0x7a8920 is set, and stores the tick. The pool, the d2char.mpq check,
FUN_004da790, FUN_00470070 and GetTickCount are hooked or supplied; the
claim is the global writes, the allocation, the free order, the 600 ms test
(unsigned) and the call arguments.
"""
import argparse

import _dungeon_rig as rig
import emu

MUT = set()
FILE = 0x6D6C1C
CACHES = ((0x7A8100, 0xE50), (0x7A78F0, 0xE61))
COUNTER = 0x7A8918
TICKS = []


def tick(_e):
    """GetTickCount stand-in: the next value of this case (stdcall, no arguments)."""
    return (TICKS.pop(0) if TICKS else 0), 0


emu.Emu.w_GetTickCount = tick


def suite(test):
    """Run the three functions."""
    sel = {}
    test.callee("pool_free", 0x40B3C0, 2, ("ecx", "edx"))
    test.callee("pak", 0x4FAC90, 0, ())
    test.callee("tokens", 0x4DA790, 0, ())
    test.callee("preload", 0x470070, 0, ("eax",))
    test.m.w32(0x744304, test.arena + 0x1000)

    def fill(img, rng, base, entries_at):
        """A random cache in game memory with a few arena entries in its buckets."""
        buckets = [0] * 0x200
        for k in range(rng.randrange(4)):
            node = test.arena + entries_at + k * 0x20
            img[node - test.arena:node - test.arena + 4] = bytes(4)
            slot = rng.randrange(0x200)
            buckets[slot] = node
        blob = bytearray(rng.randbytes(0x80C))
        blob[4:8] = rng.choice((0, test.arena + 0x3F00)).to_bytes(4, "little")
        for i, b in enumerate(buckets):
            blob[0xC + 4 * i:0x10 + 4 * i] = b.to_bytes(4, "little")
        test.m.mu.mem_write(base, bytes(blob))
        return bytes(blob)

    def build_start(img, rng):
        """Dirty caches, a MonStats count in the header."""
        for base, _line in CACHES:
            test.m.mu.mem_write(base, rng.randbytes(0x80C))
        test.m.mu.mem_write(0x7A7490, rng.randbytes(0x460))
        img[0x1000 + 0xA80:0x1000 + 0xA84] = rng.choice((0, 1, 3, 7)).to_bytes(4, "little")
        for addr in (0x7A8910, 0x7A891C, 0x7A8928, 0x7A80FC):
            test.m.w32(addr, rng.randrange(1, 1 << 32))
        return {"ecx": rng.randrange(1 << 32)}

    def start(m, args):
        """Zero the caches and slots, allocate the class table, d2char flag, FUN_004da790."""
        count = m.r(test.arena + 0x1000 + 0xA80)
        size = count * 0xA0
        block = m.alloc(0, size, 0x7E1, FILE)
        flag = m.call("pak")
        result = m.call("tokens")
        for base, length in ((0x7A8100, 0x80C), (0x7A78F0, 0x80C), (0x7A7490, 0x460)):
            rig.require(test.m.read(base, length) == bytes(length), f"{base:#x} not zeroed")
        rig.require(test.m.r32(0x7A8910) == 0, "0x7a8910")
        rig.require(test.m.r32(0x7A891C) == args["ecx"], "0x7a891c")
        rig.require(test.m.r32(0x7A80FC) == block, "0x7a80fc")
        rig.require(test.m.r32(0x7A8928) == (flag + 1 if "flag" in MUT else flag), "0x7a8928")
        return result
    test.run("FUN_00470200", 0x470200, build_start, start, 0)

    def build_stop(img, rng):
        """Two filled caches, a class table or none, a counter."""
        sel["blobs"] = [fill(img, rng, base, 0x2000 + 0x400 * i) for i, (base, _l) in enumerate(CACHES)]
        sel["counter"] = rng.randrange(1 << 32)
        test.m.w32(COUNTER, sel["counter"])
        sel["table"] = rng.choice((0, test.arena + 0x3800))
        test.m.w32(0x7A80FC, sel["table"])
        return {}

    def stop(m, args):
        """Free each cache like FUN_0046f370 / 0046f3f0 / 0046f440, then the class table."""
        counter = sel["counter"]
        for (base, line), blob in zip(CACHES, sel["blobs"], strict=True):

            def rd(addr, blob=blob, base=base):
                """A little-endian dword of this cache blob."""
                return int.from_bytes(blob[addr - base:addr - base + 4], "little")
            for i in range(0x200):
                node = rd(base + 0xC + 4 * i)
                while node:
                    nxt = m.r(node)
                    if m.r(node + 4) & 1:
                        counter = (counter + (-0x1C - m.r(node + 8))) & rig.M32
                        m.call("pool_free", m.r(node + 0x14), FILE, 0xE3C, 0)
                        m.call("pool_free", node, FILE, 0xE3D, 0)
                    node = nxt
            counter = (counter - rd(base + 8)) & rig.M32
            if rd(base + 4):
                m.call("pool_free", rd(base + 4), FILE, line, 0)
            rig.require(test.m.read(base, 0x80C) == bytes(0x80C), "cache not zeroed")
        if sel["table"]:
            m.free(0, sel["table"], 0x7F9 if "line" not in MUT else 0x7F8, FILE)
        rig.require(test.m.r32(0x7A80FC) == 0, "table pointer")
        rig.require(test.m.r32(COUNTER) == counter, f"counter {test.m.r32(COUNTER):#x} vs {counter:#x}")
    test.run("FUN_00470290", 0x470290, build_stop, stop, 0, ret=False)

    def build_tick(img, rng):
        """Timer flag, last run, two tick readings."""
        sel["flag"] = rng.choice((0, 0, 1))
        sel["last"] = rng.randrange(1 << 32)
        near = (sel["last"] + 0x258 + rng.choice((-1, 0, 1, rng.randrange(-5, 5)))) & rig.M32
        TICKS[:] = [rng.choice((near, near, rng.randrange(1 << 32))), rng.randrange(1 << 32)]
        sel["ticks"] = list(TICKS)
        test.m.w32(0x7A8920, sel["flag"])
        test.m.w32(0x7A8910, sel["last"])
        return {"ecx": rng.randrange(1 << 32), "regs": {"edi": 0}}

    def timer(m, args):
        """Run the preload when forced or 600 ms have passed (unsigned), then store the tick."""
        ticks = list(sel["ticks"])
        if not sel["flag"]:
            now = ticks.pop(0)
            if now < ((sel["last"] + (0x259 if "tick" in MUT else 0x258)) & rig.M32):
                rig.require(test.m.r32(0x7A8910) == sel["last"], "timer stored on a skipped run")
                return
        m.call("preload", args["ecx"])
        rig.require(test.m.r32(0x7A8910) == ticks.pop(0), "timer value")
    test.run("FUN_00470350", 0x470350, build_tick, timer, 0, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 500
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("flag", "line", "tick")}
    rig.run_suite(suite, mutations, "ENGINE COF cache", args.mutation_check)


if __name__ == "__main__":
    main()
