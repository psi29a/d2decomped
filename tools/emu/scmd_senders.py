# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's server-to-client packet builders of GAME/SCmd.cpp.

Every builder takes the client in ECX (it copies ECX to EDI) and hands one packet
buffer to the queue function FUN_0053b280(buffer, size), which reads the client
from EDI. Most builders are fastcall: EDX is the packet id byte or a pointer, the
stack holds the rest (`ret 4 * n`). The oracle runs the stock builders with the
queue hooked (it logs EDI, the buffer bytes and the size) and compares the queued
packet with a model written from the disassembly, over random and boundary
arguments. Garbage in the unused upper bytes of every register and stack word
shows a builder that reads more than its model says.

The queue FUN_0053b280 itself runs unhooked on a fake client (head +0x1b8,
tail +0x1bc, free list +0x1c0; block = u32 length, 0x200 data bytes, u32 next at
+0x204; 0x208 bytes from the pool allocator 0x40b430 with ".\\GAME\\SCmd.cpp", 0xa0).

Covers: FUN_0053b280 queue, 0053b320, 0053b340, 0053b390, 0053b430, 0053b4b0,
0053b520, 0053b570, 0053b9f0, 0053ba40, 0053baa0, 0053bb00, 0053bb70, 0053bc10,
0053bdd0, 0053be40, 0053bee0, 0053bfd0, 0053c320, 0053c3f0, 0053c590, 0053c900,
0053cb30, 0053cb80, 0053d2e0, 0053d4d0, 0053d710, 0053d780, 0053d7e0, 0053d8d0,
0053d960, 0053e020, 0053e060, 0053e9b0.

Hooked (their work is not claimed): 0x53_FD40 (game difficulty seed, returns a
dword), 0x40_B430 / 0x40_B480 (pool, logged), 0x40_8A60 (fatal report, logged),
0x68_1E09 (exit(-1), stops the run). The bit writers 0x41_0E40 / 0x41_0EB0 /
0x41_0E90 and the CRT memset / memcpy run as stock.

Usage: D2_MPQ_DIR=<MPQ folder> uv run python scmd_senders.py [--mutation-check]
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import require
from unicorn.x86_const import UC_X86_REG_EDI

QUEUE = 0x53_B280
FATAL_REPORT = 0x40_8A60
FATAL_EXIT = 0x68_1E09
SEED = 0x53_FD40
FILE_STR = 0x6CC837          # the first argument of every fatal report
BLOCK, DATA, NEXT = 0x208, 4, 0x204
MUTATIONS = (
    "queue-limit-0x1ff", "queue-limit-0x201", "queue-reuses-freelist-ignored", "queue-no-link-to-old-tail",
    "queue-appends-at-zero", "queue-length-not-added", "queue-appends-to-head",
    "fixed-field-swapped", "fixed-width-truncated", "fixed-id-wrong",
    "flags-swapped", "unit-null-id-zero", "unit-id-offset", "fatal-bound-7", "fatal-bound-0x100",
    "delta-signed-compare", "delta-boundary-0x100", "delta-boundary-0xfffe-inclusive", "delta-wide-uses-delta", "tier-value-24-bit",
    "delta2-first-range-includes-zero", "delta2-bound-0xfe", "delta2-wide-uses-delta",
    "ushort-bound-0x100", "stat-bound-32-bit", "ushort-tier-0x100", "ushort-tier-0xfffe", "d2e0-null-byte-zero", "d2e0-null-id-zero",
    "d4d0-flag-id", "copy-one-short", "copy-d7e0-skips-tail", "bits-msb-first", "bits-width-14", "bits-size-no-round",
    "zero-packet-wrong-size", "null-edi-still-copies",
)
mutation = None


def mut(name):
    """True while the model carries the deliberate defect `name` (--mutation-check)."""
    return mutation == name


def check(got, want, label):
    """Compare the emulated answer with the model; a mismatch stops the oracle."""
    require(got == want, f"{label}: game.exe {got!r}, model {want!r}")


def u(value, width):
    """`value` as `width` little-endian bytes."""
    return (value & ((1 << (8 * width)) - 1)).to_bytes(width, "little")


