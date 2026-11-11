# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005e7dc0's target distance decision in stock game.exe 1.14d.

    uv run python ai_town_rogue.py --exe /path/to/game.exe

Target search, attack and idle are hooked; no MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    require(hashlib.sha256(args.exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    game, unit, target = (machine.alloc(0x100) for _ in range(3))
    think_address = machine.alloc(0x20)
    think = machine.view("ai_think", think_address)
    think.distance, think.melee = 77, 0x1234
    scripted = {"distance": 0, "target": 0}
    calls = []

    def search(current):
        calls.append(("search", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      current.r32(current.arg(0)), current.r32(current.arg(1))))
        current.w32(current.arg(0), scripted["distance"])
        current.w32(current.arg(1), 0xABCD)
        return scripted["target"]

    def attack(current):
        calls.append(("attack", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      current.arg(0), current.arg(1)))
        return 0

    def idle(current):
        calls.append(("idle", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      current.arg(0)))
        return 0

    machine.hook(0x5DDC30, search, 2)
    machine.hook(0x5DDF90, attack, 2)
    machine.hook(0x5DE080, idle, 1)
    for destination, distance, should_attack in ((0, 0, False), (target, 24, True),
                                                  (target, 25, False), (target, 0xFFFFFFFF, True),
                                                  (target, 100, False)):
        scripted["target"], scripted["distance"] = destination, distance
        calls.clear()
        machine.call(0x5E7DC0, think_address, ecx=game, edx=unit, limit=1000)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "think did not return")
        expected_action = ("attack", game, unit, 4, target) if should_attack else ("idle", game, unit, 50)
        require(calls == [("search", game, unit, 77, 0x1234), expected_action], f"unexpected calls: {calls!r}")
        require((think.distance, think.melee) == (77, 0x1234), "think input changed")
    print("ok: town rogue target and distance branches")


if __name__ == "__main__":
    main()
