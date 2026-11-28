# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Cain's final death think and persistent death effects in game.exe 1.14d.

    .venv/bin/python cain_death_effects.py [--exe PATH] [--dump ../../tests/cain_death_effects_case.inc] [--mutation-check]

FUN_005e7880's portal-entry branch takes stat 6, requests DD and returns without an
idle; its portal-made branch idles 20 frames. FUN_005a6520 runs for real over a
synthetic unit, including FUN_00639fb0 and FUN_00649f70. The latter's grid leaves
are hooked so the oracle records the 3x3 0x8000 corpse stamp without constructing
a room collision grid. d2d: World::cain_step and test_cain_death_effects.
"""
import argparse
import hashlib
import os
import struct
import sys
from pathlib import Path

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("keep-life", "idle-one", "keep-flags", "keep-states", "grid-two", "grid-bit")
UNIT_X, UNIT_Y = 20, 30
INITIAL_FLAGS = 0xDEADBEEF
mutation = None


def check_final_think(exe):
    """Run FUN_005e7880's portal-made and final-entry branches; return their logs."""
    machine = emu.Emu(exe)
    game, unit, command = machine.alloc(0x200), machine.alloc(0x100), machine.alloc(0x40)
    log = []
    portal_result = {"made": True}

    machine.w32(unit + 4, 0x92)
    machine.w32(unit + 0x10, 1)
    machine.hook(0x5E77A0, lambda _current: 0, 0)
    machine.hook(0x572DC0, lambda _current: 0, 0)
    machine.hook(0x58EEF0, lambda _current: command, 1)
    machine.hook(0x5E68F0, lambda _current: log.append("heed") or 0, 0)
    machine.hook(0x5DC5C0, lambda _current: 1, 0)
    machine.hook(0x5943B0, lambda _current: log.append("portal_make") or int(portal_result["made"]), 0)

    def portal_coordinates(current):
        """Return Cain's portal coordinates through FUN_00594450's output pair."""
        machine.w32(current.arg(0), 11)
        machine.w32(current.arg(0) + 4, 12)
        log.append("portal_coordinates")
        return 1

    machine.hook(0x594450, portal_coordinates, 1)
    machine.hook(0x5944F0, lambda _current: log.append("portal_entered") or 0, 0)
    machine.hook(0x627260, lambda current: log.append(("stat", current.arg(1), current.arg(2))) or 0, 4)
    machine.hook(0x45ADF0, lambda _current: UNIT_X, 0)
    machine.hook(0x45AE20, lambda _current: UNIT_Y, 0)
    machine.hook(0x5DDFC0, lambda current: log.append(("mode", current.arg(0))) or 0, 3)
    machine.hook(0x5DE080, lambda current: log.append(("idle", current.arg(0))) or 0, 1)

    machine.w32(command + 0x14, 1)
    machine.w32(command + 0x18, 6)
    machine.call(0x5E7880, 0, ecx=game, edx=unit, limit=10000)
    expected_idle = 1 if mutation == "idle-one" else 20
    want_portal = ["heed", "portal_make", ("idle", expected_idle)]
    if log != want_portal:
        raise AssertionError(f"FUN_005e7880 portal-made branch: game.exe {log}, model {want_portal}")

    log.clear()
    machine.w32(command + 0x14, 7)
    machine.w32(command + 0x18, 0)
    machine.call(0x5E7880, 0, ecx=game, edx=unit, limit=10000)
    want_final = ["heed", "portal_coordinates", "portal_entered"]
    if mutation != "keep-life":
        want_final.append(("stat", 6, 0))
    want_final.append(("mode", 0xC))
    if log != want_final:
        raise AssertionError(f"FUN_005e7880 final branch: game.exe {log}, model {want_final}")
    return log


