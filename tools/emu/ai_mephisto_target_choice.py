# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005f77c0's target selection with d2d's Mephisto think.

--cpp emits C++ cases. Nearby-unit search is hooked; the selection and its
random draws run in game.exe. See monster-ai.md for rerun instructions.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/thinks/act3.hpp"
#include <array>
#include <iostream>
int main() {
    using namespace d2d::rules;
    ThinkAway away = [](int, bool) { return false; };
    for (unsigned seed = 0; seed < 128; ++seed)
        for (int weak = 0; weak < 2; ++weak)
            for (int near = 0; near < 2; ++near)
                for (int chance1 : std::array{0, 50, 100})
                    for (int chance2 : std::array{0, 50, 100}) {
                        int state = 3, state2 = 1, phase = 2;
                        ThinkIn input; input.state = &state; input.state2 = &state2; input.state3 = &phase;
                        input.target = true; input.dist = 10; input.weak = weak; input.near_player = near;
                        input.aip[1] = chance1; input.aip[2] = chance2; input.skill[0] = true;
                        Rng rng(seed); Act3Thinks thinker{{"Mephisto", input, rng, away}};
                        const auto act = thinker.mephisto();
                        int target = act.act != MonAct::skill ? -1 : act.unit == ThinkUnit::target ? 0 :
                                     act.unit == ThinkUnit::second ? 1 : act.unit == ThinkUnit::none ? 2 : -1;
                        std::cout << seed << ' ' << weak << ' ' << near << ' ' << chance1 << ' '
                                  << chance2 << ' ' << target << '\n';
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
    game, unit, row, target, weak_unit, near_unit = (machine.alloc(0x200) for _ in range(6))
    unit_view = machine.view("unit", unit)
    state = {"weak": 0, "near": 0}

    def search(current):
        record = current.arg(0)
        current.w32(record + 0x0C, weak_unit if state["weak"] else 0)
        current.w32(record + 0x14, near_unit if state["near"] else 0)
        return 0

    machine.hook(0x5DD0B0, search, 3)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 4608:
        raise AssertionError(f"expected 4608 cases, got {len(rows)}")
    for index, text in enumerate(rows):
        seed, weak, near, chance1, chance2, expected = map(int, text.split())
        if (seed, weak, near, chance1, chance2) != (
                index // 36, index // 18 % 2, index // 9 % 2,
                (0, 50, 100)[index // 3 % 3], (0, 50, 100)[index % 3]):
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(weak=weak, near=near)
        machine.mu.mem_write(row + 0x5C, chance1.to_bytes(2, "little"))
        machine.mu.mem_write(row + 0x62, chance2.to_bytes(2, "little"))
        unit_view.seed_low, unit_view.seed_high = seed, 666
        result = machine.call(0x5F77C0, row, target, ecx=game, edx=unit, limit=1000)
        actual = 0 if result == target else 1 if result == weak_unit else 2 if result == 0 else -1
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}")
    print(f"ok: {len(rows)} Mephisto target-choice cases")


if __name__ == "__main__":
    main()
