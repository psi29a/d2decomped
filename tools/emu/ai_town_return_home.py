# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare FUN_005e6860's home-distance branch with d2d's npc_heed.

--cpp emits a C++ case generator. The existing home command is supplied;
the distance and command-allocation boundaries are hooked.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/town_npcs.hpp"
#include <iostream>
int main() {
    using namespace d2d::rules;
    for (int offset = -32; offset <= 32; ++offset) {
        NpcBrain brain; brain.homed = true; brain.home_x = 100 + offset; brain.home_y = 100;
        NpcVisitor visitor; visitor.present = true; visitor.distance = 10; visitor.x = 110; visitor.y = 100;
        const auto result = npc_heed(brain, 100, 100, visitor);
        std::cout << offset << ' ' << (result && result->kind == NpcAct::Kind::stand) << ' '
                  << brain.linger_x << ' ' << brain.linger_y << ' ' << brain.linger_count << ' ' << brain.linger_stand << '\n';
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
    unit, home, command = (machine.alloc(0x100) for _ in range(3))
    state = {"offset": 0, "stands": 0}
    machine.hook(0x58EEF0, lambda current: home, 1)
    machine.hook(0x5DC5C0, lambda current: abs(state["offset"]), 1)
    machine.hook(0x58EFA0, lambda current: command, 2)

    def stand(current):
        """Hook: record a stand action, asserting its fixed 10-frame duration."""
        if current.arg(0) != 10:
            raise AssertionError("unexpected stand duration")
        state["stands"] += 1
        return 0

    machine.hook(0x5DE080, stand, 1)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 65:
        raise AssertionError(f"expected 65 cases, got {len(rows)}")
    for index, row in enumerate(rows):
        offset, *expected = map(int, row.split())
        if offset != index - 32:
            raise AssertionError(f"case {index}: unexpected input order")
        state.update(offset=offset, stands=0)
        machine.w32(home + 0x0C, 100 + offset)
        machine.w32(home + 0x10, 100)
        machine.mu.mem_write(command, bytes(0x100))
        result = machine.call(0x5E6860, 0, 16, ecx=0x1234, edx=unit, limit=1000) & 0xFF
        values = [machine.r32(command + offset) for offset in (0x0C, 0x10, 0x14, 0x18)]
        actual = [result, *values]
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != expected or state["stands"] != result:
            raise AssertionError(f"case {index}: game={actual}, d2d={expected}, stands={state['stands']}")
    print(f"ok: {len(rows)} town NPC return-home cases")


if __name__ == "__main__":
    main()