# Fixed layouts: address -> (stack words, packet size, [(offset, width, source)]); source is "dl" (EDX low
# byte), "edx" (EDX), "aN" (stack word N) or an integer constant. Written from the disassembly.
FIXED = {
    0x53_B320: (0, 1, [(0, 1, "dl")]),
    0x53_B390: (4, 0xC, [(0, 1, "dl"), (1, 1, "a0"), (2, 4, "a1"), (6, 2, "a3"), (8, 4, "a2")]),
    0x53_B430: (5, 9, [(0, 1, "dl"), (1, 1, "a0"), (2, 4, "a1"), (6, 1, "a2"), (7, 1, "a3"), (8, 1, "a4")]),
    0x53_B520: (7, 0x10, [(0, 1, "dl"), (1, 1, "a0"), (2, 4, "a1"), (6, 1, "a2"), (7, 1, "a3"), (8, 4, "a4"),
                          (0xC, 2, "a5"), (0xE, 2, "a6")]),
    0x53_B9F0: (5, 0xC, [(0, 1, "dl"), (1, 4, "a0"), (5, 1, "a1"), (6, 1, "a2"), (7, 4, "a3"), (0xB, 1, "a4")]),
    0x53_BA40: (6, 0xC, [(0, 1, "dl"), (1, 4, "a0"), (5, 1, "a1"), (6, 2, "a2"), (8, 2, "a3"), (0xA, 1, "a4"),
                         (0xB, 1, "a5")]),
    0x53_BAA0: (7, 0x10, [(0, 1, "dl"), (1, 4, "a0"), (5, 1, "a1"), (6, 1, "a2"), (7, 4, "a3"), (0xB, 1, "a4"),
                          (0xC, 2, "a5"), (0xE, 2, "a6")]),
    0x53_BB00: (8, 0x10, [(0, 1, "dl"), (1, 4, "a0"), (5, 1, "a1"), (6, 2, "a2"), (8, 2, "a3"), (0xA, 1, "a4"),
                          (0xB, 1, "a5"), (0xC, 2, "a6"), (0xE, 2, "a7")]),
    0x53_BB70: (3, 10, [(0, 1, 0x6D), (1, 4, "edx"), (5, 2, "a0"), (7, 2, "a1"), (9, 1, "a2")]),
    0x53_BC10: (5, 0xB, [(0, 1, "dl"), (1, 1, "a0"), (2, 4, "a1"), (6, 2, "a2"), (8, 2, "a3"), (0xA, 1, "a4")]),
    0x53_C590: (4, 0xD, [(0, 1, 0x23), (1, 1, "dl"), (2, 4, "a0"), (6, 1, "a1"), (7, 2, "a2"), (9, 4, "a3")]),
    0x53_CB30: (4, 0xD, [(0, 1, 0x7A), (1, 1, "dl"), (2, 1, "a0"), (3, 2, "a3"), (5, 4, "a2"), (9, 4, "a1")]),
    0x53_CB80: (6, 0x14, [(0, 1, 0x81), (1, 1, "a0"), (2, 2, "a3"), (4, 4, "a2"), (8, 4, "a1"), (0xC, 4, "a4"),
                          (0x10, 4, "a5")]),
}
# Fixed layouts that first reject a byte >= 6 in stack word 0 with the fatal report (call-site address).
FATAL6 = {
    0x53_B4B0: (7, 0xD, [(0, 1, "dl"), (1, 1, "a0"), (2, 4, "a1"), (6, 1, "a2"), (7, 2, "a3"), (9, 2, "a4"),
                         (0xB, 1, "a5"), (0xC, 1, "a6")], 0x53_B4F5),
    0x53_B570: (8, 0x10, [(0, 1, "dl"), (1, 1, "a0"), (2, 4, "a1"), (6, 1, "a2"), (7, 2, "a3"), (9, 2, "a4"),
                          (0xB, 1, "a5"), (0xC, 2, "a6"), (0xE, 2, "a7")], 0x53_B5BF),
}
# Whole-struct copies: address -> bytes copied from the structure EDX points at.
COPIES = {0x53_C900: 10, 0x53_D710: 6, 0x53_D780: 8, 0x53_D7E0: 15, 0x53_D8D0: 7, 0x53_D960: 0x15, 0x53_E060: 0x1A}
# Bit-packed packets: address -> (stack words, id, [(source, bits)]); EDX and stack words are 16-bit or 8-bit fields.
BITPACKED = {
    0x53_C320: (6, 0x95, [("dx", 15), ("a0", 15), ("a1", 15), ("a2", 16), ("a3", 16), ("a4", 8), ("a5", 8)]),
    0x53_C3F0: (4, 0x96, [("dx", 15), ("a0", 16), ("a1", 16), ("a2", 8), ("a3", 8)]),
}
EDGES = (0, 1, 0xFE, 0xFF, 0x100, 0xFFFE, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF)


