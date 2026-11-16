# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the five live 0x558xxx/0x559xxx ITEMS lifecycle routines.

    python items_lifecycle_live.py [--mutation-check]

Runs FUN_00558270, FUN_005585d0, FUN_00558b90, FUN_00558cb0 and
FUN_00559130 from game.exe on synthetic units.  Accessors and subsystem
boundaries are hooked and logged; item/create-record writes are real.
"""
import argparse

import emu

LIMIT = 200000
mutation = None


def need(condition, message):
    if not condition:
        raise AssertionError(message)


def item_descriptor():
    """FUN_00558270 copies every creation field and selects gold/stack/name branches."""
    machine = emu.Emu()
    item, data, path, output, name = (machine.alloc(size) for size in (0x100, 0x80, 0x40, 0x84, 0x20))
    unit = machine.view("unit", item)
    unit.type, unit.data, unit.path, unit["class"], unit.mode = 4, data, path, 77, 3
    machine.w32(path + 0x0C, 12)
    machine.w32(path + 0x10, 34)
    machine.w32(item + 0x28, 0x55667788)
    machine.mu.mem_write(name, b"Kasim\0")
    state = {"level": 0, "quality": 2, "category": 4, "stat": 12345, "stack": 0,
             "seed": 0x11112222, "file": 9, "flags": 0x33334444, "dur": 55, "maxdur": 66}
    machine.hook(0x62A670, lambda current: 0x60, 1)
    machine.hook(0x6281E0, lambda current: state["level"], 1)
    machine.hook(0x627E70, lambda current: state["quality"], 1)
    machine.hook(0x62B400, lambda current: state["category"], 1)
    machine.hook(0x6289F0, lambda current: state["stack"], 1)
    machine.hook(0x625480, lambda current: state["stat"], 3)
    machine.hook(0x627DF0, lambda current: state["seed"], 1)
    machine.hook(0x629DA0, lambda current: state["file"], 1)
    machine.hook(0x628110, lambda current: state["flags"], 1)
    machine.hook(0x6253B0, lambda current: state["maxdur"], 3)
    machine.hook(0x628340, lambda current: name, 1)
    machine.hook(0x6283A0, lambda current: 6, 1)
    machine.hook(0x6280A0, lambda current: state["flags"] & current.arg(1), 4)
    count = 0
    for category, quality, stackable, quantity, wanted in ((4, 2, 0, 12345, 12345),
                                                            (1, 7, 1, 27, 27),
                                                            (7, 7, 0, 0, 0)):
        state.update(category=category, quality=quality, stack=stackable, stat=quantity)
        machine.mu.mem_write(output, b"\xa5" * 0x84)
        machine.call(0x558270, item, ecx=0xABCDEF01, edx=output, limit=LIMIT)
        need(machine.r32(output + 8) == 0xABCDEF01 and machine.r32(output + 0x0C) == 1,
             "00558270 game/level")
        need(machine.r32(output + 0x14) == 77 and machine.r32(output + 0x18) == 3,
             "00558270 class/mode")
        need((machine.r32(output + 0x1C), machine.r32(output + 0x20)) == (12, 34),
             "00558270 coordinates")
        expected = wanted + (1 if mutation == "descriptor-quantity" else 0)
        need(machine.r32(output + 0x34) == expected, "00558270 quantity")
        need(machine.r16(output + 0x28) == 1 and machine.r32(output + 0x2C) == 0,
             "00558270 constants")
        if category == 7:
            need(machine.cstr(output + 0x58) == "Kasim" and machine.read(output + 0x50, 1)[0] == 6,
                 "00558270 ear")
        count += 1
    return count


def magic_find():
    """FUN_005585d0 accepts unit types 0/1 and sums killer and owner stat 0x50."""
    machine = emu.Emu()
    killer, owner = machine.alloc(8), machine.alloc(8)
    stats, owner_ptr, asked = {killer: 40, owner: 25}, [owner], []

    def stat(current):
        asked.append((current.arg(0), current.arg(1), current.arg(2)))
        return stats[current.arg(0)]

    machine.hook(0x625500, stat, 3)
    machine.hook(0x58F0D0, lambda current: owner_ptr[0], 0)
    count = 0
    for kind, linked, wanted in ((0, owner, 65), (1, 0, 40), (2, owner, 0), (4, owner, 0)):
        machine.w32(killer, kind)
        owner_ptr[0] = linked
        asked.clear()
        got = machine.call(0x5585D0, regs={"esi": killer}, limit=LIMIT)
        expected = wanted + (1 if mutation == "magic-find-sum" else 0)
        need(got == expected, f"005585d0 {kind}/{linked:#x}")
        if kind < 2:
            need(asked[0] == (killer, 0x50, 0), "005585d0 killer stat")
        count += 1
    return count


def expire_ground_items():
    """FUN_00558b90 walks all rooms, saves next before deletion and expires due items."""
    machine = emu.Emu()
    game, act, room, first, second, data1, data2 = (machine.alloc(size) for size in
                                                    (0x200, 8, 0x100, 0x100, 0x100, 0x40, 0x40))
    machine.w32(game + 0xBC, act)
    machine.w32(game + 0xA8, 100)
    machine.w32(room + 0x74, first)
    machine.w32(first + 0xE8, second)
    machine.w32(second + 0xE8, 0)
    for unit, data, deadline in ((first, data1, 100), (second, data2, 101)):
        machine.w32(unit, 4)
        machine.w32(unit + 0x14, data)
        machine.w32(unit + 0x0C, 7)
        machine.w32(unit + 0xC4, 0xFFFFFFFF)
        machine.w32(data + 0x24, deadline)
    calls = []
    machine.hook(0x61A180, lambda current: room, 1)
    machine.hook(0x620BB0, lambda current: 0x1234, 1)
    for address, stack, name in ((0x61A270, 3, "room-del"), (0x623830, 1, "path-del"),
                                 (0x64C370, 1, "collide-del"), (0x5538D0, 1, "drop-list"),
                                 (0x555600, 0, "destroy")):
        machine.hook(address, lambda current, s=stack, n=name: calls.append((n,) + tuple(current.arg(i) for i in range(s))) or 0, stack)
    machine.call(0x558B90, ecx=game, edx=0, limit=LIMIT)
    need(machine.r32(first + 0xC4) == 0xFFFFFFFD and machine.r32(second + 0xC4) == 0xFFFFFFFF,
         "00558b90 deadline/flag")
    wanted_names = ["room-del", "path-del", "collide-del", "drop-list", "destroy"]
    if mutation == "expiry-order":
        wanted_names[1], wanted_names[2] = wanted_names[2], wanted_names[1]
    need([entry[0] for entry in calls] == wanted_names, f"00558b90 calls {calls}")
    return 2


def load_saved_item():
    """FUN_00558cb0 validates class, creates, reads, flags and schedules; failure destroys."""
    machine = emu.Emu()
    descriptor, output, count_ptr, item = (machine.alloc(size) for size in (0x40, 4, 4, 0x100))
    machine.w32(descriptor, 2)
    machine.w32(descriptor + 4, 3)
    machine.w32(descriptor + 8, 4)
    machine.w32(count_ptr, 10)
    read_result, read_error, calls = [7], [0], []
    machine.hook(0x633590, lambda current: count_ptr, 0)
    machine.hook(0x555230, lambda current: calls.append(("create",) + tuple(current.arg(i) for i in range(7))) or item, 7)

    def read(current):
        machine.w32(current.arg(6), read_error[0])
        calls.append(("read",) + tuple(current.arg(i) for i in range(6)))
        return read_result[0]

    machine.hook(0x62E430, read, 7)
    machine.hook(0x6280D0, lambda current: calls.append((f"flag{current.arg(1):x}={current.arg(2)}",)) or 0, 3)
    for address, stack, name in ((0x558530, 0, "dur"), (0x558580, 0, "qty"),
                                 (0x555600, 0, "destroy")):
        machine.hook(address, lambda current, s=stack, n=name: calls.append((n,) + tuple(current.arg(i) for i in range(s))) or 0, stack)
    cases = 0
    for result, error, wanted in ((7, 0, item), (0, 0, 0), (7, 1, 0)):
        read_result[0], read_error[0] = result, error
        calls.clear()
        got = machine.call(0x558CB0, 11, 12, 13, descriptor, output, 14,
                           ecx=1, edx=2, limit=LIMIT)
        need(got == wanted and machine.r32(output) == result, "00558cb0 result/output")
        names = [entry[0] for entry in calls]
        if wanted:
            expected = ["create", "read", "flag80000=1", "flag2000=0", "dur", "qty"]
            if mutation == "load-flags":
                expected[2], expected[3] = expected[3], expected[2]
            need(names == expected, f"00558cb0 success {calls}")
        else:
            need(names[-1] == "destroy", f"00558cb0 failure {calls}")
        cases += 1
    return cases


def ground_creation():
    """FUN_00559130 applies offset/placement, caps level and copies the 0x84-byte block."""
    machine = emu.Emu()
    game, room, point, output = (machine.alloc(size) for size in (0x100, 0x100, 8, 0x84))
    machine.w32(game + 0x70, 1)
    machine.mu.mem_write(game + 0x6D, b"\x02")
    machine.mu.mem_write(game + 0x78, b"\x60\x00")
    machine.w32(point, 10)
    machine.w32(point + 4, 20)
    calls, placement = [], [room]
    machine.hook(0x61A1B0, lambda current: 8, 1)
    machine.hook(0x61DCA0, lambda current: 99, 3)  # d2d_ai_area_monster_level
    machine.hook(0x556240, lambda current: 0x20786F62, 5)
    machine.hook(0x463740, lambda current: 1, 1)

    def place(current):
        calls.append(("place", current.arg(0), machine.r32(current.arg(1)), machine.r32(current.arg(1) + 4),
                      current.arg(3), current.arg(4), current.arg(5), current.arg(6)))
        return placement[0]

    machine.hook(0x64E810, place, 7)
    machine.hook(0x558D90, lambda current: calls.append(("create", current.arg(0))) or 0, 1)
    machine.stub(0, {0x681A48: 0})
    machine.call(0x559130, point, output, ecx=game, edx=room, limit=LIMIT)
    need(calls[0][2:4] == ((13 if mutation == "ground-offset" else 12), 23), f"00559130 offset {calls}")
    need(machine.r32(output + 0x0C) == 64 and machine.r32(output + 0x18) == 3,
         "00559130 level/mode")
    need(machine.r32(output + 0x1C) == 12 and machine.r32(output + 0x20) == 23,
         "00559130 copied position")
    placement[0] = 0
    calls.clear()
    machine.mu.mem_write(output, b"\xa5" * 0x84)
    machine.call(0x559130, point, output, ecx=game, edx=room, limit=LIMIT)
    need(not any(entry[0] == "create" for entry in calls) and machine.read(output, 4) == b"\xa5" * 4,
         "00559130 placement failure")
    return 2


CHECKS = (item_descriptor, magic_find, expire_ground_items, load_saved_item, ground_creation)
MUTATIONS = ("descriptor-quantity", "magic-find-sum", "expiry-order", "load-flags", "ground-offset")


def run_all():
    return sum(check() for check in CHECKS)


def main():
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutation = None
    total = run_all()
    print(f"ok: {total} lifecycle ITEMS scenarios over 5 functions")
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run_all()
            except AssertionError:
                print(f"mutation caught: {name}")
            else:
                raise AssertionError(f"mutation survived: {name}")


if __name__ == "__main__":
    main()
