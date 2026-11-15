# SPDX-License-Identifier: GPL-3.0-or-later
"""Trace ten cited SOURCE, ENGINE, DUNGEON and UNITS functions.

Run: uv run python cited_core.py --exe /path/to/Game.exe [--mutation-check]

The ten entry points execute from stock game.exe 1.14d on synthetic records.
Their out-of-scope callees are hooked and logged; SOURCE's bignum import and
event-level copy execute their real allocation/copy paths.  No MPQs, files,
network, UI, or native game processes are used.

# gaps-runs: 004209e0 00423380 00426180 004706e0 0061a010 0061ad30 00620f00 00621210 00621bb0 00623c80
"""
import argparse
import hashlib
import itertools
import struct
from pathlib import Path

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TARGETS = (0x4209E0, 0x423380, 0x426180, 0x4706E0, 0x61A010,
           0x61AD30, 0x620F00, 0x621210, 0x621BB0, 0x623C80)
MUTATION = None


def check(actual, expected, label):
    """Compare one stock observation with an independent expectation."""
    if label == MUTATION:
        expected = ("deliberately wrong", expected)
    if actual != expected:
        raise AssertionError(f"{label}: {actual!r} != {expected!r}")


class Rig:
    """An emulator with deterministic allocation and call logging."""

    def __init__(self, exe):
        self.machine = emu.Emu(exe, real=TARGETS)
        self.events = []
        self.sizes = {}
        self.machine.hook(0x413020, self.allocate, 4)
        self.machine.hook(0x4133D0, self.reallocate, 5)

    def allocate(self, machine):
        size = machine.arg(0)
        pointer = machine.alloc(max(size, 1))
        self.sizes[pointer] = size
        self.events.append(("alloc", size, machine.cstr(machine.arg(1)),
                            machine.arg(2), machine.arg(3), pointer))
        return pointer

    def reallocate(self, machine):
        old, size = machine.arg(0), machine.arg(1)
        pointer = machine.alloc(max(size, 1))
        if old:
            machine.mu.mem_write(pointer, machine.read(old, min(size, self.sizes[old])))
        self.sizes[pointer] = size
        return pointer

    def hook(self, address, name, count, result=0, registers=()):
        """Log a callee's stack/register arguments and return a scripted value."""
        def run(machine):
            reg_values = tuple(machine.mu.reg_read(register) for register in registers)
            self.events.append((name, tuple(machine.arg(i) for i in range(count)), reg_values))
            return result() if callable(result) else result
        self.machine.hook(address, run, count)

    def call(self, address, *args, **registers):
        result = self.machine.call(address, *args, regs=registers or None, limit=2_000_000)
        check(self.machine.mu.reg_read(emu.UC_X86_REG_EIP), emu.RET_MAGIC, "normal return")
        check(self.machine.mu.reg_read(emu.UC_X86_REG_ESP),
              emu.STACK + emu.STACK_SIZE - 0x1000, "stack cleanup")
        return result