class Rig:
    """The emulated game.exe with the queue hooked and the fatal path logged."""

    def __init__(self):
        """Build the machine, hook the queue, the fatal report, exit and FUN_0053fd40."""
        self.w = mw.World()
        self.m = self.w.m
        self.queued, self.fatals, self.exited = [], [], False
        self.seed = 0
        self.seed_args = []
        self.m.hook(QUEUE, self._queue, 2)
        self.m.hook(FATAL_REPORT, self._report, 2)
        self.m.hook(FATAL_EXIT, self._exit, 1)
        self.m.hook(SEED, self._seed, 0)
        self.client = 0x1234_5670

    def _queue(self, current):
        """The queue hook: log (EDI, bytes, size)."""
        size = current.arg(1)
        self.queued.append((current.mu.reg_read(UC_X86_REG_EDI), bytes(current.read(current.arg(0), size)), size))

    def _report(self, current):
        """The fatal report: log both arguments (the file string and the call-site address)."""
        self.fatals.append((current.arg(0), current.arg(1)))

    def _exit(self, current):
        """exit(-1): stop the run."""
        self.exited = True
        current.mu.emu_stop()

    def _seed(self, current):
        """FUN_0053fd40: log ECX and return the scripted dword."""
        self.seed_args.append(current.mu.reg_read(mw.REGISTERS["ecx"]))
        return self.seed

    def call(self, address, stack, edx, ecx=None):
        """Run one builder; return (queued packets, fatals). Checks the `ret n` pop unless it exited."""
        self.queued.clear()
        self.fatals.clear()
        self.exited = False
        self.seed_args.clear()
        machine = self.m
        sp0 = mw.emu.STACK + mw.emu.STACK_SIZE - 0x1000 - 4 * (len(stack) + 1)
        machine.call(address, *stack, ecx=self.client if ecx is None else ecx, edx=edx, limit=mw.INSTRUCTION_LIMIT)
        if not self.exited:
            require(machine.mu.reg_read(mw.UC_X86_REG_EIP) == mw.emu.RET_MAGIC, f"{address:#x} did not return")
            popped = machine.mu.reg_read(mw.UC_X86_REG_ESP) - sp0 - 4
            require(popped == 4 * len(stack), f"{address:#x} popped {popped} bytes, expected {4 * len(stack)}")
        return list(self.queued), list(self.fatals), self.exited


def rand_word(rng):
    """A random 32-bit word, often an edge value."""
    return rng.choice(EDGES) if rng.random() < 0.4 else rng.getrandbits(32)


def fixed_model(spec, edx, stack):
    """Packet bytes of a fixed layout."""
    size, fields = spec
    out = bytearray(size)
    for offset, width, source in fields:
        if source == "dl":
            value = edx
        elif source == "edx":
            value = edx
        elif isinstance(source, int):
            value = source
        else:
            value = stack[int(source[1:])]
        out[offset:offset + width] = u(value, width)
    if mut("fixed-field-swapped"):
        pairs = [(a, b) for i, a in enumerate(fields[1:]) for b in fields[2 + i:] if a[1] == b[1]]
        if pairs:
            (o1, w1, _), (o2, w2, _) = pairs[0]
            out[o1:o1 + w1], out[o2:o2 + w2] = out[o2:o2 + w2], out[o1:o1 + w1]
    if mut("fixed-width-truncated") and size > 8:
        out[size - 1] = 0
    if mut("fixed-id-wrong"):
        out[0] ^= 1
    return bytes(out)


