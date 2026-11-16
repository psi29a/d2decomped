#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the six UI/Hireables.cpp frontier functions in stock game.exe 1.14d.

Runs the stock routines with synthetic tables and hooked UI/stat callees, and
compares their calls, returns, and globals with the model documented in
docs/research/re/ui-hireables-frontier.md.  --mutation-check proves the main
guards, release span, colour initialization, and eligibility result are seen.
"""
import argparse
import hashlib
from pathlib import Path

import unicorn.x86_const as uc

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("release-short", "colour-repeat", "name-zero-draws", "eligibility-wrong")
mutation = None


def executable(args):
    path = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(path.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    return path


def check_release(path):
    machine = emu.Emu(str(path))
    released = []
    machine.hook(0x478A00, lambda current: released.append(current.mu.reg_read(uc.UC_X86_REG_ECX)) or 0, 0)
    machine.hook(0x40B480, lambda current: 0, 3)
    tables = machine.alloc(0xC00)
    icons = machine.alloc(5 * 4 * 3)
    machine.w32(0x744304, tables)
    machine.w32(tables + 0xBF0, 3)
    machine.w32(0x7BEEAC, icons)
    expected = []
    for index, address in enumerate(range(0x7BEEB0, 0x7BEECC, 4)):
        value = 0 if index == 2 else 0x111000 + index
        machine.w32(address, value)
        if value:
            expected.append(value)
    for index in range(15):
        value = 0 if index % 4 == 1 else 0x222000 + index
        machine.w32(icons + index * 4, value)
        if value:
            expected.append(value)
    machine.call(0x492EC0)
    want = expected[:-1] if mutation == "release-short" else expected
    assert released == want and machine.r32(0x7BEEAC) == 0
    assert all(machine.r32(address) == 0 for address in range(0x7BEEB0, 0x7BEECC, 4))
    return 1


def check_colours(path):
    machine = emu.Emu(str(path))
    calls = []
    values = iter((0x31, 0x42, 0x53, 0x64, 0x75, 0x86))
    machine.hook(0x4FB180, lambda current: calls.append(tuple(current.arg(index) for index in range(3))) or next(values), 3)
    machine.call(0x492F40, ecx=0)
    first = (calls[:], tuple(machine.read(0x7BEED0, 4)))
    machine.call(0x492F40, ecx=0)
    if mutation == "colour-repeat":
        assert len(calls) == 6
    else:
        assert calls == [(0, 0x80, 0), (0xC0, 0xC0, 0), (0xFF, 0, 0)]
    assert first[1][:3] == (0x31, 0x42, 0x53) and machine.read(0x7BEED0, 3) == bytes(first[1][:3])
    return 2


def check_text_wrappers(path):
    machine = emu.Emu(str(path))
    tables = machine.alloc(0xC00)
    rows = machine.alloc(0x1C0)
    machine.w32(0x744304, tables)
    machine.w32(tables + 0xBF0, 2)
    machine.w32(tables + 0xBE8, rows)
    log = []
    for address, argc in ((0x403040, 4), (0x524A30, 0), (0x5267E0, 0), (0x492FA0, 3), (0x526320, 1),
                          (0x6823FD, 3), (0x681EF0, 3), (0x526790, 1), (0x526800, 0), (0x501820, 0),
                          (0x502EF0, 0), (0x502320, 3)):
        machine.hook(address, lambda current, target=address: log.append(target) or (1 if target == 0x501820 else 0), argc)
    cases = 0
    for index, name_id, draws in ((-1, 7, False), (0, 0, False), (0, 0x1506, False), (0, 7, True), (2, 7, False)):
        machine.w32(rows + 0x0C, name_id)
        log.clear()
        machine.call(0x493020, 10, 20, regs={"eax": index})
        observed = 0x492FA0 in log
        want = draws
        if mutation == "name-zero-draws" and name_id == 0:
            want = True
        assert observed == want
        cases += 1
    log.clear()
    machine.call(0x4930A0, 10, 20)
    assert log.count(0x526320) == 1 and log.count(0x492FA0) == 1
    log.clear()
    machine.call(0x493100, 123, 20)
    assert log.count(0x6823FD) == 1 and log.count(0x502320) == 1
    return cases + 2


def check_eligibility(path):
    machine = emu.Emu(str(path))
    unit = machine.alloc(0x100)
    machine.w32(unit + 0x60, unit)
    data = {0x44DCC0: 1, 0x463DD0: unit, 0x63C1E0: unit, 0x478F20: 0, 0x6289C0: 0,
            0x463990: unit, 0x479120: 0x100, 0x62EAF0: 1}
    sounds = []
    for address, argc in ((0x44DCC0, 0), (0x463DD0, 0), (0x63C1E0, 1), (0x478F20, 1),
                          (0x6289C0, 1), (0x463990, 0), (0x479120, 0), (0x62EAF0, 6)):
        machine.hook(address, lambda current, target=address: data[target], argc)
    machine.hook(0x6280A0, lambda current: int(current.arg(1) == 0x10), 4)
    machine.hook(0x4CBDE0, lambda current: sounds.append(current.mu.reg_read(uc.UC_X86_REG_ECX)) or 0, 0)
    results = []
    for stat, value in ((0x4C, 1), (3, 1), (3, 0)):
        machine.hook(0x629BB0, lambda current, wanted=stat, answer=value: answer if current.arg(1) == wanted else 0, 2)
        got = machine.call(0x4934D0, 0, ecx=unit)
        results.append(got)
    want = [1, 1, 0]
    if mutation == "eligibility-wrong":
        want[-1] = 1
    assert results == want, (results, want)
    return len(results)


def run(path):
    count = check_release(path) + check_colours(path) + check_text_wrappers(path) + check_eligibility(path)
    return f"ok: {count} UI/Hireables.cpp frontier scenarios over 6 functions"


def main():
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    path = executable(args)
    print(run(path))
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(path)
            except AssertionError:
                print(f"mutation {name}: failed as required")
            else:
                raise SystemExit(f"mutation {name} was not caught")
            finally:
                mutation = None


if __name__ == "__main__":
    main()
