# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's application-mode parse (Main.cpp, FUN_00406340).

    uv run python app_mode.py

FUN_00406340 takes the command line in EAX and a pointer to the mode on the
stack. It copies the line, splits the copy on "-" with strtok, and compares
each piece with strncmp against the six names at 0x7065c4 over the name's
length: modstate0, client, server, multiplayer, launcher, expand. A match
stores the name's index, except client (1). The last match wins; no match
leaves the caller's value (PreInitApplication passes 4, launcher). The CRT
strtok is replaced with a plain one (it recurses on its per-thread data under
the emulator); alloc and free are hooked. No MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
PARSE_MODE = 0x406340
ALLOC = 0x40B380
FREE = 0x40B3C0
STRTOK = 0x68264F
MODE_NAMES = 0x7065C4
DELIMITER = 0x6CCA2C
MAIN_CPP = 0x6CCA30
LAUNCHER = 4
UNSET = 0xDEAD  # the caller's value; a miss must leave it, not write 4
LIMIT = 200000

# (command line, expected mode; UNSET: no match, the caller's value stays)
CASES = [
    ("", UNSET),
    ("-w", UNSET),
    ("-expand", 5),
    ("-server", 2),
    ("-multiplayer -w", 3),
    ("-client", UNSET),             # client (1) is never stored
    ("-modstate0", 0),
    ("-exp", UNSET),                # the token must start with the whole name
    ("-expandable", 5),             # ...and may run on past it
    ("-expansion", UNSET),          # "expans" is not "expand"
    ("-Expand", UNSET),             # strncmp: case matters
    ("-server -expand", 5),         # the last match wins
    ("-expand -server", 2),
    ("-w -launcher -ns", LAUNCHER),
    ("-expand -launcher", LAUNCHER),
    ("-server -client", 2),         # client doesn't overwrite
    ("game.exe -server", 2),
    ("-servername x", 2),
]


def require(condition, message):
    """Stop the probe when a stock-code observation differs from the claim."""
    if not condition:
        raise RuntimeError(message)


def main():
    """Run FUN_00406340 on each command line and check the mode it leaves."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))

    names = [machine.cstr(machine.r32(MODE_NAMES + 4 * i)) for i in range(6)]
    require(names == ["modstate0", "client", "server", "multiplayer", "launcher", "expand"],
            f"unexpected mode names at 0x7065c4: {names!r}")
    require(machine.cstr(DELIMITER) == "-", "unexpected strtok delimiter at 0x6cca2c")
    require(machine.cstr(MAIN_CPP).lower().endswith("main.cpp"), "0x6cca30 is not Main.cpp")

    calls = {"alloc": [], "free": []}

    def alloc(current):
        """Game alloc (ECX size, EDX file, line): emulator heap, recorded."""
        size = current.mu.reg_read(UC_X86_REG_ECX)
        calls["alloc"].append((size, current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        return current.alloc(size)

    def free(current):
        """Game free (ECX block, EDX file, line): recorded, nothing released."""
        calls["free"].append((current.mu.reg_read(UC_X86_REG_ECX),
                              current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        return 0

    state = {"next": 0}

    def strtok(current):
        """cdecl strtok(str, delims) on emulator memory, as the CRT's."""
        start, delims = current.arg(0), current.cstr(current.arg(1)).encode()
        pos = start or state["next"]
        if not pos:
            return 0
        while (byte := current.read(pos, 1)[0]) and byte in delims:
            pos += 1
        if not byte:
            state["next"] = 0
            return 0
        token = pos
        while (byte := current.read(pos, 1)[0]) and byte not in delims:
            pos += 1
        if byte:
            current.mu.mem_write(pos, b"\0")
            state["next"] = pos + 1
        else:
            state["next"] = 0
        return token

    machine.hook(ALLOC, alloc, 2)
    machine.hook(FREE, free, 2)
    machine.hook(STRTOK, strtok, 0)

    for line, expected in CASES:
        calls["alloc"].clear()
        calls["free"].clear()
        mode = machine.alloc(4)
        machine.w32(mode, UNSET)
        text = machine.put(line.encode() + b"\0")
        result = machine.call(PARSE_MODE, mode, regs={"eax": text}, limit=LIMIT)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC,
                f"{line!r}: stopped at the {LIMIT} instruction cap")
        got = machine.r32(mode)
        require(result == 1, f"{line!r}: returned {result}")
        require(got == expected, f"{line!r}: mode {got}, expected {expected}")
        require(machine.cstr(text) == line, f"{line!r}: the caller's line was changed")
        require([(c[0], c[1], c[2]) for c in calls["alloc"]] == [(len(line) + 1, MAIN_CPP, 0x1BA)],
                f"{line!r}: alloc {calls['alloc']!r}")
        require(len(calls["free"]) == 1 and calls["free"][0][1:] == (MAIN_CPP, 0x1E0),
                f"{line!r}: free {calls['free']!r}")
        print(f"{line!r:22} -> {names[got] if got < len(names) else 'unchanged'}")
    print("ok")


if __name__ == "__main__":
    main()
