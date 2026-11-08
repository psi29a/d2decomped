# SPDX-License-Identifier: GPL-3.0-or-later
"""Lightning (the `lightningbolt` row) hits on standing monsters, decided by game.exe's own code (next_hit.py with the lightningbolt row).

    ../../build/tests/test_light_pierce --light-hit SEED N > d2d.txt
    uv run python light_pierce.py d2d.txt > game.txt
    grep -E '^[HT]' d2d.txt | diff game.txt -

Replays the contacts d2d's flight produced (the F lines of test_next_hit:
per frame, per live missile, the monsters in the subtiles it entered)
through game.exe's own collision filter and hit. Per frame and missile, as
FUN_005ae1f0 does after FUN_00641cb0 found a unit: the first monster, in
the line's order, that the filter at 0x5a8850 (-> FUN_005a8730) passes is handed to
FUN_005adf10, and the missile's frame is over. Run for real: the filter at 0x5a8850,
FUN_005a8730 (unit +0xc4 bits 2 / 3, NextHit row: not in state 0x56 by
FUN_00639df0, FUN_0064a470: not the missile's last unit, hostility),
FUN_005adf10 (the same tests again, FUN_0064a400 last unit := this one,
FUN_005ada40, CollideKill / ToHit / flags), FUN_005adb00 (NextHit: a
stat list ending at game frame +0xa8 + NextDelay +0x189, state 0x56 on,
timer and expiry callback 0x5adaf0), FUN_0064a470 / FUN_0064a400,
FUN_00639df0, the callback 0x5adaf0 when the timer is due.

Hooked (not the claim): FUN_00552fd0 (the owner: a player), FUN_00554200
(hostile: yes), FUN_00554010 (the game), the stat getters FUN_00625480 /
FUN_00625500 (0: no pierce stats), the state bit store FUN_00639db0 /
FUN_00625b70 (a set of (unit, state)), the stat list calls FUN_006251f0 /
FUN_006252d0 / FUN_00625ce0 / FUN_00626e10 and the timer FUN_005417d0
(recorded: the end frame), the hit event FUN_005c0c30, the damage roll
FUN_005a89a0 (counts the hit), FUN_005adcd0 (apply). The timer is run at
the end of its frame, after that frame's missiles (FUN_005414d0: the missile
timers, group 2, before the monsters', group 1): struck at f, blocked
f+1..f+NextDelay. The Missiles.txt row is built from the
1.14d Missiles.txt (LastCollide, Explosion, Pierce in +4 bits 0..2,
CollideKill, CollideFriend, NextHit, NextDelay, ToHit, AlwaysExplode,
pSrvHitFunc, pSrvDmgFunc); missile +0x10 (collision class) 3; a missile's
last unit at creation its owner (FUN_0059fa30 0x59fdbb).
"""
import sys

import emu
from drops import txt_rows
from unicorn.x86_const import UC_X86_REG_EDX

BASE_FRAME = 1000
STATE = 0x56


