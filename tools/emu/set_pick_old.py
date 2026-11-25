# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a classic item's set pick, FUN_005c2740 (with its tail LAB_005c276a), in stock game.exe 1.14d.

Run: export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
     .venv/bin/python set_pick_old.py [--mutation-check]
     .venv/bin/python set_pick_old.py inc --out ../../tests/set_pick_old_cases.inc

FUN_005c2740 (stdcall, the item) is one function: 0x5c2740..0x5c2767 looks the
item's class row up (FUN_006335f0, none: returns 0 and touches nothing) and
jumps to 0x5c276a, which is a label inside it (it keeps the frame, `ret 4` at
0x5c293b), not a function. The claim, with game.exe's own code and these hooked
(the stat list is not built): FUN_00625480 / FUN_006253b0 (the item's stats
0x48 / 0x49, durability and its maximum), FUN_00627260 (their setter, recording)
and FUN_0065fec0 (recording); the item unit, its data (seed +4, version +0x30,
set +0x28, flags +0x18), the class table and the Sets table (+0xc0c, count
+0xc10, 0x128 bytes a row: version +4, members +0xc, member pointers +0x110, a
member's SetItems index +0 and code +0x28) are real memory:
  - both stats doubled, at most 255, before anything else;
  - n = the Sets rows before the first of version 100 or more (the count loop
    at 0x5c27f1 never reads the item's version, so the item's version 100+
    test at 0x5c28ad can't fire); the start is the item's own seed's
    step (rand(n): the 64-bit step low * 0x6ac690c5 + high, then & (n-1) for a
    power of two, else % n) for n >= 1, no draw for none;
  - from the start, all n rows round: a row of version 100+ (item version
    under 100) is skipped, else its first member of the item's code wins, and
    a later row's find replaces an earlier one's;
  - found: item +0x28 = that member's SetItems index, flag 0x10 CLEARED
    (FUN_006280d0(item, 0x10, 0): a zero third argument clears), then
    FUN_0065fec0(4, 0, item, 0, 0, 0), returns 1, the seed stays stepped;
  - none: the seed is put back to (its low at entry, 0x29a), returns 0.
The members of a row are listed in SetItems order here; whether the real
table's rows are built that way is not checked (docs/research/re/drops.md).
`inc` prints the cases for tests/test_set_pick_old.cpp. No MPQs are read.
"""
import argparse
import hashlib
import random
import struct
import sys
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TARGET = 0x5C2740
STAT_GET_DUR, STAT_GET_MAX, STAT_SET, MAKE = 0x625480, 0x6253B0, 0x627260, 0x65FEC0
CLASS_COUNT, CLASS_TABLE, DATA_TABLES = 0x96CA58, 0x96CA5C, 0x744304
CLASS_ID = 7
M32 = 0xFFFFFFFF
MUT = set()
CODES = ("cap ", "plt ", "buc ", "wnd ")


def step(low, high):
    """One seed step: (new low, new high) of FUN_005c2740's 64-bit multiply and add."""
    product = low * 0x6AC690C5 + high
    return product & M32, (product >> 32) & M32


def model(rows, code, version, low, high, dur, maxdur):
    """The set pick as d2d states it: (returned, picked SetItems index or -1, dur, maxdur, low, high).

    rows: [(row version, [(SetItems index, code)])] in table order.
    """
    cap = 254 if "cap" in MUT else 255
    dur, maxdur = min(dur * 2, cap), min(maxdur * 2, cap)
    if "all_rows" in MUT:
        count = len(rows)
    else:
        count = 0
        while count < len(rows) and rows[count][0] < 100:
            count += 1
    entry_low = low
    if count < 1:
        start = 0
    else:
        low, high = step(low, high)
        start = (low & (count - 1)) if count & (count - 1) == 0 else low % count
        if "mask_only" in MUT:
            start = low & (count - 1)
        if "no_step" in MUT:
            start = 0
    picked = -1
    for index in range(count):
        row = rows[(index + start) % count]
        if row[0] >= 100 and version < 100:
            continue
        for member, member_code in row[1]:
            if member_code == code:
                if "first_wins" in MUT and picked >= 0:
                    break
                picked = member
                break
    if picked < 0:
        if "no_reset" not in MUT:
            low, high = entry_low, 0x29A if "high" not in MUT else high
        return 0, -1, dur, maxdur, low, high
    return 1, picked, dur, maxdur, low, high


def build(rng):
    """One random Sets table, item code, item version, seed and durabilities."""
    rows, member = [], 0
    for _ in range(rng.choice((0, 1, 2, 3, 4, 5, 8, 9))):
        listed = []
        for _ in range(rng.choice((0, 1, 2, 3, 4))):
            listed.append((member, rng.choice(CODES)))
            member += 1
        rows.append((rng.choice((0, 0, 0, 0, 100, 120)), listed))
    version = rng.choice((0, 0, 0, 0, 0, 100, 110))
    return (rows, rng.choice(CODES), version, rng.choice((0, 1, 0x29A, 0x12345678, M32, rng.randrange(1 << 32), rng.randrange(1 << 32))),
            rng.choice((0x29A, 0, M32, rng.randrange(1 << 32))), rng.choice((0, 1, 2, 63, 126, 127, 128, 129, 200, 254, 255, 1000)),
            rng.choice((0, 1, 20, 127, 128, 255, 300)))


def run(mutation, cases=3000, collect=None):
    """Run cases through game.exe and the model; return the failure count and a summary."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    MUT.clear()
    MUT.update(mutation)
    machine = emu.Emu(str(exe))
    log = []
    state = {"dur": 0, "max": 0}
    machine.hook(STAT_GET_DUR, lambda e: (log.append(("get", e.arg(1), e.arg(2))), state["dur"])[1], 3)
    machine.hook(STAT_GET_MAX, lambda e: (log.append(("get", e.arg(1), e.arg(2))), state["max"])[1], 3)
    machine.hook(STAT_SET, lambda e: log.append(("set", e.arg(1), e.arg(2), e.arg(3))), 4)
    machine.hook(MAKE, lambda e: log.append(("make", *(e.arg(i) for i in range(6)))), 6)
    classes = machine.alloc(0x1A8 * 16)
    tables = machine.alloc(0x1000)
    machine.w32(CLASS_COUNT, 16)
    machine.w32(CLASS_TABLE, classes)
    machine.w32(DATA_TABLES, tables)
    rng = random.Random(20261125)
    bad, buckets = 0, {}
    for index in range(cases):
        rows, code, version, low, high, dur, maxdur = build(rng)
        flags = rng.randrange(1 << 32)
        no_class = index % 97 == 5
        sets = machine.alloc(0x128 * max(len(rows), 1))
        for number, (row_version, listed) in enumerate(rows):
            base = sets + 0x128 * number
            machine.mu.mem_write(base + 4, struct.pack("<H", row_version))
            machine.w32(base + 0xC, len(listed))
            for place, (member, member_code) in enumerate(listed):
                record = machine.alloc(0x40)
                machine.mu.mem_write(record, struct.pack("<h", member))
                machine.mu.mem_write(record + 0x28, member_code.encode())
                machine.w32(base + 0x110 + 4 * place, record)
        machine.w32(tables + 0xC0C, sets)
        machine.w32(tables + 0xC10, len(rows))
        machine.mu.mem_write(classes + 0x1A8 * CLASS_ID + 0x80, code.encode())
        data = machine.alloc(0x100)
        machine.w32(data + 4, low)
        machine.w32(data + 8, high)
        machine.w32(data + 0x18, flags)
        machine.mu.mem_write(data + 0x30, struct.pack("<H", version))
        unit = machine.alloc(0x100)
        machine.w32(unit, 4)
        machine.w32(unit + 4, 99 if no_class else CLASS_ID)
        machine.w32(unit + 0x14, data)
        state["dur"], state["max"] = dur, maxdur
        log.clear()
        got = machine.call(TARGET, unit)
        seen = (got, machine.s32(data + 0x28) if got else None, machine.r32(data + 0x18), machine.r32(data + 4), machine.r32(data + 8))
        if no_class:
            want_log, want = [], (0, None, flags, low, high)
        else:
            result, picked, dur2, max2, low2, high2 = model(rows, code, version, low, high, dur, maxdur)
            want_log = [("get", 0x48, 0), ("get", 0x49, 0), ("set", 0x48, dur2, 0), ("set", 0x49, max2, 0)]
            if result:
                want_log.append(("make", 4, 0, unit, 0, 0, 0))
            want = (result, picked if result else None, (flags & ~0x10) if result else flags, low2, high2)
        if log != want_log or seen != want:
            bad += 1
            if bad <= 5:
                print(f"case {index}: game {seen} {log}\n        model {want} {want_log}")
        key = "no class" if no_class else ("found" if got else "none")
        buckets[key] = buckets.get(key, 0) + 1
        if collect is not None and not no_class and version < 100:
            collect.append((rows, code, low, high, dur, maxdur, seen[1], want_log[2][2], want_log[3][2], seen[3], seen[4]))
    return bad, buckets


def write_inc(cases):
    """The fixture text: one row per case, the sets as `v:code,code;v:...`."""
    lines = ["// Generated by tools/emu/set_pick_old.py inc: FUN_005c2740's answers from game.exe 1.14d.",
             "// { sets, code, low, high, durability, max, picked (-1 none), durability, max, low, high }"]
    for rows, code, low, high, dur, maxdur, picked, dur2, max2, low2, high2 in cases:
        text = ";".join(f"{version}:" + ",".join(member_code.strip() for _, member_code in listed) for version, listed in rows)
        lines.append(f'{{ "{text}", "{code.strip()}", {low}u, {high}u, {dur}, {maxdur}, {-1 if picked is None else picked}, {dur2}, {max2}, {low2}u, {high2}u }},')
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
        text = write_inc(collect[:600])
        if args.out:
            Path(args.out).write_text(text)
        else:
            sys.stdout.write(text)
        return
    print(f"ok: 3000 FUN_005c2740 cases ({buckets})")
    if args.mutation_check:
        for name in ("cap", "all_rows", "mask_only", "no_step", "first_wins", "no_reset", "high"):
            bad, _ = run((name,))
            print(f"mutant {name}: {'caught' if bad else 'NOT CAUGHT'} ({bad})")
            if not bad:
                sys.exit(1)
        print("mutation check ok")


if __name__ == "__main__":
    main()
