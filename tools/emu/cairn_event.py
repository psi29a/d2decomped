# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the Cairn Stones client event at LAB_0046aee0.

    .venv/bin/python cairn_event.py [--exe PATH] [--mutation-check]
    .venv/bin/python cairn_event.py [--exe PATH] --cases

The stock entry runs for real. Its current-player, room, StoneAlpha lookup,
coordinate, client-missile request and ambient-dip boundaries are hooked and
logged. No MPQ data is read. ``--cases`` writes the stock results consumed by
tests/test_cairn_client_event.cpp.
"""
import argparse
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

ENTRY = 0x46AEE0
CURRENT_PLAYER = 0x463DE0
UNIT_ROOM = 0x620BB0
FIND_UNIT = 0x65A620
UNIT_X = 0x45ADF0
UNIT_Y = 0x45AE20
REQUEST = 0x4CDB40
DIP = 0x46AE50
TABLE_GLOBAL = 0x744304
MISSILE_ROW_SIZE = 0x1A4
CAIRN_MISSILE = 0x120
MUTATIONS = (
    "skip-player-gate",
    "skip-room-gate",
    "wrong-object-id",
    "wrong-missile-id",
    "wrong-position",
    "wrong-dip",
)
MASK = 0xFFFFFFFF


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


class CairnOracle:
    """LAB_0046aee0 with its subsystem boundaries scripted."""

    def __init__(self, exe=None):
        self.machine = machine = emu.Emu(exe)
        self.player = machine.alloc(0x100)
        self.room = machine.alloc(0x100)
        self.stone = machine.alloc(0x100)
        self.table = machine.alloc(0xC00)
        self.rows = machine.alloc((CAIRN_MISSILE + 1) * MISSILE_ROW_SIZE)
        machine.w32(TABLE_GLOBAL, self.table)
        machine.w32(self.table + 0xB64, self.rows)
        machine.w32(self.table + 0xB6C, CAIRN_MISSILE + 1)
        machine.hook(CURRENT_PLAYER, self.current_player, 0)
        machine.hook(UNIT_ROOM, self.unit_room, 1)
        machine.hook(FIND_UNIT, self.find_unit, 3)
        machine.hook(UNIT_X, self.unit_x, 0)
        machine.hook(UNIT_Y, self.unit_y, 0)
        machine.hook(REQUEST, self.request, 4)
        machine.hook(DIP, self.dip, 3)

    def current_player(self, _current):
        self.log.append(("player",))
        return self.player if self.has_player else 0

    def unit_room(self, current):
        self.log.append(("room", current.arg(0)))
        return self.room if self.has_room else 0

    def find_unit(self, current):
        self.log.append(("find", current.arg(0), current.arg(1), current.arg(2)))
        return self.stone if self.has_stone else 0

    def unit_x(self, current):
        self.log.append(("x", current.mu.reg_read(UC_X86_REG_ECX)))
        return self.stone_x

    def unit_y(self, current):
        self.log.append(("y", current.mu.reg_read(UC_X86_REG_ECX)))
        return self.stone_y

    def request(self, current):
        self.log.append(("request", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                         *(current.arg(index) for index in range(4))))
        return 0xABCD

    def dip(self, current):
        self.log.append(("dip", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                         *(current.arg(index) for index in range(3))))
        return 0

    def run(self, case):
        self.has_player, self.has_room, self.has_stone, self.stone_x, self.stone_y, param1, range_frames = case
        row = self.rows + CAIRN_MISSILE * MISSILE_ROW_SIZE
        self.machine.w32(row + 0x38, param1)
        self.machine.mu.mem_write(row + 0x96, int(range_frames).to_bytes(2, "little", signed=True))
        self.log = []
        self.machine.call(ENTRY, limit=500)
        return self.log


def model(oracle, case, mutation=None):
    """The control flow and boundary arguments read at LAB_0046aee0."""
    has_player, has_room, has_stone, stone_x, stone_y, param1, range_frames = case
    log = [("player",)]
    if not has_player and mutation != "skip-player-gate":
        return log
    log.append(("room", oracle.player))
    if not has_room and mutation != "skip-room-gate":
        return log
    object_id = 0x12 if mutation == "wrong-object-id" else 0x11
    log.append(("find", oracle.room, 2, object_id))
    if not has_stone:
        return log
    log.extend((("x", oracle.stone), ("y", oracle.stone)))
    request_x = stone_x + 1 if mutation == "wrong-position" else stone_x
    missile = 0x121 if mutation == "wrong-missile-id" else CAIRN_MISSILE
    log.append(("request", oracle.player, missile, request_x & MASK, stone_y & MASK, 0, 1))
    fade = param1 + 1 if mutation == "wrong-dip" else param1
    log.append(("dip", fade, (range_frames - 2 * param1) & MASK, param1, 4, 1))
    return log


def cases():
    return [
        (False, True, True, 101, 202, 40, 300),
        (True, False, True, 101, 202, 40, 300),
        (True, True, False, 101, 202, 40, 300),
        (True, True, True, 101, 202, 40, 300),
        (True, True, True, -17, 0, 5, 20),
        (True, True, True, 0x7FFF, -0x8000, 7, 10),
    ]


def check(oracle, mutation=None):
    bad = []
    for case in cases():
        got, want = oracle.run(case), model(oracle, case, mutation)
        if got != want:
            bad.append((case, got, want))
    return bad


def dump_cases(oracle):
    print("// Stock game.exe 1.14d, LAB_0046aee0; generated by tools/emu/cairn_event.py --cases.")
    for case in cases():
        log = oracle.run(case)
        request = next((entry for entry in log if entry[0] == "request"), None)
        dip = next((entry for entry in log if entry[0] == "dip"), None)
        made = int(request is not None and dip is not None)
        missile, stone_x, stone_y = (request[2], signed(request[3]), signed(request[4])) if request else (0, 0, 0)
        fade, hold, back = (dip[1], signed(dip[2]), dip[3]) if dip else (0, 0, 0)
        values = (*map(int, case[:3]), *case[3:], made, missile, stone_x, stone_y, fade, hold, back, max(case[6], 1) if made else 0)
        print("    { " + ", ".join(str(value) for value in values) + " },")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe")
    parser.add_argument("--mutation-check", action="store_true")
    parser.add_argument("--cases", action="store_true")
    args = parser.parse_args()
    oracle = CairnOracle(args.exe)
    if args.cases:
        dump_cases(oracle)
        return
    bad = check(oracle)
    if bad:
        for case, got, want in bad:
            print(f"case {case}: game {got}, model {want}")
        sys.exit(f"{len(bad)} Cairn event cases differ")
    print(f"ok: {len(cases())} LAB_0046aee0 Cairn client-event cases")
    if args.mutation_check:
        for name in MUTATIONS:
            if not check(oracle, name):
                sys.exit(f"mutation {name} was not caught")
            print(f"mutation {name}: failed as required")
        print(f"mutation check ok ({len(MUTATIONS)} ways)")


if __name__ == "__main__":
    main()
