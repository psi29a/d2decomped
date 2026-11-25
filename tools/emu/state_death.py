# SPDX-License-Identifier: GPL-3.0-or-later
"""Check which states a death keeps, FUN_00639fb0, in stock game.exe 1.14d.

Run: export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
     .venv/bin/python state_death.py [--mutation-check]
     .venv/bin/python state_death.py inc --out ../../tests/state_death_cases.inc

FUN_00639fb0 (stdcall, unit and a boss flag, `ret 8`) runs with game.exe's own
FUN_00625bb0 / FUN_00625be0 (the unit's stat block +0x5c, flag 0x80000000 at
+0x10, the state bitmap at +0x58 and the second bitmap behind it, (states + 31)
/ 32 dwords) and FUN_0064c040 hooked (recorded). The claim:
  - nothing happens for a null unit, a unit with no stat block or one without
    flag 0x80000000, or a null bitmap;
  - the keep mask is data tables +0x108 (States.txt bossstaydeath) for a nonzero
    flag, else +0x104 (monstaydeath) when the unit's type is not 0, else +0x100
    (plrstaydeath) for a player;
  - each dword: second |= ~keep & first, then first &= keep (the states a death
    does not keep move from the first bitmap to the second);
  - FUN_0064c040(unit) then runs once, also when the state count is 0.
The States.txt columns are named by the loader FUN_00618100's strings
("plrstaydeath", "monstaydeath", "bossstaydeath"); which bit of +0x100/4/8 each
is built from was not traced here. `inc` prints the cases for
tests/test_state_death.cpp. No MPQs are read.
"""
import argparse
import hashlib
import random
import sys
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TARGET, REFRESH, DATA_TABLES = 0x639FB0, 0x64C040, 0x744304
M32 = 0xFFFFFFFF
MUT = set()


def model(count, kind, boss, first, second, keeps):
    """What d2d states: (first, second, refreshed) after the move; keeps = (player, monster, boss) masks."""
    words = (count + 31) // 32
    if boss and "boss" not in MUT:
        keep = keeps[2]
    elif kind != 0 and ("player_first" not in MUT):
        keep = keeps[1]
    else:
        keep = keeps[0]
    first, second = list(first), list(second)
    for index in range(words):
        if "order" in MUT:
            first[index] &= keep[index]
            second[index] |= ~keep[index] & M32 & first[index]
        else:
            second[index] |= ~keep[index] & M32 & first[index]
            first[index] &= keep[index]
    return first, second, True


def build(rng):
    """One random state count, masks, unit kind and boss flag."""
    count = rng.choice((0, 1, 31, 32, 33, 64, 100, 190, 200))
    words = (count + 31) // 32
    rand = lambda: [rng.randrange(1 << 32) for _ in range(words)]
    return count, rng.choice((0, 0, 1, 1, 2, 4)), rng.choice((0, 0, 1)), rand(), rand(), (rand(), rand(), rand())


def run(mutation, cases=4000, collect=None):
    """Run cases through game.exe and the model; return the failure count and a summary."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    MUT.clear()
    MUT.update(mutation)
    machine = emu.Emu(str(exe))
    refreshed = []
    machine.hook(REFRESH, lambda e: refreshed.append(e.arg(0)), 1)
    tables = machine.alloc(0x1000)
    machine.w32(DATA_TABLES, tables)
    rng = random.Random(20261125)
    bad, buckets = 0, {}
    for index in range(cases):
        count, kind, boss, first, second, keeps = build(rng)
        words = (count + 31) // 32
        machine.w32(tables + 0xC4, count)
        for offset, keep in zip((0x100, 0x104, 0x108), keeps):
            area = machine.alloc(4 * max(words, 1))
            for number, value in enumerate(keep):
                machine.w32(area + 4 * number, value)
            machine.w32(tables + offset, area)
        shape = index % 11 if index % 5 == 0 else 0     # 1: no list, 2: no flag, 3: null bitmap, 4: null unit
        bitmap = machine.alloc(8 * max(words, 1))
        for number, value in enumerate(first + second):
            machine.w32(bitmap + 4 * number, value)
        block = machine.alloc(0x100)
        machine.w32(block + 0x10, 0 if shape == 2 else 0x80000000)
        machine.w32(block + 0x58, 0 if shape == 3 else bitmap)
        unit = machine.alloc(0x100)
        machine.w32(unit, kind)
        machine.w32(unit + 0x5C, 0 if shape == 1 else block)
        refreshed.clear()
        machine.call(TARGET, 0 if shape == 4 else unit, boss)
        got = [machine.r32(bitmap + 4 * number) for number in range(2 * words)]
        if shape in (1, 2, 3, 4):
            want, want_calls = first + second, []
        else:
            moved_first, moved_second, _ = model(count, kind, boss, first, second, keeps)
            want, want_calls = moved_first + moved_second, [unit]
        if got != want or refreshed != want_calls:
            bad += 1
            if bad <= 5:
                print(f"case {index}: count {count} kind {kind} boss {boss}: game {got} {refreshed} model {want} {want_calls}")
        buckets[shape] = buckets.get(shape, 0) + 1
        if collect is not None and shape == 0:
            collect.append((count, kind, boss, first, second, keeps, got[:words], got[words:]))
    return bad, buckets


def write_inc(cases):
    """The fixture text: a row per case, masks as brace lists of dwords."""
    def lst(values):
        return "{ " + ", ".join(f"{value}u" for value in values) + " }"
    lines = ["// Generated by tools/emu/state_death.py inc: FUN_00639fb0's answers from game.exe 1.14d.",
             "// { states, unit type, boss, first, second, player keep, monster keep, boss keep, first after, second after }"]
    for count, kind, boss, first, second, keeps, got_first, got_second in cases:
        lines.append(f"{{ {count}, {kind}, {boss}, {lst(first)}, {lst(second)}, {lst(keeps[0])}, {lst(keeps[1])}, {lst(keeps[2])}, {lst(got_first)}, {lst(got_second)} }},")
    return "\n".join(lines) + "\n"


def main():
    """Run the oracle, with --mutation-check prove each mutant of the model is caught, or write the fixture."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", choices=("inc",))
    parser.add_argument("--out")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    collect = []
    bad, buckets = run((), collect=collect)
    if bad:
        print(f"FAIL: {bad} cases differ", file=sys.stderr)
        sys.exit(1)
    if args.mode == "inc":
        text = write_inc(collect[:400])
        if args.out:
            Path(args.out).write_text(text)
        else:
            sys.stdout.write(text)
        return
    print(f"ok: 4000 FUN_00639fb0 cases ({buckets})")
    if args.mutation_check:
        for name in ("boss", "player_first", "order"):
            bad, _ = run((name,))
            print(f"mutant {name}: {'caught' if bad else 'NOT CAUGHT'} ({bad})")
            if not bad:
                sys.exit(1)
        print("mutation check ok")


if __name__ == "__main__":
    main()
