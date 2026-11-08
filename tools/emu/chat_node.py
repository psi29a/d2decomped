# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's overhead message node (Chat/Chat.cpp, FUN_00661110 and FUN_006611a0).

    uv run python chat_node.py

FUN_00661110 (stdcall: pool, text, now; ret 0xc) builds the node a unit keeps
at unit + 0xa4 for the text over its head. An empty text returns 0 and
allocates nothing. Otherwise it allocates 0x110 bytes (line 0x32, file
`.\\Chat\\Chat.cpp`) and fills +0 the time on screen, length x 8 + 125 frames
with the length clamped to 254, +4 now + that time, +8 the language byte
(FUN_00525150, hooked), +0xc 0 and +0x10 the text: strncpy copies the clamped
length + 1 bytes (so up to 255 characters), and a 0 goes at +0x10f. Nothing
else is written. FUN_006611a0 (pool, node; ret 8) frees a non-null node
(line 0x44). The allocator is hooked and poisons its block with 0xab, so a
stray write shows. A text up to 254 characters is stored whole; one of 255 or
more keeps 255 characters and still shows for 2157 frames (254 x 8 + 125).
No MPQs are read.
"""
import argparse
import hashlib
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FILE_NAME = 0x6EDE98
MAKE = 0x661110
FREE_NODE = 0x6611A0
ALLOC = 0x40B430
FREE = 0x40B480
LANGUAGE = 0x525150
POOL = 0x1234ABCD
LANGUAGE_ID = 7
NODE_SIZE = 0x110
LIMIT = 100000


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


def expected_node(text, now):
    """The node image the claim predicts (0xab where nothing is written)."""
    shown = min(len(text), 254)
    time = shown * 8 + 0x7D
    node = bytearray(b"\xab" * NODE_SIZE)
    node[0:8] = struct.pack("<II", time, (now + time) & 0xFFFFFFFF)
    node[8] = LANGUAGE_ID
    node[0xC:0x10] = bytes(4)
    node[0x10:0x10 + shown + 1] = text[:shown + 1].encode("latin-1").ljust(shown + 1, b"\0")
    node[0x10 + shown + 1] = 0
    return bytes(node)


def main():
    """Make nodes for texts around every length boundary and free them."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    require(machine.cstr(FILE_NAME) == ".\\Chat\\Chat.cpp", "0x6ede98 is not .\\Chat\\Chat.cpp")
    log = {"alloc": [], "free": []}

    def alloc(current):
        """Game alloc (ECX pool, EDX size; file, line, flag on the stack): poisoned."""
        size = current.mu.reg_read(UC_X86_REG_EDX)
        log["alloc"].append((current.mu.reg_read(UC_X86_REG_ECX), size, current.arg(0), current.arg(1), current.arg(2)))
        block = current.alloc(size)
        current.mu.mem_write(block, b"\xab" * size)
        return block

    def free(current):
        """Game free (ECX pool, EDX block; file, line, flag on the stack)."""
        log["free"].append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                            current.arg(0), current.arg(1), current.arg(2)))
        return 0

    machine.hook(ALLOC, alloc, 3)
    machine.hook(FREE, free, 3)
    machine.hook(LANGUAGE, lambda current: LANGUAGE_ID, 0)

    cases = 0
    lengths = (1, 2, 4, 5, 100, 253, 254, 255, 256, 300)
    for length in lengths:
        for now in (0, 1000, 0xFFFFFF00):
            text = ("0123456789" * 30)[:length]
            log["alloc"].clear()
            node = machine.call(MAKE, POOL, machine.put(text.encode() + b"\0"), now, limit=LIMIT)
            require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{length}: stopped at the cap")
            require(log["alloc"] == [(POOL, NODE_SIZE, FILE_NAME, 0x32, 0)], f"{length}: alloc {log['alloc']}")
            got = machine.read(node, NODE_SIZE)
            want = expected_node(text, now)
            require(got == want, f"length {length} now {now:#x}: node {got.hex()}, expected {want.hex()}")
            cases += 1
    log["alloc"].clear()
    require(machine.call(MAKE, POOL, machine.put(b"\0"), 5, limit=LIMIT) == 0 and not log["alloc"], "empty text makes a node")

    time_254 = machine.r32(machine.call(MAKE, POOL, machine.put(b"x" * 254 + b"\0"), 0, limit=LIMIT))
    time_300 = machine.r32(machine.call(MAKE, POOL, machine.put(b"x" * 300 + b"\0"), 0, limit=LIMIT))
    require(time_254 == time_300 == 254 * 8 + 125 == 2157, f"clamped time {time_254}, {time_300}")

    node = machine.call(MAKE, POOL, machine.put(b"3684\0"), 10, limit=LIMIT)
    log["free"].clear()
    machine.call(FREE_NODE, POOL, node, limit=LIMIT)
    require(log["free"] == [(POOL, node, FILE_NAME, 0x44, 0)], f"free {log['free']}")
    log["free"].clear()
    machine.call(FREE_NODE, POOL, 0, limit=LIMIT)
    require(not log["free"], "a null node is freed")
    print(f"{cases} nodes: time = min(length, 254) x 8 + 125 ({time_254} frames at 254 and above), "
          "up to 255 characters kept; empty makes none; free line 0x44")
    print("ok")


if __name__ == "__main__":
    main()