def bit_pack(values):
    """LSB-first bit packing of [(value, bits)]; returns the packed bytes (size rounded up)."""
    if mut("bits-msb-first"):
        values = [(v, b) for v, b in reversed(values)]
    accumulator = position = 0
    for value, bits in values:
        if mut("bits-width-14") and bits == 15:
            bits = 14
        accumulator |= (value & ((1 << bits) - 1)) << position
        position += bits
    size = position // 8 if mut("bits-size-no-round") else (position + 7) // 8
    return accumulator.to_bytes(max(size, 1) + 1, "little")[:size]


def delta_1a(edx, stack):
    """FUN_0053bdd0: x = EDX, y = stack[0]; the difference picks 0x1a, 0x1b or 0x1c."""
    d = (edx - stack[0]) & 0xFFFFFFFF
    wide = d > 0xFFFE
    if mut("delta-boundary-0xfffe-inclusive"):
        wide = d > 0xFFFF
    signed = d - (1 << 32) if d & 0x80000000 else d
    if mut("delta-signed-compare"):
        wide = signed > 0xFFFE
    narrow = signed < 0xFF
    if mut("delta-boundary-0x100"):
        narrow = signed < 0x100
    if wide:
        return u(0x1C, 1) + u(edx, 4)
    if narrow:
        return u(0x1A, 1) + u(d, 1)
    return u(0x1B, 1) + u(d, 2)


def delta_19(edx, stack):
    """FUN_0053e9b0: x = EDX, y = stack[0]; difference 1..0xfe is 0x19, else x with a 0x0e byte."""
    d = (edx - stack[0]) & 0xFFFFFFFF
    first = 0 < d < 0xFF if not mut("delta2-first-range-includes-zero") else d < 0xFF
    if mut("delta2-bound-0xfe"):
        first = 0 < d < 0xFE
    if first:
        return u(0x19, 1) + u(d, 1)
    if edx < 0xFF:
        return b"\x1d\x0e" + u(edx, 1)
    if edx < 0xFFFF:
        return b"\x1e\x0e" + u(edx, 2)
    return b"\x1f\x0e" + u(d if mut("delta2-wide-uses-delta") else edx, 4)


def ushort_tier(edx, stack):
    """FUN_0053be40: u16 EDX is a byte index (fatal at >= 0xff), stack[0] the value: 1, 2 or 4 bytes."""
    value = stack[0]
    if mut("tier-value-24-bit"):
        value &= 0xFFFFFF
    if value < 0xFF:
        return u(0x1D, 1) + u(edx, 1) + u(value, 1)
    if value < 0xFFFF:
        return u(0x1E, 1) + u(edx, 1) + u(value, 2)
    return u(0x1F, 1) + u(edx, 1) + u(value, 4)


def unit_id(unit_ptr, rig):
    """The u32 at unit+0xc, or 0xffffffff for a null unit."""
    if unit_ptr == 0:
        return 0 if mut("unit-null-id-zero") else 0xFFFFFFFF
    offset = 0x10 if mut("unit-id-offset") else 0xC
    return struct.unpack("<I", rig.m.read(unit_ptr + offset, 4))[0]


def run_fixed(rig, rng, count):
    """The fixed-layout builders over random and edge arguments."""
    cases = 0
    for address, (nstack, size, fields) in FIXED.items():
        for _ in range(count):
            edx = rand_word(rng)
            stack = [rand_word(rng) for _ in range(nstack)]
            ecx = rng.choice((rig.client, 0, 0xFFFFFFFF))
            queued, fatals, _ = rig.call(address, stack, edx, ecx)
            want = fixed_model((size, fields), edx, stack)
            check(queued, [(ecx, want, size)], f"{address:#x} edx={edx:#x} stack={stack}")
            check(fatals, [], f"{address:#x} fatal")
            cases += 1
    for address, (nstack, size, fields, site) in FATAL6.items():
        bound = 7 if mut("fatal-bound-7") else 6
        for _ in range(count):
            edx = rand_word(rng)
            stack = [rand_word(rng) for _ in range(nstack)]
            if rng.random() < 0.5:
                stack[0] = (stack[0] & ~0xFF) | rng.choice((0, 1, 5, 6, 7, 0xFF, rng.getrandbits(8)))
            queued, fatals, exited = rig.call(address, stack, edx)
            if (stack[0] & 0xFF) >= bound:
                check((queued, fatals, exited), ([], [(FILE_STR, site)], True), f"{address:#x} fatal stack0={stack[0]:#x}")
            else:
                want = fixed_model((size, fields), edx, stack)
                check((queued, fatals, exited), ([(rig.client, want, size)], [], False), f"{address:#x} ok")
            cases += 1
    return cases


