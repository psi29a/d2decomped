# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare a merc's exploding-arrow hit with game.exe's FUN_005b07a0.

    D2_BUILD=<build> .venv/bin/python merc_exploding_hit.py \
        --d2d-answers d.txt --mutation-check

Generate d.txt with ``$D2_BUILD/tests/test_exploding_arrow --oracle``. The
stock function runs on the loaded 1.14d tables; only the owner/skill/level
accessors and final missile creator are hooked. The compared record is
``hit-sub-row skill level``: FUN_005b07a0's own return value is an internal
detail of the stock function, not something d2d's `plain_missile_hits` has
an equivalent for, so it's not compared.
"""
import argparse
import hashlib
import re
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX

import drlg
import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
ROGUE_MISSILE = 336
EXPLODING_ARROW = 41


def require(condition, message):
    """Stop when the stock call or d2d answer differs from the claim."""
    if not condition:
        raise RuntimeError(message)


def stock_answer():
    """Run FUN_005b07a0 on the real explodingarrow row."""
    executable = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(executable.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = drlg.boot()
    game = machine.alloc(0x200)
    owner = machine.alloc(0x100)
    missile = machine.alloc(0x100)
    target = machine.alloc(0x100)
    made = machine.alloc(0x100)
    machine.w32(missile, 3)
    machine.w32(missile + 4, EXPLODING_ARROW)
    creations = []

    machine.hook(0x552FD0, lambda _machine: owner, 0)
    machine.hook(0x64A280, lambda _machine: ROGUE_MISSILE, 1)
    machine.hook(0x64A210, lambda _machine: 7, 1)

    def create(emulator):
        """Record the D2SkillArgStrc handed to the final missile creator."""
        request = emulator.mu.reg_read(UC_X86_REG_EDX)
        creations.append((emulator.r32(request + 4), emulator.r32(request + 8),
                          emulator.r32(request + 0x10), emulator.r32(request + 0x2C),
                          emulator.r32(request + 0x30)))
        return made

    machine.hook(0x59FA30, create, 0)
    machine.call(0x5B07A0, target, ecx=game, edx=missile)
    require(creations == [(owner, missile, 656, ROGUE_MISSILE, 7)],
            f"unexpected FUN_005b07a0 creations: {creations}")
    return creations[0][2], creations[0][3], creations[0][4]


def d2d_answer(path):
    """Read the single numeric answer emitted among the regression test's log."""
    rows = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if re.fullmatch(r"-?\d+(?: +-?\d+){2}", line):
            rows.append(tuple(int(field) for field in line.split()))
    require(len(rows) == 1, f"expected one three-integer d2d answer in {path}, got {len(rows)}")
    return rows[0]


def answers_match(expected, actual):
    """The whole compared record must match."""
    return expected == actual


def mutation_check(answer, actual):
    """Prove the comparison rejects each field being wrong."""
    labels = ("sub-missile", "skill", "level")
    for index, label in enumerate(labels):
        mutant = list(answer)
        mutant[index] += 1
        require(not answers_match(tuple(mutant), actual), f"{label} mutation escaped")
        print(f"negative control caught: wrong {label}")


def main():
    """Run stock, compare d2d, and optionally exercise negative controls."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--d2d-answers", required=True)
    parser.add_argument("--mutation-check", action="store_true")
    arguments = parser.parse_args()
    stock = stock_answer()
    actual = d2d_answer(arguments.d2d_answers)
    require(answers_match(stock, actual), f"d2d {actual} != game.exe {stock}")
    print("ok: FUN_005b07a0 made the merc's explodingarrowexp2")
    if arguments.mutation_check:
        mutation_check(stock, actual)
        print("mutation check ok (3 ways)")


if __name__ == "__main__":
    main()
