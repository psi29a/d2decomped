# SPDX-License-Identifier: GPL-3.0-or-later
"""A unit's melee hit records (unit +0xac, SUnitDmg.cpp), by game.exe's own four list functions.

    uv run python hit_records.py gen 20000 1 > cases.tsv
    uv run python hit_records.py game cases.tsv > g.txt      # each case line + game.exe's answer
    ../../build/tests/test_hit_records g.txt | diff g.txt -
    uv run python hit_records.py coverage cases.tsv          # instructions of the four bodies never reached
    uv run python hit_records.py gen 1500 2 > f.tsv           # the fixture test_hit_records checks
    uv run python hit_records.py game f.tsv > fg.txt
    uv run python hit_records.py inc fg.txt > ../../tests/hit_records_cases.inc

# gaps-runs: FUN_0057ca80 FUN_0057d690 FUN_0057c9f0 FUN_0057c980

Four units (0..3; -1 a null unit), each a 0x100-byte block with its type at
+0, its id at +0xc and its list's head at +0xac; unit u starts as type u % 2,
id u, its list empty. A case line is `id ops...`,
tab-separated, each op comma-separated:
- `t,u,type,id`: unit u's type and id change (so a node can name its owner
  as it was; FUN_0057c980 and FUN_0057d690 read the owner's current ones).
- `a,o,t,tag`: FUN_0057ca80 (ECX a context, its +0x1c the pool; EBX the owner,
  EAX the target, the record on the stack, RET 4) with a 0x70-byte record
  whose dword k is `tag * 64 + k`; neither is null (a null target faults
  reading its type, 0x57ca9f; a null owner reading its type once the target
  passes, 0x57cac5).
- `f,o,t`: FUN_0057d690 (ECX owner, EDX target): the tag of the record found
  (its dword 0 / 64), or `-` for none.
- `d,o,t`: FUN_0057c9f0 (ECX the context, EDX owner, target on the stack, RET 4).
- `s,o`: FUN_0057c980 (ECX the context, EDX owner); o is never null (a null
  owner faults reading +0xac, 0x57c987).
`game` appends `|`, each f's answer, `|`, and each unit's list from its head:
`owner type.owner id.target type.target id.tag` per node. Real: the four, and
the null-safe readers FUN_0044be50 / FUN_00451f50. Hooked: the pool's alloc
FUN_0040b430 (EDX 0x88 asserted; a block filled with 0xcc, freed ones handed
out again) and free FUN_0040b480 (EDX the node; one not in use stops the run),
both RET 0xc. A node's +0 is checked to be the context and its record its
tag's (all 28 dwords copied); a list reaching a freed node stops the run.
d2d's side: d2d::rules::HitRecords (components/rules/combat/hit_records.hpp),
tests/test_hit_records.cpp; a null unit there is the harness's (find none,
drop nothing), as d2d's callers always have both units.
"""
import random
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EDX

import emu

ADD, FIND, DROP, DROP_SELF = 0x57CA80, 0x57D690, 0x57C9F0, 0x57C980
BODIES = {ADD: 126, DROP_SELF: 111, DROP: 132, FIND: 108}
PADDING = range(0x57C99A, 0x57C9A0)   # FUN_0057c980's alignment after its jmp at 0x57c998: never run
ALLOC, FREE = 0x40B430, 0x40B480
UNITS = 4
TYPES = (0, 1, 2, 3, 4, 5)
IDS = (0, 1, 2, 7, 0x7FFFFFFF)


def gen(count, seed):
    rng = random.Random(seed)
    for case in range(count):
        ops, tag, added = [], 0, []
        unit = lambda: rng.randrange(UNITS) if rng.random() < 0.95 else -1
        for _ in range(rng.randint(1, 24)):
            kind = rng.choices("tafds", weights=(2, 6, 5, 3, 2))[0]
            if kind == "t":   # half the time back to its start, so an older node names the owner again
                index = rng.randrange(UNITS)
                ops.append(f"t,{index},{index % 2},{index}" if rng.random() < 0.5 else f"t,{index},{rng.choice(TYPES)},{rng.choice(IDS)}")
            elif kind == "a":
                tag += 1
                pair = rng.choice(added) if added and rng.random() < 0.3 else (rng.randrange(UNITS), rng.randrange(UNITS))
                added.append(pair)
                ops.append(f"a,{pair[0]},{pair[1]},{tag}")
            elif kind in "fd":
                pair = rng.choice(added) if added and rng.random() < 0.6 else (unit(), unit())
                ops.append(f"{kind},{pair[0]},{pair[1]}")
            else:
                ops.append(f"s,{rng.choice(added)[0] if added and rng.random() < 0.6 else rng.randrange(UNITS)}")
        print(case, *ops, sep="\t")


