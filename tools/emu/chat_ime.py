# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's chat IME helpers (UI/chat.cpp: FUN_0047ad60, FUN_0047b010, FUN_0047b070).

    uv run python chat_ime.py

* FUN_0047ad60 (EDI = the IME context) refreshes the candidate list kept at
  0x7bb5f4. It asks ImmGetCandidateListCountA for the byte size (esi) and
  ImmGetCandidateListA(context, 0, NULL, 0) for the list; when both are
  non-zero it frees the old list (line 0xeb, `.\\UI\\chat.cpp`), allocates the
  first size (line 0xed), fills it with ImmGetCandidateListA(context, 0, list,
  that size), and frees it again (line 0xf2) and clears the pointer when the
  list's style (+4) is 2 and its count (+8) is 1. Otherwise nothing changes.
* FUN_0047b010 says whether the IME language is in use: mode 9 (0x7bb5dc) wants
  the system default language 0x404 or 0xc04, mode 8 wants 0x804 or 0x1004,
  any other mode asks ImmIsIME of the keyboard layout.
* FUN_0047b070 (message; ret 4) sets the message's handled flag (+0x18) to 1
  and its result (+0x1c) to 0.
The Imm and language imports and the allocator are hooked. No MPQs are read.
"""
import argparse
import hashlib
import itertools
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CANDIDATES = 0x47AD60
CHECK_LANGUAGE = 0x47B010
RESET_STATE = 0x47B070
ALLOC = 0x40B380
FREE = 0x40B3C0
LIST_POINTER = 0x7BB5F4
MODE = 0x7BB5DC
FILE_NAME = 0x6D7288
CONTEXT = 0x5150
LIMIT = 100000


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


def main():
    """Run the three helpers over their inputs and compare with the claim."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    state = {"language": 0, "ime": 0, "needed": 0, "size": 0, "style": 0, "count": 0, "layouts": []}
    log = {"alloc": [], "free": [], "fill": []}

    def count_a(machine):
        """ImmGetCandidateListCountA(context, &count): the byte size needed."""
        machine.w32(machine.arg(1), 1)
        return state["needed"]

    def list_a(machine):
        """ImmGetCandidateListA(context, index, buffer, length): the size, or fills the buffer."""
        if machine.arg(2) == 0:
            return state["size"]
        log["fill"].append((machine.arg(0), machine.arg(1), machine.arg(2), machine.arg(3)))
        machine.mu.mem_write(machine.arg(2) + 4, struct.pack("<II", state["style"], state["count"]))
        return machine.arg(3)

    emu.Emu.w_GetSystemDefaultLangID = lambda machine: (state["language"], 0)
    emu.Emu.w_GetKeyboardLayout = lambda machine: (state["layouts"].append(machine.arg(0)) or 0x4090409, 1)
    emu.Emu.w_ImmGetCandidateListCountA = lambda machine: (count_a(machine), 2)
    emu.Emu.w_ImmGetCandidateListA = lambda machine: (list_a(machine), 4)
    emu.Emu.w_ImmIsIME = lambda machine: (state["ime"], 1)
    machine = emu.Emu(str(exe))
    require(machine.cstr(FILE_NAME) == ".\\UI\\chat.cpp", "0x6d7288 is not .\\UI\\chat.cpp")

    def alloc(current):
        """Game alloc (ECX size, EDX file; line, flag on the stack)."""
        block = current.alloc(current.mu.reg_read(UC_X86_REG_ECX))
        log["alloc"].append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0), current.arg(1), block))
        return block

    def free(current):
        """Game free (ECX block, EDX file; line, flag on the stack)."""
        log["free"].append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0), current.arg(1)))
        return 0

    machine.hook(ALLOC, alloc, 2)
    machine.hook(FREE, free, 2)

    def run(address, *args, **kwargs):
        """Call and check the function returned."""
        result = machine.call(address, *args, limit=LIMIT, **kwargs)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x}: stopped at the cap")
        return result

    cases = 0
    # candidate list: every combination of the two sizes, an old list or none, style and count
    for needed, size, old, style, count in itertools.product((0, 0x40), (0, 0x30), (0, 1), (0, 1, 2), (0, 1, 3)):
        state.update(needed=needed, size=size, style=style, count=count)
        for entry in log.values():
            entry.clear()
        previous = machine.alloc(8) if old else 0
        machine.w32(LIST_POINTER, previous)
        run(CANDIDATES, regs={"edi": CONTEXT})
        got = machine.r32(LIST_POINTER)
        if not (needed and size):
            require(got == previous and not log["alloc"] and not log["free"] and not log["fill"], f"{needed},{size}: touched the list")
        else:
            frees = [(previous, FILE_NAME, 0xEB, 0)] if previous else []
            require(len(log["alloc"]) == 1 and log["alloc"][0][:4] == (needed, FILE_NAME, 0xED, 0),
                    f"alloc {log['alloc']}")
            block = log["alloc"][0][4]
            require(log["fill"] == [(CONTEXT, 0, block, needed)], f"fill {log['fill']}")
            if style == 2 and count == 1:
                require(got == 0 and log["free"] == frees + [(block, FILE_NAME, 0xF2, 0)], f"free {log['free']}")
            else:
                require(got != 0 and got == block and log["free"] == frees, f"kept {log['free']}")
        cases += 1

    # language check
    for mode, language, ime in itertools.product((0, 7, 8, 9, 12), (0x409, 0x404, 0xC04, 0x804, 0x1004), (0, 1)):
        state.update(language=language, ime=ime)
        state["layouts"].clear()
        machine.w32(MODE, mode)
        got = run(CHECK_LANGUAGE) & 0xFF
        if mode == 9:
            want = int(language in (0x404, 0xC04))
        elif mode == 8:
            want = int(language in (0x804, 0x1004))
        else:
            want = int(bool(ime))
        require(got == want, f"mode {mode} language {language:#x} ime {ime}: {got}")
        require(state["layouts"] == ([] if mode in (8, 9) else [0]), f"layout asked {state['layouts']}")
        cases += 1

    # reset state
    message = machine.alloc(0x30)
    machine.mu.mem_write(message, b"\xee" * 0x30)
    run(RESET_STATE, message)
    require(machine.read(message, 0x30) == b"\xee" * 0x18 + struct.pack("<II", 1, 0) + b"\xee" * 0x10, "reset state wrote more")
    cases += 1
    print(f"{cases} cases: candidate list refresh (frees 0xeb / 0xf2, alloc 0xed), language gate per mode, handled flag")
    print("ok")


if __name__ == "__main__":
    main()
