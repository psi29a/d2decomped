# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the client's DC6 load and free helpers (CORE\\ARCHIVE.CPP) in stock game.exe 1.14d.

    uv run python archive_dc6.py

FUN_004788b0 (name in ECX, flag in EDX) and FUN_00478950 (name in ECX, size
pointer in EDX, flag on the stack) copy the name to the stack, append
FUN_005fe1f0's extension (".dcc" only when the flag and DAT_00741efc are both
set, else ".dc6"), read the file through FUN_00517079 and hand the bytes to
FUN_00601340, returning the cel file it builds. FUN_00478a00 always calls
FUN_00601a50 on its argument, then frees it with ARCHIVE.CPP line 0x7a when
non-null. The file read, cel build and free are captured, not run, so no MPQ
is read. The last case scans every call to the two loaders: each passes flag 0.
"""
import argparse
import hashlib
from pathlib import Path

import capstone
from capstone import x86
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
LOAD_DC6 = 0x4788B0
LOAD_DC6_SIZED = 0x478950
FREE_DC6 = 0x478A00
ARCHIVE_GETTER = 0x4FAC80
READ_FILE = 0x517079
BUILD_CELS = 0x601340
DELETE_CELS = 0x601A50
FREE_ENTRY = 0x40B3C0
USE_DCC = 0x741EFC
ARCHIVE_CPP = 0x6D723C
CODE_START = 0x401000
CODE_SIZE = 0x2F0000
INSTRUCTION_LIMIT = 20000
FILE_BYTES = 0x5150
CEL_FILE = 0x6CE11
FAKE_ARCHIVE = 0x77


def require(condition, message):
    """Stop the probe when a required stock-code observation is absent."""
    if not condition:
        raise RuntimeError(message)


def signed(value):
    """A 32-bit stack argument as a signed int."""
    return value - (1 << 32) if value >> 31 else value


class Captures:
    """Hooks for the callees, recording their arguments."""

    def __init__(self, machine):
        self.machine = machine
        self.reads = []
        self.builds = []
        self.deletes = []
        self.frees = []
        machine.hook(ARCHIVE_GETTER, lambda e: FAKE_ARCHIVE, 0)
        machine.hook(READ_FILE, self.read_file, 3)
        machine.hook(BUILD_CELS, self.build_cels, 6)
        machine.hook(DELETE_CELS, self.delete_cels, 1)
        machine.hook(FREE_ENTRY, self.free, 2)

    def read_file(self, current):
        """FUN_00517079(archive ECX, name EDX, size*, file, line): return fake bytes."""
        self.reads.append((current.mu.reg_read(UC_X86_REG_ECX),
                           current.cstr(current.mu.reg_read(UC_X86_REG_EDX)),
                           current.arg(0), current.arg(1), current.arg(2)))
        return FILE_BYTES

    def build_cels(self, current):
        """FUN_00601340(bytes, out*, file, line, -1, name): store the fake cel file."""
        self.builds.append((current.arg(0), current.arg(2), current.arg(3),
                            signed(current.arg(4)), current.cstr(current.arg(5))))
        current.w32(current.arg(1), CEL_FILE)
        return 1

    def delete_cels(self, current):
        """FUN_00601a50(cel file): record only."""
        self.deletes.append(current.arg(0))
        return 0

    def free(self, current):
        """FUN_0040b3c0(pointer ECX, file EDX, line, 0): record only."""
        self.frees.append((current.mu.reg_read(UC_X86_REG_ECX),
                           current.mu.reg_read(UC_X86_REG_EDX),
                           current.arg(0), current.arg(1)))
        return 0


def finished(machine, what):
    """Require the call to have returned within the limit."""
    require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC,
            f"{what} did not reach its return within the instruction limit")


def load(machine, cap, name, flag, use_dcc):
    """FUN_004788b0: returns (result, the one read, the one build)."""
    machine.w32(USE_DCC, use_dcc)
    text = machine.put(name.encode() + b"\0")
    before = (len(cap.reads), len(cap.builds))
    result = machine.call(LOAD_DC6, ecx=text, edx=flag, limit=INSTRUCTION_LIMIT)
    finished(machine, "FUN_004788b0")
    require(len(cap.reads) == before[0] + 1 and len(cap.builds) == before[1] + 1,
            "FUN_004788b0 did not read and build exactly once")
    return result, cap.reads[-1], cap.builds[-1]


def load_sized(machine, cap, name, size_ptr, flag, use_dcc):
    """FUN_00478950: returns (result, the one read, the one build)."""
    machine.w32(USE_DCC, use_dcc)
    text = machine.put(name.encode() + b"\0")
    before = (len(cap.reads), len(cap.builds))
    result = machine.call(LOAD_DC6_SIZED, flag, ecx=text, edx=size_ptr, limit=INSTRUCTION_LIMIT)
    finished(machine, "FUN_00478950")
    require(len(cap.reads) == before[0] + 1 and len(cap.builds) == before[1] + 1,
            "FUN_00478950 did not read and build exactly once")
    return result, cap.reads[-1], cap.builds[-1]


EDX_PARTS = {x86.X86_REG_EDX, x86.X86_REG_DX, x86.X86_REG_DL, x86.X86_REG_DH}


def flag_before(code, offset, target):
    """The instruction that sets the flag for the call at `offset`, decoded forward.

    Decoding starts at the nearest earlier 16-aligned `push ebp; mov ebp, esp`
    (every caller has an EBP frame) whose linear decode lands exactly on the
    call. Walking back from the call, FUN_004788b0's flag is the last write to
    EDX, FUN_00478950's the last push; a branch or another call in between
    means the flag isn't set in this straight line, and gives None.
    """
    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    disassembler.detail = True
    for start in range(offset - offset % 16, max(offset - 0x4000, 0), -16):
        if code[start:start + 3] != b"\x55\x8b\xec":
            continue
        instructions = list(disassembler.disasm(bytes(code[start:offset]), 0))
        if instructions and instructions[-1].address + instructions[-1].size == offset - start:
            break
    else:
        return None
    for instruction in reversed(instructions):
        if instruction.group(capstone.CS_GRP_JUMP) or instruction.group(capstone.CS_GRP_CALL) \
                or instruction.group(capstone.CS_GRP_RET):
            return None
        if target == LOAD_DC6 and EDX_PARTS & set(instruction.regs_access()[1]):
            return f"{instruction.mnemonic} {instruction.op_str}"
        if target == LOAD_DC6_SIZED and instruction.mnemonic == "push":
            return f"{instruction.mnemonic} {instruction.op_str}"
    return None


def caller_flags(machine):
    """Every direct call to the two loaders, with whether it passes flag 0.

    FUN_004788b0's flag is EDX (`xor edx, edx`); FUN_00478950's is its one
    stack argument (`push 0`). See flag_before.
    """
    code = machine.read(CODE_START, CODE_SIZE)
    zero = {LOAD_DC6: "xor edx, edx", LOAD_DC6_SIZED: "push 0"}
    found = {LOAD_DC6: [], LOAD_DC6_SIZED: []}
    for offset in range(len(code) - 5):
        if code[offset] != 0xE8:
            continue
        target = (CODE_START + offset + 5 + int.from_bytes(code[offset + 1:offset + 5], "little", signed=True)) & 0xFFFFFFFF
        if target in found:
            found[target].append((CODE_START + offset, flag_before(code, offset, target) == zero[target]))
    return found


def main():
    """Run the load, load-with-size, free and caller-flag cases."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    cap = Captures(machine)
    cases = 0

    name = r"data\global\ui\PANEL\invchar6"
    # Flag 0: ".dc6" whatever DAT_00741efc holds; no size pointer; lines 0x53 / 0x56.
    for use_dcc in (0, 1):
        result, read, build = load(machine, cap, name, 0, use_dcc)
        require(read == (FAKE_ARCHIVE, name + ".dc6", 0, ARCHIVE_CPP, 0x53), f"read {read!r}")
        require(build == (FILE_BYTES, ARCHIVE_CPP, 0x56, -1, name + ".dc6"), f"build {build!r}")
        require(result == CEL_FILE, f"FUN_004788b0 returned {result:#x}")
        cases += 1
    # Flag 1: ".dcc" only while DAT_00741efc is set.
    for use_dcc, ext in ((1, ".dcc"), (0, ".dc6")):
        _, read, _ = load(machine, cap, name, 1, use_dcc)
        require(read[1] == name + ext, f"flag 1, DAT_00741efc {use_dcc}: read {read[1]!r}")
        cases += 1

    # The sized loader passes the caller's size pointer through; lines 0x68 / 0x6b.
    weather = r"data\global\UncompOverlays\Rain1"
    size_ptr = machine.alloc(4)
    result, read, build = load_sized(machine, cap, weather, size_ptr, 0, 1)
    require(read == (FAKE_ARCHIVE, weather + ".dc6", size_ptr, ARCHIVE_CPP, 0x68), f"sized read {read!r}")
    require(build == (FILE_BYTES, ARCHIVE_CPP, 0x6B, -1, weather + ".dc6"), f"sized build {build!r}")
    require(result == CEL_FILE, f"FUN_00478950 returned {result:#x}")
    cases += 1
    _, read, _ = load_sized(machine, cap, weather, size_ptr, 1, 1)
    require(read[1] == weather + ".dcc", f"sized flag 1: read {read[1]!r}")
    cases += 1

    # Free: the cel teardown runs even for null; the memory free only for non-null.
    for pointer, frees in ((0, []), (CEL_FILE, [(CEL_FILE, ARCHIVE_CPP, 0x7A, 0)])):
        deletes_before, frees_before = len(cap.deletes), len(cap.frees)
        machine.call(FREE_DC6, ecx=pointer, limit=INSTRUCTION_LIMIT)
        finished(machine, "FUN_00478a00")
        require(cap.deletes[deletes_before:] == [pointer], f"FUN_00601a50 calls {cap.deletes[deletes_before:]!r}")
        require(cap.frees[frees_before:] == frees, f"free calls {cap.frees[frees_before:]!r}")
        cases += 1

    # Every caller passes flag 0, so stock always asks for ".dc6".
    found = caller_flags(machine)
    require(len(found[LOAD_DC6]) == 68 and len(found[LOAD_DC6_SIZED]) == 8,
            f"call sites: {len(found[LOAD_DC6])} / {len(found[LOAD_DC6_SIZED])}")
    other = [hex(site) for sites in found.values() for site, zero in sites if not zero]
    require(not other, f"call sites without flag 0: {other}")
    cases += 1
    print(f"ok: {cases} DC6 load/free cases; {len(found[LOAD_DC6])} + {len(found[LOAD_DC6_SIZED])} "
          "call sites pass flag 0")


if __name__ == "__main__":
    main()
