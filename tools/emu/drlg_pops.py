# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the Preset.cpp POP updater FUN_006671e0 in stock game.exe 1.14d.

    uv run python drlg_pops.py [--mutation-check]

Runs game.exe's own code for FUN_006671e0 against a hand-written model on
random preset rooms (_dungeon_rig.py): callee log, arena, and the bytes
`ret n` pops. The import stub gives GetTickCount 0, so the deadline is 500.
Hooked, logged, not claimed: FUN_0066b980 (point in rectangle; its rectangle
is compared separately), FUN_00667060 (POP tile refresh) and the allocator.
`--mutation-check` breaks the model three ways.
"""
import argparse
import sys

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, require, s32

MUT = set()
DEADLINE = 500


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value):
    """Write a dword at arena offset `off`."""
    img[off:off + 4] = (value & M32).to_bytes(4, "little")


class Suite:
    """Builds and runs the cases."""

    def __init__(self, rig):
        """Hook the callees this oracle leaves unclaimed."""
        self.rig = rig
        self.base = rig.arena
        self.rects, self.exp_rects = [], []
        rig.callee("c66b980", 0x66_B980, 1, ("edx",))
        rig.callee("c667060", 0x66_7060, 2, ("ecx", "ebx", "edi"))

        def inside(e):
            """FUN_0066b980 stand-in: note the rectangle ECX points at, answer a random bit."""
            from unicorn.x86_const import UC_X86_REG_ECX
            ptr = e.mu.reg_read(UC_X86_REG_ECX)
            self.rects.append(tuple(e.r32(ptr + 4 * k) for k in range(4)))
            return rig.rng.getrandbits(1)
        rig.custom["c66b980"] = inside

    def at(self, off):
        """Arena address of offset `off`."""
        return self.base + off

    def pops(self):
        """FUN_006671e0: refresh the POP tiles of the preset rooms around a point."""
        rig, at = self.rig, self.at
        # arena: primary room 0x100, its near array 0x300, level-prest 0x180, near rooms 0x600 + 0x60 k,
        # preset records 0x500 + 0x10 k, maps 0x1000 + 0x100 k (index @+0x60, sub @+0x70, orient @+0x80, bounds @+0x90)

        def build(img, rng):
            """A primary room (preset type 2 most of the time) with up to four near rooms sharing or not its map."""
            self.rects.clear()
            self.exp_rects.clear()
            put(img, 0x180 + 0x3C, rng.randrange(0, 6))
            maps = [0x1000, 0x1100, 0x1200]

            def setup_map(base):
                """Fill one map's pop arrays."""
                put(img, base + 8, at(0x180))
                put(img, base + 0x40, rng.choice((-1, 0, 1, 2, 3, 4)))
                put(img, base + 0x44, at(base + 0x60))
                put(img, base + 0x48, at(base + 0x70))
                put(img, base + 0x4C, at(base + 0x80))
                put(img, base + 0x50, at(base + 0x90))
                for k in range(4):
                    put(img, base + 0x60 + 4 * k, rng.randrange(0, 3))
                    put(img, base + 0x70 + 4 * k, rng.getrandbits(32))
                    put(img, base + 0x80 + 4 * k, rng.choice((0, 0, 0x777, rng.getrandbits(32))))
                    for f in range(4):
                        put(img, base + 0x90 + 0x10 * k + 4 * f, rng.randrange(0, 12))
            for mp in maps:
                setup_map(mp)
            put(img, 0x100 + 0x48, 2 if rng.random() < 0.85 else rng.choice((0, 1, 3)))
            put(img, 0x100 + 0x20, at(0x500))
            put(img, 0x500 + 8, at(maps[0]))
            n = rng.randrange(0, 5)
            put(img, 0x100 + 0x2C, n)
            put(img, 0x100 + 8, at(0x300))
            for k in range(4):
                nr = 0x600 + 0x60 * k
                put(img, 0x300 + 4 * k, at(nr))
                put(img, nr + 0x48, 2 if rng.random() < 0.85 else 1)
                put(img, nr + 0x30, 0 if rng.random() < 0.2 else rng.getrandbits(32) | 1)
                put(img, nr + 0x20, at(0x510 + 0x10 * k))
                put(img, 0x510 + 0x10 * k + 8, at(rng.choice((maps[0], maps[0], maps[1], maps[2]))))
            return {"ecx": at(0x100), "edx": rng.randrange(0, 40), "stack": [rng.randrange(0, 40), rng.choice((0, 1))]}

        def model(m, a):
            """Find the POP under the point, refresh the neighbours' other POPs and the matching one, then rewrite the deadlines."""
            room, nx = a["ecx"], a["edx"]
            ny, other = a["stack"]
            matched, pmap, did = 0, 0, False
            if m.r(room + 0x48) == 2:
                pmap = m.r(m.r(room + 0x20) + 8)
                pad = m.r(m.r(pmap + 8) + 0x3C)
                for i in range(max(s32(m.r(pmap + 0x40)), 0)):
                    e = m.r(pmap + 0x50) + 0x10 * i
                    rect = (m.r(e) * 5, m.r(e + 4) * 5, m.r(e + 8) * 5 + pad, m.r(e + 12) * 5 + pad)
                    self.exp_rects.append(tuple(v & M32 for v in rect))
                    if m.call("c66b980", nx, ny):
                        matched = m.r(m.r(pmap + 0x44) + 4 * i)
                        break
            near, arr = s32(m.r(room + 0x2C)), m.r(room + 8)
            for i in range(max(near, 0)):
                nr = m.r(arr + 4 * i)
                if m.r(nr + 0x48) != 2 or m.r(nr + 0x30) == 0:
                    continue
                mp = m.r(m.r(nr + 0x20) + 8)
                for p in range(max(s32(m.r(mp + 0x40)), 0)):
                    dl = m.r(m.r(mp + 0x4C) + 4 * p)
                    idx = m.r(m.r(mp + 0x44) + 4 * p)
                    sub = m.r(m.r(mp + 0x48) + 4 * p)
                    bounds = m.r(mp + 0x50) + 0x10 * p
                    same = mp == pmap and idx == matched
                    if not same and dl != 0:
                        m.call("c667060", bounds, nr, DEADLINE, sub, 1)
                        did = True
                    if same:
                        if dl == 0:
                            m.call("c667060", bounds, nr, DEADLINE, sub, 0)
                        elif other or mut("other-guard"):
                            m.call("c667060", bounds, nr, dl, sub, 0)
                        else:
                            continue
                        did = True
            if did:
                for i in range(max(s32(m.r(room + 0x2C)), 0)):
                    nr = m.r(m.r(room + 8) + 4 * i)
                    if m.r(nr + 0x48) != 2:
                        continue
                    mp = m.r(m.r(nr + 0x20) + 8)
                    for p in range(max(s32(m.r(mp + 0x40)), 0)):
                        same = mp == pmap and m.r(m.r(mp + 0x44) + 4 * p) == matched
                        slot = m.r(mp + 0x4C) + 4 * p
                        if not same:
                            if not mut("clear"):
                                m.w(slot, 0)
                        else:
                            m.w(slot, DEADLINE + (1 if mut("deadline") else 0))
            require(self.rects == self.exp_rects, f"rectangles {self.rects}, model {self.exp_rects}")
        rig.run("FUN_006671e0", 0x6671E0, build, model, 2, ret=False)


def suite(rig):
    """Run every group; returns the number of cases."""
    rig_mod.ROUNDS = 300
    s = Suite(rig)
    s.pops()
    return rig.cases


MUTATIONS = {
    name: (lambda on, n=name: MUT.add(n) if on else MUT.discard(n))
    for name in ("other-guard", "deadline", "clear")
}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Preset.cpp POP updater", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
