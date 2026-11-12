# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005fc830's live-clone count with d2d's clone gate.

--cpp emits cases. The fixture uses synchronized life and death modes and
models the candidate as either Baal himself or one of his clones; see
monster-ai.md for rerun commands and limits.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "game/ai.hpp"
#include <iostream>
int main() {
    for (int self = 0; self < 2; ++self)
        for (int mode = 0; mode < 3; ++mode)
            for (int count = 0; count < 4; ++count) {
                d2d::game::Monster monster;
                monster.mode = mode == 0 ? "NU" : mode == 1 ? "DT" : "DD";
                monster.hit_points = mode == 0 ? 1 : 0;
                const bool is_clone = !self;
                const int result = count + int(monster.alive() && is_clone);
                std::cout << self << ' ' << mode << ' ' << count << ' ' << result << '\n';
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
    boss, candidate, context, counter = (machine.alloc(0x100) for _ in range(4))
    machine.w32(context, boss)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 24:
        raise AssertionError(f"expected 24 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        self_unit, mode, count, expected = map(int, text.split())
        if (self_unit, mode, count) != (index // 12, index // 4 % 3, index % 4):
            raise AssertionError(f"case {index}: unexpected input order")
        selected = boss if self_unit else candidate
        machine.view("unit", selected).mode = (1, 0, 12)[mode]
        machine.w32(counter, count)
        machine.call(0x5FC830, counter, ecx=selected, edx=context, limit=1000)
        actual = machine.r32(counter)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} Baal live-clone count cases")


if __name__ == "__main__":
    main()
