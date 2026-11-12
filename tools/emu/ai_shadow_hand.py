# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005eaf00 with d2d's shadow_hand_level on bounded inputs.

Use --cpp to print the C++ case generator; compile it with -Icomponents and
save its output, then pass that output with --cases and stock game.exe with
--exe. The skill-level lookup is hooked; no MPQs are loaded.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/shadows.hpp"
#include <iostream>
int main() {
    for (int owner = 0; owner < 2; ++owner)
        for (int summon = 0; summon <= 60; ++summon)
            for (int level = 0; level <= 40; ++level)
                std::cout << owner << ' ' << summon << ' ' << level << ' '
                          << d2d::rules::shadow_hand_level(summon, owner ? level : 1) << '\n';
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", action="store_true")
    parser.add_argument("--cases", type=Path)
    parser.add_argument("--exe", type=Path)
    args = parser.parse_args()
    if args.cpp:
        print(CPP, end="")
        return
    if not args.exe or not args.cases:
        parser.error("--cases and --exe are required unless --cpp is used")
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d")
    machine = emu.Emu(str(args.exe))
    think = machine.alloc(0x40)
    control = machine.alloc(0x40)
    machine.w32(think, control)
    state = {"level": 0, "calls": 0}

    def skill_level(current):
        state["calls"] += 1
        return state["level"]

    machine.hook(0x6442A0, skill_level, 3)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 2 * 61 * 41:
        raise AssertionError(f"expected 5002 cases, got {len(rows)}")
    for index, row in enumerate(rows):
        owner, summon, level, expected = map(int, row.split())
        if (owner, summon, level) != (index // (61 * 41), (index // 41) % 61, index % 41):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(level=level, calls=0)
        machine.w32(control + 0x1C, summon)
        actual = machine.call(0x5EAF00, 123, think, ecx=0, edx=0x1234 if owner else 0, limit=1000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"case {index}: did not return")
        if actual != expected or state["calls"] != owner:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}, lookup calls={state['calls']}")
    print(f"ok: {len(rows)} Shadow Warrior hand-level cases")


if __name__ == "__main__":
    main()
