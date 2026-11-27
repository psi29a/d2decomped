# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Cain's mode-0xc death start, FUN_005a6520, in game.exe 1.14d.

    .venv/bin/python cain_death_start.py [--exe /path/to/game.exe] [--dump ../../tests/cain_death_start_cases.inc] [--mutation-check]

FUN_005a6520 (direction on the stack, game in EBX, unit in ESI) runs for real on synthetic
game, unit, MonStats and path records, its leaves hooked and logged in call order: the aura
freed (FUN_006611a0, FUN_0064c040, unit +0xc4 | 0x100), the leader told (FUN_0058f6c0), the
unit taken off targets (FUN_005b1a90), +0xc4 &= 0xffff7ff3, FUN_00627540, the boss row lookup
and the death states (FUN_0063e9f0, FUN_00639fb0), the corpse (FUN_0063a320: +0xc4 &= ~2), the
corpse grid unless the composite says not (FUN_004638a0, FUN_00649f70(unit, 1)); then
FUN_006488a0 and FUN_005738d0 run for real: path +0x64 / +0x65 = direction & 0x3f, events 2
and 3 dropped.
Then Tristram Cain's path end to end: FUN_005ddfc0(0xc) and FUN_005de080(1), as FUN_005e77a0
asks them, with FUN_005a7e60, FUN_005a7c20, FUN_005a78a0, the DD start (0x5a7390: FUN_005a6520,
mode 12, events 8 and 9 dropped), FUN_005a6520 and the NU start (0x5a73e0) all run for real,
their leaves hooked. FUN_005a7e60 zeroes the request's +0x14 (only +0x15 gets 100), so the
death start's direction is 0: he's left facing 0 and NU keeps it.
d2d: World::cain_step, tests/game/test_cain_death_start.cpp, which reads the facing `--dump`
writes. Without --exe, $D2_MPQ_DIR/bin/game.exe.
`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import os
import sys
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu
from quest_cain_first_think import REQUEST_LEAVES

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("facing-unmasked", "keep-unit-flags", "skip-corpse-flag", "skip-collision", "skip-timers",
             "wrong-boss-row", "chain-direction-100", "chain-no-dd-drops")
INITIAL_FLAGS = 0xDEADBEEF
CLASS = 0x92                      # Tristram Cain
POOL = 0xABCDEF                   # the game's memory pool, +0x1c
START_FACING = 0x2A               # any facing other than the result
UNIT_X, UNIT_Y, FRAME = 20, 30, 100
mutation = None


def expected_case(aura, valid_class, corpse, composite, row, game, unit, direction):
    """FUN_005a6520's expected call log, unit flags and path facing."""
    calls = []
    if aura:
        calls += [("chat_free", POOL, aura), ("refresh", unit)]
    boss_row = row if valid_class and mutation != "wrong-boss-row" else 0
    calls += [("leader_died", unit), ("target_remove", game, unit), ("drop_unstated", unit),
              ("boss", boss_row, unit), ("states_death", unit), ("corpse", unit), ("composite_death",)]
    if not composite and mutation != "skip-collision":
        calls.append(("corpse_grid", unit, 1))
    if mutation != "skip-timers":
        calls += [("drop_event", 2, 0), ("drop_event", 3, 0)]
    flags = INITIAL_FLAGS | (0x100 if aura else 0)
    if mutation != "keep-unit-flags":
        flags &= 0xFFFF7FF3
    if corpse and mutation != "skip-corpse-flag":
        flags &= 0xFFFFFFFD
    facing = direction if mutation == "facing-unmasked" else direction & 0x3F
    return calls, flags, facing


def hook_death_leaves(machine, log, corpse=False, composite=False, boss=0):
    """Hook FUN_005a6520's leaves on `machine`, each logging into `log`."""
    def registers(current):
        """ECX and EDX at the hooked call."""
        return current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)

    machine.hook(0x6611A0, lambda current: log.append(("chat_free", current.arg(0), current.arg(1))) or 0, 2)
    machine.hook(0x64C040, lambda current: log.append(("refresh", current.arg(0))) or 0, 1)
    machine.hook(0x58F6C0, lambda current: log.append(("leader_died", registers(current)[0])) or 0, 0)
    machine.hook(0x5B1A90, lambda current: log.append(("target_remove", *registers(current))) or 0, 0)
    machine.hook(0x627540, lambda current: log.append(("drop_unstated", current.arg(0))) or 0, 1)
    machine.hook(0x63E9F0, lambda current: log.append(("boss", current.arg(0), current.arg(1))) or boss, 2)
    machine.hook(0x639FB0, lambda current: log.append(("states_death", current.arg(0))) or 0, 2)
    machine.hook(0x63A320, lambda current: log.append(("corpse", current.arg(0))) or int(corpse), 1)
    machine.hook(0x4638A0, lambda _current: log.append(("composite_death",)) or int(composite), 0)
    machine.hook(0x649F70, lambda current: log.append(("corpse_grid", current.arg(0), current.arg(1))) or 0, 2)
    machine.hook(0x540E60, lambda current: log.append(("drop_event", current.arg(0), current.arg(1))) or 0, 2)


def check_death_start(exe):
    """Run FUN_005a6520 across its branches and directions; returns the case count."""
    cases = 0
    for aura in (0, 0x123456):
        for valid_class in (False, True):
            for corpse in (False, True):
                for composite in (False, True):
                    for direction in (0, 100, 0xFF):
                        machine = emu.Emu(exe)
                        game, unit, path = machine.alloc(0x200), machine.alloc(0x100), machine.alloc(0x100)
                        tables, rows_base = machine.alloc(0xB00), machine.alloc(0x1A8 * 8)
                        row = rows_base + 7 * 0x1A8
                        machine.w32(0x744304, tables)
                        machine.w32(tables + 0xA78, rows_base)
                        machine.w32(tables + 0xA80, 8)
                        machine.w32(game + 0x1C, POOL)
                        machine.w32(unit + 4, 7 if valid_class else 9)
                        machine.w32(unit + 0x2C, path)
                        machine.w32(unit + 0xA4, aura)
                        machine.w32(unit + 0xC4, INITIAL_FLAGS)
                        machine.mu.mem_write(path + 0x64, bytes([START_FACING, START_FACING]))
                        log = []
                        hook_death_leaves(machine, log, corpse, composite)
                        machine.call(0x5A6520, direction, regs={"ebx": game, "esi": unit}, limit=10000)
                        want_log, want_flags, want_facing = expected_case(aura, valid_class, corpse, composite, row, game, unit, direction)
                        seen = (log, machine.r32(unit + 0xC4), machine.r32(unit + 0xA4), *machine.read(path + 0x64, 2))
                        want = (want_log, want_flags, 0, want_facing, want_facing)
                        if seen != want:
                            raise AssertionError(f"FUN_005a6520 aura={aura != 0} class={valid_class} corpse={corpse} "
                                                 f"composite={composite} dir={direction}: game.exe {seen}, model {want}")
                        cases += 1
    return cases


def check_chain(exe, rows=None):
    """Run Cain's DD then NU requests with the mode starts and FUN_005a6520 for real; returns 1."""
    machine = emu.Emu(exe)
    game, unit, path = machine.alloc(0x200), machine.alloc(0x100), machine.alloc(0x100)
    tables, monstats = machine.alloc(0x1000), machine.alloc(0x1A8 * 0x100)
    machine.w32(game + 0xA8, FRAME)
    machine.w32(0x744304, tables)               # MonStats rows, none with the +0x1a5 mode override
    machine.w32(tables + 0xA78, monstats)
    machine.w32(tables + 0xA80, 0x100)
    machine.w32(unit, 1)
    machine.w32(unit + 4, CLASS)
    machine.w32(unit + 0x10, 1)                 # NU, as the think finds him
    machine.w32(unit + 0x2C, path)
    machine.mu.mem_write(path + 0x64, bytes([START_FACING, START_FACING]))
    log = []

    def set_mode(current):
        """Hook FUN_00553570 (free combat, set the anim mode): log it and write unit +0x10."""
        log.append(("mode", current.arg(0)))
        machine.w32(unit + 0x10, current.arg(0))
        return 0

    machine.stub(0, REQUEST_LEAVES)
    hook_death_leaves(machine, log)
    machine.hook(0x5A68E0, lambda _current: log.append("leave") or 0, 0)
    machine.hook(0x553570, set_mode, 1)
    machine.hook(0x5417D0, lambda current: log.append(("event", current.arg(0), current.arg(1))) or 0, 4)
    machine.hook(0x5A7B30, lambda _current: log.append("refused") or 0, 1)
    machine.call(0x5DDFC0, 0xC, UNIT_X, UNIT_Y, ecx=game, edx=unit, limit=40000)
    machine.call(0x5DE080, 1, ecx=game, edx=unit, limit=40000)
    facing = tuple(machine.read(path + 0x64, 2))
    death = [("leader_died", unit), ("target_remove", game, unit), ("drop_unstated", unit),
             ("boss", monstats + CLASS * 0x1A8, unit), ("states_death", unit), ("corpse", unit),
             ("composite_death",), ("corpse_grid", unit, 1), ("drop_event", 2, 0), ("drop_event", 3, 0)]
    want = death + [("mode", 0xC)]
    if mutation != "chain-no-dd-drops":
        want += [("drop_event", 8, 0), ("drop_event", 9, 0)]
    want += ["leave", ("mode", 1), ("drop_event", 2, 0), ("event", 2, FRAME + 1)]
    want_facing = (36, 36) if mutation == "chain-direction-100" else (0, 0)
    seen = (log, facing, machine.r32(unit + 0x10))
    if seen != (want, want_facing, 1):
        raise AssertionError(f"Cain DD then NU: game.exe {seen}, model {(want, want_facing, 1)}")
    if rows is not None:
        rows.append(facing[0])
    return 1


def run(exe, rows=None):
    """Both checks on fresh machines; returns the case count."""
    return check_death_start(exe) + check_chain(exe, rows)


def write_cases(path, rows):
    """Write game.exe's facing after Cain's DD and NU for tests/game/test_cain_death_start.cpp."""
    lines = ["// tools/emu/cain_death_start.py --dump: game.exe's path facing (+0x64, 64 steps) after",
             "// FUN_005ddfc0(0xc) and FUN_005de080(1) for Tristram Cain (class 0x92), FUN_005a7c20,",
             f"// both mode starts and FUN_005a6520 run for real, starting from facing {START_FACING}."]
    lines += [f"{fine}," for fine in rows]
    path.write_text("\n".join(lines) + "\n")


def main():
    """Parse the arguments, verify game.exe, run, optionally dump rows and run the mutations."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=Path(os.environ.get("D2_MPQ_DIR", ".")) / "bin" / "game.exe")
    parser.add_argument("--dump", type=Path, help="write the facing row (tests/cain_death_start_cases.inc)")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    rows = []
    print(f"ok: {run(str(args.exe), rows)} Cain death-start cases (FUN_005a6520, 0x5a7390, 0x5a73e0)")
    if args.dump:
        write_cases(args.dump, rows)
        print(f"wrote {len(rows)} row to {args.dump}")
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
