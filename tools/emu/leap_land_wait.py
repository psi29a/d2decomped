# SPDX-License-Identifier: GPL-3.0-or-later
"""FUN_005da120's wait for a leaper not on its landing, run in game.exe and diffed against d2d.

    D2_MPQ_DIR=<folder with bin/game.exe> .venv/bin/python leap_land_wait.py [N [SEED]] [--cases-out FILE | --d2d-answers FILE] [--mutation-check]

A monster (unit type 1, MonStats class with a chosen BaseId, row +2) at one subtile with the landing event's record
(+0x18 x, +0x1c y) at another: game.exe's FUN_005da120 runs for real (FUN_00463860, FUN_006417f0, FUN_006212c0, the
path's x / y getters hooked as the accessors they are) and must return 0 with the sequence position (unit +0x38) set to
its row and the length (+0x48) one row longer. Rows: BaseId 0x4e 8, 0x21c 12 within 2 of the landing else 10, any other 0.
The model below is written from the decompile. `--cases-out FILE` also writes the cases for d2d's `leap_wait`
(tests/game/test_leap_landing_wait.cpp `--oracle FILE > answers`), and `--d2d-answers FILE` compares that output. A unit on the landing returns 1
(checked, its effects hooked, not ported). Not covered: the landed branch's own effects, a player.
"""
import argparse
import random
import struct
import sys

import _monster_world as mw
from _monster_world import require

MUTATIONS = ("row-0x4e", "row-near-10", "row-far-12", "near-limit-3", "length-keeps", "position-no-low-byte", "other-row-8")
mutation = None
BASES = (0x4e, 0x21c, 0x21d, 0x21b, 0, 5, 0x7f, 0x4f)
CLASSES = 800


def ai_distance(dx, dy):
    """FUN_006417f0's formula on the deltas."""
    dx, dy = abs(dx), abs(dy)
    return (dy + dx * 2) // 2 if dy < dx else (dx + dy * 2) // 2


def model(base, distance, position, length):
    """(position, length) after the wait, as the decompile reads."""
    row = 0
    if base == (0x4f if mutation == "row-0x4e" else 0x4e):
        row = 8
    elif base == 0x21c:
        near = distance < (3 if mutation == "near-limit-3" else 2)
        row = (10 if mutation == "row-near-10" else 12) if near else (12 if mutation == "row-far-12" else 10)
    elif mutation == "other-row-8":
        row = 8
    low = 0 if mutation == "position-no-low-byte" else position & 0xff
    keep = length if mutation == "length-keeps" else (length & 0xffffff00) + 0x100
    return (low + row * 256) & 0xffffffff, (keep if mutation == "length-keeps" else keep) & 0xffffffff


def make_cases(count, seed):
    """Cases (base, unit x, y, landing x, y, position, length): never on the landing."""
    rng = random.Random(seed)
    cases = []
    while len(cases) < count:
        base = rng.choice(BASES)
        ux, uy = rng.randrange(0, 4000), rng.randrange(0, 4000)
        lx, ly = ux + rng.choice((-1, 0, 1, 2, 3, rng.randrange(-30, 30))), uy + rng.choice((-1, 0, 1, 2, 3, rng.randrange(-30, 30)))
        if (lx, ly) != (ux, uy):
            cases.append((base, ux, uy, lx, ly, rng.getrandbits(32), rng.getrandbits(32)))
    return cases


def run_game():
    """Build the world once; return run(case) -> (eax, position, length)."""
    world = mw.World()
    machine = world.m
    state = {}
    machine.hook(0x6488C0, lambda current: state["x"], 1)    # the path's x and y getters
    machine.hook(0x648900, lambda current: state["y"], 1)
    # The landed branch's callees (stack arguments popped), hooked to nothing: it only has to return 1.
    for address, popped in {0x64EC10: 4, 0x648C30: 2, 0x648CE0: 2, 0x648CF0: 2, 0x644660: 2, 0x649180: 2, 0x643CE0: 3, 0x5DA020: 3,
                            0x5544B0: 0, 0x571B70: 0, 0x540E60: 2, 0x5417D0: 4}.items():
        machine.hook(address, lambda current: 0, popped)
    table, rows = machine.alloc(0x1000), machine.alloc(CLASSES * 0x1A8)
    machine.w32(0x744304, table)
    machine.w32(table + 0xA78, rows)
    machine.w32(table + 0xA80, CLASSES)
    unit, path, grid, record = (machine.alloc(0x100) for _ in range(4))
    machine.w32(unit, 1)
    machine.w32(unit + 0x2C, path)
    machine.w32(path + 0x1C, grid)

    def run(case):
        base, ux, uy, lx, ly, position, length = case
        cls = base % CLASSES
        machine.mu.mem_write(rows + cls * 0x1A8 + 2, struct.pack("<h", base))
        machine.w32(unit + 4, cls)
        machine.w32(unit + 0x38, position)
        machine.w32(unit + 0x48, length)
        machine.w32(record + 0x18, lx)
        machine.w32(record + 0x1C, ly)
        state["x"], state["y"] = ux, uy
        eax = world.run(0x5DA120, 0, record, regs={"eax": unit}, nstack=2)
        return eax, machine.r32(unit + 0x38), machine.r32(unit + 0x48), (world, unit, record, lx, ly)

    return run, machine, state, unit, path, record


def check(count, seed, cases_out=None, d2d_answers=None):
    """Run the cases in game.exe against the model (and d2d's answers); return how many matched."""
    run, machine, state, unit, path, record = run_game()
    cases = make_cases(count, seed)
    answers = []
    for case in cases:
        base, ux, uy, lx, ly, position, length = case
        eax, got_position, got_length, _ = run(case)
        want = model(base, ai_distance(lx - ux, ly - uy), position, length)
        require(eax == 0, f"{case}: returned {eax}, not 0")
        require((got_position, got_length) == want, f"{case}: game.exe {(got_position, got_length)}, model {want}")
        answers.append((base, ai_distance(lx - ux, ly - uy), position, length, got_position, got_length))
    for case in cases[:50]:                                    # on the landing: it lands
        base, ux, uy = case[:3]
        eax = run((base, ux, uy, ux, uy, 0, 0))[0]
        require(eax == 1, f"on the landing: returned {eax}")
    if cases_out:
        with open(cases_out, "w", encoding="utf-8") as handle:
            for index, (base, distance, position, length, _, _) in enumerate(answers):
                handle.write(f"{index} {base} {distance} {position} {length}\n")
    if d2d_answers:
        with open(d2d_answers, encoding="utf-8") as handle:
            out = handle.read().split("\n")
        for index, answer in enumerate(answers):
            require(index < len(out) and out[index] == f"{index} {answer[4]} {answer[5]}", f"d2d case {index}: game.exe {answer[4:]}")
    return len(cases)


def main():
    """Command line."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("count", nargs="?", type=int, default=2000)
    parser.add_argument("seed", nargs="?", type=int, default=1)
    parser.add_argument("--cases-out")
    parser.add_argument("--d2d-answers")
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    if options.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                check(options.count, options.seed)
            except mw.Mismatch:
                print(f"caught: {name}")
                continue
            sys.exit(f"NOT caught: {name}")
        print(f"ok: {len(MUTATIONS)} deliberate model defects all caught")
        return
    print(f"ok: {check(options.count, options.seed, options.cases_out, options.d2d_answers)} leap landing wait cases")


if __name__ == "__main__":
    main()
