# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005df990's 100-percent damage roll with d2d object_blast.

--cpp emits a C++ generator. The fixture supplies an object row with 100
percent damage and starts game.exe after the guaranteed-hit draw; it does
not check the caller's hit calculation or other object damage percentages.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/shrines.hpp"
#include <algorithm>
#include <array>
#include <iostream>
int main() {
    using namespace d2d::rules;
    for (unsigned seed = 0; seed < 128; ++seed)
        for (int life : std::array{32, 1024, 8192}) {
            Rng before(seed); before.next();
            Rng rng(seed); const int damage = object_blast(life, 0, 0, 0, rng);
            const int low = std::max(life >> 5, 1), high = std::max(life >> 3, low + 1);
            std::cout << seed << ' ' << life << ' ' << low << ' ' << high << ' ' << before.low << ' '
                      << before.high << ' ' << damage << ' ' << rng.low << ' ' << rng.high << '\n';
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
    unit, row = machine.alloc(0x100), machine.alloc(0x200)
    view = machine.view("unit", unit)
    machine.mu.mem_write(row + 0x19C, bytes([100]))
    machine.hook(0x640E90, lambda current: row, 1)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 384:
        raise AssertionError(f"expected 384 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        seed, life, low, high, before_low, before_high, damage, after_low, after_high = map(int, text.split())
        if (seed, life) != (index // 3, (32, 1024, 8192)[index % 3]):
            raise AssertionError(f"case {index}: unexpected input order")
        view.seed_low, view.seed_high = before_low, before_high
        result = machine.call(0x5DF990, edx=high, regs={"EAX": unit, "EDI": low}, limit=1000)
        actual = (result, view.seed_low, view.seed_high)
        expected = (damage, after_low, after_high)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} object blast damage-roll cases")


if __name__ == "__main__":
    main()
