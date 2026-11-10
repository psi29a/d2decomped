# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the token cache, monster COF preload and cel-request layer in stock 1.14d.

Run: uv run python engine_cof_request.py [--mutation-check]
FUN_004da790 fills the five type-token tables at 0x7c88d0..0x7c89cc from the
token-record lookups (16 player, 20 player-mode, 16 monster-mode, 8
object-mode, 3 more; fatal 0x36, 0x3f, 0x48, 0x51, 0x5a on the first missing
record). FUN_0046f870 (ECX class, EDX argument) calls FUN_0046f650 for each
mode in 0x712ac4 that the monster's MonStats2 row has, with a zeroed 0x34-byte
request whose first word is the class. FUN_004db2e0 (ECX, EDX kind, seven stack
arguments) fills one layer's draw type and parameter (FUN_004daf70,
FUN_004db290, FUN_004db1e0 and FUN_00600e20 are hooked). The token-record
lookups, FUN_0046f650 and those four are hooked with this case's synthetic
values; the claim is the table layout, the zeroing, the lookup order, the
fatal lines, the bit tests, the call arguments and the bytes popped.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32, s32
from unicorn.x86_const import UC_X86_REG_EBX

MUT = set()
TABLES = ((0x7C8920, 16, 0x36, "t1", 1), (0x7C88D0, 20, 0x3F, "t2", 2), (0x7C8980, 16, 0x48, "t3", 2),
          (0x7C8960, 8, 0x51, "t4", 2), (0x7C89C0, 3, 0x5A, "t5", 1))
REGION = (0x7C88D0, 0x100)