def run_copies(rig, rng, count):
    """The builders that copy n bytes of the structure at EDX."""
    cases = 0
    for address, length in COPIES.items():
        for _ in range(count):
            data = bytes(rng.getrandbits(8) for _ in range(0x30))
            pointer = rig.m.alloc(0x30)
            rig.m.mu.mem_write(pointer, data)
            queued, fatals, _ = rig.call(address, [], pointer)
            n = length - 1 if mut("copy-one-short") else length
            want = data[:n]
            if mut("copy-d7e0-skips-tail") and address == 0x53_D7E0:
                want = data[:14] + b"\0"
            check((queued, fatals), ([(rig.client, want, length)], []), f"{address:#x} copy {length}")
            cases += 1
    return cases


def run_zero_packet(rig, rng):
    """FUN_0053e020: id 0x8f and 32 zero bytes; ECX only."""
    queued, fatals, _ = rig.call(0x53_E020, [], rng.getrandbits(32))
    zeros = 0x1F if mut("zero-packet-wrong-size") else 0x20
    check((queued, fatals), ([(rig.client, b"\x8f" + bytes(zeros), zeros + 1)], []), "0x8f zero packet")
    return 1


def run_difficulty(rig, rng, count):
    """FUN_0053b340: id, difficulty byte, seed dword (FUN_0053fd40 on the game), two flags."""
    cases = 0
    for _ in range(count):
        game = rig.m.alloc(0x100)
        raw = bytes(rng.getrandbits(8) for _ in range(0x100))
        flags = [rng.choice((0, 0, 1, 0x100, 0x80000000)) for _ in range(2)]
        raw = bytearray(raw)
        raw[0x70:0x74] = u(flags[0], 4)
        raw[0x74:0x78] = u(flags[1], 4)
        rig.m.mu.mem_write(game, bytes(raw))
        rig.seed = rng.getrandbits(32)
        edx = rand_word(rng)
        queued, fatals, _ = rig.call(0x53_B340, [game], edx)
        difficulty = raw[0x6D]
        flag_a, flag_b = int(flags[0] != 0), int(flags[1] != 0)
        if mut("flags-swapped"):
            flag_a, flag_b = flag_b, flag_a
        want = u(edx, 1) + bytes([difficulty]) + u(rig.seed, 4) + bytes([flag_a, flag_b])
        check((queued, fatals, rig.seed_args), ([(rig.client, want, 8)], [], [game]), "0x53b340")
        cases += 1
    return cases


