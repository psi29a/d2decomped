# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare same-act player range filtering in FUN_005e8eb0 and FUN_005fc000.

--cpp emits a C++ case generator using the d2d chooser's ai_distance helper
and threshold. Units use static paths; null and differing-act cases are not
covered by d2d's single-level chooser representation.
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
    constexpr std::array<int, 9> deltas{-1021, -1020, -1019, -2, 0, 2, 1019, 1020, 1021};
    for (int dx : deltas)
        for (int dy : deltas)
            std::cout << dx << ' ' << dy << ' ' << (d2d::rules::ai_distance(dx, dy) < 0x3fc) << '\n';
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", action="store_true")
    parser.add_argument("--reverse", action="store_true", help="reverse function order for mutation checks")
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
    boss, player, boss_path, player_path = (machine.alloc(0x100) for _ in range(4))
    for unit, path in ((boss, boss_path), (player, player_path)):
        machine.view("unit", unit).type = 2
        machine.view("unit", unit).path = path
    machine.w32(boss_path + 0x0C, 1200)
    machine.w32(boss_path + 0x10, 1200)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    deltas = (-1021, -1020, -1019, -2, 0, 2, 1019, 1020, 1021)
    if len(rows) != len(deltas) ** 2:
        raise AssertionError(f"expected 81 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        dx, dy, expected = map(int, text.split())
        if (dx, dy) != (deltas[index // len(deltas)], deltas[index % len(deltas)]):
            raise AssertionError(f"case {index}: unexpected input order")
        machine.w32(player_path + 0x0C, 1200 + dx)
        machine.w32(player_path + 0x10, 1200 + dy)
        addresses = (0x5FC000, 0x5E8EB0) if args.reverse else (0x5E8EB0, 0x5FC000)
        for address in addresses:
            result = machine.call(address, ecx=boss, edx=player, limit=1000)
            if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or result != expected:
                raise AssertionError(f"{address:08x} case {index}: game={result}, d2d={expected}")
    print(f"ok: {len(rows) * 2} same-act boss range cases")


if __name__ == "__main__":
    main()
