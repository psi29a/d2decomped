# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the monster COF path FUN_0046f490 and the life tint FUN_0046ed50 in stock 1.14d.

Run: uv run python engine_cof_path.py [--mutation-check]
FUN_0046f490 (EAX MonStats row, stack: mode, output buffer; ESI the 4-byte
weapon-class token output) writes
DATA\\GLOBAL\\MONSTERS\\<token>\\COF\\<token><mode><class>.COF into the buffer:
the mode token from the token record (FUN_0065b500, fatal 0xe86 when missing)
unless the one (mode, token) override entry at 0x712b08 / 0x712b04 matches (count at 0x712b0c), the weapon
class `hth ` for modes 0 and 0xc when MonStats2 +6 bit 0 is clear, spaces
dropped from each token. FUN_0046ed50 (ESI unit) returns a palette index by
life: blue for a monster with MonStats +0xd bit 1, else green / yellow / red
by life percent (palette lookups made once, FUN_004fb180). The token
record lookup, the transform, the palette lookup, the life getters are
hooked; wsprintfA is the emulator's. The claim is the path text, the override
and space handling, the fatal line, the palette arguments and the thresholds.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import M32, s32

MUT = set()
FILE = 0x6D6C1C
GREEN, YELLOW, RED, BLUE = 0x7A890C, 0x7A890D, 0x7A890E, 0x7A890F