def source(exe):
    """Exercise the SOURCE list lookup, bignum import and event-level copy."""
    rig = Rig(exe)
    machine = rig.machine
    cases = 0

    # The list deliberately includes a negative pointer: the stock signed test
    # rejects it before dereferencing.  Links are also tested as signed values.
    nodes = [machine.alloc(12) for _ in range(3)]
    for index, node in enumerate(nodes):
        machine.w32(node + 4, nodes[index + 1] if index + 1 < len(nodes) else 0)
        machine.w32(node + 8, 100 + index)
    for head in (0, 0xFFFFFFFF, nodes[0]):
        machine.w32(0x708868, head)
        for wanted in (99, 100, 101, 102, 103):
            actual = rig.call(0x4209E0, ecx=wanted)
            expected = nodes[wanted - 100] if head == nodes[0] and 100 <= wanted <= 102 else 0
            check(actual, expected, "event window lookup")
            cases += 1
    machine.w32(nodes[0] + 4, 0xFFFFFFFF)
    machine.w32(0x708868, nodes[0])
    check(rig.call(0x4209E0, ecx=999), 0, "negative next pointer stops")
    cases += 1

    # Construct real SBig objects, enter the internal importer directly with
    # ESI, then use the real exporter as an observation surface.
    slot = machine.alloc(4)
    for payload in (b"", b"\x01", b"\x78\x56\x34\x12", bytes(range(1, 34))):
        rig.call(0x423950, slot)
        handle = machine.r32(slot)
        rig.call(0x423380, machine.put(payload or b"\0"), len(payload), esi=handle)
        out = machine.put(b"\xa5" * 64)
        used = machine.alloc(4)
        rig.call(0x423B30, handle, out, 64, used)
        expected = payload
        check(machine.r32(used), len(expected), "SBig imported length")
        check(machine.read(out, len(expected)), expected, "SBig imported bytes")
        check(machine.read(out + len(expected), 4), b"\xa5" * 4, "SBig import guard")
        cases += 1

    # Deep-copy mixed empty/nonempty hash buckets, including every payload
    # dword and order.  Allocator metadata is part of the claim.
    source_level = machine.alloc(16)
    destination = machine.put(b"\xa5" * 16)
    buckets = machine.alloc(12)
    machine.w32(source_level, buckets)
    machine.w32(source_level + 4, 3)
    machine.w32(source_level + 8, 0x12345678)
    expected_chains = []
    for bucket, length in enumerate((2, 0, 3)):
        head = 0
        chain = []
        for item in reversed(range(length)):
            node = machine.alloc(16)
            values = (bucket, item, 0xA000 + bucket * 16 + item, head)
            machine.mu.mem_write(node, struct.pack("<IIII", *values))
            head = node
            chain.insert(0, values[:3])
        machine.w32(buckets + bucket * 4, head)
        expected_chains.append(chain)
    rig.events.clear()
    rig.call(0x426180, destination, source_level)
    check((machine.r32(destination + 4), machine.r32(destination + 8)),
          (3, 0x12345678), "event level header")
    copied_buckets = machine.r32(destination)
    check(copied_buckets != buckets, True, "event bucket storage distinct")
    actual_chains = []
    for bucket in range(3):
        node = machine.r32(copied_buckets + bucket * 4)
        chain = []
        while node:
            chain.append(tuple(machine.r32(node + offset) for offset in (0, 4, 8)))
            node = machine.r32(node + 12)
        actual_chains.append(chain)
    check(actual_chains, expected_chains, "event level deep copy")
    check([(event[1], event[2], event[3]) for event in rig.events if event[0] == "alloc"],
          [(12, ".\\SOURCE\\SEVT.CPP", 0x3E)] +
          [(16, ".\\SOURCE\\SEVT.CPP", 0x45)] * 5,
          "event copy allocations")
    cases += 1
    return cases


def engine(exe):
    """Exercise the ENGINE frame-size resolver's routing and early returns."""
    rig = Rig(exe)
    machine = rig.machine
    unit = machine.alloc(0x100)
    owner = machine.alloc(0x100)
    client = machine.alloc(0x80)
    node = machine.alloc(0x20)
    cof = machine.alloc(0x20)
    out_w = machine.put(b"\xa5" * 8)
    state = {"class": 7, "state": 0, "owner": 0, "token": 0x4444,
             "load1": 0, "load2": 0, "client": 0, "build": 0,
             "parse": 0, "cell": 0x5555}
    rig.hook(0x621190, "class", 1, lambda: state["class"])
    rig.hook(0x645270, "remap", 6)
    rig.hook(0x63A7B0, "state", 2, lambda: state["state"])
    rig.hook(0x4639D0, "owner", 0, lambda: state["owner"], (emu.UC_X86_REG_ECX,))
    rig.hook(0x64F380, "cof_name", 5, lambda: state["token"])
    rig.hook(0x46E5A0, "load1", 1, lambda: state["load1"],
             (emu.UC_X86_REG_ECX, emu.UC_X86_REG_EDX))
    rig.hook(0x46FA80, "load2", 1, lambda: state["load2"],
             (emu.UC_X86_REG_ECX, emu.UC_X86_REG_EDX))
    rig.hook(0x463E10, "client", 0, lambda: state["client"], (emu.UC_X86_REG_ECX,))
    rig.hook(0x6487F0, "direction", 1, 3)

    def build(inner):
        rig.events.append(("build", (inner.arg(0), inner.arg(1)),
                           (inner.mu.reg_read(emu.UC_X86_REG_ECX),
                            inner.mu.reg_read(emu.UC_X86_REG_EDX))))
        return state["build"]

    def parse(inner):
        rig.events.append(("parse", tuple(inner.arg(i) for i in range(3)), ()))
        if state["parse"]:
            inner.w32(inner.arg(0) + 0x3C, state["cell"])
        return state["parse"]

    machine.hook(0x4DBB50, build, 2)
    machine.hook(0x6001F0, parse, 3)
    rig.hook(0x601920, "width", 1, 77)
    rig.hook(0x601950, "height", 1, 88)
    machine.w32(unit, 1)
    machine.w32(unit + 4, 2)
    machine.w32(unit + 0x2C, 0xAAAA)
    machine.w32(unit + 0x44, 0x12300)
    machine.w32(unit + 0x60, 0x1111)
    machine.w32(owner + 0x60, 0x2222)
    machine.w32(client + 0x28, node)
    machine.w32(node, state["class"])
    machine.w32(node + 8, cof)
    machine.w32(cof + 0x14, 1)
    cases = 0
    scenarios = (
        # flag/state/owner, load1/load2, client, matching node, build/parse, success
        (0, 0, 0, 0, 0, 0, 1, 1, 1, False),
        (1, 1, owner, 0, 0, client, 1, 1, 1, False),
        (1, 1, owner, 1, 0, 0, 1, 1, 1, False),
        (1, 1, owner, 0, 1, client, 0, 1, 1, False),
        (1, 1, owner, 0, 1, client, 1, 0, 1, False),
        (1, 1, owner, 0, 1, client, 1, 1, 0, False),
        (1, 1, owner, 0, 1, client, 1, 1, 1, True),
    )
    for flag, has_state, owner_ptr, load1, load2, client_ptr, match, build_ok, parse_ok, success in scenarios:
        machine.w32(unit + 0xC8, flag << 3)
        state.update(state=has_state, owner=owner_ptr, load1=load1, load2=load2,
                     client=client_ptr, build=build_ok, parse=parse_ok)
        machine.w32(node, state["class"] if match else state["class"] + 1)
        machine.mu.mem_write(out_w, b"\xa5" * 8)
        rig.events.clear()
        rig.call(0x4706E0, out_w, out_w + 4, ecx=unit, edx=9)
        check(machine.read(out_w, 8), struct.pack("<II", 77, 88) if success else b"\xa5" * 8,
              "ENGINE frame outputs")
        cof_calls = [event for event in rig.events if event[0] == "cof_name"]
        check(len(cof_calls), 1, "ENGINE builds one COF name")
        selected = cof_calls[0][1][1]
        check(selected, 0x2222 if flag and has_state and owner_ptr else 0x1111,
              "ENGINE owner graphics selection")
        cases += 1
    return cases


