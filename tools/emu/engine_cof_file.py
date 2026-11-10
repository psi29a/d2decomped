# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the COF cache loaders FUN_0046dcc0 and FUN_0046ddf0 in stock 1.14d.

Run: uv run python engine_cof_file.py [--mutation-check]
FUN_0046dcc0 (EAX path, stack: cache) loads one COF file into a cache entry:
it takes the lower-cased file name, returns 0 when the cache has it, else
allocates a 0x1c-byte entry (line 0xdc), copies the name to +0xc, loads the
file (size to +8, data to +0x14, line 0xe0), a layer count above 0x40 in the
data's byte 2 is fatal 0xec, links the entry into the bucket the name hashes
to and adds size + 0x1c to the byte counter at 0x7a8918. FUN_0046ddf0 (EBX
cache, stack: path) loads a bundle once ([cache] = 1 after): the file (lines
0x10b, 0x10c fatal when missing), then each entry (0x18-byte header: +8 size,
name at +0xc, data from +0x18): flags cleared, layer count above 0x40 is
fatal 0x126, a name of 8 or more characters fatal 0x79, the bucket from the
name hash folded with shifts of 8, each entry linked at the bucket head.
The pool, the file name extraction, the string lower-casing, the entry
lookup, the name hash (FUN_0046db90 / SStrHash) and the file loader are
hooked. The claim: the field writes, the order, the fatal lines, the bucket
fold, the counter and the bytes popped.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32
from unicorn.x86_const import UC_X86_REG_EDI

MUT = set()
FILE = 0x6D6C1C
COUNTER = 0x7A8918


