# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the to-hit percentage FUN_004a7340 (the attack-rating tooltip) in stock game.exe 1.14d.

    uv run python hit_chance.py --exe /path/to/game.exe [--mutation-check]

FUN_004a7340 (one stack word, `ret 4`; 0 returns 0) turns an attack rating
(FUN_004a7300 of the player and the word) against the current monster's defense
into a percentage: the monster's defense comes from FUN_006538a0 for the
difficulty (FUN_0044dcd0, 0..2, else fatal 0x18e) and the monster class
(FUN_00466dc0, in MonStats, else fatal 0x193), scaled by 10 / 12 outside
hardcore-ish cases; then pct = ar * 100 / (ar + defense) (100 when both are 0, a
negative side moved to the other), result = level * pct * 2 / (row level + level),
clamped to 5..0x5f. The six-word FUN_006538a0 call reuses the four words the
caller pushed for FUN_0044dca0, which does not pop them: the hooks follow that.

The lookups are hooked over a fake MonStats row; the arithmetic is game.exe's own,
compared with a model written from the disassembly. `--mutation-check` breaks the
model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("no-scale", "scale-wrong", "negative-defense-kept", "negative-rating-kept", "both-zero-default", "clamp-high-0x60",
             "clamp-low-4", "difficulty-3-ok")
mutation = None
ROWS, ROW_SIZE = 8, 0x1A8


class Fatal(Exception):
    """game.exe's fatal exit with its error number."""


def signed(value):
    """A 32-bit word as a signed int."""
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & 0x80000000 else value


def divide(top, bottom):
    """C signed division (idiv): truncates toward zero."""
    quotient = abs(top) // abs(bottom)
    return quotient if (top < 0) == (bottom < 0) else -quotient


def model(row_level, row_flag, tier, rating, defense, level, hardcore_like, difficulty):
    """The percentage FUN_004a7340 returns for these scripted lookups."""
    scale = row_flag != 1 and not hardcore_like and difficulty != 0
    if scale and mutation != "no-scale":
        defense = divide(defense * 10, 12 if mutation != "scale-wrong" else 10)
    if defense < 0 and mutation != "negative-defense-kept":
        rating, defense = rating - defense, 0
    if rating < 0 and mutation != "negative-rating-kept":
        defense, rating = defense - rating, 0
    if rating == 0 and defense == 0:
        pct = 100 if mutation != "both-zero-default" else 0
    else:
        pct = divide(rating * 100, defense + rating)
    result = divide(level * pct * 2, row_level + level)
    low = 4 if mutation == "clamp-low-4" else 5
    if result < low:
        return low
    return min(result, 0x60 if mutation == "clamp-high-0x60" else 0x5F)


def run(exe):
    """Random scripted worlds; returns the summary line."""
    machine = emu.Emu(str(exe))
    rng = random.Random(0x4A7340)
    state = {}
    array = machine.alloc(ROWS * ROW_SIZE)
    block = machine.alloc(0x1000)
    machine.w32(block + 0xA78, array)
    machine.w32(block + 0xA80, ROWS)
    machine.w32(0x744304, block)
    player = machine.alloc(0x40)
    machine.hook(0x463DD0, lambda current: player, 0)
    machine.hook(0x44DCD0, lambda current: state["difficulty"], 0)
    machine.hook(0x44DCC0, lambda current: state["hardcore"], 0)
    machine.hook(0x466DC0, lambda current: state["class"], 0)
    machine.hook(0x44DCA0, lambda current: 0x4444, 0)
    machine.hook(0x408090, lambda current: state.__setitem__("error", current.arg(0)) or 0, 0)
    machine.hook(0x408A60, lambda current: 0, 0)

    def exit_now(current):
        """FUN_00681e09: the fatal exit."""
        raise Fatal(state["error"])

    def defense(current):
        """FUN_006538a0 (six words: class, 0x4444, tier, row level, 2, buffer): write the scripted defense at buffer +8."""
        state["defense_args"] = tuple(current.arg(index) for index in range(6))
        machine.w32(current.arg(5) + 8, state["defense"])
        return 0

    machine.hook(0x681E09, exit_now, 0)
    machine.hook(0x6538A0, defense, 6)
    machine.hook(0x4A7300, lambda current: state["rating"], 0)
    machine.hook(0x625480, lambda current: state["level"], 3)
    cases = 0
    for case in range(3000):
        cls = rng.randrange(ROWS)
        row_levels = [rng.randrange(1, 90) for _ in range(3)]
        flag = rng.choice((0, 1, 2))
        base = array + cls * ROW_SIZE
        machine.mu.mem_write(base + 0x4C, bytes((flag,)))
        for index, value in enumerate(row_levels):
            machine.mu.mem_write(base + 0xAA + 2 * index, struct.pack("<h", value))
        state.update(difficulty=rng.choice((0, 1, 2, 2, 1)), hardcore=rng.choice((0, 0, 1)), **{"class": cls},
                     rating=rng.choice((rng.randrange(-300, 3000), rng.randrange(0, 60), 0)), defense=rng.choice((rng.randrange(-300, 3000), rng.randrange(-40, 0), rng.randrange(-40, 0), 0)),
                     level=rng.randrange(1, 100))
        state["defense"] &= 0xFFFFFFFF
        state["rating"] &= 0xFFFFFFFF
        if mutation == "difficulty-3-ok" and case % 50 == 0:
            state["difficulty"] = 3
        word = rng.choice((1, rng.getrandbits(32) | 1))
        if case % 97 == 0:
            word = 0
        state["error"] = None
        try:
            got = machine.call(0x4A7340, word, limit=5000)
            fatal = None
        except Fatal as error:
            got, fatal = None, error.args[0]
        if word == 0:
            if got != 0:
                raise AssertionError("a null word didn't return 0")
            cases += 1
            continue
        if mutation == "difficulty-3-ok" and state["difficulty"] == 3:
            raise AssertionError("mutation: difficulty 3 accepted")
        want = model(row_levels[state["difficulty"]], flag, state["difficulty"], signed(state["rating"]), signed(state["defense"]),
                     state["level"], state["hardcore"], state["difficulty"])
        if fatal is not None or got != want:
            raise AssertionError(f"case {case}: ret {got} fatal {fatal}, model {want}; state {state}")
        if state["defense_args"][:3] != (cls, 0x4444, state["difficulty"]) or state["defense_args"][3] != row_levels[state["difficulty"]]:
            raise AssertionError(f"FUN_006538a0 got {state['defense_args']}")
        cases += 1
    # The fatal lines: difficulty 3 and a class outside MonStats.
    for difficulty, cls, code in ((3, 0, 0x18E), (5, 0, 0x18E), (0, ROWS, 0x193), (0, 0xFFFFFFFF, 0x193)):
        state.update(difficulty=difficulty, hardcore=0, defense=0, rating=0, level=1)
        state["class"] = cls
        try:
            machine.call(0x4A7340, 1, limit=5000)
            raise AssertionError(f"difficulty {difficulty} class {cls:#x} didn't end the game")
        except Fatal as error:
            if error.args[0] != code:
                raise AssertionError(f"fatal {error.args[0]:#x}, game.exe line {code:#x}") from None
        cases += 1
    return f"ok: {cases} to-hit percentages and fatal lines"


def main():
    """Entry point: run the checks, or every mutation and require each to fail."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(run(exe))
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(exe)
            except AssertionError:
                print(f"mutation {name}: failed as required")
                continue
            finally:
                mutation = None
            raise SystemExit(f"mutation {name} was not caught")


if __name__ == "__main__":
    main()
