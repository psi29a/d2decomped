# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005e4830's distant-owner branches with necropet_think.

--cpp emits C++ cases; see pet-ai.md for rerun commands. Owner lookup,
distance and follow actions are hooked; the original chooses the branch.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/pets.hpp"
#include <array>
#include <iostream>
int main() {
    using namespace d2d::rules;
    for (int gap = 29; gap <= 52; ++gap)
        for (int speed_case = 0; speed_case < 3; ++speed_case) {
            PetView view; view.x = 100; view.y = 100; view.size = 1;
            view.owner_x = 101 + gap; view.owner_y = 100;
            view.end_x = view.owner_x; view.end_y = view.owner_y;
            view.velocity = speed_case ? 10 : 0; view.run = speed_case == 1 ? 15 : 25;
            Rng rng(1);
            auto area = [](int, int, int, int) { return 1; };
            auto move = [](const MercMove&) { return true; };
            const auto act = necropet_think(view, rng, area, move);
            int branch = act.kind == PetAct::Kind::teleport ? 3 : act.kind == PetAct::Kind::moved ? 0 : -1;
            std::cout << gap << ' ' << speed_case << ' ' << branch << ' '
                      << (branch == 0 ? act.move.pct : 0) << '\n';
        }
}
'''


def main():
    """Emit --cpp cases, or compare --cases against --exe and report mismatches."""
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
    game, unit, owner, think, control, row = (machine.alloc(0x200) for _ in range(6))
    machine.view("ai_think", think).control = control
    machine.view("ai_think", think).monstats = row
    state = {"gap": 0, "branch": -1, "pct": 0}
    machine.hook(0x58F0D0, lambda current: owner, 0)
    machine.hook(0x5DC380, lambda current: state["gap"], 0)

    def follow(current):
        """Hook: record which follow() branch and pace ran."""
        state["branch"], state["pct"] = current.arg(1), current.arg(3)
        return 1

    machine.hook(0x5E3EA0, follow, 5)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 72:
        raise AssertionError(f"expected 72 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        gap, speed_case, branch, pct = map(int, text.split())
        if (gap, speed_case) != (index // 3 + 29, index % 3):
            raise AssertionError(f"case {index}: unexpected input order")
        velocity, run = ((0, 25), (10, 15), (10, 25))[speed_case]
        machine.mu.mem_write(row + 0x32, velocity.to_bytes(2, "little"))
        machine.mu.mem_write(row + 0x34, run.to_bytes(2, "little"))
        state.update(gap=gap, branch=-1, pct=0)
        machine.call(0x5E4830, think, ecx=game, edx=unit, limit=2000)
        actual = (state["branch"], state["pct"])
        expected = (branch, pct)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} NecroPet distant-owner cases")


if __name__ == "__main__":
    main()
