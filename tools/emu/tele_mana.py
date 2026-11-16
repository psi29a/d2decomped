# SPDX-License-Identifier: GPL-3.0-or-later
"""Does a refused Teleport cost mana in game.exe?  FUN_0056f7f0 (the do wrapper) run natively with the REAL do 27
(FUN_005ca360) on sk_tele_spot's synthetic rooms. Teleport has no srvmissile (-1), so a zero do return leaves at
0x56fa3d..0x56fa41, before the payment FUN_0056bfe0 at 0x56fa7a.
    tele_mana.py game N SEED  ->  `id did x y pays tele T ret R` per case of `sk_tele_spot.py gen N SEED --no-units`
    did = the do put the unit somewhere; pays = calls of FUN_0056bfe0 (0 or 1).
Hooked (not the claim): sk_tele_spot's set, plus FUN_00620250 (no current skill), FUN_0056c160 (the mana check, sk_mana.py's
claim: passes), FUN_00643ad0, FUN_00644140 (Teleport's Skills.txt record), FUN_006442a0 (level 1), FUN_00625480 (stat 8 =
100 x 256), FUN_0063a400, FUN_00639df0, FUN_006272b0 (stat writes: none), FUN_0056bfe0 (the payment: counted).
Tags: V run for the game column. Not covered: Teleport 2's clear line (sk_tele_spot answers "clear")."""
import os
import random
import sys

USAGE = "usage: tele_mana.py game N SEED"
if len(sys.argv) != 4 or sys.argv[1] != "game" or not sys.argv[2].isdigit() or not sys.argv[3].lstrip("-").isdigit():
    sys.exit(USAGE)
sys.path.insert(0, os.environ.get("EMU", os.path.dirname(os.path.abspath(__file__))))   # emu.py: $EMU, else this directory
import emu  # noqa: E402
import skill_tips  # noqa: E402

_BOOTED = skill_tips.boot()
emu.Emu = lambda: _BOOTED          # the Skills.txt records are loaded (sk_mana.py's boot); sk_tele_spot builds its rooms on it
import sk_tele_spot as T  # noqa: E402

RECORD, TELE = 0x23c, 54


class TM(T.Oracle):
    def __init__(self):
        super().__init__()
        e = self.e
        self.skills = e.r32(e.r32(0x744304) + 0xb98)
        self.lvl, self.pays = 1, 0
        e.hook(0x620250, lambda e: 0, 1)
        e.stub(1, {0x56c160: 0})      # the mana check (sk_mana.py's claim): passes; the payment is what is read
        e.stub(0xFFFFFFFF, {0x643ad0: 1})
        e.hook(0x644140, lambda e: self.skills + TELE * RECORD, 0)
        e.hook(0x6442a0, lambda e: self.lvl, 3)
        e.hook(0x625480, lambda e: 100 * 256 if e.arg(1) == 8 else 0, 3)
        e.stub(0, {0x63a400: 1, 0x639df0: 2})
        e.hook(0x6272b0, lambda e: 0, 4)
        e.hook(0x56bfe0, self.pay, 2)
        r = self.skills + TELE * RECORD
        if (e.s32(r + 0x2c) & 0xffff, e.s32(r + 0x2e) & 0xffff, e.s32(r + 0x46) & 0xffff) != (0, 27, 0xffff):
            sys.exit("tele_mana: Teleport's row is not srvstfunc 0, srvdofunc 27, srvmissile -1")

    def pay(self, e):
        self.pays += 1
        return 1

    def cast(self, tele, cx, cy, tx, ty, words):
        self.load(words)
        self.e.view("levels", self.levelrow).teleport = tele
        self.here = self.rooms[(cx // T.ROOM, cy // T.ROOM)]
        self.xy, self.moved, self.line = (tx & 0xFFFFFFFF, ty & 0xFFFFFFFF), None, None
        self.pays = 0
        ret = self.e.call(0x56f7f0, TELE, self.lvl, 1, 1, 0, ecx=0x2000, edx=self.unit) & 0xFFFFFFFF
        return ret, self.moved, self.pays


def main():
    n, seed = int(sys.argv[2]), int(sys.argv[3])
    rng, oracle = random.Random(seed), TM()
    for cid in range(n):
        tele, cx, cy, tx, ty, words = T.make_case(rng, False)
        ret, pos, pays = oracle.cast(tele, cx, cy, tx, ty, words)
        print(cid, int(pos is not None), *(pos if pos else ("-", "-")), pays, "tele", tele, "ret", ret)


main()
