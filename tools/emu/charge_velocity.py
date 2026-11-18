# SPDX-License-Identifier: GPL-3.0-or-later
"""Charge velocity from game.exe's FUN_00620e80 and FUN_005cf6b0.

    .venv/bin/python charge_velocity.py --exe /path/to/game.exe
    .venv/bin/python charge_velocity.py --exe /path/to/game.exe --dump ../../tests/charge_velocity_cases.inc
    .venv/bin/python charge_velocity.py --exe /path/to/game.exe --mutation-check

For each synthetic CharStats row, FUN_00620e80 runs for real and its stat
0x43 write is recorded. That run bonus, the case's other velocitypercent and
chill's -50 are returned by the hooked stat getter while FUN_005cf6b0 runs for
real through its player movement path; FUN_00648690 records its 8.8 velocity.

Hooked, not claimed: stat-list allocation/attachment, the selected skill and
target lookups, skill-list field setters, path calculation, state setting and
FUN_00625480's aggregate-stat read. FUN_00483360 (MulDiv), the CharStats and
Skills.txt lookups, FUN_00620e80 and FUN_005cf6b0 run stock code.

The output fixture is consumed by tests/game/test_charge_velocity.cpp, which
calls d2d's rules::charge_velocity on the same inputs. ``--mutation-check``
breaks each arithmetic term or clamp and patches each stock entry to ``ret``;
all seven negative controls must be detected.
"""
import argparse
import pathlib
import sys

import emu

RUN_BONUS = 0x620E80
CHARGE_START = 0x5CF6B0
TABLES = 0x744304
SKILL_ROW = 0x23C
CHAR_ROW = 0xC4
CALL_LIMIT = 200000

CASES = tuple(
    (walk, run, velocity, chilled, param1)
    for walk, run in ((6, 9), (7, 9), (5, 6), (8, 8), (0, 9))
    for velocity in (-200, -100, -51, -50, -49, 0, 40, 200)
    for chilled in (0, 1)
    for param1 in (0, 50, 150)
)


class Mismatch(Exception):
    """The stock result and model disagree."""


def trunc_div(value, divisor):
    """x86 signed division, truncating toward zero."""
    quotient = abs(value) // abs(divisor)
    return quotient if (value >= 0) == (divisor > 0) else -quotient


def model(case, mutation=None):
    """rules::charge_velocity, optionally with one deliberate defect."""
    walk, run, velocity, chilled, param1 = case
    run_bonus = trunc_div(run * 100, walk) - 100 if walk > 0 else 0
    if mutation == "no_run_bonus":
        run_bonus = 0
    chill = -50 if chilled and mutation != "no_chill" else 0
    percent = 100 + velocity + run_bonus + chill
    floor = 49 if mutation == "floor_49" else 50
    percent = max(percent, floor)
    if mutation == "no_param1":
        param1 = 0
    numerator = (run << 8) * (percent + param1)
    result = trunc_div(numerator, 100)
    return result + (1 if mutation == "plus_one" else 0)