def run_delta(rig, rng, count):
    """FUN_0053bdd0, 0053e9b0, 0053be40: the one/two/four-byte tiers."""
    cases = 0
    pairs = []
    for _ in range(count):
        x = rand_word(rng)
        d = rng.choice((0, 1, 0xFE, 0xFF, 0x100, 0xFFFD, 0xFFFE, 0xFFFF, 0x10000, 0xFFFFFFFF, 0x80000000, rng.getrandbits(32)))
        pairs.append((x, (x - d) & 0xFFFFFFFF))
    for x, y in pairs:
        for address, model in ((0x53_BDD0, delta_1a), (0x53_E9B0, delta_19)):
            queued, fatals, _ = rig.call(address, [y], x)
            want = model(x, [y])
            check((queued, fatals), ([(rig.client, want, len(want))], []), f"{address:#x} x={x:#x} y={y:#x}")
            cases += 1
    bound = 0x100 if mut("ushort-bound-0x100") else 0xFF
    for _ in range(count):
        edx = rng.choice((0, 1, 0xFE, 0xFF, 0x100, 0xFFFF, rng.getrandbits(32), (rng.getrandbits(16) << 16) | rng.randrange(0xFF)))
        y = rng.choice(EDGES + (rng.getrandbits(32),))
        queued, fatals, exited = rig.call(0x53_BE40, [y], edx)
        if (edx if mut("stat-bound-32-bit") else edx & 0xFFFF) >= bound:
            check((queued, fatals, exited), ([], [(FILE_STR, 0x53_BE5A)], True), f"0x53be40 fatal edx={edx:#x}")
        else:
            want = ushort_tier(edx & 0xFFFF, [y])
            check((queued, fatals, exited), ([(rig.client, want, len(want))], [], False), f"0x53be40 edx={edx:#x} y={y:#x}")
        cases += 1
    return cases


def run_unit_tiers(rig, rng, count):
    """FUN_0053bee0 and 0053bfd0: the unit id (unit+0xc, or -1 for null) with a 1/2/4-byte value."""
    cases = 0
    unit = rig.m.alloc(0x40)
    for _ in range(count):
        word = rng.getrandbits(32)
        rig.m.mu.mem_write(unit, bytes(rng.getrandbits(8) for _ in range(0x40)))
        pointer = rng.choice((unit, unit, 0))
        ident = unit_id(pointer, rig)
        index = rng.choice((0, 1, 0xFE, 0xFF, 0x100, 0x1FF, word & 0xFFFF, word, (rng.getrandbits(16) << 16) | rng.randrange(0xFF)))
        value = rng.choice(EDGES + (word,))
        fatal = (index & 0xFFFF) >= (0x100 if mut("fatal-bound-0x100") else 0xFF)
        queued, fatals, exited = rig.call(0x53_BEE0, [index, value], pointer)
        if fatal:
            check((queued, fatals, exited), ([], [(FILE_STR, 0x53_BEFE)], True), f"0x53bee0 fatal index={index:#x}")
        else:
            tier1 = 0xFE if mut("ushort-tier-0x100") else 0xFF
            tier2 = 0xFFFE if mut("ushort-tier-0xfffe") else 0xFFFF
            head = u(index, 1) + u(ident, 4)
            if value < tier1:
                want = u(0x9E, 1) + head + u(value, 1)
            elif value < tier2:
                want = u(0x9F, 1) + head + u(value, 2)
            else:
                want = u(0xA0, 1) + head + u(value, 4)
            check((queued, fatals, exited), ([(rig.client, want, len(want))], [], False), f"0x53bee0 v={value:#x}")
        cases += 1
        first = rand_word(rng)
        second = rng.choice((first, (first + rng.choice((0, 1, 0xFE, 0xFF, 0x100, 0xFFFE, 0xFFFF, 0x10000))) & 0xFFFFFFFF,
                             rand_word(rng)))
        queued, fatals, exited = rig.call(0x53_BFD0, [index, first, second], pointer)
        if fatal:
            check((queued, fatals, exited), ([], [(FILE_STR, 0x53_BFF0)], True), f"0x53bfd0 fatal index={index:#x}")
        else:
            d = (second - first) & 0xFFFFFFFF
            head = u(index, 1) + u(ident, 4)
            if d < 0xFF:
                want = u(0xA1, 1) + head + u(d, 1)
            elif d > 0xFFFE:
                want = u(0xA0, 1) + head + u(d if mut("delta-wide-uses-delta") else first, 4)
            else:
                want = u(0xA2, 1) + head + u(d, 2)
            check((queued, fatals, exited), ([(rig.client, want, len(want))], [], False), f"0x53bfd0 d={d:#x}")
        cases += 1
    return cases