def suite(test):
    """Run both loaders."""
    sel = {}
    cache, bundle, data = test.arena + 0x1000, test.arena + 0x2000, test.arena + 0x3000
    path = test.slot(0, 0x100)

    def extract(e):
        """FUN_0046dbf0: write this case's file name into the buffer EDI points to."""
        sel["buf"] = e.mu.reg_read(UC_X86_REG_EDI)
        e.mu.mem_write(sel["buf"], sel["fname"] + b"\0")
        return 0

    def lower(e):
        """FUN_006864e1 (_strlwr): lower-case the buffer in place."""
        sel["buf2"] = e.arg(0)
        text = e.cstr(e.arg(0)).lower().encode("latin-1")
        e.mu.mem_write(e.arg(0), text + b"\0")
        return e.arg(0)

    def pool(e):
        """FUN_0040b380: a fresh zeroed 0x1c-byte entry."""
        sel["entry"] = e.alloc(0x1C)
        return sel["entry"]

    def load(e):
        """FUN_00517079: store the size through the third-from-last pointer, give the data."""
        e.w32(e.arg(0), sel["size"])
        return sel["data"]

    test.callee("pool_free", 0x40B3C0, 2, ("ecx", "edx"))
    test._callee("extract", 0x46DBF0, 0, ("edx", "edi"), extract)
    test._callee("lower", 0x6864E1, 0, (), lower, nlog=0)
    test._callee("find", 0x46DC40, 2, (), lambda _e: sel["found"], nlog=0)
    test._callee("pool", 0x40B380, 2, ("ecx", "edx"), pool)
    test._callee("zero", 0x4FAC80, 0, (), None)
    test._callee("load", 0x517079, 3, ("ecx", "edx"), load, nlog=3)
    test._callee("hash", 0x46DB90, 0, ("esi",), lambda _e: sel["bucket"])
    test._callee("sstr", 0x413CE0, 3, (), lambda _e: sel["h"])

    def build_file(img, rng):
        """A cache, a file name, and the loader's outcomes."""
        name = bytes(rng.choice(b"ABCxyz_0.cof") for _ in range(rng.randrange(1, 15)))
        sel.update(fname=name, found=rng.choice((0, 0, 0, test.arena + 0x100)), bucket=rng.randrange(0x200),
                   size=rng.randrange(1 << 32), data=data if rng.randrange(5) else 0)
        img[data - test.arena + 2] = rng.choice((0, 0x40, 0x41, 0xFF))
        sel["counter"] = rng.randrange(1 << 32)
        test.m.w32(COUNTER, sel["counter"])
        for i in range(0x200):
            if rng.randrange(8) == 0:
                img[cache - test.arena + 0xC + 4 * i:cache - test.arena + 0x10 + 4 * i] = rng.randrange(1, 1 << 32).to_bytes(4, "little")
        return {"regs": {"eax": path}, "stack": [cache]}

    def model_file(m, args):
        """Load one COF file into the cache."""
        m.call("extract", path, sel["buf"])
        m.call("lower")
        if m.call("find"):
            return 0
        pointer = m.call("pool", 0x1C, FILE, 0xDC, 0)
        buf = bytearray(0x1C)
        m.blocks.append((pointer, buf))
        m.regions.append((pointer, buf))
        for off in range(0, 0x1C, 4):
            m.w(pointer + off, 0)
        m.w(pointer + 4, 1)
        for i, b in enumerate(sel["fname"].lower() + b"\0"):
            m.w(pointer + 0xC + i, b, 1)
        zero = m.call("zero")
        data_ptr = m.call("load", zero, path, pointer + 8, FILE, 0xE0)
        m.w(pointer + 8, sel["size"] + (1 if "size" in MUT else 0))
        m.w(pointer + 0x14, data_ptr)
        if not data_ptr:
            return 0
        if m.r(data_ptr + 2, 1) > (0x41 if "layers" in MUT else 0x40):
            raise rig.Halt(0xEC)
        idx = m.call("hash", sel["buf"])
        m.w(pointer, m.r(cache + 0xC + idx * 4))
        m.w(cache + 0xC + idx * 4, pointer)
        total = (sel["counter"] + 0x1C + sel["size"]) & M32
        rig.require(test.m.r32(COUNTER) == total, f"counter {test.m.r32(COUNTER):#x} vs {total:#x}")
        return pointer
    test.run("FUN_0046dcc0", 0x46DCC0, build_file, model_file, 1)

    def load_bundle(e):
        """FUN_00517079 for the bundle: size through its pointer, the bundle data."""
        e.w32(e.arg(0), sel["total"])
        return sel["bundle"]
    test.custom["load"] = load_bundle

    def build_bundle(img, rng):
        """A bundle of 0..3 entries with random names, flags and layer counts."""
        count = rng.choice((0, 1, 2, 3))
        sel["bundle"] = bundle if rng.randrange(10) else 0
        off, total = bundle - test.arena, 0
        for i in range(count):
            size = rng.choice((0, 3, 8, 0x20))
            img[off + 8:off + 12] = size.to_bytes(4, "little")
            img[off + 4:off + 8] = rng.choice((0, 1, 0xFFFFFFFF)).to_bytes(4, "little")
            name = bytes(rng.choice(b"abc012") for _ in range(rng.choice((1, 3, 7, 7, 8))))
            img[off + 0xC:off + 0x14] = (name + bytes(8))[:8] if len(name) < 8 else name[:8]
            if len(name) >= 8:
                img[off + 0x14] = 0x41          # byte after the 8-character name: the name runs on
            img[off + 0x1A] = rng.choice((0, 0x40, 0x41, 0x7F)) if size >= 3 else 0
            off += 0x18 + size
            total += 0x18 + size
        sel["total"] = total
        sel["h"] = rng.randrange(1 << 32)
        sel["counter"] = rng.randrange(1 << 32)
        test.m.w32(COUNTER, sel["counter"])
        flag = rng.choice((0, 0, 1))
        img[cache - test.arena:cache - test.arena + 0x80C] = rng.randbytes(0x80C)
        img[cache - test.arena:cache - test.arena + 4] = flag.to_bytes(4, "little")
        sel["flag"] = flag
        sel["count"] = count
        return {"regs": {"ebx": cache}, "stack": [path]}

    def model_bundle(m, args):
        """Walk the bundle's entries once."""
        if m.r(cache):
            return
        zero = m.call("zero")
        data_ptr = m.call("load", zero, path, cache + 8, FILE, 0x10B)
        total = sel["total"]
        m.w(cache + 8, total)       # the loader stored the size through its pointer
        m.w(cache + 4, data_ptr)
        if not data_ptr:
            raise rig.Halt(0x10C)
        counter = (sel["counter"] + total) & M32
        rig.require(test.m.r32(COUNTER) == counter, "counter")
        remaining, entry = total, data_ptr
        while remaining:
            if m.r(entry + 4):
                m.w(entry + 4, 0)
            if m.r(entry + 0x1A, 1) > 0x40:
                raise rig.Halt(0x126)
            m.w(entry + 0x14, entry + 0x18)
            length = 0
            while m.r(entry + 0xC + length, 1):
                length += 1
            if length >= 8:
                raise rig.Halt(0x79)
            h = m.call("sstr", entry + 0xC, 1, 0)
            bucket = (((h >> 8 ^ h) >> 8 ^ h) >> 8 ^ h) & (0x1FF if "mask" not in MUT else 0xFF)
            m.w(entry, m.r(cache + 0xC + bucket * 4))
            m.w(cache + 0xC + bucket * 4, entry)
            size = m.r(entry + 8)
            remaining = (remaining - 0x18 - size) & M32
            entry = (entry + 0x18 + size) & M32
        m.w(cache, 1)
    test.run("FUN_0046ddf0", 0x46DDF0, build_bundle, model_bundle, 1, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 800
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("layers", "mask", "size")}
    rig.run_suite(suite, mutations, "ENGINE COF file", args.mutation_check)


if __name__ == "__main__":
    main()
