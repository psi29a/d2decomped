# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's chat command string helpers (UI/chat.cpp's neighbours of FUN_0047c420).

    uv run python chat_helpers.py [--exe path/to/Game.exe] [--cases N] [--seed S]

* FUN_0047b3b0 (EAX prefix, stack line; ret 4): -1 when the line is shorter
  than the prefix; else the prefix's length in bytes compared through toupper
  (both bytes sign-extended, MOVSX): 0 when all match, else 1 when the line's
  byte is the larger, -1 when smaller.
* FUN_0047c3f0 (EAX line, ESI out, stack size; ret 4): what follows the first
  space, copied by FUN_004135d0(out, after, size) (at most size - 1 bytes and a
  0); no space: out = "".
* FUN_0047bd60 (ESI name): the text after the first '*' when that isn't
  empty, else the name itself (a Battle.net "character*account" name's account).
* FUN_0047bd80 (ESI name, EBX out): the text before the first '*' (memcpy and a
  0); a name starting with '*', or with none, is copied whole (FUN_004135d0
  with 0x7fffffff).
* FUN_0047bd00 (ECX UTF-16 text): meant to say every unit is under 0x100, but
  the load of text[length] sits before the loop (0x47bd0e), so each pass tests
  that one unit, the terminating 0: it returns 1 for every text.
The CRT (toupper, memcpy) and Storm's string helpers run for real. No MPQs are read.
"""
import argparse
import hashlib
import random
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
PREFIX = 0x47B3B0
AFTER_SPACE = 0x47C3F0
ACCOUNT_PART = 0x47BD60
CHARACTER_PART = 0x47BD80
ALL_NARROW = 0x47BD00
LIMIT = 200_000


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


def signed_byte(value):
    """A byte as MOVSX sees it."""
    return value - 256 if value >= 128 else value


def upper(value):
    """The CRT's toupper in the C locale on a sign-extended byte: only a..z change."""
    return value - 32 if ord("a") <= value <= ord("z") else value


def prefix_model(line, prefix):
    """FUN_0047b3b0 as claimed."""
    if len(line) < len(prefix):
        return -1
    for mine, theirs in zip(line, prefix):
        difference = upper(signed_byte(mine)) - upper(signed_byte(theirs))
        if difference:
            return 1 if difference > 0 else -1
    return 0


def copy_model(text, size):
    """FUN_004135d0(out, text, size): at most size - 1 bytes."""
    return text[:max(size - 1, 0)]


def after_space_model(line, size):
    """FUN_0047c3f0 as claimed."""
    space = line.find(b" ")
    return copy_model(line[space + 1:], size) if space >= 0 else b""


def account_model(name):
    """FUN_0047bd60 as claimed."""
    star = name.find(b"*")
    return name[star + 1:] if star >= 0 and name[star + 1:] else name


def character_model(name):
    """FUN_0047bd80 as claimed."""
    star = name.find(b"*")
    return name[:star] if not name.startswith(b"*") and star >= 0 else name


def random_text(rng, alphabet, most):
    """A NUL-free byte string from the alphabet."""
    return bytes(rng.choice(alphabet) for _ in range(rng.randint(0, most)))


def main():
    """Run each helper on generated strings and compare with its model."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", help="game.exe 1.14d (default: <D2_MPQ_DIR>/bin/game.exe)")
    parser.add_argument("--cases", type=int, default=4000)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    exe = Path(args.exe) if args.exe else Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    machine.crt_init()
    rng = random.Random(args.seed)
    letters = list(b"aAbBzZ/ *!@?09_") + [0x7F, 0x80, 0xC9, 0xE9, 0xFF, 0x60, 0x7B, 0x40, 0x5B]
    scratch = machine.alloc(0x400)
    out = machine.alloc(0x400)

    def text_at(address, data):
        machine.mu.mem_write(address, data + b"\0")
        return address

    commands = [b"/ignore ", b"/unignore ", b"/squelch ", b"/msg", b"/w", b"/help", b"/?", b"/players"]
    for _ in range(args.cases):
        prefix = rng.choice(commands) if rng.random() < 0.5 else random_text(rng, letters, 10)
        line = (prefix if rng.random() < 0.4 else b"") + random_text(rng, letters, 12)
        if rng.random() < 0.3:
            line = bytes(upper(signed_byte(byte)) & 0xFF if rng.random() < 0.5 else byte for byte in line)
        prefix_at = text_at(scratch + 0x200, prefix)
        got = machine.call(PREFIX, text_at(scratch, line), regs={"eax": prefix_at}, limit=LIMIT)
        got = got - (1 << 32) if got & 0x80000000 else got
        require(got == prefix_model(line, prefix), f"FUN_0047b3b0({line!r}, {prefix!r}) = {got}")

        size = rng.choice([1, 2, 3, 5, 0x11, 0x22, 0x104])
        machine.mu.mem_write(out, b"\xab" * 0x120)
        machine.call(AFTER_SPACE, size, regs={"eax": text_at(scratch, line), "esi": out}, limit=LIMIT)
        want = after_space_model(line, size)
        require(machine.read(out, len(want) + 1) == want + b"\0", f"FUN_0047c3f0({line!r}, {size})")

        name = random_text(rng, list(b"ab*Zq9"), 8)
        got = machine.call(ACCOUNT_PART, regs={"esi": text_at(scratch, name)}, limit=LIMIT)
        require(machine.cstr(got).encode("latin-1") == account_model(name), f"FUN_0047bd60({name!r})")
        machine.mu.mem_write(out, b"\xab" * 0x40)
        machine.call(CHARACTER_PART, regs={"esi": text_at(scratch, name), "ebx": out}, limit=LIMIT)
        want = character_model(name)
        require(machine.read(out, len(want) + 1) == want + b"\0", f"FUN_0047bd80({name!r})")

    wide_alphabet = [0x41, 0x7A, 0xE9, 0xFF, 0x100, 0x101, 0x3042, 0x4E2D, 0xAC00, 0xFFFD]
    meant_differs = 0
    for _ in range(args.cases):
        units = [rng.choice(wide_alphabet) for _ in range(rng.randint(0, 20))]
        machine.mu.mem_write(scratch, b"".join(unit.to_bytes(2, "little") for unit in units) + b"\0\0")
        got = machine.call(ALL_NARROW, ecx=scratch, limit=LIMIT) & 0xFF
        require(got == 1, f"FUN_0047bd00({units}) = {got}")
        meant_differs += any(unit > 0xFF for unit in units)
    print(f"chat_helpers: FUN_0047b3b0, FUN_0047c3f0, FUN_0047bd60, FUN_0047bd80 match on {args.cases} cases each")
    print(f"chat_helpers: FUN_0047bd00 returned 1 for all {args.cases} texts; "
          f"{meant_differs} of them hold a unit over 0xff")


if __name__ == "__main__":
    main()
