# SPDX-License-Identifier: GPL-3.0-or-later
"""Three bugs of game.exe's server side, run on its own code: bugs.md #111, #112 and #113.

    uv run python net_join_bugs.py [--exe PATH]

Prints `ok` and what each check saw, or stops at the first that does not hold.

- #111, S->C 0x16's nearby-player list. FUN_00537d10 builds the chain of
  the players near a client, nearest first; FUN_00537ee0 writes it to the
  client's +0x1cc buffer and counts it at +0x3bc. With 1, 2, 3 and 5
  players the count is 1, 1, 2 and 4: the chain less its last entry.
- #112, a game's name made unique (FUN_0052f940). The probe FUN_0052f8b0
  is hooked to refuse or take each candidate it is asked. The first
  candidate after a refusal is the name, `~` and the name's address in
  decimal; the next are `name~1`, `name~2`. A 7-character name's first
  candidate runs over the 16-byte buffer into the stack cookie (the check
  FUN_00681a48 is hooked: the function's own check, the last, sees a
  changed cookie; a 3-character name's checks all see it whole). When only `name~99`
  is taken the fatal report (FUN_00408090) gets line 0x263.
- #113, S->C 0x7e (FUN_0053db70). The queue FUN_0053b280 is hooked; with
  the stack filled with 0xaa first, the 5 bytes queued are 7e aa aa aa aa.

Set up by hand, not the claim: the units (type 0, an id at +0xc, a path at
+0x2c) and the path getters FUN_006488c0 / FUN_00648900, hooked to read
x and y from a two-dword stand-in.

Not covered: the senders (FUN_0052da00, FUN_00537fd0), what the probe's
table holds in play, the client's side of either packet.
"""
import argparse
import struct

from unicorn.x86_const import UC_X86_REG_ECX

import emu

CHAIN_ADD, CHAIN_WRITE = 0x537D10, 0x537EE0
PATH_X, PATH_Y = 0x6488C0, 0x648900
UNIQUE_NAME, NAME_PROBE, COOKIE_CHECK, COOKIE, FATAL = 0x52F940, 0x52F8B0, 0x681A48, 0x749DE0, 0x408090
BUILD_7E, QUEUE = 0x53DB70, 0x53B280


def check(held, what):
    """Stop with `what` when `held` is false."""
    if not held:
        raise SystemExit(f"FAIL: {what}")


def nearby_list(e):
    """#111: the count and ids FUN_00537ee0 writes for 1, 2, 3 and 5 players near."""
    e.hook(PATH_X, lambda emulator: emulator.r32(emulator.arg(0)), 1)
    e.hook(PATH_Y, lambda emulator: emulator.r32(emulator.arg(0) + 4), 1)
    seen = []
    for players in (1, 2, 3, 5):
        client, table, farthest = e.alloc(0x600), e.alloc(0x300), e.alloc(4)
        for index in reversed(range(players)):                 # farthest first: the chain sorts them
            unit, path = e.alloc(0x100), e.alloc(0x20)
            e.w32(unit + 0xC, 1000 + index)
            e.w32(unit + 0x2C, path)
            e.w32(path, 100 + 3 * index)
            e.w32(path + 4, 100)
            e.call(CHAIN_ADD, unit, 100, farthest, client, 20, regs={"eax": 100, "esi": table})
        chain, slot = [], 0
        while True:
            chain.append(e.r32(e.r32(table + slot * 12) + 0xC))
            slot = e.r32(table + slot * 12 + 8)
            if slot == 0:
                break
        check(chain == [1000 + index for index in range(players)], f"the chain of {players} is {chain}")
        e.call(CHAIN_WRITE, table, regs={"esi": client})
        count = e.r32(client + 0x3BC)
        sent = [struct.unpack_from("<BIHH", e.read(client + 0x1CC + 9 * entry, 9))[1] for entry in range(count)]
        check(sent == (chain if players == 1 else chain[:-1]), f"{players} players: sent {sent} of {chain}")
        seen.append(count)
    return seen


def unique_name(e):
    """#112: the candidates FUN_0052f940 asks the probe about, the buffer overrun and the suffix 99."""
    e.crt_init()
    asked, answers, cookies, lines = [], [], [], []

    def probe(emulator):
        """FUN_0052f8b0's stand-in: note the candidate in ECX, answer from the script."""
        asked.append(emulator.cstr(emulator.mu.reg_read(UC_X86_REG_ECX)))
        return answers.pop(0) if answers else 0

    e.hook(NAME_PROBE, probe, 0)
    e.hook(FATAL, lambda emulator: lines.append(emulator.arg(0)) or 0, 1)

    def run(name, script):
        """Run FUN_0052f940 on a fresh record; the name's address and the record's +0x2a after it."""
        asked.clear()
        answers[:] = script
        record, text = e.alloc(0x100), e.put(name + b"\0")
        try:
            e.call(UNIQUE_NAME, ecx=record, edx=text, limit=5_000_000)
        except SystemExit:                                     # emu's own fatal hook, past ours
            pass
        return text, e.cstr(record + 0x2A)

    text, stored = run(b"abc", [1])
    check(asked == ["abc"] and stored == "abc", f"a name the probe takes: asked {asked}, stored {stored!r}")
    text, stored = run(b"abc", [0, 0, 0, 1])
    first = f"abc~{text}"
    check(asked == ["abc", first, "abc~1", "abc~2"] and stored == "abc~2", f"refused: asked {asked}, stored {stored!r}")
    check(len(str(text)) >= 9, f"the name's address {text} has under 9 digits")
    text, stored = run(b"abc", [0] * 100 + [1])
    check(asked[-1] == "abc~99" and len(asked) == 101, f"the last candidate is {asked[-1]} of {len(asked)}")
    check(lines == [0x263] and stored == "", f"after abc~99: fatal lines {lines}, stored {stored!r}")
    e.hook(COOKIE_CHECK, lambda emulator: cookies.append(emulator.mu.reg_read(UC_X86_REG_ECX) != emulator.r32(COOKIE)) or 0, 0)
    run(b"abc", [0, 1])                                        # the control: every check on the way sees its cookie whole
    check(cookies and not any(cookies), f"a 3-character name's cookie checks: {cookies}")
    cookies.clear()
    run(b"abcdefg", [0, 1])
    check(cookies and cookies[-1], f"a 7-character name's cookie checks: {cookies}")
    return first


def packet_7e(e):
    """#113: the five bytes FUN_0053db70 queues when the stack held 0xaa."""
    sent = []
    e.hook(QUEUE, lambda emulator: sent.append(emulator.read(emulator.arg(0), emulator.arg(1))) or 0, 2)
    top = emu.STACK + emu.STACK_SIZE - 0x1000
    e.mu.mem_write(top - 0x100, b"\xaa" * 0x100)
    e.call(BUILD_7E, ecx=e.alloc(0x10))
    check(sent == [b"\x7e\xaa\xaa\xaa\xaa"], f"0x7e queued {[packet.hex() for packet in sent]}")
    return sent[0].hex()


def main():
    """`[--exe PATH]`: run the three checks and print what they saw."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", help="another copy of game.exe")
    args = parser.parse_args()
    counts = nearby_list(emu.Emu(args.exe))
    first = unique_name(emu.Emu(args.exe))
    packet = packet_7e(emu.Emu(args.exe))
    print(f"ok: 0x16 sends {counts} of 1, 2, 3, 5 players; first suffix {first}; 0x7e is {packet}")


if __name__ == "__main__":
    main()
