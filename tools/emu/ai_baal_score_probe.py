# SPDX-License-Identifier: GPL-3.0-or-later
"""Probe Baal target scoring (FUN_005fbb00) against d2d's diablo_score: unlike
Diablo's own FUN_005e8530, Baal's copy counts cold resist whether or not the
player is in reach (confirmed against stock 1.14d; diablo_score's new
cold_in_reach=false covers Baal's chooser()/chooser_pick() calls, act4.cpp).

--cpp emits the compiled d2d case generator. Use --cases and --exe to run
the comparison; stats, reach, skills and line-of-sight are scripted.

From the worktree root with GAME_EXE set to stock 1.14d:
    mkdir -p build/ai-baal-score
    uv run --no-sync --project tools/emu python tools/emu/ai_baal_score_probe.py --cpp > build/ai-baal-score/cases.cpp
    c++ -std=c++23 -Icomponents -Icomponents/d2s -Icomponents/txt build/ai-baal-score/cases.cpp -o build/ai-baal-score/cases
    build/ai-baal-score/cases > build/ai-baal-score/cases.txt
    uv run --no-sync --project tools/emu python tools/emu/ai_baal_score_probe.py --cases build/ai-baal-score/cases.txt --exe "$GAME_EXE"
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/monsters.hpp"
#include <array>
#include <iostream>
int main() {
    using namespace d2d::rules;
    for (int reach = 0; reach < 2; ++reach)
        for (int cold : std::array{0, 30, 165, 330, 660}) {
            DiabloPick pick; pick.cold_resist = cold;
            std::cout << reach << ' ' << cold << ' ' << diablo_score(pick, 100, reach, true, false, false) << '\n';
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
    boss, target = machine.alloc(0x100), machine.alloc(0x100)
    state = {"reach": 0, "cold": 0}
    machine.hook(0x622C40, lambda current: state["reach"], 3)
    machine.hook(0x625480, lambda current: state["cold"] if current.arg(1) == 0x2B else 0, 3)
    machine.hook(0x639DF0, lambda current: 0, 2)
    machine.hook(0x621F20, lambda current: 100, 1)
    machine.hook(0x620190, lambda current: 0, 1)
    machine.hook(0x6201D0, lambda current: 0, 1)
    machine.hook(0x5DD290, lambda current: 1, 0)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 10:
        raise AssertionError(f"expected 10 cases, got {len(rows)}")
    mismatches = []
    for index, text in enumerate(rows):
        reach, cold, expected = map(int, text.split())
        if (reach, cold) != (index // 5, (0, 30, 165, 330, 660)[index % 5]):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(reach=reach, cold=cold)
        result = machine.call(0x5FBB00, boss, ecx=target, regs={"EAX": 0}, limit=3000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"case {index}: did not return")
        if result != expected:
            mismatches.append((reach, cold, result, expected))
    if mismatches:
        for reach, cold, result, expected in mismatches:
            print(f"mismatch reach={reach} cold={cold}: game={result}, d2d={expected}")
        raise SystemExit(1)
    print(f"ok: {len(rows)} Baal target-score cases")


if __name__ == "__main__":
    main()