class Game:
    def __init__(self, exe=None):
        e = self.e = emu.Emu(exe)
        self.context = e.alloc(0x40)
        e.w32(self.context + 0x1C, 0x5EED)                     # the pool the alloc / free are handed
        self.units = [e.alloc(0x100) for _ in range(UNITS)]
        self.record = e.alloc(0x70)
        self.live, self.spare = set(), []                   # nodes in use; freed ones, handed out again
        e.hook(ALLOC, self._alloc, 3)
        e.hook(FREE, self._free, 3)

    def _alloc(self, e):
        size = e.mu.reg_read(UC_X86_REG_EDX)
        if size != 0x88:
            sys.exit(f"FUN_0040b430 asked for {size:#x}, not 0x88")
        node = self.spare.pop() if self.spare else e.alloc(0x88)
        e.mu.mem_write(node, bytes([0xCC]) * 0x88)       # no field left over from its last use
        self.live.add(node)
        return node

    def _free(self, e):
        node = e.mu.reg_read(UC_X86_REG_EDX)
        if node not in self.live:
            sys.exit(f"FUN_0040b480 freed {node:#x}, not a live node")
        self.live.discard(node)
        self.spare.append(node)
        return 1

    def ptr(self, index):
        return 0 if index < 0 else self.units[index]

    def run(self, ops):
        e = self.e
        for index, unit in enumerate(self.units):
            e.w32(unit, index % 2); e.w32(unit + 0xC, index); e.w32(unit + 0xAC, 0)
        self.spare += self.live
        self.live.clear()
        record = self.record
        found = []
        for op in ops:
            kind, *args = op.split(",")
            a = [int(x) for x in args]
            if kind == "t":
                e.w32(self.units[a[0]], a[1]); e.w32(self.units[a[0]] + 0xC, a[2])
            elif kind == "a":
                for k in range(28):
                    e.w32(record + 4 * k, a[2] * 64 + k)
                e.call(ADD, record, ecx=self.context, regs={"ebx": self.ptr(a[0]), "eax": self.ptr(a[1])})
            elif kind == "f":
                got = e.call(FIND, ecx=self.ptr(a[0]), edx=self.ptr(a[1]))
                found.append("-" if got == 0 else str(e.r32(got) // 64))
            elif kind == "d":
                e.call(DROP, self.ptr(a[1]), ecx=self.context, edx=self.ptr(a[0]))
            elif kind == "s":
                e.call(DROP_SELF, ecx=self.context, edx=self.ptr(a[0]))
            else:
                sys.exit(f"unknown op {op!r}")
        lists = []
        for unit in self.units:
            nodes, node, seen = [], e.r32(unit + 0xAC), set()
            while node:
                if node in seen or node not in self.live:
                    sys.exit(f"unit list reaches {node:#x}: a cycle or a freed node")
                seen.add(node)
                words = [e.s32(node + 4 * k) for k in range(0x22)]
                if words[0] != self.context:
                    sys.exit(f"node {node:#x} +0 is {words[0]:#x}, not the context")
                tag = words[5] // 64
                if words[5:33] != [tag * 64 + k for k in range(28)]:
                    sys.exit(f"node {node:#x}'s record isn't tag {tag}'s copy")
                nodes.append(f"{words[1]}.{words[2]}.{words[3]}.{words[4]}.{tag}")
                node = e.r32(node + 0x84)
            lists.append(",".join(nodes) or "-")
        return " ".join(found) or "-", " ".join(lists)


def cases(path):
    with open(path, newline="") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line:
                continue
            fields = line.split("\t")
            if len(fields) < 2 or not fields[0].isdigit():
                sys.exit(f"malformed case in {path}: {line!r}")
            yield line, fields[1:]


def game(path, exe=None):
    g = Game(exe)
    for line, ops in cases(path):
        found, lists = g.run(ops)
        print(f"{line}|{found}|{lists}")


def coverage(path, exe=None):
    g = Game(exe)
    seen = set()
    g.e.mu.hook_add(UC_HOOK_CODE, lambda mu, address, size, user: seen.add(address))
    for _line, ops in cases(path):
        g.run(ops)
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    missed = 0
    for start, size in sorted(BODIES.items()):
        for ins in md.disasm(g.e.read(start, size), start):
            if ins.address not in seen and ins.address not in PADDING and ins.mnemonic not in ("int3", "nop"):
                missed += 1
                print(f"not reached: {ins.address:08x} {ins.mnemonic} {ins.op_str}")
    print(f"{missed} instructions of {len(BODIES)} bodies not reached (the alignment at 0x57c99a aside)")


def inc(path):
    print("// Generated by tools/emu/hit_records.py inc: each case line and game.exe's answer.")
    with open(path, newline="") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if line:
                print('"' + line.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t") + '",')


def main():
    args = sys.argv[1:]
    exe = None
    if "--exe" in args:
        at = args.index("--exe")
        exe = args[at + 1]
        del args[at:at + 2]
    if args[:1] == ["gen"] and len(args) == 3:
        gen(int(args[1]), int(args[2]))
    elif args[:1] == ["game"] and len(args) == 2:
        game(args[1], exe)
    elif args[:1] == ["coverage"] and len(args) == 2:
        coverage(args[1], exe)
    elif args[:1] == ["inc"] and len(args) == 2:
        inc(args[1])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
