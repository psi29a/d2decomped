# SPDX-License-Identifier: GPL-3.0-or-later
"""Check book skill-count add/remove FUN_0055c110 in stock game.exe 1.14d.

Run: python item_book_count_update.py --exe /path/to/game.exe
Callees are scripted. Missing-node removal and failed relearn assertions are
not run, and hooked skill, stat and network implementations are not checked.
"""
import argparse
import hashlib
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
    player_view.type, player_view.id = 0, 0x87654321
    skill = machine.view("skill_entry", node)
    calls = []
    selected = {}

    def book(current):
        calls.append(("book", current.mu.reg_read(UC_X86_REG_ESI)))
        return selected["skill"]

    def stat(current):
        calls.append(("stat", current.arg(0), current.arg(1), current.arg(2)))
        return selected["quantity"]

    def item_type(current):
        calls.append(("type", current.arg(0), current.arg(1)))
        return selected["book"]

    def lookup(current):
        calls.append(("lookup", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        selected["lookups"] += 1
        return node if selected["present"] or selected["lookups"] > 1 else 0

    def change_stat(current):
        calls.append(("change_stat", *(current.arg(index) for index in range(4))))
        return 0

    def learn(current):
        calls.append(("learn", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)))
        return 0

    def get_count(current):
        calls.append(("get", current.arg(0)))
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

    def hand(current, side):
        calls.append((side, current.arg(0)))
        return node if selected[side] else 0

    def clear_hand(current):
        calls.append(("clear", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      current.arg(0), current.arg(1)))
        return 0

    machine.hook(0x55bff0, book, 0)
    machine.hook(0x625480, stat, 3)
    machine.hook(0x629bb0, item_type, 2)
    machine.hook(0x6439b0, lookup, 1)
    machine.hook(0x6272b0, change_stat, 4)
    machine.hook(0x570080, learn, 0)
    machine.hook(0x6450f0, get_count, 1)
    machine.hook(0x645120, set_count, 2)
    machine.hook(0x5531c0, get_client, 1)
    machine.hook(0x53c520, send, 3)
    machine.hook(0x620190, lambda current: hand(current, "left"), 1)
    machine.hook(0x6201d0, lambda current: hand(current, "right"), 1)
    machine.hook(0x5701b0, clear_hand, 2)

    # (skill, quantity, book, add, node exists, old count, left, right)
    cases = [(0xffffffff, 3, 0, 1, 0, 0, 0, 0), (9, 0, 1, 1, 0, 0, 0, 0),
             (9, 0, 0, 1, 1, 5, 0, 0), (9, 3, 1, 1, 1, 5, 0, 0),
             (9, 3, 1, 1, 0, 5, 0, 0), (9, 3, 1, 0, 1, 5, 0, 0),
             (9, 5, 1, 0, 1, 3, 1, 1), (9, 3, 1, 0, 1, 3, 1, 0),
             (9, 3, 1, 0, 1, 3, 0, 1), (9, 0, 0, 0, 1, 1, 0, 0)]
    for skill_id, quantity, is_book, add, present, initial, left, right in cases:
        selected.update(skill=skill_id, quantity=quantity, book=is_book, present=present,
                        left=left, right=right, lookups=0)
        skill.quantity = initial
        calls.clear()
        result = machine.call(0x55c110, add, regs={"eax": item, "edi": player}, limit=1000)
        expected = [("book", item)]
        updated = initial
        success = False
        if skill_id != 0xffffffff:
            expected.append(("stat", item, 0x46, 0))
            if quantity == 0:
                expected.append(("type", item, 0x12))
            if quantity != 0 or not is_book:
                contribution = quantity or 1
                expected.append(("lookup", player, skill_id, 0xffffffff))
                if add and not present:
                    expected += [("change_stat", player, 5, contribution, 0), ("learn", player, skill_id),
                                 ("lookup", player, skill_id, 0xffffffff)]
                    updated = contribution
                else:
                    expected.append(("get", node))
                    updated = (initial + contribution) & 0xffffffff if add else max(0, initial - contribution)
                expected += [("set", node, updated), ("client", player, 0x234),
                             ("send", client, 0, player_view.id, skill_id, updated)]
                if updated == 0:
                    expected.append(("left", player))
                    if left:
                        expected.append(("clear", player, 1, 0, 0xffffffff))
                    expected.append(("right", player))
                    if right:
                        expected.append(("clear", player, 0, 0, 0xffffffff))
                success = True
        if (result != int(success) or calls != expected or skill.quantity != updated or
                machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
            raise AssertionError(f"case {(skill_id, quantity, is_book, add, present, initial, left, right)}: {calls!r}")
    print(f"ok: {len(cases)} book skill-count add/remove cases (callees hooked)")


if __name__ == "__main__":
    main()
