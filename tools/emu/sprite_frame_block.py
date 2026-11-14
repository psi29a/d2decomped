# SPDX-License-Identifier: GPL-3.0-or-later
"""Check SPRITECACHE's frame finder FUN_005feb80 in stock game.exe 1.14d.

Run: export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
     uv run python sprite_frame_block.py [--mutation-check]

FUN_005feb80 (EAX = the decoded sprite blob, ECX = the draw state, stack: the
frame data base; ret 4) returns a pointer to one frame's block, or 0 for "no
cell". The claim, run against game.exe's own code on random blobs:
  - 0 for a null base or blob, and for a frame at or past the blob's byte +4
    (unsigned compare: the last frame is `+4 - 1`; bugs.md #29 / #30);
  - the direction: mode (state +8) 6 with state flag +4 & 2 or any mode but 2
    and 4 asks FUN_00600c90(state +0x40, byte +3 - 1); mode 2 is direction 0
    (no call); mode 4 is state +0x40 as it stands;
  - 0 when the direction is >= byte +3 - 1 (signed) or its offset (+0xc +
    4 * direction) is negative;
  - frame blocks are walked from that offset: a block whose first dword is
    not 0, or width / height (+4, +8) above 0x100, ends it with 0; the next
    block is at offset + 0x23 + [+0x1c] + [+0x14], negative gives 0 (even
    after the last wanted block); the result is the block of the wanted frame.
FUN_00600c90 is hooked (its arguments are logged and checked). No MPQs are read.
"""
import argparse
import hashlib
import random
import struct
import sys
import zlib
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TARGET, DIRFN = 0x5FEB80, 0x600C90
M32 = 0xFFFFFFFF
BLOB, BASE, STATE = 0x2000, 0x3000, 0x100
CASES = 30000
MUT = set()


def s32(value):
    """Signed view of a 32-bit value."""
    value &= M32
    return value - (1 << 32) if value >> 31 else value


def dir_result(direction, last):
    """What the hooked FUN_00600c90 returns for these arguments."""
    return zlib.crc32(struct.pack("<II", direction & M32, last & M32)) % 12


def model(blob, base, state):
    """FUN_005feb80 re-stated from its disassembly: (result offset into base or None, calls).

    `blob` and `base` are bytearrays (None: a null pointer); the result is the block's offset, 0 for none.
    """
    calls = []
    if base is None or blob is None:
        return 0, calls
    frame, flags, mode, direction = state
    frames_limit = blob[4] if "frame_lt" not in MUT else blob[4] + 1
    if frame >= frames_limit:
        return 0, calls
    dirs = blob[3]
    last = (dirs - 1) & M32
    if mode == 6 and flags & 2 or mode not in (2, 4):
        calls.append((direction, last) if "argswap" not in MUT else (last, direction))
        found = dir_result(direction, last)
    elif mode == 2:
        found = 0
    else:
        found = direction
    if "dir_gt" in MUT:
        if s32(last) < s32(found) :
            return 0, calls
    elif s32(last) <= s32(found):
        return 0, calls
    offset = struct.unpack_from("<i", blob, 0xC + 4 * s32(found))[0]   # direction -1 reads blob +8, below the table
    if offset < 0:
        return 0, calls
    block = offset
    for _ in range(frame + 1):
        here = block
        if struct.unpack_from("<I", base, here)[0] != 0:
            return 0, calls
        width, height = struct.unpack_from("<II", base, here + 4)
        if width > 0x100 or height > (0x100 if "height" not in MUT else 0x101):
            return 0, calls
        extra = (struct.unpack_from("<I", base, here + 0x1C)[0] + struct.unpack_from("<I", base, here + 0x14)[0]) & M32
        block = s32(block + extra + 0x23)
        if block < 0:
            return 0, calls
    return here, calls