class Game:
    def __init__(self):
        e = self.e = emu.Emu()
        rows = {r["Missile"]: r for r in txt_rows(e, "Missiles") if r.get("Missile")}
        self.rows = {}
        self.tables = e.alloc(0x1000)
        e.w32(0x744304, self.tables)
        e.w32(self.tables + 0xc4, 0x200)                # states: 0x56 in range
        names = ("lightningbolt",)
        block = e.alloc(0x1a4 * len(names))
        e.w32(self.tables + 0xb64, block); e.w32(self.tables + 0xb6c, len(names))
        for k, name in enumerate(names):
            r = rows[name]
            def n(col, r=r): return int(r.get(col) or 0)
            at = block + k * 0x1a4
            e.w32(at + 4, n("LastCollide") | n("Explosion") << 1 | n("Pierce") << 2)
            e.mu.mem_write(at + 0xe, n("pSrvHitFunc").to_bytes(2, "little"))
            e.mu.mem_write(at + 0x10, n("pSrvDmgFunc").to_bytes(2, "little"))
            e.mu.mem_write(at + 0x186, bytes([n("CollideKill"), n("CollideFriend"), n("NextHit"), n("NextDelay"), 0, n("ToHit"), n("AlwaysExplode")]))
            self.rows[name] = (k, at)
            for col in ("pSrvHitFunc", "pSrvDmgFunc", "ToHit"):
                if n(col): raise SystemExit(f"{name}: {col} {n(col)} is not handled by this oracle")
        self.game = e.alloc(0x200)
        self.owner = e.alloc(0x200); e.w32(self.owner + 0xc, 1)          # type 0: a player, id 1
        self.ctx = e.alloc(0x10)
        self.states = set()
        self.timers = []
        self.hits = {}
        self.units = {}
        e.hook(0x552fd0, lambda e: self.owner, 0)
        e.hook(0x554200, lambda e: 1, 1)
        e.hook(0x554010, lambda e: self.game, 0)
        e.hook(0x625480, lambda e: 0, 3)
        e.hook(0x625500, lambda e: 0, 3)
        e.hook(0x639db0, lambda e: self.set_state(e.arg(0), e.arg(1), e.arg(2)), 3)
        e.hook(0x625b70, lambda e: int((e.arg(0), e.arg(1)) in self.states), 2)
        e.hook(0x6251f0, lambda e: self.e.alloc(0x40), 5)
        e.hook(0x5417d0, lambda e: self.timers.append(self.timer(e)) or 0, 4)
        for addr, n in ((0x6252d0, 2), (0x625ce0, 2), (0x626e10, 3), (0x5c0c30, 3), (0x5adcd0, 0)):
            e.hook(addr, lambda e: 0, n)
        e.hook(0x5a89a0, lambda e: self.hit(e.arg(1)), 3)

    @staticmethod
    def timer(e):
        return (e.mu.reg_read(UC_X86_REG_EDX), e.arg(1))          # (unit, frame)

    def set_state(self, unit, state, on):
        (self.states.add if on else self.states.discard)((unit, state))
        return 0

    def hit(self, unit):
        self.hits[unit] = self.hits.get(unit, 0) + 1
        return 0

    def case(self, skill, count):
        e = self.e
        self.states.clear(); self.timers.clear(); self.units.clear()
        self.row_index, self.row = self.rows[skill]
        self.monsters = []
        for i in range(count):
            unit = e.alloc(0x200)
            e.w32(unit, 1); e.w32(unit + 4, 0); e.w32(unit + 0xc, 100 + i); e.w32(unit + 0xc4, 0xc)
            self.monsters.append(unit)
        self.total = [0] * count

    def missile(self, key):
        if key not in self.units:
            e = self.e
            unit = e.alloc(0x200)
            data = e.alloc(0x40)
            e.w32(unit, 3); e.w32(unit + 4, self.row_index); e.w32(unit + 0xc, 5000 + key); e.w32(unit + 0x10, 3)
            e.w32(unit + 0x14, data)
            e.w32(data + 0x18, 0); e.w32(data + 0x1c, 1)                   # last unit: the owner
            self.units[key] = unit
        return self.units[key]

    def frame(self, frame, contacts):
        e = self.e
        now = BASE_FRAME + frame
        e.w32(self.game + 0xa8, now)
        self.hits = {}
        for key, touched in contacts:
            missile = self.missile(key)
            e.w32(self.ctx, self.game); e.w32(self.ctx + 4, missile); e.w32(self.ctx + 8, self.owner); e.w32(self.ctx + 0xc, self.row)
            for index in touched:   # the unit order is d2d's monster order
                unit = self.monsters[index]
                if e.call(0x5a8850, ecx=unit, edx=self.ctx):
                    e.call(0x5adf10, unit, 0, ecx=self.game, edx=missile)
                    break
        # FUN_005414d0 runs the missiles' timers (group 2) before the monsters' (group 1):
        # the expiry (FUN_005a7f80 -> FUN_00627460 -> 0x5adaf0) follows this frame's contacts.
        for timer in [t for t in self.timers if t[1] <= now]:
            self.timers.remove(timer)
            e.call(0x5adaf0, 0, ecx=timer[0], edx=STATE)
        out = []
        for index, unit in enumerate(self.monsters):
            if self.hits.get(unit):
                self.total[index] += self.hits[unit]
                out.append(f"{index}x{self.hits[unit]}")
        return out


def main():
    g = Game()
    tag = None

    def finish():
        if tag is not None: print(" ".join([f"T{tag}", *map(str, g.total)]))

    for line in open(sys.argv[1]):
        if line.startswith("C"):
            finish()
            fields = line.split()
            tag = fields[0][1:]
            g.case(fields[1], int(fields[3]))
        elif line.startswith("F"):
            fields = line.split()
            contacts = [(int(m), [int(u) for u in us.split(".")]) for m, us in (f.split(":") for f in fields[2:])]
            hits = g.frame(int(fields[1]), contacts)
            if hits: print(f"H{tag} {fields[1]} " + " ".join(hits))
    finish()


if __name__ == "__main__": main()