def suite(test):
    """Run both functions."""
    sel = {}
    unit = test.slot(0)
    hdr, rows1, rows2 = test.arena + 0x1000, test.arena + 0x2000, test.arena + 0x2800
    out, tokout = test.slot(5, 0x200), test.slot(6)
    test.m.w32(0x744304, hdr)
    test._callee("modetok", 0x65B500, 2, (), lambda _e: sel["rec"])
    fmt, prefix = test.m.cstr(0x6D6C94), test.m.cstr(0x6D6CAC)

    def build_path(img, rng):
        """Rows, token bytes (with spaces), overrides."""
        def put(off, value, size=4):
            """Store an arena value."""
            img[off:off + size] = (value & ((1 << 8 * size) - 1)).to_bytes(size, "little")
        word = lambda: bytes(rng.choice(b" abchtx01\0") for _ in range(4))
        put(0x1000 + 0xA80, 4)
        put(0x1000 + 0xA78, rows1)
        put(0x1000 + 0xA98, 3)
        put(0x1000 + 0xA90, rows2)
        for i in range(4):
            put(0x2000 + i * 0x1A8 + 0x18, rng.choice((0, 1, 2, 0xFFFF, 0x8000)), 2)
            img[0x2000 + i * 0x1A8 + 0x10:0x2000 + i * 0x1A8 + 0x14] = word()
        for i in range(3):
            img[0x2800 + i * 0x134 + 0x10:0x2800 + i * 0x134 + 0x14] = word()
        sel["rec"] = test.arena + 0x2F00 if rng.randrange(8) else 0
        img[0x2F20:0x2F24] = word()
        mode = rng.choice((0, 0xC, 1, 2, 0xE, 5, 0xD))
        test.m.w32(0x712B0C, rng.choice((0, 1, 1)))     # the table has one 8-byte entry at 0x712b04; the count follows it
        test.m.w32(0x712B08, rng.choice((mode, mode, rng.randrange(16))))
        test.m.mu.mem_write(0x712B04, word())
        return {"regs": {"eax": rng.choice((0xFFFFFFFF, 0, 1, 2, 3, 4, 7)), "esi": tokout}, "stack": [mode, out]}

    def clean(raw):
        """A token as the format sees it: spaces dropped to NUL, the first three bytes only."""
        text = bytes(0 if b == 0x20 else b for b in raw[:3])
        return text.split(b"\0")[0].decode("latin-1")

    def path(m, args):
        """The COF path of (class, mode)."""
        cls, (mode, buf) = args["regs"]["eax"], args["stack"]
        if s32(cls) < 0 or s32(cls) >= m.r(hdr + 0xA80):
            return
        row1 = m.r(hdr + 0xA78) + cls * 0x1A8
        idx = m.r(row1 + 0x18, 2)
        idx = idx - 0x10000 if idx & 0x8000 else idx
        if idx < 0 or idx >= m.r(hdr + 0xA98):
            return
        row2 = m.r(hdr + 0xA90) + idx * 0x134
        rec = m.call("modetok", mode, 1)
        if not rec:
            raise rig.Halt(0xE86)
        modetok = m.r(rec + 0x20).to_bytes(4, "little")
        if mode != 0 and mode != 0xC:
            wclass = m.r(row2 + 0x10).to_bytes(4, "little")
        elif m.r(row2 + 6, 1) & 1:
            wclass = m.r(row2 + 0x10).to_bytes(4, "little")
        else:
            wclass = b"hth "
        m.w(args["regs"]["esi"], int.from_bytes(wclass, "little"))
        for i in range(test.m.r32(0x712B0C)):
            if mode == test.m.r32(0x712B08 + 8 * i):
                modetok = test.m.read(0x712B04 + 8 * i, 4) if "override" not in MUT else modetok
        base = m.r(row1 + 0x10).to_bytes(4, "little")
        text = fmt % (prefix, clean(base), clean(base), clean(modetok), clean(wclass))
        data = text.encode("latin-1") + b"\0"
        for i, b in enumerate(data):
            m.w(buf + i, b, 1)
    test.run("FUN_0046f490", 0x46F490, build_path, path, 2, ret=False)

    # FUN_0046ed50 ---------------------------------------------------------
    def palette(emu):
        """FUN_004fb180: a palette index made from its three arguments."""
        return (emu.arg(0) * 3 + emu.arg(1) * 5 + emu.arg(2) * 7 + 1) & 0xFF   # nlog=3 logs the arguments
    test._callee("palette", 0x4FB180, 3, (), palette, nlog=3)

    def transform(emu):
        """The type / class this case wants."""
        emu.w32(emu.arg(1), sel["type"])
        emu.w32(emu.arg(2), sel["cls"])
        sel["site"] = (emu.arg(4), emu.arg(5))
        return 0
    test._callee("mode", 0x621190, 1, (), lambda _e: 0)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test._callee("maxlife", 0x625D10, 1, (), lambda _e: sel["max"])
    test._callee("life", 0x625480, 3, (), lambda _e: sel["cur"])

    def build_tint(img, rng):
        """A unit of any type with a life ratio near the thresholds."""
        for i in range(3):
            img[0x2000 + i * 0x1A8 + 0xD] = rng.choice((0, 2, 3, 0xFD))
        img[0x1000 + 0xA80:0x1000 + 0xA84] = (4).to_bytes(4, "little")
        img[0x1000 + 0xA78:0x1000 + 0xA7C] = rows1.to_bytes(4, "little")
        sel.update(type=rng.choice((0, 1, 1, 2)), cls=rng.randrange(3))
        sel["max"] = rng.choice((0, 1, 7, 100, 200, 0x100000))
        want = rng.choice((0, 32, 33, 65, 66, 100, 150, -1, rng.randrange(0, 200)))
        sel["cur"] = (want * sel["max"]) // 100 + rng.choice((0, 0, 1, -1)) if sel["max"] else rng.randrange(-5, 50)
        sel["cur"] &= M32
        test.m.w32(0x7A8924, rng.choice((0, 1)))
        for a in (GREEN, YELLOW, RED, BLUE):
            test.m.mu.mem_write(a, bytes([rng.randrange(256)]))
        sel["before"] = test.m.read(GREEN, 4)
        sel["init"] = test.m.r32(0x7A8924)
        return {"regs": {"esi": unit}}

    def tint(m, args):
        """The palette index for the unit's life."""
        got = test.m.read(GREEN, 4)
        if not sel["init"]:
            want = [(0, 0xC0, 0), (0xE0, 0xE0, 0), (0xE0, 0, 0), (0, 0, 0xC0 if "blue" not in MUT else 0xC1)]
            made = bytes(m.call("palette", *a) & 0xFF for a in want)
            rig.require(got == made, f"palette bytes {got.hex()} vs {made.hex()}")
            rig.require(test.m.r32(0x7A8924) == 1, "init flag")
        else:
            rig.require(got == sel["before"], "palette changed after init")
        green, yellow, red, blue = got
        m.call("mode", unit)
        m.call("transform", unit)
        rig.require(sel["site"] == (FILE, 0x991), "transform site")
        if sel["type"] != 1:
            return green
        row = rows1 + sel["cls"] * 0x1A8
        if m.r(row + 0xD, 1) & 2:
            return blue
        top = m.call("maxlife", unit)
        if not top:
            return green
        cur = m.call("life", unit, 6, 0)
        pct = int(s32(cur * 100 & M32) / s32(top))
        if pct < 0x21:
            return red
        if pct < (0x41 if "yellow" in MUT else 0x42):
            return yellow
        return green
    test.run("FUN_0046ed50", 0x46ED50, build_tint, tint, 0, mask=0xFF)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 800
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("override", "yellow", "blue")}
    rig.run_suite(suite, mutations, "ENGINE COF path", args.mutation_check)


if __name__ == "__main__":
    main()
