#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check thirteen small UI frontier functions in stock game.exe 1.14d.

The checks cover text teardown, quest-log resource teardown/close, and every
controls-screen hit-test boundary.  --mutation-check injects model defects.
"""
import argparse
import hashlib
import random
from pathlib import Path

import unicorn.x86_const as uc

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("text-keeps-node", "quest-keeps-one", "hit-exclusive", "release-row-flip")
mutation = None


def executable(args):
    path = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(path.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    return path


def check_teardown(path):
    machine = emu.Emu(str(path))
    log = []
    machine.hook(0x49F960, lambda current: log.append(0x49F960) or 0, 0)
    machine.hook(0x6612A0, lambda current: log.append(current.arg(0)) or 0, 1)
    machine.w32(0x7BF250, 0x123456)
    machine.w32(0x7BF1FA, 7)
    machine.call(0x4A1730)
    assert log == [0x49F960, 0x123456]
    want_node = 0x123456 if mutation == "text-keeps-node" else 0
    assert machine.r32(0x7BF250) == want_node and machine.read(0x7BF1FA, 1) == b"\0"
    return 1


def check_quest(path):
    machine = emu.Emu(str(path))
    released = []
    machine.hook(0x478A00, lambda current: released.append(current.mu.reg_read(uc.UC_X86_REG_ECX)) or 0, 0)
    close = []
    machine.hook(0x455F20, lambda current: close.append((current.mu.reg_read(uc.UC_X86_REG_ECX), current.mu.reg_read(uc.UC_X86_REG_EDX), current.arg(0))) or 0, 1)
    addresses = (0x7BF2D5, 0x7BF2D9, 0x7BF34D, 0x7BF2D1, 0x7BF2DD, 0x7BF351) + tuple(range(0x7BF2E1, 0x7BF34D, 4))
    expected = []
    for index, address in enumerate(addresses):
        value = 0 if index % 5 == 2 else 0x500000 + index
        machine.w32(address, value)
        if value:
            expected.append(value)
    machine.call(0x4A2550)
    want = expected[:-1] if mutation == "quest-keeps-one" else expected
    assert released == want and all(machine.r32(address) == 0 for address in addresses)
    machine.w32(0x7BF298, 2)
    machine.mu.mem_write(0x7BF2B0, b"\x09")
    machine.mu.mem_write(0x7BF2B3, b"\x09")
    machine.call(0x4A26B0, 77)
    assert machine.r32(0x7BF298) == 0 and machine.read(0x7BF2B0, 1) == b"\0" and machine.read(0x7BF2B3, 1) == b"\0"
    assert close == [(15, 1, 77)]
    machine.call(0x4A2600)
    return 3


def signed_half(value):
    return int(value / 2)


def predicates(width, height, mouse_x, mouse_y, thumb_y, thumb_rows):
    left = signed_half(width - 0x26C)
    top = signed_half(height - 0x1A4)
    bar_x = width - left - 0x1F
    in_list = left + 0x12 <= mouse_x <= left + ((width - 2 * left - 0x31) // 3 + 6) * 3 and top + 0x28 <= mouse_y <= top + 0x136
    values = [in_list,
              bar_x <= mouse_x <= width - left - 0x12 and top + 0x23 <= mouse_y <= top + 0x2F,
              bar_x <= mouse_x <= width - left - 0x12 and top + 0x125 <= mouse_y <= top + 0x131,
              bar_x <= mouse_x <= width - left - 0x12 and top + 0x2F < mouse_y < thumb_y,
              bar_x <= mouse_x <= width - left - 0x12 and thumb_y <= mouse_y <= thumb_y + thumb_rows * 12,
              bar_x <= mouse_x <= width - left - 0x12 and thumb_y + thumb_rows * 12 < mouse_y < top + 0x125]
    if mutation == "hit-exclusive":
        values[1] = not values[1]
    return values


def check_controls(path):
    machine = emu.Emu(str(path))
    rng = random.Random(0x4A47C0)
    cases = 0
    targets = (0x4A47F0, 0x4A4860, 0x4A48B0, 0x4A4900, 0x4A4950, 0x4A49A0)
    for width, height in ((640, 480), (800, 600), (641, 481)):
        machine.w32(0x71146C, width)
        machine.w32(0x7A521C, height)
        for _ in range(300):
            mouse_x = rng.randrange(-30, width + 31)
            mouse_y = rng.randrange(-30, height + 31)
            thumb_y = rng.randrange(20, height - 20)
            thumb_rows = rng.randrange(1, 8)
            machine.w32(0x7C02CC, thumb_y)
            machine.w32(0x7C0278, thumb_rows)
            want = predicates(width, height, mouse_x, mouse_y, thumb_y, thumb_rows)
            got = [bool(machine.call(address, regs=({"edi": mouse_x, "ebx": mouse_y} if address == 0x4A47F0
                                                    else {"esi": mouse_x, "edi": mouse_y})))
                   for address in targets]
            assert got == want
            cases += 1
    # FUN_004a47c0 walks three inclusive button rectangles, returning 3 for none.
    for index in range(3):
        base = 0x7246E2 + index * 0x1A
        machine.w32(base, 10 + index * 30)
        machine.w32(base + 4, 20 + index * 30)
        machine.w32(base + 8, 100)
        machine.w32(base + 12, 120)
    for mouse_x, mouse_y, expected in ((10, 100, 0), (50, 110, 1), (80, 120, 2), (0, 0, 3)):
        got = machine.call(0x4A47C0, edx=mouse_y, regs={"esi": mouse_x})
        if mutation == "release-row-flip" and expected == 1:
            expected = 2
        assert got == expected
        cases += 1
    return cases


def check_release_match(path):
    machine = emu.Emu(str(path))
    machine.hook(0x4A47F0, lambda current: 0, 0)
    machine.hook(0x4A4720, lambda current: 0, 0)
    assert machine.call(0x4A4A10, ecx=10, regs={"eax": 20}) == 0
    return 1


def run(path):
    count = check_teardown(path) + check_quest(path) + check_controls(path) + check_release_match(path)
    return f"ok: {count} small UI frontier scenarios over 13 functions"


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
