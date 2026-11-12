# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005f3960 with d2d's wisp candidate predicate.

--cpp emits C++ cases. Live monsters and their death modes are represented
directly; MonStats records are fake. Once the generator is compiled, pass
its output with --cases and stock game.exe with --exe.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "game/ai.hpp"
#include <array>
#include <iostream>
int main() {
    for (int mode = 0; mode < 3; ++mode)
        for (int base : std::array{117, 118, 119}) {
            d2d::game::Monster monster;
            monster.hit_points = 1;
            monster.mode = mode == 0 ? "NU" : mode == 1 ? "DT" : "DD";
            const bool candidate = monster.alive() && monster.mode != "DT" && monster.mode != "DD" && base == 118;
            std::cout << mode << ' ' << base << ' ' << candidate << '\n';
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
    unit, tables, monstats = (machine.alloc(0x1000) for _ in range(3))
    view = machine.view("unit", unit)
    view.type, view["class"] = 1, 0
    machine.w32(0x744304, tables)
    machine.w32(tables + 0xA78, monstats)
    machine.w32(tables + 0xA80, 200)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 9:
        raise AssertionError(f"expected 9 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        mode, base, expected = map(int, text.split())
        if (mode, base) != (index // 3, (117, 118, 119)[index % 3]):
            raise AssertionError(f"case {index}: unexpected input order")
        view.mode = (1, 0, 12)[mode]
        machine.mu.mem_write(monstats + 2, base.to_bytes(2, "little"))
        result = machine.call(0x5F3960, ecx=unit, limit=1000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or result != expected:
            raise AssertionError(f"case {index}: game={result}, d2d={expected}")
    print(f"ok: {len(rows)} wisp candidate cases")


if __name__ == "__main__":
    main()
