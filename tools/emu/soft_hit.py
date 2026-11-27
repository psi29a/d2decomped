# SPDX-License-Identifier: GPL-3.0-or-later
"""Which damage records reach a monster's get-hit in game.exe 1.14d: FUN_0057cee0 by the record's flags.

    .venv/bin/python soft_hit.py [--exe /path/to/game.exe] [--dump ../../tests/soft_hit_cases.inc] [--mutation-check]

FUN_0057cee0 (ECX game, EDX attacker 0, stack the unit and the record) runs for real on a monster in mode 1 (NU), its
leaves hooked as tools/emu/crush_hit.py hooks them: the get-hit roll FUN_0057cb00 (logged; returns 0 or 1), the state
test FUN_006256b0 (state 0x15, stunned, on or off), the mode asks FUN_005a7e60 (logged), the refresh FUN_0064c040 and
event 4 (FUN_005a43a0, logged). A record "reaches the get-hit" when FUN_0057cb00 runs or mode 3 (GH) is asked. The record
flags (+4) include Static Field's 0x4001 (0x5c97d2, FUN_005c96a0) and Thorns' 0x4021 (FUN_005bf670): those take the 0x4000
branch, the refresh and event 4 only, stunned or not. d2d: record_gets_hit and hurt's rolls_get_hit (components/game/ai.cpp),
tests/game/test_soft_hit_no_get_hit.cpp, which replays the rows `--dump` writes.
Without --exe, $D2_MPQ_DIR/bin/game.exe. `--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import os
import sys
from pathlib import Path

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TAKE_RECORD = 0x57CEE0
MONSTER_GH = 3
FLAGS = (0x0001, 0x0004, 0x0005, 0x0006, 0x000C, 0x0014, 0x4001, 0x4004, 0x4005, 0x4021)
MUTATIONS = ("soft-gets-hit", "stun-skips-gh", "bit-2-gets-hit", "no-event-4")
mutation = None


def gets_hit(flags):
    """d2d's record_gets_hit: bit 4 without 2, 8 or 0x10 (the model under test)."""
    if mutation == "soft-gets-hit" and flags & 0x4000:
        return True
    if mutation == "bit-2-gets-hit" and flags & 6 == 6:
        return True
    return flags & (2 | 8 | 0x10) == 0 and flags & 4 != 0


def model(flags, stunned, roll):
    """The expected (get-hit roll run, GH asked, event 4 raised) for one record on a monster in NU."""
    if gets_hit(flags):
        rolled = not stunned
        staggered = stunned if mutation != "stun-skips-gh" else False
        staggered = staggered or (rolled and roll == 0)
        return rolled, staggered, mutation != "no-event-4"
    soft = flags & 0x4000 != 0 and flags & (2 | 8 | 0x10) == 0
    return False, False, soft and mutation != "no-event-4"


def run(exe, rows=None):
    """FUN_0057cee0 over flags x stunned x roll; returns the case count."""
    machine = emu.Emu(exe)
    game, target, rec = machine.alloc(0x1000), machine.alloc(0x200), machine.alloc(0x100)
    state = {"stunned": 0, "roll": 0}
    log = []
    machine.hook(0x620BB0, lambda _e: 0, 1)                                     # no room: the room test skipped
    machine.hook(0x639DF0, lambda _e: 0, 2)                                     # not state 0x36
    machine.hook(0x6256B0, lambda current: state["stunned"] if current.arg(1) == 0x15 else 0, 2)
    machine.hook(0x57CB00, lambda _e: log.append("roll") or state["roll"], 3)
    machine.hook(0x5A7E60, lambda current: log.append(("ask", current.arg(1))) or 0, 3)
    machine.hook(0x5A43A0, lambda _e: log.append("event4") or 0, 0)
    machine.hook(0x46C140, lambda _e: 1, 0)                                     # every mode present (KB too)
    for addr, nstack in ((0x5A54F0, 2), (0x5A7C20, 3), (0x5734C0, 0), (0x64C040, 1), (0x57CCB0, 2), (0x625480, 3),
                         (0x5A5650, 0), (0x5809D0, 5), (0x580A70, 5)):
        machine.hook(addr, lambda _e: 0, nstack)
    cases = 0
    for flags in FLAGS:
        for stunned in (0, 1):
            for roll in (0, 1):
                state.update(stunned=stunned, roll=roll)
                log.clear()
                machine.mu.mem_write(target, b"\0" * 0x200)
                machine.mu.mem_write(rec, b"\0" * 0x100)
                machine.w32(target, 1)
                machine.w32(target + 4, 60)
                machine.w32(target + 0x10, 1)
                machine.w32(rec + 4, flags)
                machine.w32(rec + 0x60, 0x10)                                  # a hit class set: FUN_0057ce30 not asked
                machine.call(TAKE_RECORD, target, rec, ecx=game, edx=0, limit=20000)
                seen = ("roll" in log, ("ask", MONSTER_GH) in log, "event4" in log)
                want = model(flags, stunned, roll)
                if seen != want:
                    raise AssertionError(f"FUN_0057cee0 flags {flags:#x} stunned {stunned} roll {roll}: game.exe {seen} {log}, model {want}")
                if rows is not None and roll == 0:
                    rows.append((flags, stunned, int(seen[0] or seen[1])))
                cases += 1
    return cases


def write_cases(path, rows):
    """Write game.exe's rows for tests/game/test_soft_hit_no_get_hit.cpp."""
    lines = ["// tools/emu/soft_hit.py --dump: FUN_0057cee0 on a monster in NU. A row: the record's flags (+4),",
             "// stunned (state 0x15), and whether it reaches the get-hit (FUN_0057cb00 run or GH asked)."]
    lines += [f"{{ {flags:#06x}, {str(bool(stunned)).lower()}, {str(bool(reaches)).lower()} }}," for flags, stunned, reaches in rows]
    path.write_text("\n".join(lines) + "\n")


def main():
    """Parse the arguments, verify game.exe, run, optionally dump the rows and run the mutations."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=Path(os.environ.get("D2_MPQ_DIR", ".")) / "bin" / "game.exe")
    parser.add_argument("--dump", type=Path, help="write the rows (tests/soft_hit_cases.inc)")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    rows = []
    print(f"ok: {run(str(args.exe), rows)} FUN_0057cee0 record-flag cases")
    if args.dump:
        write_cases(args.dump, rows)
        print(f"wrote {len(rows)} rows to {args.dump}")
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
