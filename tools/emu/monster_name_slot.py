# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe's monster display-name setter (FUN_0063e810).

    python monster_name_slot.py

The client setter accepts a monster unit and a UTF-16 name, allocates a new
copy into MonsterData +0x2c, and leaves other unit kinds or incomplete inputs
untouched. This checks the assignment behavior used by the random-unique name
path (FUN_004ac870); it does not emulate the name generation or rendering.
"""
import sys

from unicorn.x86_const import UC_X86_REG_EIP

import emu


ENTRY = 0x63E810
RET_MAGIC = emu.RET_MAGIC
CALL_LIMIT = 1_000_000
STARTUP_LIMIT = 50_000_000


class BoundedEmu(emu.Emu):
    """Cap every game.exe call, including calls made internally by crt_init."""

    def call(self, address, *args, ecx=0, edx=0, cdecl=False, limit=0, regs=None):
        """Forward a call with an instruction cap and reject a non-returning call."""
        cap = limit or STARTUP_LIMIT
        result = super().call(address, *args, ecx=ecx, edx=edx, cdecl=cdecl, limit=cap, regs=regs)
        actual = self.mu.reg_read(UC_X86_REG_EIP)
        if actual != RET_MAGIC:
            raise AssertionError(f"game.exe call {address:08x} stopped at {actual:08x}, not its return sentinel (limit {cap})")
        return result


def call_returned(machine, address, *args, ecx=0, edx=0):
    """Run one game.exe call with an instruction cap and require its sentinel return."""
    result = machine.call(address, *args, ecx=ecx, edx=edx, limit=CALL_LIMIT)
    actual = machine.mu.reg_read(UC_X86_REG_EIP)
    if actual != RET_MAGIC:
        raise AssertionError(f"game.exe call {address:08x} stopped at {actual:08x}, not its return sentinel")
    return result


def wide_string(machine, address):
    """Read a bounded, NUL-terminated UTF-16LE string from emulated memory."""
    units = []
    for index in range(2048):
        unit = machine.r16(address + index * 2)
        if unit == 0:
            return bytes().join(value.to_bytes(2, "little") for value in units).decode("utf-16le")
        units.append(unit)
    raise AssertionError(f"game.exe name at {address:08x} had no terminator within 2048 UTF-16 units")


def make_monster(machine, unit_type=1, has_data=True):
    """Build the unit and optional MonsterData fields read by FUN_0063e810."""
    unit = machine.alloc(0x40)
    data = machine.alloc(0x40) if has_data else 0
    machine.w32(unit, unit_type)
    machine.w32(unit + 0x14, data)
    return unit, data


def assert_assignment(machine, unit, data, expected):
    """Require a copied name in MonsterData and a return value naming its buffer."""
    source = machine.put(expected.encode("utf-16le") + b"\0\0")
    result = call_returned(machine, ENTRY, ecx=unit, edx=source)
    target = machine.r32(data + 0x2C)
    if not target or target == source or wide_string(machine, target) != expected:
        raise AssertionError(f"name assignment mismatch for {expected!r}")
    if result != target:
        raise AssertionError(f"setter returned {result:08x}; copied name is at {target:08x}")


def main():
    """Exercise name writes, replacement, and every null/type guard."""
    if len(sys.argv) != 1:
        raise SystemExit("usage: monster_name_slot.py")
    machine = BoundedEmu()
    # The CRT startup initializes the heap and per-thread allocator state used
    # by the native allocation. Its existing setup routine owns those calls.
    machine.crt_init()

    unit, data = make_monster(machine)
    expected_names = ["Champion Fallen", "Bane Poison the Hunter", "Dämon 🐉", "A", ""]
    for expected in expected_names:
        assert_assignment(machine, unit, data, expected)

    untouched = 0xA5A5A5A5
    cases = []
    other, other_data = make_monster(machine, unit_type=0)
    cases.append((other, machine.put("ignored".encode("utf-16le") + b"\0\0"), other_data))
    missing_data, _ = make_monster(machine, has_data=False)
    cases.append((missing_data, machine.put("ignored".encode("utf-16le") + b"\0\0"), 0))
    no_name, no_name_data = make_monster(machine)
    cases.append((no_name, 0, no_name_data))
    for index, (candidate, name, candidate_data) in enumerate(cases):
        if candidate_data:
            machine.w32(candidate_data + 0x2C, untouched)
        call_returned(machine, ENTRY, ecx=candidate, edx=name)
        if candidate_data and machine.r32(candidate_data + 0x2C) != untouched:
            raise AssertionError(f"guard case {index} unexpectedly changed MonsterData +0x2c")
    call_returned(machine, ENTRY, ecx=0, edx=machine.put("ignored".encode("utf-16le") + b"\0\0"))

    mutant = BoundedEmu()
    mutant.crt_init()
    mutant_unit, mutant_data = make_monster(mutant)
    mutant.hook(ENTRY, lambda _machine: 0, 0)
    try:
        assert_assignment(mutant, mutant_unit, mutant_data, "Mutation control")
    except AssertionError as error:
        if "name assignment mismatch" not in str(error):
            raise
    else:
        raise AssertionError("the no-op setter mutation was not detected")

    print(f"ok: {len(expected_names)} copied UTF-16 names; 4 null/type/data guards unchanged; no-op mutation detected")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, MemoryError) as error:
        sys.exit(f"monster_name_slot: {error}")
