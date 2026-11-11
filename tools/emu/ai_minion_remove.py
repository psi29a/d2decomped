# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_0058f160 unlinking and FUN_0058f260 disbanding in 1.14d.

    uv run python ai_minion_remove.py --exe /path/to/game.exe

Unit lookup and pool free are hooked. No MPQs are read.
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
    game_address, pool = machine.alloc(0x100), machine.alloc(0x10)
    machine.view("game", game_address).pool = pool
    leader_address, minion_address = machine.alloc(0x100), machine.alloc(0x100)
    leader_data, minion_data = machine.alloc(0x80), machine.alloc(0x80)
    leader_control_address, minion_control_address = machine.alloc(0x40), machine.alloc(0x40)
    leader = machine.view("unit", leader_address)
    minion = machine.view("unit", minion_address)
    leader.type, leader.data, leader.id = 1, leader_data, 222
    minion.type, minion.data = 1, minion_data
    machine.view("monster_data", leader_data).ai_control = leader_control_address
    machine.view("monster_data", minion_data).ai_control = minion_control_address
    leader_control = machine.view("ai_control", leader_control_address)
    minion_control = machine.view("ai_control", minion_control_address)
    leader_control.game = game_address
    nodes = [machine.alloc(8) for _ in range(3)]
    calls = []
    scripted = {"find_minion": True, "disband": False, "self_owner": True, "members": {}}

    def lookup(current):
        record = (current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0))
        calls.append(("lookup", *record))
        if record == (game_address, 1, 222):
            return leader_address if scripted["self_owner"] else minion_address
        if scripted["disband"]:
            return scripted["members"].get(record[2], 0)
        if record == (game_address, 1, minion.id) and scripted["find_minion"]:
            return minion_address
        return 0

    def free(current):
        calls.append(("free", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                      current.cstr(current.arg(0)), current.arg(1), current.arg(2)))
        return 0

    machine.hook(0x552F60, lookup, 1)
    machine.hook(0x40B480, free, 3)
    source = ".\\Ai\\AiGeneral.cpp"
    for wanted, found in ((101, True), (102, True), (103, True), (104, True), (102, False)):
        for address, identifier, following in zip(nodes, (101, 102, 103), nodes[1:] + [0]):
            node = machine.view("minion_node", address)
            node.unit_id, node.next = identifier, following
        leader_control.minions = nodes[0]
        minion.id = wanted
        minion_control.game, minion_control.owner_id, minion_control.owner_type = game_address, 222, 1
        scripted["find_minion"] = found
        calls.clear()
        machine.call(0x58F160, ecx=minion_address, limit=10000)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "remove did not return")
        index = wanted - 101
        if index >= len(nodes):
            require(calls == [("lookup", game_address, 1, 222)], "missing ID calls differ")
            require(leader_control.minions == nodes[0], "missing ID changed head")
            continue
        expected = [("lookup", game_address, 1, 222), ("lookup", game_address, 1, wanted),
                    ("free", pool, nodes[index], source, 0x217, 0)]
        require(calls == expected, f"calls differ: {calls!r}")
        remaining = nodes[:index] + nodes[index + 1:]
        require(leader_control.minions == remaining[0], "head differs")
        for address, following in zip(remaining, remaining[1:] + [0]):
            require(machine.view("minion_node", address).next == following, "list linkage differs")
        expected_owner = (0xFFFFFFFF, 1) if found else (222, 1)
        require((minion_control.owner_id, minion_control.owner_type) == expected_owner,
                "minion ownership differs")
        require(minion_control.game == game_address, "minion game differs")

    member_controls = []
    for identifier in (101, 103):
        member_address, member_data, member_control_address = machine.alloc(0x100), machine.alloc(0x80), machine.alloc(0x40)
        member = machine.view("unit", member_address)
        member.type, member.data, member.id = 1, member_data, identifier
        machine.view("monster_data", member_data).ai_control = member_control_address
        member_control = machine.view("ai_control", member_control_address)
        member_control.game, member_control.owner_id, member_control.owner_type = game_address, 222, 1
        scripted["members"][identifier] = member_address
        member_controls.append(member_control)

    scripted["disband"] = True
    leader_control.owner_id, leader_control.owner_type = 222, 1
    for address, identifier, following in zip(nodes, (101, 102, 103), nodes[1:] + [0]):
        node = machine.view("minion_node", address)
        node.unit_id, node.next = identifier, following
    leader_control.minions = nodes[0]
    scripted["self_owner"] = False
    calls.clear()
    machine.call(0x58F260, ecx=leader_address, limit=10000)
    require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "non-owner disband did not return")
    require(leader_control.minions == nodes[0] and calls == [("lookup", game_address, 1, 222)],
            "non-owner disband changed list")
    scripted["self_owner"] = True
    calls.clear()
    machine.call(0x58F260, ecx=leader_address, limit=10000)
    require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "disband did not return")
    expected = [("lookup", game_address, 1, 222)]
    for address, identifier in zip(nodes, (101, 102, 103)):
        expected.extend([("lookup", game_address, 1, identifier), ("free", pool, address, source, 0x237, 0)])
    require(calls == expected, f"disband calls differ: {calls!r}")
    require(leader_control.minions == 0, "disband did not clear head")
    for control in member_controls:
        require((control.game, control.owner_id, control.owner_type) == (game_address, 0xFFFFFFFF, 1),
                "disband member ownership differs")
    print("ok: minion unlinking and list disbanding")


if __name__ == "__main__":
    main()
