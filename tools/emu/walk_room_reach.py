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
import math

import emu

RECTS = ((100, 100, 40, 40), (0, 0, 8, 8), (500, 300, 1, 1))   # room +0x4c: x, y, w, h
RADII = (-50, -5, -1, 0, 1, 4, 20, 50)

def check_pulse_boundary(machine):
    """Check the squared distance used by FUN_0056b7e0 for the pulse fixtures."""
    cases = (
        ((0, 0), (4, 0), 16, True),
        ((0, 0), (5, 0), 25, True),
        ((0, 0), (6, 0), 36, False),
        ((0.1, 0), (5.9, 0), 25, True),
        ((0.9, 0.9), (4, 4), 32, False),
    )
    for caster, target, expected_distance, expected_hit in cases:
        coordinates = [50 + math.floor(value) for value in (*caster, *target)]
        distance = machine.call(0x6492A0, *coordinates)
        # LAB_0056b954 rejects distances strictly greater than radius squared.
        assert (distance, distance <= 5 * 5) == (expected_distance, expected_hit)
    return len(cases)

def check_unit_walk(exe):
    """Run the original walk, coordinate getters and distance test with one target."""
    machine = emu.Emu(exe)
    game, caster, target = (machine.alloc(0x100) for _ in range(3))
    caster_path, target_path = (machine.alloc(0x100) for _ in range(2))
    room, rooms = machine.alloc(0x100), machine.alloc(4)
    callback = 0x5AB2A0
    machine.w32(caster + 0x2C, caster_path)
    machine.w32(target + 0x2C, target_path)
    machine.w32(target, 1)
    machine.w32(room + 0x74, target)
    machine.w32(rooms, room)
    selected = []

    def room_list(engine):
        engine.w32(engine.arg(1), rooms)
        engine.w32(engine.arg(2), 1)
        return 0

    def accept(engine):
        selected.append(engine.mu.reg_read(emu.UC_X86_REG_EDX))
        return 1

    machine.hook(0x620BB0, lambda engine: room, 1)
    machine.hook(0x619790, room_list, 3)
    machine.hook(0x56B770, lambda engine: 1, 0)
    machine.hook(0x56B3E0, lambda engine: 1, 1)
    machine.hook(callback, accept, 0)
    cases = 0
    for origin in (50, 5000):
        for caster_xy, target_xy, expected in (
                ((0, 0), (4, 0), True), ((0, 0), (5, 0), True),
                ((0, 0), (6, 0), False), ((0.1, 0), (5.9, 0), True),
                ((0.9, 0.9), (4, 4), False)):
            for path, position in ((caster_path, caster_xy), (target_path, target_xy)):
                for offset, value in zip((0, 4), position):
                    machine.w32(path + offset, int((origin + value) * 65536))
            for explicit in (False, True):
                selected.clear()
                centre = [origin + math.floor(value) for value in caster_xy] if explicit else [0, 0]
                machine.call(0x56B7E0, *centre, 5, 1, callback, 0, 0, 0, 0, ecx=game, edx=caster)
                assert selected == ([target] if expected else []), (origin, caster_xy, target_xy, explicit, selected)
                cases += 1
    return cases


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
    boundary_cases = check_pulse_boundary(e)
    walk_cases = check_unit_walk(args.exe)
    print(f"ok: {walk_cases} complete FUN_0056b7e0 walks with original coordinate getters")
    print(f"ok: {boundary_cases} FUN_006492a0 aura pulse boundary cases")
    print(f"ok: {cases} FUN_0056b770 cases, 0 rooms refused at a radius >= 0, {refused[False]} at a negative one; the walk stops before any room at radius 0 and -1")


if __name__ == "__main__":
    main()
