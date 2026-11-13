# SPDX-License-Identifier: GPL-3.0-or-later
"""Probe a slowed pet's run mode (FUN_005deeb0) against d2d's PetFollow::follow
(pets.hpp): a requested run downgrades to a walk under Decrepify (state 0x3c),
confirmed against stock 1.14d (PetView::slowed was missing, always running).

From the worktree root with GAME_EXE set to stock 1.14d:
    mkdir -p build/ai-pet-slow
    uv run --no-sync --project tools/emu python tools/emu/ai_pet_decrepify_probe.py --cpp > build/ai-pet-slow/cases.cpp
    c++ -std=c++23 -Icomponents -Icomponents/rules -Icomponents/d2s -Icomponents/txt build/ai-pet-slow/cases.cpp -o build/ai-pet-slow/cases
    build/ai-pet-slow/cases > build/ai-pet-slow/cases.txt
    uv run --no-sync --project tools/emu python tools/emu/ai_pet_decrepify_probe.py --cases build/ai-pet-slow/cases.txt --exe "$GAME_EXE"

Movement, mode initialization and the slowed-state query are hooked; the
original chooses the movement mode and d2d's PetFollow chooses its mode.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/pets.hpp"
#include <iostream>
int main() {
    using namespace d2d::rules;
    for (int slowed = 0; slowed < 2; ++slowed) {
        PetView view; view.x = 100; view.y = 100; view.owner_x = 110; view.owner_y = 110;
        view.end_x = 110; view.end_y = 110; view.slowed = slowed != 0;
        Rng rng(1);
        auto area = [](int, int, int, int) { return 1; };
        auto move = [](const MercMove&) { return true; };
        PetFollow follower(view, rng, area, move);
        if (!follower.follow(0, true, 100, 0)) return 1;
        std::cout << slowed << ' ' << follower.act.move.mode << '\n';
    }
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
    unit = machine.alloc(0x100)
    state = {"slowed": 0, "mode": -1}
    machine.hook(0x639DF0, lambda current: state["slowed"], 2)
    machine.hook(0x5A61F0, lambda current: 0, 0)
    machine.hook(0x649070, lambda current: 0, 2)

    def initialize(current):
        state["mode"] = current.arg(1)
        return 0

    machine.hook(0x5A7E60, initialize, 3)
    machine.hook(0x5A7C20, lambda current: 1, 3)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 2:
        raise AssertionError(f"expected 2 cases, got {len(rows)}")
    mismatches = []
    for index, text in enumerate(rows):
        slowed, expected = map(int, text.split())
        if slowed != index:
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(slowed=slowed, mode=-1)
        machine.call(0x5DEEB0, 100, 100, ecx=0x1234, edx=unit, limit=1000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"case {index}: did not return")
        if state["mode"] != expected:
            mismatches.append((slowed, state["mode"], expected))
    if mismatches:
        for slowed, actual, expected in mismatches:
            print(f"mismatch slowed={slowed}: game_mode={actual}, d2d_mode={expected}")
        raise SystemExit(1)
    print(f"ok: {len(rows)} pet run-mode cases")


if __name__ == "__main__":
    main()
