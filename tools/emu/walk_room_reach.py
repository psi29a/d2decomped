# SPDX-License-Identifier: GPL-3.0-or-later
"""The area walk's per-room reach test (FUN_0056b770) refuses no room: bugs.md #114.

    uv run python walk_room_reach.py

FUN_0056b770 (ECX the room, EDX x, EAX y, EDI the radius, subtiles) runs for
real with FUN_00619730, which copies the room's rect from +0x4c (x, y, w, h).
Its tests pair `x + r < left` with `x - r > right` (and likewise for y), which
can't both hold for r >= 0, so it returns 1 for every point and radius >= 0.
Here it runs over three rooms, points on a 7-subtile grid 120 subtiles round
each, and radii -50..50: 0 comes back only for a negative radius. Its caller,
the walk FUN_0056b7e0, returns at 0x56b809 before any room for a radius <= 0
(run here too: it never asks for the unit's room, FUN_00620bb0), so in play
every room of the near list is walked.
"""
import argparse

import emu

RECTS = ((100, 100, 40, 40), (0, 0, 8, 8), (500, 300, 1, 1))   # room +0x4c: x, y, w, h
RADII = (-50, -5, -1, 0, 1, 4, 20, 50)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe")
    args = parser.parse_args()
    e = emu.Emu(args.exe) if args.exe else emu.Emu()
    room = e.alloc(0x100)
    refused, cases = {True: 0, False: 0}, 0
    for rect in RECTS:
        for k, value in enumerate(rect):
            e.w32(room + 0x4C + 4 * k, value)
        left, top, width, height = rect
        for radius in RADII:
            for x in range(left - 120, left + width + 121, 7):
                for y in range(top - 120, top + height + 121, 7):
                    got = e.call(0x56B770, ecx=room, edx=x & 0xFFFFFFFF, regs={"eax": y & 0xFFFFFFFF, "edi": radius & 0xFFFFFFFF})
                    assert got in (0, 1)
                    cases += 1
                    if got == 0:
                        refused[radius >= 0] += 1
    assert refused[True] == 0 and refused[False] > 0, refused
    asked = []
    e.hook(0x620BB0, lambda h: asked.append(h.arg(0)) or 0, 1)
    game, unit = e.alloc(0x100), e.alloc(0x100)
    for radius in (0, -1):
        e.call(0x56B7E0, 100, 200, radius & 0xFFFFFFFF, 0, 0x5AB2A0, e.alloc(0x20), 1, 0, 0, ecx=game, edx=unit)
    assert not asked
    e.call(0x56B7E0, 100, 200, 1, 0, 0x5AB2A0, e.alloc(0x20), 1, 0, 0, ecx=game, edx=unit)
    assert asked == [unit]                      # at radius 1 it asks (the hooked room is 0: it returns)
    print(f"ok: {cases} FUN_0056b770 cases, 0 rooms refused at a radius >= 0, {refused[False]} at a negative one; the walk stops before any room at radius 0 and -1")


if __name__ == "__main__":
    main()