def run_misc(rig, rng, count):
    """FUN_0053d2e0 (a null-able record), FUN_0053d4d0 (0x4d / 0x9a by a flag word)."""
    cases = 0
    for _ in range(count):
        record = rig.m.alloc(0x20)
        raw = bytes(rng.getrandbits(8) for _ in range(0x20))
        rig.m.mu.mem_write(record, raw)
        pointer = rng.choice((record, 0))
        if pointer:
            want = b"\x42" + raw[0:1] + raw[12:16]
        else:
            want = b"\x42" + (b"\0" if mut("d2e0-null-byte-zero") else b"\x06") + b"\xff\xff\xff\xff"
            if mut("d2e0-null-id-zero"):
                want = b"\x42\x06\0\0\0\0"
        queued, fatals, _ = rig.call(0x53_D2E0, [], pointer)
        check((queued, fatals), ([(rig.client, want, 6)], []), "0x53d2e0")
        cases += 1
        edx = rand_word(rng)
        stack = [rand_word(rng) for _ in range(7)]
        stack[6] = rng.choice((0, 1, 0x100, 0x80000000, rng.getrandbits(32)))
        queued, fatals, _ = rig.call(0x53_D4D0, stack, edx)
        flag = (stack[6] != 0)
        ident = 0x4D if flag == bool(mut("d4d0-flag-id")) else 0x9A
        want = (u(ident, 1) + u(edx, 1) + u(stack[0], 4) + u(stack[3], 4) + u(stack[5], 1) + u(stack[1], 2) + u(stack[2], 2)
                + u(stack[4], 2))
        check((queued, fatals), ([(rig.client, want, 0x11)], []), f"0x53d4d0 flag={stack[6]:#x}")
        cases += 1
    return cases


def run_bits(rig, rng, count):
    """FUN_0053c320 and 0053c3f0: LSB-first bit fields from the stock bit writer."""
    cases = 0
    for address, (nstack, ident, fields) in BITPACKED.items():
        for _ in range(count):
            edx = rand_word(rng)
            stack = [rand_word(rng) for _ in range(nstack)]
            values = [(ident, 8)]
            for source, bits in fields:
                values.append(((edx if source == "dx" else stack[int(source[1:])]), bits))
            want = bit_pack(values)
            queued, fatals, _ = rig.call(address, stack, edx)
            check((queued, fatals), ([(rig.client, want, len(want))], []), f"{address:#x} bits")
            cases += 1
    return cases


