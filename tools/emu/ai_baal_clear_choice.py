# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005fc450's clear-line choice with the d2d Baal think.

--cpp emits cases. Reuses the blocked-line generator with a clear line and
maps clone action 15 separately. Life, distance and spell checks are hooked.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_EIP

import ai_baal_blocked_choice
import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def cpp():
    """Build the clear-line variant of ai_baal_blocked_choice.CPP."""
    source = ai_baal_blocked_choice.CPP
    replacements = (
        ("input.reach = false; input.line_clear = false;", "input.reach = false; input.line_clear = true;"),
        ("if (act.act == MonAct::idle) choice = 1;", "if (act.act == MonAct::idle) choice = act.n == 5 ? 15 : 1;"),
    )
    for before, after in replacements:
        if source.count(before) != 1:
            raise AssertionError("case generator no longer matches the shared template")
        source = source.replace(before, after)
    return source


def main():
    """Emit --cpp cases, or compare --cases against --exe and report mismatches."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", action="store_true")
    parser.add_argument("--cases", type=Path)
    parser.add_argument("--exe", type=Path)
    args = parser.parse_args()
    if args.cpp:
        print(cpp(), end="")
        return
    if not args.exe or not args.cases:
        parser.error("--cases and --exe are required unless --cpp is used")
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d")
    machine = emu.Emu(str(args.exe))
    game, boss, target, control = (machine.alloc(0x100) for _ in range(4))
    boss_view = machine.view("unit", boss)
    state = {"casting": 0, "distance": 0, "difficulty": 0}
    machine.hook(0x621F20, lambda current: 80, 1)
    machine.hook(0x5FC0C0, lambda current: state["casting"], 0)
    machine.hook(0x5DC380, lambda current: state["distance"], 1)
    machine.hook(0x620BB0, lambda current: 0, 1)

    def difficulty(current):
        """Hook: feed the scripted difficulty into the think's AI params."""
        current.w32(current.mu.reg_read(UC_X86_REG_EDX) + 0x0C, state["difficulty"])
        return 0

    machine.hook(0x573930, difficulty, 2)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 1536:
        raise AssertionError(f"expected 1536 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        seed, mode, players, casting, target_x, expected = map(int, text.split())
        if (seed, mode, players, casting, target_x) != (
                index // 24, index // 12 % 2, (1, 2)[index // 6 % 2], index // 3 % 2,
                (0, 90, 120)[index % 3]):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(casting=casting, distance=max(target_x - 2, 0), difficulty=mode)
        machine.view("game", game).difficulty = mode
        machine.view("ai_control", control).scratch_two = 0
        boss_view.seed_low, boss_view.seed_high = seed, 666
        result = machine.call(0x5FC450, game, control, target, players, int(target_x > 76),
                              int(target_x > 101), 0, 0, regs={"EAX": boss}, limit=1000)
        result = 2 if result == 6 else result
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or result != expected:
            raise AssertionError(f"case {index}: game={result}, d2d={expected}")
    print(f"ok: {len(rows)} Baal clear-line choice cases")


if __name__ == "__main__":
    main()