def suite(test):
    """Run the three functions."""
    sel = {}

    def record(table, index):
        """The synthetic token record of (table, index): an arena slot or none."""
        if sel["fail"] == (table, index):
            return 0
        return test.arena + 0x400 + (["t1", "t2", "t3", "t4", "t5"].index(table) * 20 + index) * 0x20

    for name, addr, nstack in (("t1", 0x65B5E0, 1), ("t2", 0x65B480, 2), ("t3", 0x65B500, 2), ("t4", 0x65B570, 2), ("t5", 0x65B620, 1)):
        test._callee(name, addr, nstack, (), lambda e, name=name: record(name, e.arg(0)))

    def build_tokens(img, rng):
        """Dirty tables and the record to fail on (or none)."""
        test.m.mu.mem_write(REGION[0], rng.randbytes(REGION[1]))
        pick = rng.choice([None] + [(t[3], rng.randrange(t[1])) for t in TABLES for _ in range(2)])
        sel["fail"] = pick
        sel["before"] = test.m.read(*REGION)
        return {}

    def tokens(m, args):
        """Fill each table from record+0x20; the first missing record is fatal."""
        mem = bytearray(sel["before"])
        base = REGION[0]

        def put(addr, value):
            """Write a dword of the expected tables."""
            mem[addr - base:addr - base + 4] = (value & M32).to_bytes(4, "little")

        def finish(line):
            """Check the tables, then raise the fatal line (or return)."""
            got = test.m.read(*REGION)
            rig.require(got == bytes(mem), f"tables differ at {[hex(base + i) for i in range(len(got)) if got[i] != mem[i]][:6]}")
            if line:
                raise rig.Halt(line)
        for table, count, line, name, nargs in TABLES:
            if name in ("t1", "t2", "t3"):
                mem[table - base:table - base + (0x40 if name != "t2" else 0x50)] = bytes(0x40 if name != "t2" else 0x50)
            elif name == "t4":
                mem[table - base:table - base + 0x20] = bytes(0x20)
            else:
                mem[table - base:table - base + 0xC] = bytes(12)
            for i in range(count):
                rec = m.call(name, *((i, 1) if nargs == 2 else (i,)))
                if not rec:
                    finish(line)
                put(table + 4 * i, m.r(rec + (0x24 if "off" in MUT else 0x20)))
        finish(None)
    test.run("FUN_004da790", 0x4DA790, build_tokens, tokens, 0, ret=False)

    # FUN_0046f870 ---------------------------------------------------------
    hdr, rows1, rows2 = test.arena + 0x1000, test.arena + 0x2000, test.arena + 0x2800
    test.m.w32(0x744304, hdr)

    def request(e):
        """FUN_0046f650: capture the request EBX points to."""
        sel["req"] = e.read(e.mu.reg_read(UC_X86_REG_EBX), 0x34)
        sel.setdefault("reqs", []).append(sel["req"])
        sel["ebx"] = e.mu.reg_read(UC_X86_REG_EBX)
        return 0
    test._callee("loadmode", 0x46F650, 3, ("ebx",), request, nlog=3)

    def build_modes(img, rng):
        """MonStats / MonStats2 rows, a mode list (count may be negative)."""
        def put(off, value, size=4):
            """Store an arena value."""
            img[off:off + size] = (value & ((1 << 8 * size) - 1)).to_bytes(size, "little")
        put(0x1000 + 0xA80, 4)
        put(0x1000 + 0xA78, rows1)
        put(0x1000 + 0xA98, 3)
        put(0x1000 + 0xA90, rows2)
        for i in range(4):
            put(0x2000 + i * 0x1A8 + 0x18, rng.choice((0, 1, 2, 0xFFFF, 0x8000, 3)), 2)
        test.m.w32(0x712B00, rng.choice((0, 1, 3, 5, 0xFFFFFFFF)))
        for i in range(5):
            test.m.w32(0x712AC4 + 4 * i, rng.choice((0, 1, 7, 8, 0x1F, 0xFFFFFFFF, rng.randrange(0x40))))
        sel["reqs"] = []
        return {"ecx": rng.choice((0xFFFFFFFF, 0, 1, 2, 3, 4, 0x12345)), "edx": rng.randrange(1 << 32)}

    def modes(m, args):
        """One preload per mode the MonStats2 row has."""
        cls, extra = args["ecx"], args["edx"]
        if s32(cls) < 0 or s32(cls) >= m.r(hdr + 0xA80):
            return
        row1 = m.r(hdr + 0xA78) + cls * 0x1A8
        index = m.r(row1 + 0x18, 2)
        index = index - 0x10000 if index & 0x8000 else index
        if index < 0 or index >= m.r(hdr + 0xA98):
            return
        row2 = m.r(hdr + 0xA90) + index * 0x134
        count = s32(test.m.r32(0x712B00))
        for i in range(max(count, 0)):
            mode = test.m.r32(0x712AC4 + 4 * i)
            if m.r(row2 + (s32(mode) >> 3) + 0xF0, 1) & (1 << (mode & 7)):
                m.call("loadmode", sel["ebx"], row1, mode, extra)
                want = (cls & 0xFFFF).to_bytes(2, "little") + bytes(0x32 if "size" not in MUT else 0x31) + b"\1" * (1 if "size" in MUT else 0)
                rig.require(sel["req"] == want, f"request {sel['req'].hex()}")
    test.run("FUN_0046f870", 0x46F870, build_modes, modes, 0, ret=False)

    # FUN_004db2e0 ---------------------------------------------------------
    args_at = [test.slot(i) for i in (2, 3, 4)]
    test._callee("drawtype", 0x4DAF70, 1, ("eax", "ebx", "edi"), lambda _e: sel["draw"])

    def convert(e):
        """FUN_004db290: write this case's converted direction through ESI."""
        e.w32(e.mu.reg_read(__import__("unicorn.x86_const", fromlist=["x"]).UC_X86_REG_ESI), sel["conv"])
        return 0
    test._callee("dirconv", 0x4DB290, 0, ("eax", "ecx", "esi"), convert)
    test.callee("death_dir", 0x4DB1E0, 2, ("esi", "ebx", "edi"))
    test._callee("final", 0x600E20, 2, (), lambda _e: sel["final"])

    def build_param(img, rng):
        """Seven stack arguments: a2..a4 point at arena words."""
        sel.update(draw=rng.randrange(1 << 32), conv=rng.randrange(1 << 32), final=rng.randrange(1 << 32))
        a2, a3, a4 = args_at
        img[a2 - test.arena:a2 - test.arena + 4] = rng.choice((0, 0x3F, 0x40, 0x41, 0xFFFFFFFF)).to_bytes(4, "little")
        return {"ecx": rng.randrange(1 << 32), "edx": rng.randrange(1 << 32),
                "stack": [rng.randrange(1 << 32), rng.randrange(1 << 32), a2, a3, a4, rng.randrange(1 << 32), rng.choice((0, 0, 1, 2))]}

    def param(m, args):
        """A layer's draw type, direction conversion and parameter word."""
        a0, a1, a2, a3, a4, a5, a6 = args["stack"]
        if m.r(a2) >= 0x40:
            m.w(a4, 1)
            m.w(a3, 0)
            return
        kind = m.call("drawtype", args["edx"], a1, a0, args["ecx"])
        m.w(a4, kind)
        if a6:
            m.call("dirconv", a5, kind, a2)
            m.w(a2, sel["conv"])
            m.call("death_dir", args["ecx"], m.r(a4), a5, m.r(a2), a6)
        m.w(a3, m.call("final", m.r(a2), a5 ^ (1 if "final" in MUT else 0)))
    test.run("FUN_004db2e0", 0x4DB2E0, build_param, param, 7, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 500
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("off", "size", "final")}
    rig.run_suite(suite, mutations, "ENGINE COF request", args.mutation_check)


if __name__ == "__main__":
    main()
