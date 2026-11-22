# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's second UTF-8 reader and the bounded copy on it, as the oracle.

    uv run python utf8_copy.py gen 20000 5 > cases.tsv
    uv run python utf8_copy.py game cases.tsv > g.txt     # each case line + game.exe's answer
    ../../build/tests/test_utf8_copy g.txt | diff g.txt -
    uv run python utf8_copy.py gen 600 1 > f.tsv           # the fixture test_utf8_copy checks
    uv run python utf8_copy.py game f.tsv > fg.txt
    uv run python utf8_copy.py inc fg.txt > ../../tests/utf8_copy_cases.inc
    uv run python utf8_copy.py coverage cases.tsv          # instructions of the two bodies never reached

# gaps-runs: FUN_00526e40 FUN_006c7410

FUN_006c7410(out, capacity, text, length, &units, &taken) is stdcall (ret
0x18) and calls nothing. FUN_00526e40 is fastcall (ECX the output, EDX the
text, the capacity on the stack, ret 4); it calls FUN_006c7410 and the
stack cookie check FUN_00681a48, both run for real.

A case line is `id kind capacity bytes`, tab-separated, the bytes hex (`-`
for none):
- kind `r`: FUN_006c7410 over exactly those bytes with that capacity.
  Answers: the status, the bytes taken, the units written (a count), and
  those units as 4-hex-digit words (`-` for none).
- kind `c`: FUN_00526e40 on the bytes plus a NUL (a NUL among them ends
  the text, as strlen does). Answer: the output's units up to its first
  0, as 4-hex-digit words (`-` for none), searched over capacity + 1
  units: text of exactly `capacity` units puts its 0 there.