def run_queue(rng, count):
    """FUN_0053b280 unhooked: append, spill to a new block, take a block from the free list, null client."""
    world = mw.World()
    machine = world.m
    cases = 0
    for _ in range(count):
        client = machine.alloc(0x200)
        machine.mu.mem_write(client, bytes(rng.getrandbits(8) for _ in range(0x200)))
        tail_len = rng.choice((None, 0, 1, 0x100, 0x1F0, 0x1FF, 0x200, rng.randrange(0, 0x201)))
        size = rng.choice((1, 8, 0x15, 0x21, 0x100, 0x1FF, 0x200, 1 + rng.randrange(0x40)))
        free_block = rng.random() < 0.5
        if tail_len is None:
            head = tail = 0
            old = 0
        else:
            old = machine.alloc(BLOCK)
            machine.mu.mem_write(old, bytes([0x33]) * BLOCK)
            machine.w32(old, tail_len)
            machine.w32(old + NEXT, 0)
            head = tail = old
            if rng.random() < 0.4:          # a list of two blocks: the queue must work on the tail, not the head
                head = machine.alloc(BLOCK)
                machine.mu.mem_write(head, bytes([0x55]) * BLOCK)
                machine.w32(head, rng.randrange(0, 0x201))
                machine.w32(head + NEXT, old)
        head_before = machine.read(head, BLOCK) if head else b""
        recycled = 0
        free_next = 0
        if free_block:
            recycled = machine.alloc(BLOCK)
            free_next = machine.alloc(BLOCK)
            machine.mu.mem_write(recycled, bytes([0x77]) * BLOCK)
            machine.w32(recycled + NEXT, free_next)
        machine.w32(client + 0x1B8, head)
        machine.w32(client + 0x1BC, tail)
        machine.w32(client + 0x1C0, recycled)
        data = bytes(rng.getrandbits(8) for _ in range(size))
        pointer = machine.put(data)
        world.run(QUEUE, pointer, size, regs={"edi": client}, nstack=2)
        log = [entry for entry in world.log]
        limit = 0x1FF if mut("queue-limit-0x1ff") else (0x201 if mut("queue-limit-0x201") else 0x200)
        fits = tail_len is not None and tail_len + size <= limit
        if fits:
            block, offset = (head if mut("queue-appends-to-head") else tail), tail_len
            check(log, [], "append: no allocation")
            check((machine.r32(client + 0x1B8), machine.r32(client + 0x1BC), machine.r32(client + 0x1C0)),
                  (head, tail, recycled), "append: client lists untouched")
        else:
            if free_block and not mut("queue-reuses-freelist-ignored"):
                block, offset = recycled, 0
                check(log, [], "spill: free list block reused, no allocation")
                check(machine.r32(client + 0x1C0), free_next, "spill: free list head advanced")
            else:
                require(len(log) == 1 and log[0][0] == "alloc", f"spill: pool allocation expected, got {log}")
                _, pool, asked, name, line, flag = log[0]
                check((pool, asked, name, line, flag), (0, BLOCK, ".\\GAME\\SCmd.cpp", 0xA0, 0), "pool allocate arguments")
                block, offset = machine.r32(client + 0x1BC), 0
            check(machine.r32(client + 0x1BC), block, "spill: tail is the new block")
            check(machine.r32(client + 0x1B8), block if tail_len is None else head, "spill: head")
            if tail_len is not None:
                check(machine.r32(old + NEXT), 0 if mut("queue-no-link-to-old-tail") else block,
                      "spill: old tail linked to the new block")
            if size <= 0x200:
                check(machine.r32(block + NEXT), 0, "spill: next pointer cleared")
        if mut("queue-appends-at-zero"):
            offset = 0
        total = machine.r32(block)
        want_total = (tail_len + size) if fits else size
        if mut("queue-length-not-added") and fits:
            want_total = size
        check(total, want_total, "block length")
        check(machine.read(block + DATA + offset, size), data, "payload copied after the previous bytes")
        if head and head != old:
            check(machine.read(head, BLOCK), head_before, "two-block list: the head block is untouched")
        cases += 1
    # no size check: a packet over 0x200 bytes runs past the data area, over the next link, and past the block
    client = machine.alloc(0x200)
    machine.mu.mem_write(client, bytes(0x200))
    big = bytes(range(256)) * 3
    pointer = machine.put(big[:0x205])
    world.run(QUEUE, pointer, 0x205, regs={"edi": client}, nstack=2)
    block = machine.r32(client + 0x1BC)
    check(machine.r32(block), 0x205, "oversize: length is the size")
    check(machine.read(block + DATA, 0x205), big[:0x205], "oversize: all bytes copied, over the next link and past the block")
    check(machine.r32(block + NEXT), struct.unpack("<I", big[0x200:0x204])[0], "oversize: next link clobbered")
    cases += 1
    # a null client queues nothing and touches nothing
    pointer = machine.put(b"x" * 8)
    before_brk = machine.brk
    world.run(QUEUE, pointer, 8, regs={"edi": 0}, nstack=2)
    queued_for_null = [("alloc", 0, BLOCK, ".\\GAME\\SCmd.cpp", 0xA0, 0)] if mut("null-edi-still-copies") else []
    check((world.log, machine.brk), (queued_for_null, before_brk), "null client: nothing")
    return cases + 1


def run_all():
    """Run every group; returns the case count."""
    rng = random.Random(0x5C3D)
    rig = Rig()
    cases = run_fixed(rig, rng, 60)
    cases += run_copies(rig, rng, 20)
    cases += run_zero_packet(rig, rng)
    cases += run_difficulty(rig, rng, 40)
    cases += run_delta(rig, rng, 150)
    cases += run_unit_tiers(rig, rng, 150)
    cases += run_misc(rig, rng, 60)
    cases += run_bits(rig, rng, 80)
    cases += run_queue(rng, 150)
    return cases


if __name__ == "__main__":
    mw.drive(__doc__, sys.modules[__name__], run_all, "SCmd.cpp builder and queue cases match the stock code")
