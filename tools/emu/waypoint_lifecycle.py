# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the stock waypoint block's allocation and release, without game processes.

Usage: uv run python waypoint_lifecycle.py
Runs FUN_00660f30 and FUN_00660fd0; allocator, diagnostic and fatal-exit
boundaries are intercepted. This checks stock behavior, not d2d equivalence.
"""
import argparse


class FatalWaypoint(Exception):
    """The stock invalid-header path reached its fatal-exit boundary."""


def expect(actual, expected, label):
    """Reject any mismatch, including missing boundary calls."""
    if actual != expected:
        raise AssertionError(f"{label}: got {actual!r}, expected {expected!r}")


def call(machine, address, *args, limit=100):
    """Bound execution and reject instruction-budget exhaustion as a return."""
    from emu import RET_MAGIC
    from unicorn.x86_const import UC_X86_REG_EIP

    result = machine.call(address, *args, limit=limit)
    expect(machine.mu.reg_read(UC_X86_REG_EIP), RET_MAGIC, "returned to caller")
    return result


def check_new(machine, pool, fill):
    """Check allocation arguments, all 16 initialized bytes and both guards."""
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

    storage = machine.put(bytes([fill]) * 48)
    block = storage + 16
    allocations = []

    def allocate(current):
        """Supply dirty storage without masking initialization by stock code."""
        expect(current.mu.reg_read(UC_X86_REG_ECX), pool, "allocation pool")
        expect(current.mu.reg_read(UC_X86_REG_EDX), 16, "allocation size")
        source_matches = current.cstr(current.arg(0)).lower().endswith("waypoint.cpp")
        expect(source_matches, True, "allocation source")
        expect((current.arg(1), current.arg(2)), (0xFC, 0), "allocation metadata")
        allocations.append(block)
        return block

    machine.hook(0x0040B430, allocate, 3)
    expect(call(machine, 0x00660F30, pool), block, "new block pointer")
    expect(allocations, [block], "allocation count")
    expect(machine.read(block, 16), b"\x02\x01\x01" + bytes(13), "new block bytes")
    expect(machine.read(storage, 16), bytes([fill]) * 16, "leading guard")
    expect(machine.read(block + 16, 16), bytes([fill]) * 16, "trailing guard")


def check_free(machine, pool, header, fill):
    """Check header normalization, payload preservation and invalid rejection."""
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

    initial = header.to_bytes(2, "little") + bytes([fill]) * 14
    storage = machine.put(bytes([0xA7]) * 16 + initial + bytes([0xD3]) * 16)
    block = storage + 16
    freed, diagnostics, exits = [], [], []

    def release(current):
        """Observe the block at the memory-release boundary."""
        expect(current.mu.reg_read(UC_X86_REG_ECX), pool, "release pool")
        expect(current.mu.reg_read(UC_X86_REG_EDX), block, "release pointer")
        source_matches = current.cstr(current.arg(0)).lower().endswith("waypoint.cpp")
        expect(source_matches, True, "release source")
        expect((current.arg(1), current.arg(2)), (0x121, 0), "release metadata")
        expect(current.read(block, 16), b"\x02\x01" + initial[2:], "normalized block")
        freed.append(block)
        return 1

    def diagnostic(current):
        """Record the cdecl diagnostic without running its output machinery."""
        expect(current.arg(0), 0x006CC837, "diagnostic format")
        expect(current.arg(2), 0x113, "diagnostic source line")
        diagnostics.append(True)
        return 0

    def fatal(current):
        """Stop at the fatal boundary; never terminate the host interpreter."""
        expect(current.arg(0), 0xFFFFFFFF, "fatal exit status")
        exits.append(True)
        raise FatalWaypoint

    machine.hook(0x0040B480, release, 3)
    machine.hook(0x00408A60, diagnostic, 0)
    machine.hook(0x00681E09, fatal, 0)
    valid = header in (0, 0x101, 0x102)
    try:
        expect(call(machine, 0x00660FD0, pool, block), 1, "release return")
    except FatalWaypoint:
        if valid:
            raise AssertionError("valid header reached fatal exit") from None
    else:
        expect(valid, True, "invalid header returned")
    expect(freed, [block] if valid else [], "release count")
    expect(diagnostics, [] if valid else [True], "diagnostic count")
    expect(exits, [] if valid else [True], "fatal count")
    final = b"\x02\x01" + initial[2:] if valid else initial
    expect(machine.read(block, 16), final, "final block")
    expect(machine.read(storage, 16), bytes([0xA7]) * 16, "leading guard")
    expect(machine.read(block + 16, 16), bytes([0xD3]) * 16, "trailing guard")


def main(argv=None):
    """Validate arguments before loading stock code, then run every case."""
    argparse.ArgumentParser(description=__doc__).parse_args(argv)
    import emu

    machine = emu.Emu()
    new_count = free_count = 0
    for pool in (0, 0x12345678, 0xFFFFFFFF):
        for fill in (0, 0xA5, 0xFF):
            check_new(machine, pool, fill)
            new_count += 1
            for header in (0, 0x101, 0x102, 1, 0x100, 0x103, 0xFFFF):
                check_free(machine, pool, header, fill)
                free_count += 1
    print(f"ok: {new_count} allocations, {free_count} releases/rejections")


if __name__ == "__main__":
    main()
