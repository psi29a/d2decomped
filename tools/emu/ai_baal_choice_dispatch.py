# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005fc630's blocked-line dispatch with the d2d Baal think.

Uses ai_baal_blocked_choice.py's compiled C++ cases. The dispatcher and the
blocked-line chooser run for real; proximity, life, line and spell lookups
are hooked. See monster-ai.md for the bounded inputs.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    """Emit --cpp cases, or compare --cases against --exe and report mismatches."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d")
    machine = emu.Emu(str(args.exe))
    game, boss, target, control, point = (machine.alloc(0x100) for _ in range(5))
    machine.view("unit", target).type = 1
    boss_view = machine.view("unit", boss)
    state = {"casting": 0, "distance": 0}
    machine.hook(0x621F20, lambda current: 80, 1)
    machine.hook(0x5FC0C0, lambda current: state["casting"], 0)
    machine.hook(0x5DC480, lambda current: state["distance"], 1)
    machine.hook(0x622C40, lambda current: 0, 3)
    machine.hook(0x5DD290, lambda current: 0, 0)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 1536:
        raise AssertionError(f"expected 1536 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        seed, difficulty, players, casting, target_x, expected = map(int, text.split())
        if (seed, difficulty, players, casting, target_x) != (
                index // 24, index // 12 % 2, (1, 2)[index // 6 % 2], index // 3 % 2,
                (0, 90, 120)[index % 3]):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(casting=casting, distance=max(target_x - 1, 0))
        machine.view("game", game).difficulty = difficulty
        machine.view("ai_control", control).scratch_one = 0
        boss_view.seed_low, boss_view.seed_high = seed, 666
        result = machine.call(0x5FC630, game, control, 0, players, point,
                              ecx=boss, regs={"EAX": target}, limit=2000)
        result = 2 if result == 6 else result
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or result != expected:
            raise AssertionError(f"case {index}: game={result}, d2d={expected}")
    print(f"ok: {len(rows)} Baal blocked-line dispatch cases")


if __name__ == "__main__":
    main()