def build(rng):
    """One random blob, frame data and draw state, with rare corruptions."""
    dirs, frames = rng.randrange(0, 9), rng.randrange(0, 7)
    blob = bytearray(0x80)
    blob[3], blob[4] = dirs, frames
    struct.pack_into("<i", blob, 8, rng.choice((0x40, -1)))
    base = bytearray(0x1000)
    cursor = 0x40
    for direction in range(0, 9):
        struct.pack_into("<i", blob, 0xC + 4 * direction, cursor if rng.randrange(14) else -rng.randrange(1, 100))
        start = cursor
        for _ in range(7):
            struct.pack_into("<IIIII", base, cursor, 0 if rng.randrange(30) else 1,
                             rng.choice((8, 0x100, 0x100, 0x101, 20)), rng.choice((8, 0x100, 0x101, 30)), 0, 0)
            pad, bodies = rng.randrange(0, 5), rng.randrange(0, 6)
            struct.pack_into("<I", base, cursor + 0x14, bodies)
            struct.pack_into("<I", base, cursor + 0x1C, pad)
            cursor += 0x23 + bodies + pad
            if cursor > 0xF00:
                break
        if cursor > 0xF00:
            break
        cursor = max(cursor, start + 0x23)
    if rng.randrange(40) == 0:
        struct.pack_into("<I", base, 0x40 + 0x14, 0xFFFFFF00)       # the next offset wraps negative
    state = (rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFFFFFFFF)), rng.choice((0, 2, 6)),
             rng.choice((0, 1, 2, 3, 4, 5, 6, 7)), rng.choice((0, 1, 2, 3, 7, 8, 15, 0xFFFFFFFF)))
    if rng.randrange(25) == 0:
        base = None
    elif rng.randrange(25) == 0:
        blob = None
    return blob, base, state


def run(mutation):
    """Run CASES random cases through game.exe and the model; return the failure count and a summary."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    MUT.clear()
    MUT.update(mutation)
    machine = emu.Emu(str(exe))
    machine.mu.mem_map(0x10000, 0x8000)
    seen = []

    def dirfn(e):
        """FUN_00600c90 stand-in: log (direction, last) and return the model's value."""
        seen.append((e.arg(0), e.arg(1)))
        return dir_result(e.arg(0), e.arg(1))

    machine.hook(DIRFN, dirfn, 2)
    rng = random.Random(1)
    bad, buckets = 0, {}
    for index in range(CASES):
        blob, base, state = build(rng)
        # real pointers are the arena addresses; a null pointer is 0
        blob_ptr = 0x10000 if blob is not None else 0
        base_ptr = 0x14000 if base is not None else 0
        if blob is not None:
            machine.mu.mem_write(blob_ptr, bytes(blob).ljust(0x100, b"\0"))
        if base is not None:
            machine.mu.mem_write(base_ptr, bytes(base))
        machine.mu.mem_write(0x16000, struct.pack("<I", state[0]) + struct.pack("<I", state[1]) + struct.pack("<I", state[2])
                             + bytes(0x34) + struct.pack("<I", state[3]))
        seen.clear()
        got = machine.call(TARGET, base_ptr, ecx=0x16000, regs={"eax": blob_ptr})
        want, calls = model(blob, base, state)
        want_ptr = base_ptr + want if want and base is not None else 0
        key = "none" if got == 0 else "cell"
        buckets[key] = buckets.get(key, 0) + 1
        if got != want_ptr or seen != calls:
            bad += 1
            if bad <= 5:
                print(f"case {index}: state {state} game {got:#x} (calls {seen}) model {want_ptr:#x} (calls {calls})")
    return bad, buckets


def main():
    """Run the oracle, and with --mutation-check prove each mutant of the model is caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    bad, buckets = run(())
    if bad:
        print(f"FAIL: {bad} of {CASES} differ")
        sys.exit(1)
    print(f"ok: {CASES} FUN_005feb80 cases ({buckets})")
    if args.mutation_check:
        for name in ("frame_lt", "dir_gt", "height", "argswap"):
            bad, _ = run((name,))
            print(f"mutant {name}: {'caught' if bad else 'NOT CAUGHT'} ({bad})")
            if not bad:
                sys.exit(1)
        print("mutation check ok")


if __name__ == "__main__":
    main()
