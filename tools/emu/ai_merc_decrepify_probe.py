# SPDX-License-Identifier: GPL-3.0-or-later
"""Probe a slowed merc's run mode (FUN_005dede0, MONSTERAI_CommandMoveToCoords)
against d2d's hireable_think footsteps call (merc.hpp): a requested run
downgrades to a walk under Decrepify (state 0x3c), confirmed against stock
1.14d (MercView::slowed was missing, always running).

From the worktree root with GAME_EXE set to stock 1.14d:
    mkdir -p build/ai-merc-slow
    uv run --no-sync --project tools/emu python tools/emu/ai_merc_decrepify_probe.py --cpp > build/ai-merc-slow/cases.cpp
    c++ -std=c++23 -Icomponents -Icomponents/rules -Icomponents/d2s -Icomponents/txt build/ai-merc-slow/cases.cpp -o build/ai-merc-slow/cases
    build/ai-merc-slow/cases > build/ai-merc-slow/cases.txt
    uv run --no-sync --project tools/emu python tools/emu/ai_merc_decrepify_probe.py --cases build/ai-merc-slow/cases.txt --exe "$GAME_EXE"

Movement, mode initialization and the slowed-state query are hooked; the
original chooses the movement mode and d2d's hireable_think chooses its mode
the same way the pet's ai_pet_decrepify_probe.py does for FUN_005deeb0 (the
two functions share the same decrepify-gated Run/Walk pattern).
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/merc.hpp"
#include <iostream>
int main() {
    using namespace d2d::rules;
    for (int slowed = 0; slowed < 2; ++slowed) {
        MercView view; view.mode = kMonsterNeutral; view.x = 5000; view.y = 5000;
        view.owner_x = 5100; view.owner_y = 5000; view.owner_mode = kMonsterNeutral;
        view.end_x = 5100; view.end_y = 5000; view.cursor = 0;
        for (auto& spot : view.ring) spot = { 5050, 5000 };
        view.slowed = slowed != 0;
        Rng rng(1);
        int calls = 0;
        auto area = [](int, int, int, int) { return 1; };
        auto move = [&](const MercMove&) { ++calls; return calls == 2; };
        const auto act = hireable_think(view, rng, area, move);
        std::cout << slowed << ' ' << act.move.mode << '\n';
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
    unit = machine.alloc(0x100)
    state = {"slowed": 0, "mode": -1}
    machine.hook(0x639DF0, lambda current: state["slowed"], 2)
    machine.hook(0x5A61F0, lambda current: 0, 0)
    machine.hook(0x649070, lambda current: 0, 2)

    def initialize(current):
        """Hook: record the mode FUN_005a7e60 was asked to set."""
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
        machine.call(0x5DEDE0, 100, 100, ecx=0x1234, edx=unit, limit=1000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"case {index}: did not return")
        if state["mode"] != expected:
            mismatches.append((slowed, state["mode"], expected))
    if mismatches:
        for slowed, actual, expected in mismatches:
            print(f"mismatch slowed={slowed}: game_mode={actual}, d2d_mode={expected}")
        raise SystemExit(1)
    print(f"ok: {len(rows)} merc run-mode cases")


if __name__ == "__main__":
    main()
