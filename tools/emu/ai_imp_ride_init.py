# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the skill choice in FUN_005e2cd0 with d2d's imp_ride_pick.

--cpp prints a C++ case generator; --cases reads its compiled output. Skill
lookups are hooked. See monster-ai.md for the complete rerun commands.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CPP = r'''#include "rules/monsters.hpp"
#include <iostream>
int main() {
    for (unsigned seed = 0; seed < 256; ++seed)
        for (int mask = 0; mask < 4; ++mask) {
            d2d::rules::Rng rng(seed);
            const int choice = d2d::rules::imp_ride_pick(rng, mask & 1, mask & 2);
            std::cout << seed << ' ' << mask << ' ' << choice << ' ' << rng.low << ' ' << rng.high << '\n';
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
    unit_address = machine.alloc(0x100)
    think_address = machine.alloc(0x40)
    control_address = machine.alloc(0x40)
    monstats_address = machine.alloc(0x1A8)
    unit = machine.view("unit", unit_address)
    control = machine.view("ai_control", control_address)
    machine.view("ai_think", think_address).control = control_address
    machine.view("ai_think", think_address).monstats = monstats_address
    # MonStats Skill3 and Skill5 are 16-bit entries following Skill1.
    machine.mu.mem_write(monstats_address + 0x174, (2).to_bytes(2, "little"))
    machine.mu.mem_write(monstats_address + 0x178, (4).to_bytes(2, "little"))
    lookup = {"mask": 0}

    def skill(current):
        """Hook: answer the rider's has-skill check from the scripted mask."""
        skill_id = current.mu.reg_read(UC_X86_REG_EDX)
        if skill_id not in (2, 4):
            raise AssertionError(f"unexpected skill lookup {skill_id}")
        return int(bool(lookup["mask"] & (1 if skill_id == 2 else 2)))

    machine.hook(0x6439B0, skill, 1)
    rows = args.cases.read_text(encoding="utf-8").splitlines()
    if len(rows) != 1024:
        raise AssertionError(f"expected 1024 cases, got {len(rows)}")
    for index, row in enumerate(rows):
        seed, mask, expected, low, high = map(int, row.split())
        if (seed, mask) != divmod(index, 4):
            raise AssertionError(f"case {index}: unexpected input order")
        lookup["mask"] = mask
        unit.seed_low, unit.seed_high = seed, 666
        control.scratch_two, control.scratch_three = 0xA5A5A5A5, 0xA5A5A5A5
        machine.call(0x5E2CD0, think_address, ecx=0, edx=unit_address, limit=1000)
        actual = (control.scratch_two, unit.seed_low, unit.seed_high)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or actual != (expected, low, high):
            raise AssertionError(f"case {index}: game={actual}, d2d={(expected, low, high)}")
    print(f"ok: {len(rows)} riding imp initialization cases")


if __name__ == "__main__":
    main()
