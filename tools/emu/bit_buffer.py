# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's bit buffer and the save checksum, as the oracle.

    uv run python bit_buffer.py gen 20000 5 > cases.tsv
    uv run python bit_buffer.py game cases.tsv > g.txt     # each case line + game.exe's answer
    ../../build/tests/test_bit_buffer g.txt | diff g.txt -
    uv run python bit_buffer.py gen 600 1 > f.tsv           # the fixture test_bit_buffer checks
    uv run python bit_buffer.py game f.tsv > fg.txt
    uv run python bit_buffer.py inc fg.txt > ../../tests/bit_buffer_cases.inc
    uv run python bit_buffer.py model                       # FUN_00411170, FUN_004111a0 against the models below
    uv run python bit_buffer.py coverage cases.tsv          # instructions of the bodies never reached

# gaps-runs: FUN_00410e40 FUN_00410e90 FUN_00410eb0 FUN_00410f60 FUN_00411020 FUN_00411030 FUN_00411070 FUN_00411130 FUN_00411170 FUN_004111a0

Every function here is stdcall with its arguments on the stack; none calls
another but FUN_00411020 (a jmp to FUN_00410f60), FUN_00411030 (calls
it) and FUN_00410e40's null-reader assert (FUN_00408090, FUN_00408a60,
FUN_00681e09), which no case reaches.

A case line is `id kind size bytes ops...`, tab-separated:
- kind `r`: a reader (FUN_00410e40) over `size` bytes (hex, `-` for none),
  then ops `R,n` (FUN_00411020 on even ids, FUN_00410f60 on odd: n bits),
  `S,n` (FUN_00411030: n bits, signed) and `U` (FUN_00410e90, the bytes
  used). Answers: each R / S / U's value.
- kind `w`: a writer over those bytes (the stale contents game.exe leaves
  or zeroes), ops `W,value,n` (FUN_00410eb0), `A` (FUN_00411070) and `U`.
  Answers: each U's value.
- kind `c`: FUN_00411130 over the bytes, the save checksum; the gen keeps
  bytes 0xc..0xf zero, as its callers do before summing (d2d's
  save_checksum zeroes them itself).
`game` appends `|`, the answers, and for r / w the cursor (word +8 the
byte, +0xc the bit, +0x10 the flag), the pointer less the buffer (word
+0) and the bytes after; for c the sum.

