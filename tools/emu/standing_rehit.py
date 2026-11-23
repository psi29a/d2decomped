# SPDX-License-Identifier: GPL-3.0-or-later
"""Which unit a standing cloud strikes each frame, by game.exe's own code.

    uv run python standing_rehit.py [FRAMES] > game.txt

A srvdofunc 3 row (the standing clouds: Poison Explosion's, the javelin
clouds, Blizzard's shards, the Mummy's corpsepoisoncloud) steps through
FUN_005ae480 -> FUN_005ae1f0. Runs next to missiles.py (a Blood Moor game,
every room up). Per row, two monsters of one size-1 class stand on
neighbouring free subtiles inside the cloud's footprint (on the cloud's own
subtile and the next one east, both hostile), the row is made by the real
FUN_0059fa30 on the first and stepped by its own missile do for FRAMES
frames. FUN_005adf10 (the hit) is hooked to the part this asks about: a unit
the missile struck last (FUN_0064a470, 0x5adf9f) is passed by, any other is
struck and made the last (FUN_0064a400, 0x5adfae), as those two read and
write the missile's +0x14 record (+0x18 type, +0x1c id; every row here has
LastCollide but the burners').
The burners (srvdofunc 5, FUN_005ae520: Fire Wall's flames, Meteor's fire) step
through the same FUN_005ae1f0 and strike one unit, never both: Meteor's fire
(Size 1, both units in its footprint) takes B, the newer, its room list's head
(FUN_00641cb0). Only frame 1 strikes here: FUN_005ae1f0 searches only on the
path's contact flags (FUN_00648eb0), which these still units don't set again;
how often a burner strikes in play is not what this run shows.

Prints `S row frame unit` per strike, unit A (the first) or B, and `N row`
for a row that made no missile. d2d's rule (Fight::skill_missile_hits,
ai.hpp's monster missiles): one unit a frame, never the one struck last.
"""
import sys

import missiles

ROWS = {"firewall": 69, "meteorfire": 240, "poisonjavcloud": 39, "corpsepoisoncloud": 155, "blizzard1": 159, "poisonexplosioncloud": 217}   # Missiles.txt Id


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    if frames < 1:
        sys.exit("standing_rehit: FRAMES must be a positive integer")
    g = missiles.Game()
    e = g.e
    state = {"missile": 0, "units": {}}

    def on_hit(emu):
        unit = emu.arg(0)
        if not unit:
            return 1
        # FUN_0064a470 / FUN_0064a400 on a LastCollide row: the record at missile +0x14 keeps the
        # last unit's type (+0x18) and id (+0x1c, the unit's +0xc).
        record = emu.r32(state["missile"] + 0x14)
        kind, ident = emu.r32(unit), emu.r32(unit + 0xc)
        if record and emu.r32(record + 0x18) == kind and emu.r32(record + 0x1c) == ident:
            return 0
        if record:
            emu.w32(record + 0x18, kind)
            emu.w32(record + 0x1c, ident)
        state["struck"].append(state["units"].get(unit, "?"))
        return 2
    e.hook(0x5adf10, on_hit, 2)
    foes = set()
    e.hook(0x554200, lambda emu: int(emu.arg(0) in foes), 1)
    # Two neighbouring free subtiles, room for a size-1 unit each.
    spot = None
    for room in g.rooms:
        for y in range(room[2] + 4, room[2] + room[4] - 4):
            for x in range(room[1] + 4, room[1] + room[3] - 4):
                if all(g.collision(x + dx, y + dy) & 0x1f0f == 0 for dx in range(-3, 4) for dy in range(-3, 4)):
                    spot = (x, y)
                    break
            if spot:
                break
        if spot:
            break
    if not spot:
        raise SystemExit("standing_rehit: no free spot")
    for name, mid in ROWS.items():
        first = second = None
        for cls in range(0, 800):
            first = g.unit(cls, *spot)
            if first and first[3] == 1:
                second = g.unit(cls, spot[0] + 1, spot[1])
                if second:
                    break
        if not first or not second:
            raise SystemExit("standing_rehit: no size-1 pair")
        foes.clear()
        foes.update((first[0], second[0]))
        state["units"] = {first[0]: "A", second[0]: "B"}
        shooter = g.unit(63, spot[0] + 14, spot[1] + 14) or g.unit(63, spot[0] - 14, spot[1] - 14)
        p = e.alloc(0x60)
        for i, v in {0: 3, 1: shooter[0], 4: mid, 5: first[1], 6: first[2], 7: first[1], 8: first[2], 0xc: 1}.items():
            e.w32(p + 4 * i, v)
        m = e.call(0x59fa30, ecx=g.g, edx=p)
        if not m:
            print(f"N {name}")
            continue
        state["missile"] = m
        do = e.r32(0x73c768 + 4 * g.row(mid, 0xc, 2))
        for frame in range(1, frames + 1):
            state["struck"] = []
            e.call(do, ecx=g.g, edx=m)
            for unit in state["struck"]:
                print(f"S {name} {frame} {unit}")


if __name__ == "__main__":
    main()
