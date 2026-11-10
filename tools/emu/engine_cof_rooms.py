# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the near-room COF preload walk FUN_00470070 in stock 1.14d.

Run: uv run python engine_cof_rooms.py [--mutation-check]
FUN_00470070 (EAX argument, passed on) takes the local player's room, its
neighbour room list (FUN_00619790) and for each room walks the unit list
(room +0x74, next at unit +0xe8): a monster (type 1, class below the header
count) goes to FUN_0046feb0 unless it is in mode 0xc and its class slot in
the table at 0x7a80fc has +0x30 set; a player (type 0) sets the argument to 0
when 0x7a8928 is set and runs FUN_0046ff50 for each mode in 0x712aa0
(count 0x712ac0). Unless 0x7a8920 is set it stops as soon as FUN_005ff2a0
(queued loads) reaches 0x1e, checked before each room and after each unit;
when 0x7a8920 is set it pumps FUN_004565e0 once in 32 rooms instead
(and not at all when the argument is non-zero and 0x7a8928 clear). The
local-player, room and neighbour lookups, the pump, the queue count, the
state transform and the two workers are hooked. The claim: the loop order
and exits, the sentinels, the argument that reaches each worker and the
sticky zeroing of it.
"""
import argparse

import _dungeon_rig as rig
from _dungeon_rig import s32

MUT = set()


def suite(test):
    """Run FUN_00470070 over random room / unit lists."""
    sel = {}
    rooms_arr = test.slot(0, 0x100)
    room_slots = [test.slot(i) for i in (1, 2, 3)]
    unit_slots = [test.slot(i) for i in (4, 5, 6, 7, 8, 9)]
    hdr, table = test.arena + 0x1000, test.arena + 0x2000

    def rooms(e):
        """FUN_00619790: write the list pointer and count through the output pointers."""
        sel["p1"], sel["p2"] = e.arg(1), e.arg(2)
        e.w32(e.arg(1), rooms_arr)
        e.w32(e.arg(2), sel["count"])
        return 0

    def transform(e):
        """Replace the type / class / mode locals with this unit's."""
        unit = e.arg(0)
        sel.setdefault("site", set()).add((e.arg(4), e.arg(5)))
        kind, cls, mode = sel["tf"][unit]
        e.w32(e.arg(1), kind)
        e.w32(e.arg(2), cls)
        e.w32(e.arg(3), mode)
        return 0

    test._callee("local", 0x463DD0, 0, (), lambda _e: 7)
    test._callee("room", 0x620BB0, 1, (), lambda _e: sel["room"])
    test._callee("rooms", 0x619790, 3, (), rooms, nlog=3)
    test._callee("pump", 0x4565E0, 0, ("ecx",))
    test._callee("queue", 0x5FF2A0, 0, (), lambda _e: sel["queue"].pop(0) if sel["queue"] else 0x1E)
    test._callee("mode", 0x621190, 1, (), lambda _e: 0)
    test._callee("transform", 0x645270, 6, (), transform, nlog=1)
    test.callee("walk", 0x46FEB0, 1, ("eax",))
    test.callee("slot", 0x46FF50, 1, ("eax", "esi"))

    def build(img, rng):
        """Random rooms and units, the globals, the queue counts."""
        def put(addr, value):
            """Store an arena dword."""
            img[addr - test.arena:addr - test.arena + 4] = (value & rig.M32).to_bytes(4, "little")
        sel["room"] = rng.choice((0, 1, 1, 1))
        sel["count"] = rng.choice((0, 1, 2, 3, 3, 33, 40))
        sel["site"] = set()
        sel["queue"] = [rng.choice((0, 3, 0x1D, 0x1E, 0x1F)) for _ in range(40)]
        sel["tf"] = {}
        used = iter(unit_slots)
        for i in range(40):
            put(rooms_arr + 4 * i, room_slots[i % 3])
        for i, room in enumerate(room_slots):
            chain = [next(used) for _ in range(rng.choice((0, 1, 2)))] if i < 3 else []
            put(room + 0x74, chain[0] if chain else 0)
            for j, unit in enumerate(chain):
                put(unit + 0xE8, chain[j + 1] if j + 1 < len(chain) else 0)
                sel["tf"][unit] = (rng.choice((0, 1, 1, 2)), rng.choice((0xFFFFFFFF, 0, 1, 2, 5)), rng.choice((0xC, 0xC, 0, 1)))
        put(hdr + 0xA80, 3)
        test.m.w32(0x744304, hdr)
        test.m.w32(0x7A80FC, table)
        for cls in range(-1, 5):
            put(table + cls * 160 + 0x30, rng.choice((0, 1)))
        test.m.w32(0x7A8920, rng.choice((0, 1, 1)))
        test.m.w32(0x7A8928, rng.choice((0, 1)))
        test.m.w32(0x712AC0, rng.choice((0, 1, 3)))
        for i in range(8):
            test.m.w32(0x712AA0 + 4 * i, rng.randrange(0x20))
        return {"regs": {"eax": rng.choice((0, 1, 0x1234))}}

    def model(m, args):
        """The room / unit walk."""
        arg = args["regs"]["eax"]
        m.call("local")
        room = m.call("room", 7)
        if not room:
            return
        m.call("rooms", room, sel["p1"], sel["p2"])
        count = sel["count"]
        flag, glob = test.m.r32(0x7A8920), test.m.r32(0x7A8928)
        counter, i = 0, 0
        while i < count:
            if flag:
                if arg == 0 or glob != 0:
                    cur = counter
                    counter += 1
                    if cur & (0x3F if "pump" in MUT else 0x1F):
                        m.call("pump", 1)
            elif m.call("queue") >= 0x1E:
                return
            unit = m.r(m.r(rooms_arr + 4 * i) + 0x74)
            while unit:
                m.call("mode", unit)
                m.call("transform", unit)
                kind, cls, mode = sel["tf"][unit]
                if kind == 1:
                    if s32(cls) < m.r(hdr + 0xA80) and not (mode == 0xC and m.r(table + s32(cls) * 160 + 0x30)):
                        m.call("walk", unit, arg)
                elif kind == 0:
                    if glob:
                        arg = 0
                    for k in range(test.m.r32(0x712AC0)):
                        m.call("slot", test.m.r32(0x712AA0 + 4 * k), unit, arg if "arg" not in MUT else 0)
                if not flag and m.call("queue") >= (0x1F if "limit" in MUT else 0x1E):
                    return
                unit = m.r(unit + 0xE8)
            i += 1
    test.run("FUN_00470070", 0x470070, build, model, 0, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 1200
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("arg", "limit", "pump")}
    rig.run_suite(suite, mutations, "ENGINE COF rooms", args.mutation_check)


if __name__ == "__main__":
    main()