Domain: sizes 0..63, counts -3..40 and the 32-bit ends. A writer's
count of 0x7fffffff comes only first (at bit 0): later, with bits used,
the sum wraps negative and game.exe writes on past the buffer. A write of
0 bits or less with every bit used stores a zero one byte past the
buffer (0x410ef3); the answer shows only the buffer's own bytes, and d2d
skips that store.
d2d's side: d2d::net::d2gs::BitReader / BitWriter (components/net/d2gs/
bit_buffer.cpp) and d2d::d2s::save_checksum (tests/test_bit_buffer.cpp).
"""
import random
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EIP

import emu

BODIES = {0x410E40: 73, 0x410E90: 22, 0x410EB0: 173, 0x410F60: 180, 0x411020: 9, 0x411030: 57, 0x411070: 49,
          0x411130: 50, 0x411170: 40, 0x4111A0: 104}
ASSERT_PATH = range(0x410E4C, 0x410E6B)   # FUN_00410e40 with a null reader: never called that way
INT_MIN, INT_MAX = -(1 << 31), (1 << 31) - 1
GUARD = 16                                 # bytes after the buffer, to catch a store past it


def s32(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value >> 31 else value


def gen(count, seed):
    rng = random.Random(seed)

    def width():
        kind = rng.random()
        return rng.randrange(1, 33) if kind < 0.75 else rng.randrange(-3, 1) if kind < 0.85 else rng.randrange(33, 41) if kind < 0.95 else rng.choice((INT_MIN, INT_MAX))

    def value():
        kind = rng.random()
        return rng.randrange(INT_MIN, INT_MAX + 1) if kind < 0.6 else rng.choice((0, -1, 1, INT_MIN, INT_MAX)) if kind < 0.8 else rng.randrange(-300, 300)

    for case in range(count):
        kind = rng.choice("rrrwwwc")
        if kind == "c":
            size = rng.choice((0, 1, 0xC, 0x10, 0x14F, rng.randrange(0, 0x40), rng.randrange(0x14F, 0x600)))
            data = bytearray(rng.randrange(256) for _ in range(size))
            data[0xC:0x10] = bytes(len(data[0xC:0x10]))
            print(case, kind, size, data.hex() or "-", sep="\t")
            continue
        size = rng.choice((0, 1, 2, 3, 4, 5, 8, rng.randrange(0, 21), rng.randrange(8, 64), rng.randrange(8, 64)))
        data = bytes(rng.randrange(256) for _ in range(size))
        ops = []
        for at in range(rng.randrange(1, 14)):
            pick = rng.random()
            if pick < 0.12:
                ops.append("U")
            elif kind == "r":
                ops.append(f"{'S' if pick < 0.4 else 'R'},{width()}")
            elif pick < 0.22:
                ops.append("A")
            else:
                n = width()
                if n == INT_MAX and at:
                    n = rng.randrange(1, 33)
                ops.append(f"W,{value()},{n}")
        print(case, kind, size, data.hex() or "-", *ops, sep="\t")


def call(e, address, *args, limit=10000):
    """One call, which must return (not stop at the instruction limit)."""
    answer = e.call(address, *args, limit=limit)
    if e.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
        raise AssertionError(f"FUN_{address:08x}{args} did not return")
    return answer


class Game:
    def __init__(self, exe=None):
        self.e = emu.Emu(exe)
        self.state = self.e.alloc(0x14)
        self.buf = self.e.alloc(0x600 + GUARD)

    def run(self, case, kind, size, data, ops):
        e = self.e
        e.mu.mem_write(self.buf, data + b"\xa5" * GUARD)
        if kind == "c":
            return [s32(call(e, 0x411130, self.buf, size, limit=100000))]
        call(e, 0x410E40, self.state, self.buf, size, limit=100)
        answers = []
        for op in ops:
            name, *args = op.split(",")
            args = [int(arg) & 0xFFFFFFFF for arg in args]
            if name == "U":
                answers.append(s32(call(e, 0x410E90, self.state, limit=100)))
            elif name == "R":
                answers.append(call(e, 0x411020 if case % 2 == 0 else 0x410F60, self.state, args[0], limit=10000))
            elif name == "S":
                answers.append(s32(call(e, 0x411030, self.state, args[0], limit=10000)))
            elif name == "W":
                call(e, 0x410EB0, self.state, args[0], args[1], limit=10000)
            elif name == "A":
                call(e, 0x411070, self.state, limit=100)
        after = e.read(self.buf, size)
        pointer = s32(e.r32(self.state) - self.buf)
        return answers + [e.s32(self.state + 8), e.s32(self.state + 12), e.s32(self.state + 16), pointer, after.hex() or "-"]


def cases(path):
    """Each case of a gen file (or of a game file: the part before `|`)."""
    for line in open(path):
        fields = line.split()
        if "|" in fields:
            fields = fields[:fields.index("|")]
        if fields:
            data = b"" if fields[3] == "-" else bytes.fromhex(fields[3])
            yield fields, int(fields[0]), fields[1], int(fields[2]), data, fields[4:]


def game(path, exe=None):
    g = Game(exe)
    for fields, case, kind, size, data, ops in cases(path):
        print(*fields, "|", *g.run(case, kind, size, data, ops), sep="\t")


def model(exe=None):
    """FUN_00411170 (the set bits over the bytes, table 0x6ce398) and FUN_004111a0
    (the lowest set bit, table 0x6ce798; -1 for 0) against Python's own; d2d has
    no caller of either."""
    e = emu.Emu(exe)
    buf = e.alloc(0x400)
    rng = random.Random(7)
    checked = 0
    runs = [bytes(range(256))] + [bytes(rng.randrange(256) for _ in range(rng.randrange(0, 0x300))) for _ in range(300)] + [bytes([byte]) for byte in range(256)]
    for data in runs:
        e.mu.mem_write(buf, data)
        got = call(e, 0x411170, buf, len(data), limit=100000)
        want = sum(bin(byte).count("1") for byte in data)
        if got != want:
            print(f"FUN_00411170 over {data.hex()}: got {got}, want {want}")
            return 1
        checked += 1
    values = [0] + [byte << shift for byte in range(1, 256) for shift in (0, 8, 16, 24)] + [rng.randrange(1 << 32) for _ in range(5000)]
    for number in values:
        got = s32(call(e, 0x4111A0, number, limit=100))
        want = (number & -number).bit_length() - 1
        if got != want:
            print(f"FUN_004111a0({number:#x}): got {got}, want {want}")
            return 1
        checked += 1
    print(f"ok: {len(runs)} FUN_00411170 runs, {len(values)} FUN_004111a0 values, {checked} in all")
    return 0


def coverage(path, exe=None):
    g = Game(exe)
    seen = set()
    g.e.mu.hook_add(UC_HOOK_CODE, lambda mu, address, size, user: seen.add(address))
    for _fields, case, kind, size, data, ops in cases(path):
        g.run(case, kind, size, data, ops)
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    missed = 0
    for start, size in sorted(BODIES.items()):
        if start in (0x411170, 0x4111A0):
            continue                       # run by `model`, not by the cases
        for ins in md.disasm(g.e.read(start, size), start):
            if ins.address not in seen and ins.address not in ASSERT_PATH and ins.mnemonic not in ("int3", "nop"):
                print(f"not reached: {ins.address:08x}  {ins.mnemonic} {ins.op_str}")
                missed += 1
    print(f"{missed} instructions of {len(BODIES) - 2} bodies not reached (the null-reader assert aside)")
    return 1 if missed else 0


def inc(path):
    lines = [line.rstrip("\n") for line in open(path) if line.strip()]
    print(f"// Stock game.exe 1.14d's bit buffer and save checksum; generated by tools/emu/bit_buffer.py (gen {len(lines)} 1, then game, then inc).")
    print("// Each line: the case, `|`, game.exe's answers (bit_buffer.py's docstring).")
    for line in lines:
        print(f'    "{line.replace(chr(9), " ")}",')


if __name__ == "__main__":
    args = sys.argv[1:]
    exe = None
    if "--exe" in args:
        at = args.index("--exe")
        exe = args[at + 1]
        del args[at:at + 2]
    if len(args) == 3 and args[0] == "gen":
        gen(int(args[1]), int(args[2]))
    elif len(args) == 2 and args[0] == "game":
        game(args[1], exe)
    elif len(args) == 1 and args[0] == "model":
        sys.exit(model(exe))
    elif len(args) == 2 and args[0] == "coverage":
        sys.exit(coverage(args[1], exe))
    elif len(args) == 2 and args[0] == "inc":
        inc(args[1])
    else:
        sys.exit(__doc__)