def run_stock(executable, patch_function=None):
    """Return (case, stock 8.8 velocity) rows from both real functions."""
    machine = emu.Emu(exe=executable)
    tables = machine.alloc(0xC00)
    skill = machine.alloc(SKILL_ROW)
    chars = machine.alloc(CHAR_ROW)
    unit = machine.alloc(0x200)
    node = machine.alloc(0x40)
    path = machine.alloc(0x100)
    stat_list = machine.alloc(0x60)
    machine.w32(TABLES, tables)
    machine.w32(tables + 0xB98, skill)
    machine.w32(tables + 0xBA0, 1)
    machine.w32(tables + 0xBC4, chars)
    machine.w32(tables + 0xBC8, 1)
    machine.w32(unit, 0)
    machine.w32(unit + 4, 0)
    machine.w32(unit + 0x2C, path)

    state = {"stat": 100, "run_bonus": None, "velocity": None}
    machine.hook(0x6251F0, lambda current: stat_list, 5)

    def set_stat(current):
        """Record FUN_00620e80's SetStat(list, 0x43, bonus, 0)."""
        if current.arg(1) == 0x43:
            state["run_bonus"] = current.arg(2)
            if state["run_bonus"] & 0x80000000:
                state["run_bonus"] -= 0x100000000
        return 1

    machine.hook(0x6270B0, set_stat, 4)
    machine.hook(0x626E10, lambda current: 0, 3)
    machine.hook(0x620250, lambda current: node, 1)
    machine.hook(0x553540, lambda current: 0, 2)
    machine.stub(0, {0x644560: 2, 0x6445A0: 2, 0x644620: 2, 0x649970: 3, 0x644660: 2, 0x639DB0: 3})
    machine.hook(0x625480, lambda current: state["stat"] & 0xFFFFFFFF, 3)

    def set_velocity(current):
        """Record FUN_005cf6b0's FUN_00648690(path, velocity, file, line)."""
        state["velocity"] = current.arg(1)
        if state["velocity"] & 0x80000000:
            state["velocity"] -= 0x100000000
        return 0

    machine.hook(0x648690, set_velocity, 4)
    if patch_function == 20:
        machine.mu.mem_write(RUN_BONUS, b"\xc3")
        machine.mu.ctl_remove_cache(RUN_BONUS, RUN_BONUS + 1)
    if patch_function == 31:
        machine.mu.mem_write(CHARGE_START, b"\xc3")
        machine.mu.ctl_remove_cache(CHARGE_START, CHARGE_START + 1)

    rows = []
    for case in CASES:
        walk, run, velocity, chilled, param1 = case
        machine.mu.mem_write(chars + 0x40, bytes((walk & 0xFF, run & 0xFF)))
        machine.w32(skill + 0x148, param1)
        state["run_bonus"] = None
        machine.call(RUN_BONUS, regs={"eax": 0, "ebx": unit}, limit=CALL_LIMIT)
        run_bonus = state["run_bonus"] if walk > 0 else 0
        if run_bonus is None:
            run_bonus = 0
        state["stat"] = 100 + velocity + run_bonus + (-50 if chilled else 0)
        state["velocity"] = None
        machine.call(CHARGE_START, 0, ecx=0x1234, edx=unit, limit=CALL_LIMIT)
        rows.append((case, state["velocity"]))
    return rows


def check(executable, mutation=None, patch_function=None):
    """Compare stock output with the port model; return rows."""
    rows = run_stock(executable, patch_function)
    expected = [model(case, mutation) for case, _ in rows]
    actual = [result for _, result in rows]
    if actual != expected:
        index = next(index for index, pair in enumerate(zip(actual, expected)) if pair[0] != pair[1])
        raise Mismatch(f"case {rows[index][0]}: game.exe {actual[index]!r}, d2d {expected[index]!r}")
    return rows


def dump(path, rows):
    """Write the frozen C++ cases used by test_charge_velocity."""
    output = [
        "// Stock game.exe 1.14d, FUN_00620e80 -> FUN_005cf6b0;",
        "// generated by tools/emu/charge_velocity.py --dump.",
    ]
    for case, result in rows:
        output.append(f"    {{ {case[0]}, {case[1]}, {case[2]}, {case[3]}, {case[4]}, {result} }},")
    pathlib.Path(path).write_text("\n".join(output) + "\n", encoding="utf-8")


def main():
    """Run parity, optionally dump its fixture and check mutations."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="local game.exe 1.14d")
    parser.add_argument("--dump", help="write test_charge_velocity's captured cases")
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    rows = check(options.exe)
    if options.dump:
        dump(options.dump, rows)
    print(f"ok: {len(rows)} Charge velocity cases")
    if not options.mutation_check:
        return 0
    caught = 0
    for mutation in ("no_run_bonus", "no_chill", "floor_49", "no_param1", "plus_one"):
        try:
            check(options.exe, mutation=mutation)
        except Mismatch:
            caught += 1
            print(f"mutation {mutation}: caught")
        else:
            raise SystemExit(f"mutation {mutation}: NOT caught")
    for function in (20, 31):
        try:
            check(options.exe, patch_function=function)
        except Mismatch:
            caught += 1
            print(f"mutation function {function} entry ret: caught")
        else:
            raise SystemExit(f"mutation function {function} entry ret: NOT caught")
    print(f"mutation check ok ({caught} ways)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
