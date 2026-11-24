# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's party-screen hostile button (C->S 0x5d action 4, FUN_005a5e50) as the oracle.

FUN_005a5e50 runs natively, with FUN_0061ab00 / FUN_0066bab0 / FUN_006426a0
(the room's town test) real. Hooked, as the inputs and the effects the case
reads: FUN_00620bb0 (the declarer's room: a made block, or 0), FUN_006253b0
and FUN_00625480 (the two units' level), FUN_0055b300 (the relation's flag 8),
FUN_0055b6c0 (the pair stamp), GetTickCount, FUN_0055b140 (adds the node:
the model always has it); the effects FUN_0055b4c0, FUN_005a6080, FUN_0055b720,
FUN_00553380, FUN_005a5dc0 and the S->C senders are recorded. The party
branch (FUN_005a5dc0's walk, FUN_005a5d20) is not run: the declarer is in no
party (FUN_00554630 = 0xffff).

    .venv/bin/python hostile_toggle.py inc [--mutation-check] > ../../tests/hostile_toggle_cases.inc

A row is { in_room, room level id, self level, other level, other level again,
hostile, pair stamp, now, on, outcome } with the outcome the rules::HostileToggle
enumerator's index. --mutation-check reruns with the town test removed, then
with the 9 test made 10, then with the stamp compared as >=; each must change
a row, or it fails.
"""
import sys

import emu

OUTCOMES = ["kViolation", "kResend", "kIgnored", "kCleared", "kCoolingDown", "kDeclared"]


class Oracle:
    def __init__(self, min_level=9, town_any=False, stamp_ge=False):
        self.cur = {}
        self.log = []
        emu.Emu.w_GetTickCount = lambda _self: (self.cur["now"], 0)
        self.e = e = emu.Emu()
        self.game, self.me, self.other = e.alloc(0x2000), e.alloc(0x200), e.alloc(0x200)
        self.room, self.level, self.info = e.alloc(0x100), e.alloc(0x100), e.alloc(0x400)
        e.w32(self.me, 0); e.w32(self.me + 0xc, 1)
        e.w32(self.other, 0); e.w32(self.other + 0xc, 2)
        e.w32(self.room + 0x10, self.level); e.w32(self.level + 0x58, self.info)
        rec = self.log.append
        e.hook(0x620bb0, lambda _e: self.room if self.cur["in_room"] else 0, 1)
        e.hook(0x6253b0, lambda _e: self.cur["self_level"] if e.arg(0) == self.me else self.cur["other_level"], 3)
        e.hook(0x625480, lambda _e: self.cur["other_again"], 3)
        e.hook(0x55b140, lambda _e: 0, 1)
        e.hook(0x55b300, lambda _e: 8 if self.cur["hostile"] else 0, 1)
        e.hook(0x55b6c0, lambda _e: self.cur["stamp"], 0)
        e.hook(0x55b4c0, lambda _e: rec("resend") or 0, 1)
        e.hook(0x5a6080, lambda _e: rec("cleared") or 1, 1)
        e.hook(0x55b720, lambda _e: rec("declared") or 0, 1)
        e.hook(0x553380, lambda _e: rec("cooling") or 0, 1)
        e.hook(0x5a5dc0, lambda _e: 0, 1)
        e.hook(0x535430, lambda _e: 0, 0)
        e.hook(0x554630, lambda _e: 0xffff, 0)
        e.hook(0x5a5d20, lambda _e: 0, 3)
        e.hook(0x5531c0, lambda _e: 0, 1)
        e.hook(0x5a5690, lambda _e: 0, 2)
        e.hook(0x566d90, lambda _e: 0, 1)
        self.mutations = (min_level, town_any, stamp_ge)

    def run(self, in_room, level_id, self_level, other_level, again, hostile, stamp, now, on):
        e = self.e
        min_level, town_any, stamp_ge = self.mutations
        self.cur.update(in_room=in_room, self_level=self_level + (9 - min_level) * (self_level == 9), other_level=other_level + (9 - min_level) * (other_level == 9),
                        other_again=again, hostile=hostile, stamp=stamp + 1 if stamp_ge and stamp == now else stamp, now=now)
        e.w32(self.info + 0x1d0, 1 if town_any else level_id)
        self.log.clear()
        out = e.alloc(4)
        e.w32(out, 0)
        e.call(0x5a5e50, self.other, on, out, ecx=self.game, edx=self.me)
        if e.r32(out):
            return 0
        if "cooling" in self.log:
            return 4
        if "declared" in self.log:
            return 5
        if "cleared" in self.log:
            return 3
        return 1 if "resend" in self.log else 2


def rows(**mutation):
    o = Oracle(**mutation)
    out = []
    for now in (1_000_000, 0xFFFFFF00):
        for in_room in (1, 0):
            for level_id in (1, 2, 40, 109):
                for self_level in (8, 9):
                    for other_level in (8, 9, 30):
                        for hostile in (0, 1):
                            for on in (0, 1, 8):
                                for delta in (-1, 0, 1, 60000):
                                    stamp = (now + delta) & 0xFFFFFFFF
                                    args = (in_room, level_id, self_level, other_level, other_level, hostile, stamp, now, on)
                                    out.append((args, o.run(*args)))
    return out


def main():
    if len(sys.argv) < 2 or sys.argv[1] != "inc":
        raise SystemExit(__doc__)
    stock = rows()
    if "--mutation-check" in sys.argv[2:]:
        for name, mutation in (("town test removed", {"town_any": True}), ("level 9 made 10", {"min_level": 10}), ("stamp compared >=", {"stamp_ge": True})):
            if rows(**mutation) == stock:
                raise SystemExit(f"negative control escaped: {name}")
            print(f"negative control caught: {name}", file=sys.stderr)
    print("// Stock game.exe 1.14d, FUN_005a5e50; generated by tools/emu/hostile_toggle.py inc.")
    print("// { in room, room level, self level, other level, other level again, hostile, pair stamp, now, on, outcome }")
    for (in_room, level_id, self_level, other_level, again, hostile, stamp, now, on), outcome in stock:
        print(f"    {{ {in_room}, {level_id}, {self_level}, {other_level}, {again}, {hostile}, {stamp:#x}u, {now:#x}u, {on}u, {outcome} }},")


if __name__ == "__main__":
    main()
