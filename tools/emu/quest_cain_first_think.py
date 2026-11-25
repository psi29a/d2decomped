# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Tristram Cain's first think FUN_005e77a0 and its mode steps in game.exe 1.14d.

    .venv/bin/python quest_cain_first_think.py [--exe /path/to/game.exe] [--mutation-check]

FUN_005e77a0 (fastcall: game in ECX, unit in EDX; its callees hooked and logged): no AI command 3
record (FUN_0058efa0) or one with +0xc / +0x10 set: 0, nothing done. Else the record's +0xc / +0x10
= the unit's x + 3 / y + 3 (FUN_0045adf0 / FUN_0045ae20), the spot note (FUN_005944b0; class 0x20f
FUN_0058a940); a note that returns 0 takes the take (FUN_005944f0; 0x20f FUN_0058a980), then
FUN_00627260(unit, 6, 0, 0) (life gone) and FUN_005ddfc0(game, unit, 0xc, x, y); then +0x14 = 1,
+0x18 = 0, idle 1 (FUN_005de080), 1. FUN_005ddfc0 run for real (FUN_005a7e60 too, its callees
hooked): FUN_005a7c20(game, request, 1) with request mode 0xc, the unit, no target unit, (x, y).
FUN_005de080 run for real: a unit out of mode 1 (mode 12 here) is asked for mode 1, the unit its
own target; one in mode 1 isn't; then AI event 2 at the game frame + n (0 counts as 1).
d2d: World::cain_step (CainWalk::lifeless), tests/game/test_cain_first_think.cpp.
Without --exe, $D2_MPQ_DIR/bin/game.exe.
`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import os
import sys
from pathlib import Path

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("no-life-cut", "life-cut-when-fresh", "mode-not-dd", "mode-target-swapped", "no-neutral-after", "idle-zero-frames")
UNIT_X, UNIT_Y, FRAME = 20, 30, 100
mutation = None


def model_think(record, busy, noted, alt):
    """FUN_005e77a0's (result, calls, record goal and stage) for a unit at (UNIT_X, UNIT_Y)."""
    if not record or busy:
        return 0, [], None
    note, take = ("note_alt", "take_alt") if alt else ("note", "take")
    calls = [note]
    if noted:                                   # the note returns 0: the take, life and mode 0xc
        calls.append(take)
        if mutation != "no-life-cut":
            calls.append(("stat", 6, 0, 0))
        calls.append(("mode", 0 if mutation == "mode-not-dd" else 0xC, UNIT_X, UNIT_Y))
    elif mutation == "life-cut-when-fresh":
        calls.append(("stat", 6, 0, 0))
    calls.append(("idle", 1))
    return 1, calls, (UNIT_X + 3, UNIT_Y + 3, 1, 0)


def check_think(machine):
    """Run FUN_005e77a0 over the record / note / class cases; returns the case count."""
    game, unit, record = machine.alloc(0x200), machine.alloc(0x100), machine.alloc(0x40)
    state = {"record": True, "noted": False}
    log = []

    def logged(name, value=0):
        """Make a hook that logs `name` and returns `value`."""
        def run(_current):
            """Log the call; return the canned value."""
            log.append(name)
            return value
        return run

    machine.hook(0x58efa0, lambda _current: record if state["record"] else 0, 2)
    machine.hook(0x45adf0, lambda _current: UNIT_X, 0)
    machine.hook(0x45ae20, lambda _current: UNIT_Y, 0)
    machine.hook(0x5944b0, lambda current: logged("note", 0 if state["noted"] else 1)(current), 0)
    machine.hook(0x58a940, lambda current: logged("note_alt", 0 if state["noted"] else 1)(current), 0)
    machine.hook(0x5944f0, logged("take"), 0)
    machine.hook(0x58a980, logged("take_alt"), 0)

    def stat(current):
        """Hook FUN_00627260 (set stat): check the unit, log (stat, value, param)."""
        if current.arg(0) != unit:
            raise AssertionError("FUN_00627260 not on the unit")
        log.append(("stat", current.arg(1), current.arg(2), current.arg(3)))
        return 0

    def mode(current):
        """Hook FUN_005ddfc0 (ask for a mode): check ECX/EDX, log (mode, x, y)."""
        regs = (current.mu.reg_read(emu.UC_X86_REG_ECX), current.mu.reg_read(emu.UC_X86_REG_EDX))
        if regs != (game, unit):
            raise AssertionError(f"FUN_005ddfc0 ECX/EDX {regs}")
        log.append(("mode", current.arg(0), current.arg(1), current.arg(2)))
        return 0

    machine.hook(0x627260, stat, 4)
    machine.hook(0x5ddfc0, mode, 3)
    machine.hook(0x5de080, lambda current: log.append(("idle", current.arg(0))) or 0, 1)
    cases = 0
    for has_record in (True, False):
        for busy in (False, True):
            for noted in (False, True):
                for alt in (False, True):
                    state.update(record=has_record, noted=noted)
                    log.clear()
                    machine.mu.mem_write(record, b"\0" * 0x40)
                    if busy:
                        machine.w32(record + 0xC, 5)
                    machine.w32(unit + 4, 0x20F if alt else 0x92)
                    got = machine.call(0x5E77A0, 0, ecx=game, edx=unit, limit=5000)
                    fields = tuple(machine.r32(record + off) for off in (0xC, 0x10, 0x14, 0x18))
                    expect = model_think(has_record, busy, noted, alt)
                    seen = (got, list(log), fields if expect[2] else None)
                    if seen != expect:
                        raise AssertionError(f"FUN_005e77a0 record={has_record} busy={busy} noted={noted} alt={alt}: game.exe {seen}, model {expect}")
                    cases += 1
    return cases


def check_modes(machine):
    """Run FUN_005ddfc0 and FUN_005de080 for real, FUN_005a7c20 hooked; returns the case count."""
    game, unit = machine.alloc(0x200), machine.alloc(0x100)
    machine.w32(game + 0xA8, FRAME)
    asked, events = [], []

    def request(current):
        """Hook FUN_005a7c20: record the mode request's fields."""
        req = current.arg(1)
        asked.append((current.arg(0), machine.r32(req), machine.r32(req + 4), machine.r32(req + 8), machine.r32(req + 0xC), machine.r32(req + 0x10), current.arg(2)))
        return 1

    machine.hook(0x620210, lambda _current: 0, 2)
    machine.hook(0x5A6B10, lambda _current: 0, 0)
    machine.hook(0x5A7C20, request, 3)
    machine.hook(0x540E60, lambda _current: 0, 2)
    machine.hook(0x5417D0, lambda current: events.append((current.arg(0), current.arg(1))) or 0, 4)
    machine.call(0x5DDFC0, 0xC, UNIT_X, UNIT_Y, ecx=game, edx=unit, limit=5000)
    target = (UNIT_Y, UNIT_X) if mutation == "mode-target-swapped" else (UNIT_X, UNIT_Y)
    expect = [(game, 0xC, unit, 0, *target, 1)]
    if asked != expect:
        raise AssertionError(f"FUN_005ddfc0 asked {asked}, model {expect}")
    cases = 1
    for unit_mode in (12, 1):
        for frames in (1, 0, 20):
            asked.clear()
            events.clear()
            machine.w32(unit + 0x10, unit_mode)
            machine.call(0x5DE080, frames, ecx=game, edx=unit, limit=5000)
            back = unit_mode != 1 and mutation != "no-neutral-after"
            want = [(game, 1, unit, unit, 0, 0, 1)] if back else []
            wait = frames if mutation == "idle-zero-frames" else frames or 1
            if asked != want or events != [(2, FRAME + wait)]:
                raise AssertionError(f"FUN_005de080 mode={unit_mode} n={frames}: asked {asked} events {events}, model {want} {[(2, FRAME + wait)]}")
            cases += 1
    return cases


def check(exe):
    """Both parts on fresh machines; returns the case count."""
    return check_think(emu.Emu(exe)) + check_modes(emu.Emu(exe))


def main():
    """Parse the arguments, verify the exe and run (with --mutation-check, then each defect)."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=Path(os.environ.get("D2_MPQ_DIR", ".")) / "bin" / "game.exe")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(f"ok: {check(str(args.exe))} Cain first-think cases (FUN_005e77a0, FUN_005ddfc0, FUN_005de080; callees hooked)")
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                check(str(args.exe))
            except AssertionError:
                print(f"caught: {name}")
            else:
                print(f"NOT CAUGHT: {name}")
                sys.exit(1)


if __name__ == "__main__":
    main()
