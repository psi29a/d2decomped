# SPDX-License-Identifier: GPL-3.0-or-later
"""Check selected FUN_0067a740 expansion branches with synthetic state.

    uv run python drlg_expand_oracle.py --exe /path/to/game.exe

Collision, direction selection and backtracking are scripted. The cases
include one-step branches, randomized direction draws and a straight
three-step walk; general pathfinding remains outside the check.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
DIRECTIONS = ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1))
DIRECTION_OFFSETS = (0, 0, 0, 1, 6, 7, 0, 0)
RANDOM_OFFSETS = (-2, -1, 0, 1, 2) * 6 + (-1, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    state_address = machine.alloc(0x37000)
    node_address = machine.alloc(0x1C)
    path_address = machine.alloc(0x40)
    seed_address = machine.alloc(8)
    state = machine.view("drlg_astar_state", state_address)
    node = machine.view("drlg_astar_node", node_address)
    path = machine.view("drlg_astar_path", path_address)
    seed = machine.view("seed", seed_address)
    calls = []
    blocked = False

    def collision(current):
        calls.append(("collision", tuple(current.arg(index) for index in range(5))))
        return int(blocked)

    def direction(current):
        calls.append(("direction", (current.arg(0), current.arg(1))))
        return 0

    def backtrack(current):
        calls.append(("backtrack", (current.mu.reg_read(UC_X86_REG_EAX), current.arg(0))))
        return 0

    machine.hook(0x64D910, collision, 5)
    machine.hook(0x678D10, direction, 2)
    machine.hook(0x67A690, backtrack, 1)
    count = 0
    for index, (delta_x, delta_y) in enumerate(DIRECTIONS):
        for scenario in ("child", "random", "goal", "outside", "blocked", "visited", "equal", "greater", "bound", "full"):
            machine.mu.mem_write(state_address, bytes(0x37000))
            machine.mu.mem_write(node_address, bytes(0x1C))
            machine.mu.mem_write(path_address, bytes(0x40))
            state.root, state.count = node_address, 1
            state.min_x, state.min_y, state.max_x, state.max_y = 90, 90, 110, 110
            state.width, state.offset_x, state.offset_y = 32, -90, -90
            node.position = 100 | (100 << 16)
            node.cost, node.direction = 5, index
            path.target = (100 + 3 * delta_x) | ((100 + 3 * delta_y) << 16)
            path.room, path.shape, path.mask, path.radius = 0x1111, 2, 0xCAFE, 255
            if scenario == "goal":
                path.target = node.position
            if scenario == "outside":
                state.min_x = 101
            if scenario == "full":
                state.count = 900
            if scenario == "random":
                state.randomized, state.seed = 1, seed_address
                seed.low, seed.high = 0x12345678, 666
            cell_index = ((100 + delta_y - 90) * 32) + (100 + delta_x - 90)
            cell = machine.view("drlg_astar_cell", state_address + 0x6290 + 4 * cell_index)
            if scenario == "visited":
                cell.cost = 2
            if scenario in ("equal", "greater"):
                cell.cost = 5 + (3 if delta_x and delta_y else 2) + (scenario == "greater")
            blocked = scenario == "blocked"
            calls.clear()
            bound = 0 if scenario == "bound" else 1000
            result = machine.call(0x67A740, bound, path_address, regs={"edi": state_address}, limit=100000)
            if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
                raise AssertionError(f"{scenario}: did not return")
            if scenario in ("goal", "outside"):
                if result != node_address or calls:
                    raise AssertionError(f"{scenario}: wrong return or calls {result:x} {calls}")
            else:
                x, y = 100 + delta_x, 100 + delta_y
                expected_cost = 5 + (3 if delta_x and delta_y else 2)
                expected_calls = [] if scenario in ("visited", "equal", "greater") else [("collision", (path.room, x, y, path.shape, path.mask))]
                if scenario in ("blocked", "visited", "bound"):
                    expected_calls.append(("backtrack", (node_address, state_address)))
                elif scenario in ("child", "random", "equal", "greater"):
                    expected_calls.append(("direction", (x | (y << 16), path.target)))
                if calls != expected_calls:
                    raise AssertionError(f"{scenario}: unexpected calls {calls!r} != {expected_calls!r}")
                expected_cell = 1 if blocked else 2 if scenario == "visited" else expected_cost
                if cell.cost != expected_cell:
                    raise AssertionError(f"{scenario}: cell {cell.cost} != {expected_cell}")
                if scenario in ("child", "random", "equal", "greater"):
                    child_address = state_address + 4 + 0x1C
                    child = machine.view("drlg_astar_node", child_address)
                    remaining = sorted((abs((path.target & 0xFFFF) - x), abs((path.target >> 16) - y)))
                    heuristic = remaining[0] + 2 * remaining[1]
                    if (result, state.count, node.child, child.parent, child.position, child.cost,
                        child.heuristic, child.total) != (child_address, 2, child_address, node_address,
                                                        x | (y << 16), expected_cost, heuristic,
                                                        expected_cost + heuristic):
                        raise AssertionError("child state differs")
                    if scenario == "random":
                        new_seed = 0x12345678 * 0x6AC690C5 + 666
                        expected_direction = (index + DIRECTION_OFFSETS[-index & 7] + RANDOM_OFFSETS[new_seed & 31]) & 7
                        if (seed.low, seed.high, child.direction) != (new_seed & 0xFFFFFFFF, new_seed >> 32, expected_direction):
                            raise AssertionError("randomized direction or seed differs")
                elif result != 0:
                    raise AssertionError(f"{scenario}: expected null, got {result:x}")
            count += 1
    machine.mu.mem_write(state_address, bytes(0x37000))
    machine.mu.mem_write(node_address, bytes(0x1C))
    machine.mu.mem_write(path_address, bytes(0x40))
    state.root, state.count = node_address, 1
    state.min_x, state.min_y, state.max_x, state.max_y = 90, 90, 110, 110
    state.width, state.offset_x, state.offset_y = 32, -90, -90
    node.position, node.cost, node.direction = 100 | (100 << 16), 5, 0
    path.target, path.room, path.shape, path.mask, path.radius = 103 | (100 << 16), 0x1111, 2, 0xCAFE, 0
    blocked = False
    calls.clear()
    result = machine.call(0x67A740, 1000, path_address, regs={"edi": state_address}, limit=100000)
    if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
        raise AssertionError("three-step walk did not return")
    expected_calls = []
    for x in (101, 102, 103):
        expected_calls.extend((("collision", (path.room, x, 100, path.shape, path.mask)),
                               ("direction", (x | (100 << 16), path.target))))
    if calls != expected_calls or result != state_address + 4 + 3 * 0x1C or state.count != 4:
        raise AssertionError(f"three-step walk differs: {result:x} {state.count} {calls!r}")
    parent_address = node_address
    for slot, x in enumerate((101, 102, 103), start=1):
        child_address = state_address + 4 + slot * 0x1C
        child = machine.view("drlg_astar_node", child_address)
        parent = machine.view("drlg_astar_node", parent_address)
        if (parent.child, child.parent, child.position, child.cost) != (child_address, parent_address,
                                                                      x | (100 << 16), 5 + slot * 2):
            raise AssertionError(f"three-step node {slot} differs")
        parent_address = child_address
    print(f"ok: {count} one-step expansion cases and one three-step walk")


if __name__ == "__main__":
    main()
