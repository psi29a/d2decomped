# SPDX-License-Identifier: GPL-3.0-or-later
"""Check book skill-count delta FUN_0055c070 in stock game.exe 1.14d.

Run: python item_book_count_delta.py --exe /path/to/game.exe
Book-skill, node lookup, count accessor and network helpers are hooked; null
assertions and those helpers' implementations are outside this check.
"""
import argparse
import hashlib
from itertools import product
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    player, item, node, client = (machine.alloc(0x100) for _ in range(4))
    player_view = machine.view("unit", player)
    player_view.type, player_view.id = 0x1234, 0x87654321
    skill = machine.view("skill_entry", node)
    calls = []
    selected = {"skill": 0, "node": 0}

    def book(current):
        calls.append(("book", current.mu.reg_read(UC_X86_REG_ESI)))
        return selected["skill"]

    def lookup(current):
        calls.append(("lookup", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        return selected["node"]

    def get_count(current):
        calls.append(("count", current.arg(0)))
        return skill.quantity

    def set_count(current):
        calls.append(("set", current.arg(0), current.arg(1)))
        skill.quantity = current.arg(1)
        return 0

    def get_client(current):
        calls.append(("client", current.mu.reg_read(UC_X86_REG_ECX), current.arg(0)))
        return client

    def send(current):
        calls.append(("send", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX) & 0xff,
                      current.arg(0), current.arg(1), current.arg(2)))
        return 0

    machine.hook(0x55bff0, book, 0)
    machine.hook(0x6439b0, lookup, 1)
    machine.hook(0x6450f0, get_count, 1)
    machine.hook(0x645120, set_count, 2)
    machine.hook(0x5531c0, get_client, 1)
    machine.hook(0x53c520, send, 3)
    count = 0
    for skill_id, present, initial, delta in product((0xffffffff, 9), (0, 1), (0, 7, 0xffffffff), (0, 1, -2)):
        selected.update(skill=skill_id, node=node if present else 0)
        skill.quantity = initial
        calls.clear()
        machine.call(0x55c070, delta & 0xffffffff, regs={"eax": item, "edi": player}, limit=1000)
        expected = [("book", item)]
        if skill_id != 0xffffffff:
            expected.append(("lookup", player, skill_id, 0xffffffff))
            if present:
                updated = (initial + delta) & 0xffffffff
                expected += [("count", node), ("set", node, updated), ("client", player, 0x20b),
                             ("send", client, player_view.type & 0xff, player_view.id, skill_id, updated)]
        else:
            updated = initial
        if not (skill_id != 0xffffffff and present):
            updated = initial
        if (calls != expected or skill.quantity != updated or
                machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
            raise AssertionError(f"case {(skill_id, present, initial, delta)}: {calls!r}")
        count += 1
    print(f"ok: {count} book skill-count delta cases (callees hooked)")


if __name__ == "__main__":
    main()
