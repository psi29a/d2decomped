# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_005b4da0's Act IV player share gate in stock game.exe 1.14d.

Run: python quest_diablo_act_share.py --exe /path/to/game.exe
Called helpers are hooked; tests cover their arguments, order, and return gates.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    game, player, player_data, room = (machine.alloc(0x100) for _ in range(4))
    buffers = [machine.alloc(0x60) for _ in range(3)]
    for index, pointer in enumerate(buffers):
        machine.w32(player_data + 0x10 + 4 * index, pointer)
    calls = []
    scripted = {"flag": 0, "room": 0, "act": 0}

    def hook(name, result):
        def callback(current):
            calls.append((name, *(current.arg(i) for i in range(3 if name == "flag" else 1))))
            return scripted[result] if isinstance(result, str) else result
        return callback

    machine.hook(0x6221a0, hook("data", player_data), 1)
    machine.hook(0x65c310, hook("flag", "flag"), 3)
    machine.hook(0x620bb0, hook("room", "room"), 1)
    machine.hook(0x61a1b0, hook("level", 0x55), 1)
    machine.hook(0x6427f0, hook("act", "act"), 1)
    machine.hook(0x5b4d20, hook("share", 0), 1)
    count = 0
    for difficulty in range(3):
        for flag in (0, 1, 2, 0x100):
            for has_room in (0, 1):
                for act in (0, 3, 0x103):
                    machine.mu.mem_write(game + 0x6d, bytes([difficulty]))
                    scripted.update(flag=flag, room=room if has_room else 0, act=act)
                    calls.clear()
                    machine.call(0x5b4da0, 0x1234, ecx=game, edx=player, limit=1000)
                    expected = [("data", player), ("flag", buffers[difficulty], 0x1a, 0)]
                    if flag != 1:
                        expected.append(("room", player))
                        if has_room:
                            expected.extend([("level", room), ("act", 0x55)])
                            if act & 0xff == 3:
                                expected.append(("share", player))
                    if (calls != expected or machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                            machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
                        raise AssertionError(f"case {(difficulty, flag, has_room, act)}: {calls!r}")
                    count += 1
    print(f"ok: {count} Diablo Act IV share cases (callees hooked)")


if __name__ == "__main__":
    main()
