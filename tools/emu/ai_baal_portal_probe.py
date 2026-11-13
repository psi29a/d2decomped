# SPDX-License-Identifier: GPL-3.0-or-later
"""Probe Baal's chamber-portal search boundary (FUN_005ef5e0) against d2d's
baal_to_stairs (act5.cpp): the boundary itself, 25, is in range (confirmed
against stock 1.14d; act5.cpp used `dist >= 25` to reject, one off).

From the worktree root with GAME_EXE set to stock 1.14d:
    mkdir -p build/ai-baal-portal
    uv run --no-sync --project tools/emu python tools/emu/ai_baal_portal_probe.py --cpp > build/ai-baal-portal/cases.cpp
    c++ -std=c++23 -Icomponents -Icomponents/d2s -Icomponents/txt build/ai-baal-portal/cases.cpp -o build/ai-baal-portal/cases
    build/ai-baal-portal/cases > build/ai-baal-portal/cases.txt
    uv run --no-sync --project tools/emu python tools/emu/ai_baal_portal_probe.py --cases build/ai-baal-portal/cases.txt --exe "$GAME_EXE"

Both fixtures use static paths and scripted footprint size one, so the
original's unit-range distance equals d2d's center distance in these cases.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/monsters.hpp"
#include <iostream>
int main() {
    for (int distance = 20; distance <= 30; ++distance)
        std::cout << distance << ' ' << (d2d::rules::ai_distance(distance, 0) <= 25) << '\n';
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
    boss, portal, boss_path, portal_path, search = (machine.alloc(0x100) for _ in range(5))
    for unit, path in ((boss, boss_path), (portal, portal_path)):
        machine.view("unit", unit).type = 2
        machine.view("unit", unit).path = path
    machine.view("unit", portal)["class"] = 0x233
    machine.w32(boss_path + 0x0C, 100)
    machine.w32(boss_path + 0x10, 100)
    machine.w32(portal_path + 0x10, 100)
    machine.w32(search + 4, 25)
    machine.hook(0x620510, lambda current: 1, 1)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 11:
        raise AssertionError(f"expected 11 cases, got {len(rows)}")
    mismatches = []
    for index, text in enumerate(rows):
        distance, expected = map(int, text.split())
        if distance != index + 20:
            raise AssertionError(f"case {index}: unexpected input order")
        machine.w32(portal_path + 0x0C, 100 + distance)
        result = machine.call(0x5EF5E0, portal, search, edx=boss, limit=2000)
        actual = int(result == portal)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise AssertionError(f"case {index}: did not return")
        if actual != expected:
            mismatches.append((distance, actual, expected))
    if mismatches:
        for distance, actual, expected in mismatches:
            print(f"mismatch distance={distance}: game_accepts={actual}, d2d_accepts={expected}")
        raise SystemExit(1)
    print(f"ok: {len(rows)} portal-range cases")


if __name__ == "__main__":
    main()