Domain: capacities 0..40 for `r`; 1..40, 0x400 and the text's own length
(and one either side) for `c`. Under 1, FUN_00526e40 stores its 0 before
the output for any text but "", which d2d doesn't model. The length
0x7fffffff (FUN_006c7410's "no end") isn't given: d2d takes a span.
d2d's side: d2d::tbl::read_utf8 / copy_utf8 (components/tbl/unicode.cpp).
"""
import random
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EIP

import emu

READ, COPY = 0x6C7410, 0x526E40
BODIES = {COPY: 0xE0, READ: 0x17C}  # to each one's last ret / jmp
PADDING = set(range(0x526EB8, 0x526EC0)) | set(range(0x6C745A, 0x6C7460))   # alignment after a jmp
NO_END = {0x6C742D, 0x6C7430}   # the length 0x7fffffff: outside the domain
GUARD = 0xA5A5          # each output unit before a call
OUT_UNITS = 0x500


def s32(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value >> 31 else value


def encode(code, length):
    """`code` in a `length`-byte sequence (overlong when it's longer than needed)."""
    if length == 1:
        return bytes([code & 0x7F])
    lead = (0xFF << (8 - length)) & 0xFF
    tail = [0x80 | (code >> (6 * at)) & 0x3F for at in range(length - 1)]
    return bytes([lead | (code >> (6 * (length - 1))) & (0x7F >> length)] + tail[::-1])


def piece(rng):
    kind = rng.random()
    if kind < 0.35:
        return bytes([rng.randrange(0x20, 0x7F)])
    if kind < 0.5:
        return chr(rng.choice((rng.randrange(0x80, 0x800), rng.randrange(0x800, 0x10000)))).encode("utf-8", "surrogatepass")
    if kind < 0.6:
        return chr(rng.randrange(0x10000, 0x110000)).encode("utf-8")
    if kind < 0.68:
        return encode(rng.randrange(0, 1 << 31), rng.randrange(1, 7))   # any code, any length: overlong, too big
    if kind < 0.72:
        return rng.choice((b"\xc0\x80", b"\xe0\x80\x80", b"\xf4\x90\x80\x80", b"\xed\xa0\x80"))
    if kind < 0.75:
        return b"\x00"
    if kind < 0.85:
        return bytes([rng.randrange(0x80, 0x100)])        # a stray continuation or a lone lead
    return bytes([rng.randrange(256)])


def text(rng):
    shape = rng.random()
    if shape < 0.05:
        return bytes(rng.randrange(0x20, 0x7F) for _ in range(rng.randrange(1000, 1100)))   # past the 0x3ff scratch
    if shape < 0.08:
        return chr(rng.randrange(0x10000, 0x110000)).encode() * rng.randrange(250, 260)      # pairs at its end
    data = b"".join(piece(rng) for _ in range(rng.randrange(0, 25)))
    return data[:-rng.randrange(1, 3)] if data and rng.random() < 0.15 else data             # a cut tail


def units_of(data):
    """FUN_00526e40's text as Python reads it, for picking capacities near its length."""
    return len(data.split(b"\x00")[0].decode("utf-8", "replace").encode("utf-16-le")) // 2


def gen(count, seed):
    rng = random.Random(seed)
    for case in range(count):
        data = text(rng)
        if rng.random() < 0.4:
            data = data.replace(b"\x00", b"") if rng.random() < 0.5 else data
            print(case, "r", rng.randrange(0, 41), data.hex() or "-", sep="\t")
            continue
        length = units_of(data)
        capacity = rng.choice((rng.randrange(1, 41), 0x400, max(1, length), length + 1, max(1, length - 1)))
        print(case, "c", capacity, data.hex() or "-", sep="\t")


def call(e, address, *args, limit, **regs):
    answer = e.call(address, *args, limit=limit, **regs)
    if e.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
        raise AssertionError(f"FUN_{address:08x} did not return")
    return answer


def words(data):
    return "".join(f"{data[at] | data[at + 1] << 8:04x}" for at in range(0, len(data), 2)) or "-"


class Game:
    def __init__(self, exe=None):
        self.e = emu.Emu(exe)
        self.text = self.e.alloc(0x1200)
        self.out = self.e.alloc(OUT_UNITS * 2 + 16)
        self.counts = self.e.alloc(8)

    def run(self, kind, capacity, data):
        e = self.e
        e.mu.mem_write(self.out, GUARD.to_bytes(2, "little") * OUT_UNITS)
        if kind == "r":
            e.mu.mem_write(self.text, data + b"\x5a" * 8)
            e.mu.mem_write(self.counts, b"\xff" * 8)
            status = s32(call(e, READ, self.out, capacity, self.text, len(data), self.counts, self.counts + 4, limit=200000))
            units = e.s32(self.counts)
            return [status, e.s32(self.counts + 4), units, words(e.read(self.out, units * 2))]
        e.mu.mem_write(self.text, data + b"\x00")
        call(e, COPY, capacity, ecx=self.out, edx=self.text, limit=400000)
        got = e.read(self.out, (capacity + 1) * 2)
        end = next((at for at in range(0, len(got), 2) if got[at] == 0 and got[at + 1] == 0), None)
        return [words(got[:end]) if end is not None else "no-0"]


def cases(path):
    for line in open(path):
        fields = line.split()
        if "|" in fields:
            fields = fields[:fields.index("|")]
        if fields:
            yield fields, fields[1], int(fields[2]), b"" if fields[3] == "-" else bytes.fromhex(fields[3])


def game(path, exe=None):
    g = Game(exe)
    for fields, kind, capacity, data in cases(path):
        print(*fields, "|", *g.run(kind, capacity, data), sep="\t")


def coverage(path, exe=None):
    g = Game(exe)
    seen = set()
    g.e.mu.hook_add(UC_HOOK_CODE, lambda mu, address, size, user: seen.add(address))
    for _fields, kind, capacity, data in cases(path):
        g.run(kind, capacity, data)
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    missed = 0
    for start, size in sorted(BODIES.items()):
        for ins in md.disasm(g.e.read(start, size), start):
            if ins.address not in seen and ins.address not in PADDING | NO_END:
                print(f"not reached: {ins.address:08x}  {ins.mnemonic} {ins.op_str}")
                missed += 1
    print(f"{missed} instructions of {len(BODIES)} bodies not reached (the no-end length aside)")
    return 1 if missed else 0


def inc(path):
    lines = [line.rstrip("\n") for line in open(path) if line.strip()]
    print(f"// Stock game.exe 1.14d's UTF-8 reader and copy; generated by tools/emu/utf8_copy.py (gen {len(lines)} 1, then game, then inc).")
    print("// Each line: the case, `|`, game.exe's answers (utf8_copy.py's docstring).")
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
    elif len(args) == 2 and args[0] == "coverage":
        sys.exit(coverage(args[1], exe))
    elif len(args) == 2 and args[0] == "inc":
        inc(args[1])
    else:
        sys.exit(__doc__)
