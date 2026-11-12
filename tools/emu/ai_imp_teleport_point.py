# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005df850's random destination with d2d's imp_teleport.

--cpp emits a C++ case generator. See monster-ai.md for rerun commands.
The game and skill-action boundaries are hooked; no MPQs are loaded.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/thinks/thinker.hpp"
#include <iostream>
int main() {
    d2d::rules::ThinkAway away = [](int, bool) { return false; };
    for (unsigned seed = 0; seed < 64; ++seed)
        for (int radius = 0; radius <= 16; ++radius) {
            d2d::rules::ThinkIn input;
            input.self_x = 123; input.self_y = 321; input.imp_aip[0][1] = radius;
            d2d::rules::Rng rng(seed);
            d2d::rules::Thinker thinker{"Imp", input, rng, away};
            const auto act = thinker.imp_teleport();
            std::cout << seed << ' ' << radius << ' ' << act.x << ' ' << act.y << ' ' << rng.low << ' ' << rng.high << '\n';
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
    unit_address, path_address = machine.alloc(0x100), machine.alloc(0x40)
    unit = machine.view("unit", unit_address)
    unit.type, unit.path = 2, path_address
    machine.w32(path_address + 0x0C, 123)
    machine.w32(path_address + 0x10, 321)
    actions = []
    machine.hook(0x554010, lambda current: 0x1234, 0)

    def action(current):
        actions.append((current.arg(3), current.arg(4)))
        return 0

    machine.hook(0x5DEAD0, action, 5)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 1088:
        raise AssertionError(f"expected 1088 cases, got {len(rows)}")
    for index, row in enumerate(rows):
        seed, radius, x, y, low, high = map(int, row.split())
        if (seed, radius) != divmod(index, 17):
            raise AssertionError(f"case {index}: unexpected input order")
        unit.seed_low, unit.seed_high = seed, 666
        actions.clear()
        machine.call(0x5DF850, 101, 202, ecx=unit_address, edx=radius, limit=1000)
        actual = (actions, unit.seed_low, unit.seed_high)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != ([(x, y)], low, high):
            raise AssertionError(f"case {index}: game={actual}, d2d={([(x, y)], low, high)}")
    print(f"ok: {len(rows)} imp teleport destination cases")


if __name__ == "__main__":
    main()
