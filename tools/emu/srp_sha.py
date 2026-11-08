# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the stock 1.14d SRP allocator wrapper and interleaved SHA helper.

Run: uv run python srp_sha.py
"""
import hashlib
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_EIP

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
ALLOC = 0x413020
FREE = 0x412650
SRP_NEW = 0x528500
SHA_BROKEN = 0x52A120
LIMIT = 200000


def require(condition, message):
    """Reject oracle mismatches with their relevant case details."""
    if not condition:
        raise RuntimeError(message)


def broken_sha(data):
    """Return the two interleaved SHA-1 lanes after the game's prefix trimming."""
    data = bytes(data)
    if data and data[0] == 0:
        data = data.lstrip(b"\0")
    if len(data) & 1:
        data = data[1:]
    even = hashlib.sha1(data[::2]).digest()
    odd = hashlib.sha1(data[1::2]).digest()
    return bytes(byte for pair in zip(even, odd, strict=True) for byte in pair)


def call_checked(machine, address, *args, **kwargs):
    """Call a bounded game function and reject an instruction-limit stop."""
    result = machine.call(address, *args, **kwargs)
    require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x}: instruction cap")
    return result


def main():
    """Compare SRP allocation and SHA results with stock game.exe behavior."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    machine = emu.Emu(str(exe))
    allocations = []
    allocated_ptrs = []
    frees = []
    fail_alloc = False

    def allocate(e):
        """Record the game's allocator request and return poisoned test memory."""
        size, filename, line, flags = (e.arg(i) for i in range(4))
        allocations.append((size, e.cstr(filename), line, flags))
        if fail_alloc:
            return 0
        address = e.alloc(size, zero=False)
        e.mu.mem_write(address, b"\xCD" * size)
        allocated_ptrs.append(address)
        return address

    def release(e):
        """Record the game's matching free request."""
        frees.append(tuple(e.arg(i) for i in range(4)))
        return 1

    machine.hook(ALLOC, allocate, 4)
    machine.hook(FREE, release, 4)

    slot = machine.alloc(4)
    machine.w32(slot, 0xDEADBEEF)
    call_checked(machine, SRP_NEW, ecx=slot, limit=LIMIT)
    require(allocations[-1] == (0x4C, ".\\SRP.cpp", 0x157, 0), f"SRP allocation: {allocations[-1]}")
    srp = machine.r32(slot)
    require(srp != 0 and machine.r32(srp) == 0 and machine.r32(srp + 0x48) == 0,
            "SRP constructor did not return initialized state")

    fail_alloc = True
    machine.w32(slot, 0xDEADBEEF)
    call_checked(machine, SRP_NEW, ecx=slot, limit=LIMIT)
    require(machine.r32(slot) == 0, "SRP allocation failure did not clear the output pointer")
    fail_alloc = False

    inputs = []
    for length in (1, 2, 31, 32, 33, 55, 56, 63, 64, 65, 128):
        inputs.append(bytes((1 + (i * 73 + length) % 255 for i in range(length))))
    inputs.extend((b"\0\0\x01\x02\x03\x04", b"\0\x05\x06\x07\x08", b"\x11\0\x22\0\x33\0\x44\0"))
    for index, data in enumerate(inputs):
        in_addr = machine.put(data + b"\xA5")
        out = machine.alloc(40, zero=False)
        machine.mu.mem_write(out, b"\xCC" * 40)
        before_alloc = len(allocations)
        before_ptrs = len(allocated_ptrs)
        before_free = len(frees)
        call_checked(machine, SHA_BROKEN, len(data), ecx=out, edx=in_addr, limit=LIMIT)
        effective = data.lstrip(b"\0") if data.startswith(b"\0") else data
        if len(effective) & 1:
            effective = effective[1:]
        require(machine.read(out, 40) == broken_sha(data), f"SHA case {index}, input {data.hex()}")
        request = allocations[before_alloc]
        require(request == (len(effective) // 2, ".\\SHA.cpp", 0xC8, 0), f"SHA alloc case {index}: {request}")
        require(len(frees) == before_free + 1 and frees[-1][0] == allocated_ptrs[before_ptrs],
                f"SHA free case {index}: {frees[before_free:]} vs {allocated_ptrs[before_ptrs]:#x}")

    fail_alloc = True
    out = machine.alloc(40, zero=False)
    machine.mu.mem_write(out, b"\xCC" * 40)
    data = b"\x11\x22"
    call_checked(machine, SHA_BROKEN, len(data), ecx=out, edx=machine.put(data), limit=LIMIT)
    require(machine.read(out, 40) == b"\xCC" * 40, "SHA allocation failure changed output")

    print(f"ok: SRP constructor success/failure and {len(inputs)} interleaved SHA-1 cases")


if __name__ == "__main__":
    main()