def check_death_start(exe):
    """Run FUN_005a6520 with its state and corpse-grid functions real."""
    machine = emu.Emu(exe)
    game, unit, path = machine.alloc(0x200), machine.alloc(0x100), machine.alloc(0x100)
    tables, state_mask = machine.alloc(0x200), machine.alloc(8)
    stat_block, state_bits = machine.alloc(0x100), machine.alloc(16)
    room = machine.alloc(0x20)
    first = (0xF0F0F0F0, 0x12345678)
    second = (0x01020304, 0xA0B0C0D0)
    keep = (0x0FF00FF0, 0x00FF00FF)
    machine.w32(0x744304, tables)
    machine.w32(tables + 0xC4, 64)
    machine.w32(tables + 0x104, state_mask)
    machine.mu.mem_write(state_mask, struct.pack("<II", *keep))
    machine.w32(unit, 1)
    machine.w32(unit + 4, 0x92)
    machine.w32(unit + 0x2C, path)
    machine.w32(unit + 0x5C, stat_block)
    machine.w32(unit + 0xC4, INITIAL_FLAGS)
    machine.w32(stat_block + 0x10, 0x80000000)
    machine.w32(stat_block + 0x58, state_bits)
    machine.mu.mem_write(state_bits, struct.pack("<IIII", *first, *second))
    machine.w32(path + 0x1C, room)
    machine.mu.mem_write(path + 2, struct.pack("<H", UNIT_X))
    machine.mu.mem_write(path + 6, struct.pack("<H", UNIT_Y))
    calls = []
    machine.hook(0x58F6C0, lambda _current: calls.append("leader") or 0, 0)
    machine.hook(0x5B1A90, lambda _current: calls.append("targets") or 0, 0)
    machine.hook(0x627540, lambda _current: calls.append("drop_lists") or 0, 1)
    machine.hook(0x63E9F0, lambda _current: 0, 2)
    machine.hook(0x63A320, lambda _current: 0, 1)
    machine.hook(0x4638A0, lambda _current: 0, 0)
    machine.hook(0x64C040, lambda _current: calls.append("refresh") or 0, 1)
    machine.hook(0x649560, lambda current: calls.append(("lift", current.arg(0), current.arg(1))) or 0, 2)
    machine.hook(0x64DE30, lambda current: calls.append(("grid",) + tuple(current.arg(index) for index in range(6))) or 0, 6)
    machine.hook(0x650590, lambda current: calls.append(("path_refresh", current.arg(0))) or 0, 1)
    machine.hook(0x6488A0, lambda _current: calls.append("facing") or 0, 2)
    machine.hook(0x5738D0, lambda _current: calls.append("events") or 0, 0)
    machine.call(0x5A6520, 0, regs={"ebx": game, "esi": unit}, limit=20000)

    expected_flags = INITIAL_FLAGS if mutation == "keep-flags" else INITIAL_FLAGS & 0xFFFF7FF3
    expected_first = first if mutation == "keep-states" else tuple(value & mask for value, mask in zip(first, keep))
    expected_second = second if mutation == "keep-states" else tuple(old | (~mask & value & 0xFFFFFFFF) for old, value, mask in zip(second, first, keep))
    grid_width = 2 if mutation == "grid-two" else 3
    grid_bit = 0x4000 if mutation == "grid-bit" else 0x8000
    expected_grid = ("grid", room, UNIT_X, UNIT_Y, grid_width, 3, grid_bit)
    grids = [call for call in calls if isinstance(call, tuple) and call[0] == "grid"]
    seen_states = struct.unpack("<IIII", machine.read(state_bits, 16))
    want_states = (*expected_first, *expected_second)
    if machine.r32(unit + 0xC4) != expected_flags or seen_states != want_states or grids != [expected_grid]:
        raise AssertionError(f"FUN_005a6520: flags {machine.r32(unit + 0xC4):#x}, states {seen_states}, grid {grids}; "
                             f"model {expected_flags:#x}, {want_states}, {[expected_grid]}")
    return grid_width, 3, grid_bit


def run(exe):
    """Run the final-think and death-start checks."""
    check_final_think(exe)
    return check_death_start(exe)


def write_case(path, result):
    """Write the corpse-grid dimensions for test_cain_death_effects."""
    width, height, bit = result
    lines = ["// Generated by tools/emu/cain_death_effects.py --dump: FUN_005a6520 calls",
             "// FUN_00649f70(unit, 1), which stamps this corpse rectangle at the unit's subtile.",
             f"{width}, {height}, {bit:#06x}"]
    path.write_text("\n".join(lines) + "\n")


def main():
    """Parse arguments, run the oracle, optionally dump and check mutations."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=Path(os.environ.get("D2_MPQ_DIR", ".")) / "bin" / "game.exe")
    parser.add_argument("--dump", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    result = run(str(args.exe))
    print("ok: Cain final life/DD, 20-frame idle, death flags/states and 3x3 corpse grid")
    if args.dump:
        write_case(args.dump, result)
        print(f"wrote {args.dump}")
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(str(args.exe))
            except AssertionError:
                print(f"caught: {name}")
            else:
                print(f"NOT CAUGHT: {name}")
                return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