def dungeon(exe):
    """Exercise the DUNGEON grid accessor and area-cell forwarding wrapper."""
    rig = Rig(exe)
    machine = rig.machine
    room = machine.alloc(0x80)
    grid = machine.alloc(0x40)
    machine.w32(room + 0x20, grid)
    check(rig.call(0x61A010, ecx=room), grid, "room collision grid")
    calls = []

    def area(inner):
        calls.append((inner.mu.reg_read(emu.UC_X86_REG_ECX),
                      inner.mu.reg_read(emu.UC_X86_REG_EDX), inner.arg(0)))
        return 0x12345678

    machine.hook(0x66CEB0, area, 1)
    room2 = machine.alloc(0x30)
    area_record = machine.alloc(0x10)
    machine.w32(room2 + 0x10, area_record)
    for x, y in ((0, 0), (1, 2), (0xFFFFFFFF, 0x80000000)):
        calls.clear()
        check(rig.call(0x61AD30, room2, x, y), 0x12345678, "area cell propagated result")
        check(calls, [(area_record, x, y)], "area cell forwarding")
    return 4


def units(exe):
    """Exercise four UNITS animation, sequence, object and weapon helpers."""
    rig = Rig(exe)
    machine = rig.machine
    unit = machine.alloc(0x100)
    data = machine.alloc(0x40)
    machine.w32(unit + 0x60, data)
    cases = 0

    # FUN_00620f00: register/stack forwarding and the sole store at +0x50.
    calls = []
    def anim(inner):
        calls.append((inner.mu.reg_read(emu.UC_X86_REG_ECX),
                      inner.mu.reg_read(emu.UC_X86_REG_EDX),
                      tuple(inner.arg(i) for i in range(3))))
        return 0xCAFEBABE
    machine.hook(0x66A9B0, anim, 3)
    for values in ((1, 2, 3), (0, 0, 0), (0xFFFFFFFF, 0x80000000, 7)):
        calls.clear()
        rig.call(0x620F00, unit, values[0], values[1], values[2])
        check(calls, [(unit, values[1], (values[2], values[0], data))], "AnimData lookup arguments")
        check(machine.r32(unit + 0x50), 0xCAFEBABE, "AnimData pointer store")
        cases += 1

    # FUN_00621210: the row resolver writes through three output pointers.
    row = {"mode": 0, "frame": 0, "event": 0}
    row_calls = []
    def sequence_row(inner):
        row_calls.append(tuple(inner.arg(i) for i in range(7)))
        inner.w32(inner.arg(3), row["mode"])
        inner.w32(inner.arg(4), row["frame"])
        inner.mu.mem_write(inner.arg(6), bytes([row["event"] & 0xFF]))
        return 0
    machine.hook(0x663410, sequence_row, 7)
    machine.w32(unit + 0x30, 0x111)
    machine.w32(unit + 0x38, 0x222)
    for old, new, frame, event in ((0, 0, 0, 0), (3, 4, 9, 2), (4, 4, 0x7FFFFF, 0x1FF)):
        machine.w32(unit + 0x40, old)
        row.update(mode=new, frame=frame, event=event)
        row_calls.clear()
        result = rig.call(0x621210, 0x333, esi=unit)
        check(result, int(new != old), "sequence mode changed")
        check((machine.r32(unit + 0x40), machine.r32(unit + 0x44), machine.read(unit + 0x4E, 1)[0]),
              (new, (frame << 8) & 0xFFFFFFFF, event & 0xFF), "sequence row stores")
        check(row_calls[0][:3], (0x111, 0x222, 0x333), "sequence row inputs")
        cases += 1

    # FUN_00621bb0: an object data field, with no normalization.
    object_unit = machine.alloc(0x80)
    object_data = machine.alloc(0x20)
    machine.w32(object_unit, 2)
    machine.w32(object_unit + 0x14, object_data)
    for value in (0, 1, 0xFFFFFFFF, 0x12345678):
        rig.call(0x621BB0, object_unit, value)
        check(machine.r32(object_data + 8), value, "object field store")
        cases += 1

    # FUN_00623c80: cross dual-wield, selected/primary/other weapons and
    # eligibility.  The exact ordered update list distinguishes all branches.
    state = {"dual": 0, "selected": 0, "primary": 0, "other": 0, "ok": set()}
    updates = []
    rig.hook(0x6235A0, "dual", 1, lambda: state["dual"])
    rig.hook(0x623990, "selected", 2, lambda: state["selected"])
    rig.hook(0x63BEF0, "primary", 1, lambda: state["primary"])
    rig.hook(0x63BF90, "other", 1, lambda: state["other"])
    def eligible(inner):
        return int(inner.arg(0) in state["ok"])
    def update(inner):
        updates.append((inner.arg(0), inner.arg(1), inner.arg(2)))
        return 0
    machine.hook(0x625820, eligible, 2)
    machine.hook(0x627910, update, 3)
    for dual, selected_kind, ok_primary, ok_selected, ok_other in itertools.product((0, 1), range(3), (0, 1), (0, 1), (0, 1)):
        primary, other = 0x1001, 0x1002
        selected = (0, primary, 0x1003)[selected_kind]
        state.update(dual=dual, selected=selected, primary=primary, other=other,
                     ok={weapon for weapon, yes in ((primary, ok_primary), (selected, ok_selected), (other, ok_other)) if yes})
        updates.clear()
        rig.call(0x623C80, unit, 77)
        expected = []
        if dual:
            if selected == 0:
                if primary in state["ok"]:
                    expected.append((unit, primary, 1))
            elif selected == primary:
                if primary in state["ok"]:
                    expected.append((unit, primary, 1))
                if other in state["ok"]:
                    expected.append((unit, other, 0))
            else:
                if selected in state["ok"]:
                    expected.append((unit, selected, 1))
                if primary in state["ok"]:
                    expected.append((unit, primary, 0))
        check(updates, expected, "dual-wield update order")
        cases += 1
    return cases


def run(exe):
    counts = {}
    for group in (source, engine, dungeon, units):
        counts[group.__name__] = group(exe)
        print(f"{group.__name__}: {counts[group.__name__]} cases")
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    check(hashlib.sha256(args.exe.read_bytes()).hexdigest(), SHA256, "game.exe SHA-256")
    counts = run(args.exe)
    print(f"ok: {sum(counts.values())} cases over {len(TARGETS)} cited functions")
    if args.mutation_check:
        global MUTATION
        for group, label in ((source, "event level deep copy"),
                             (engine, "ENGINE owner graphics selection"),
                             (dungeon, "area cell forwarding"),
                             (units, "dual-wield update order")):
            MUTATION = label
            try:
                group(args.exe)
            except AssertionError as error:
                if not str(error).startswith(label + ":"):
                    raise
                print(f"negative control caught: {label}")
            else:
                raise AssertionError(f"negative control escaped: {label}")
            finally:
                MUTATION = None


if __name__ == "__main__":
    main()
