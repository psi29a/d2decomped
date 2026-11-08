# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's excel name-link table and txt splitter (Src/Excel/Excel.cpp, 8 functions).

    uv run python excel_links.py

A "link" is the table that turns a txt cell's name into a row number. A link
(16 bytes) is +0 record count, +4 capacity, +8 array pointer, +0xc name tree
root. A tree node (0x2c bytes) is +0 name (0x20, lower case), +0x20 row,
+0x24 left, +0x28 right. An array element (8 bytes) is {code, row}. A link
uses the tree or the array, never both. The functions, all run here from
game.exe with the allocator and the error handler hooked:

* 0x6bd500 add a name (stdcall link, text; halts 0x4b2 null, 0x4b6 when the
  link has an array): 0x2c node, the text cut to 31 characters and lower-cased,
  row = the record count, which then goes up by 1. 0x6bd490 (EAX = slot, EDX
  = node; not fastcall) puts the node in the tree by an unsigned byte compare
  and **frees the node when the name is already there, yet 0x6bd500 still
  returns a new row and counts it**.
* 0x6bd5a0 (fastcall ECX link, EDX text; halts 0x4cc, 0x4d0) looks the name up
  with 0x6bd3c0 (stdcall link, name, x; the same cut and lower-casing; -1 when
  absent; halts 0x475 for a count without a tree) and adds it only when absent.
* 0x6bd230 (stdcall link, code; halts 0x3d1 null, 0x3d8 when the link has a
  tree) keeps the array sorted by code: capacity grows by 0x40 (realloc line
  0x3df, the new 0x200 bytes cleared), a code already there is bumped up by one
  until it is free, the new element's row is the record count.
* 0x6bd070 (fastcall node) frees left, right, then the node (line 0x346);
  0x6bd0b0 (stdcall link; null ok) frees the tree the same way, the array
  (0x35d) and the link (0x360).
* 0x6bcda0 (stdcall buffer struct) frees the data (client free, line 0x9a)
  and the struct (0x9b). 0x6bd640 (stdcall data, size) splits a txt buffer in
  place into a 16-byte struct (+0 data, +4 first data row, +8 row count, +0xc
  column count): tabs and CRLFs become NULs, a "\\r" without "\\n" halts (0x5c in
  the header, 0x75 after), more than 0x118 columns halts 0x67, a header without
  a data row returns 0 (and frees), and a row whose first cell is "Expansion"
  is cut out of the buffer (the count goes down by one, so an unterminated last
  "Expansion" row, never counted, takes it to 0xffffffff). A last row with no
  CRLF is not counted.
No MPQs are read.
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FILE_NAME = 0x6FC448
ALLOC, FREE, FREE_CLIENT, REALLOC, FATAL = 0x40B430, 0x40B480, 0x40B3C0, 0x40B4B0, 0x408A60
FREE_FIELD, FREE_NODE, FREE_LINK, ADD_CODE = 0x6BCDA0, 0x6BD070, 0x6BD0B0, 0x6BD230
LOOKUP, INSERT, ADD_NAME, ADD_OR_FIND, SPLIT = 0x6BD3C0, 0x6BD490, 0x6BD500, 0x6BD5A0, 0x6BD640
LIMIT = 200000


class Halt(Exception):
    """game.exe's unrecoverable-error call, with the source line it passed."""


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


class Harness:
    """game.exe with a logging allocator, a raising error handler and link helpers."""

    def __init__(self, machine):
        """Hook the allocator, client free, realloc and the error handler."""
        self.machine = machine
        self.allocs, self.frees, self.client_frees, self.reallocs = [], [], [], []
        self.sizes = {}
        machine.hook(ALLOC, self.alloc, 3)
        machine.hook(FREE, self.free, 3)
        machine.hook(FREE_CLIENT, self.free_client, 2)
        machine.hook(REALLOC, self.realloc, 4)
        machine.hook(FATAL, self.fatal, 0)

    def alloc(self, current):
        """Log (size, file, line, flag) and return a block poisoned with 0xab."""
        size = current.mu.reg_read(UC_X86_REG_EDX)
        self.allocs.append((size, current.arg(0), current.arg(1), current.arg(2)))
        block = current.alloc(size)
        current.mu.mem_write(block, b"\xab" * size)
        self.sizes[block] = size
        return block

    def free(self, current):
        """Log (block, file, line, flag)."""
        self.frees.append((current.mu.reg_read(UC_X86_REG_EDX), current.arg(0), current.arg(1), current.arg(2)))
        return 0

    def free_client(self, current):
        """Log (block, file, line, flag) of a client-memory free (block in ECX, file in EDX)."""
        self.client_frees.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                                  current.arg(0), current.arg(1)))
        return 1

    def realloc(self, current):
        """Log (old, size, file, line); return a new block (old bytes copied, rest poisoned)."""
        old = current.mu.reg_read(UC_X86_REG_EDX)
        size = current.arg(0)
        self.reallocs.append((old, size, current.arg(1), current.arg(2), current.arg(3)))
        block = current.alloc(size)
        current.mu.mem_write(block, b"\xab" * size)
        if old:
            keep = min(size, self.sizes[old])
            current.mu.mem_write(block, current.read(old, keep))
        self.sizes[block] = size
        return block

    def fatal(self, current):
        """Stop at the unrecoverable-error call: arg 2 is the line."""
        raise Halt(current.arg(2))

    def call(self, address, *args, **kwargs):
        """Run a function, check it returned to the stub and clear the logs first."""
        for log in (self.allocs, self.frees, self.client_frees, self.reallocs):
            log.clear()
        result = self.machine.call(address, *args, limit=LIMIT, **kwargs)
        require(self.machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x}: stopped at the cap")
        return result

    def halts(self, line, address, *args, **kwargs):
        """Run a function that must reach the error handler with this line."""
        try:
            self.call(address, *args, **kwargs)
        except Halt as halt:
            require(halt.args[0] == line, f"{address:#x}: halt {halt.args[0]:#x}, expected {line:#x}")
            return
        raise RuntimeError(f"{address:#x}: no halt, expected {line:#x}")

    def link(self, count=0, capacity=0, array=0, root=0):
        """A link in memory."""
        link = self.machine.alloc(16)
        for offset, value in ((0, count), (4, capacity), (8, array), (12, root)):
            self.machine.w32(link + offset, value)
        return link

    def tree(self, link):
        """The tree's (name, row) in order, with each node's address; checks it ends."""
        machine = self.machine
        found = []

        def walk(node, depth):
            if not node:
                return
            require(depth < 64 and len(found) < 4096, "tree does not end")
            walk(machine.r32(node + 0x24), depth + 1)
            found.append((machine.cstr(node), machine.r32(node + 0x20), node))
            walk(machine.r32(node + 0x28), depth + 1)

        walk(machine.r32(link + 12), 0)
        return found


def model_name(text):
    """What 0x4135d0 (cut to 31 characters) and 0x4113c0 (lower case, whatever Ghidra calls it) make of a name."""
    return text.encode()[:31].lower()


def model_split(data, size):
    """0x6bd640 on the first size bytes of data: (status, rows, columns, first row offset, bytes) or a halt line."""
    buf = bytearray(data) + bytearray(64)    # emu.put pads with zeros
    end = size
    cols = 0
    pos = 0
    if pos >= end:
        return ("fail", buf[:size])
    while True:
        char = buf[pos]
        pos += 1
        if char == 9:
            buf[pos - 1] = 0
            cols += 1
        elif char == 0xD:
            buf[pos - 1] = 0
            if buf[pos] != 0xA:
                return ("halt", 0x5C)
            cols += 1
            pos += 1
            if pos >= end:
                return ("fail", buf[:size])
            if cols > 0x118:
                return ("halt", 0x67)
            break
        if pos >= end:
            return ("fail", buf[:size])
    first, rows = pos, 0
    line = pos
    while True:
        while pos < end:
            char = buf[pos]
            pos += 1
            if char == 9:
                buf[pos - 1] = 0
            elif char == 0xD:
                buf[pos - 1] = 0
                if buf[pos] != 0xA:
                    return ("halt", 0x75)
                pos += 1
                rows += 1
                break
        cell = bytes(buf[line:line + 10])
        if cell[:10] == b"Expansion\0":
            rows -= 1
            buf[line:line + (end - pos)] = bytes(buf[pos:end])
            end += line - pos
            pos = line
        line = pos
        if pos >= end:
            return ("ok", buf[:size], rows & 0xFFFFFFFF, cols, first)


def main():
    """Run every function on links and buffers built in emulated memory and compare with a model."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    require(machine.cstr(FILE_NAME) == ".\\Src\\Excel\\Excel.cpp", "0x6fc448 is not .\\Src\\Excel\\Excel.cpp")
    harness = Harness(machine)
    rng = random.Random(28)
    cases = 0
    alphabet = "abcXYZ_09 "

    def word():
        """A random name, sometimes longer than the 31 characters a node keeps."""
        return "".join(rng.choice(alphabet) for _ in range(rng.choice((0, 1, 2, 3, 5, 31, 32, 45)))) or "a"

    # add a name: row = count, node 0x2c, cut and lower-cased; a repeat is freed but still counted
    link = harness.link()
    seen = {}
    names = [word() for _ in range(60)]
    names += [names[3], names[3].upper(), names[10]]
    for text in names:
        index = machine.r32(link)
        got = harness.call(ADD_NAME, link, machine.put(text.encode() + b"\0"))
        require(got == index, f"add-name row {got} vs {index}")
        require(machine.r32(link) == index + 1, "add-name count")
        require(harness.allocs[0] == (0x2C, FILE_NAME, 0x4B9, 0), f"add-name alloc {harness.allocs}")
        key = model_name(text)
        if key in seen:
            require(len(harness.frees) == 1 and harness.frees[0][1:] == (FILE_NAME, 0x4A7, 0), "repeat frees the node")
        else:
            require(not harness.frees, "new name frees nothing")
            seen[key] = index
        cases += 1
    nodes = harness.tree(link)
    got_tree = [(n, r) for n, r, _ in nodes]
    want_tree = sorted(((k.decode(), v) for k, v in seen.items()), key=lambda e: e[0].encode())
    require(got_tree == want_tree, f"tree holds the first row of each name, in byte order: {[(g, w) for g, w in zip(got_tree, want_tree) if g != w][:3]} {len(got_tree)} {len(want_tree)}")
    harness.halts(0x4B2, ADD_NAME, 0, machine.put(b"x\0"))
    harness.halts(0x4B6, ADD_NAME, harness.link(array=machine.alloc(8)), machine.put(b"x\0"))
    cases += 2

    # look up / add-or-find
    for text in names[:20] + ["nothere", "NOTHERE2"]:
        key = model_name(text)
        got = harness.call(LOOKUP, link, machine.put(text.encode() + b"\0"), 0)
        want = seen.get(key, 0xFFFFFFFF)
        require(got == want, f"lookup {text!r}: {got:#x} vs {want:#x}")
        before = machine.r32(link)
        got = harness.call(ADD_OR_FIND, ecx=link, edx=machine.put(text.encode() + b"\0"))
        if key in seen:
            require(got == seen[key] and machine.r32(link) == before and not harness.allocs, "find leaves the link alone")
        else:
            require(got == before and machine.r32(link) == before + 1, "add-or-find adds an absent name")
            seen[key] = before
            require(harness.allocs[0] == (0x2C, FILE_NAME, 0x4D9, 0), f"add-or-find alloc {harness.allocs}")
        cases += 1
    require(harness.call(LOOKUP, harness.link(), machine.put(b"x\0"), 0) == 0xFFFFFFFF, "lookup in an empty link")
    harness.halts(0x475, LOOKUP, harness.link(count=1), machine.put(b"x\0"), 0)
    harness.halts(0x4CC, ADD_OR_FIND, ecx=0, edx=machine.put(b"x\0"))
    harness.halts(0x4D0, ADD_OR_FIND, ecx=harness.link(array=machine.alloc(8)), edx=machine.put(b"x\0"))
    cases += 4

    # code array: sorted, grows by 0x40, a taken code goes up by one
    link = harness.link()
    model = {}
    for _ in range(200):
        code = rng.choice((rng.randrange(1 << 32), rng.randrange(40), 0xFFFFFFFE, 0x20202020 + rng.randrange(8)))
        index = machine.r32(link)
        capacity = machine.r32(link + 4)
        want_code = code
        while want_code in model:
            want_code = (want_code + 1) & 0xFFFFFFFF
        got = harness.call(ADD_CODE, link, code)
        require(got == index, f"add-code row {got} vs {index}")
        if capacity <= index:
            require(len(harness.reallocs) == 1 and harness.reallocs[0][1:] == ((capacity + 0x40) * 8, FILE_NAME, 0x3DF, 0),
                    f"add-code grow {harness.reallocs}")
            require(machine.r32(link + 4) == capacity + 0x40, "add-code capacity")
        else:
            require(not harness.reallocs, "add-code grew early")
        model[want_code] = index
        array = machine.r32(link + 8)
        rows = [(machine.r32(array + 8 * i), machine.r32(array + 8 * i + 4)) for i in range(machine.r32(link))]
        require(rows == sorted(model.items()), f"add-code array after {code:#x}")
        cases += 1
    require(len(set(model.values())) == 200, "rows are distinct")
    require(machine.read(machine.r32(link + 8) + 200 * 8, 0x40) == b"\0" * 0x40, "new array space is cleared")
    harness.halts(0x3D1, ADD_CODE, 0, 1)
    harness.halts(0x3D8, ADD_CODE, harness.link(root=machine.alloc(0x2C)), 1)
    cases += 2

    # frees: node, link
    def build(shape):
        """A tree node from (left, right) tuples or None; returns the address."""
        if shape is None:
            return 0
        node = machine.alloc(0x2C)
        machine.w32(node + 0x24, build(shape[0]))
        machine.w32(node + 0x28, build(shape[1]))
        return node

    def addresses(node):
        """Left, right, then the node: the free order the code claims."""
        if not node:
            return []
        return addresses(machine.r32(node + 0x24)) + addresses(machine.r32(node + 0x28)) + [node]

    shapes = [None, (None, None), ((None, None), None), (None, (None, (None, None))), (((None, None), (None, None)), (None, None))]
    for shape in shapes:
        root = build(shape)
        if root:
            harness.call(FREE_NODE, ecx=root)
            require([f[0] for f in harness.frees] == addresses(root), f"free-node order {harness.frees}")
            require(all(f[1:] == (FILE_NAME, 0x346, 0) for f in harness.frees), "free-node file / line")
            cases += 1
        for array in (0, machine.alloc(8)):
            root = build(shape)
            link = harness.link(root=root, array=array)
            harness.call(FREE_LINK, link)
            want = [(a, 0x346) for a in addresses(root)] + ([(array, 0x35D)] if array else []) + [(link, 0x360)]
            require([(f[0], f[2]) for f in harness.frees] == want, f"free-link {harness.frees} vs {want}")
            cases += 1
    harness.call(FREE_LINK, 0)
    require(not harness.frees, "free-link of null")
    struct = machine.alloc(16)
    data = machine.alloc(8)
    machine.w32(struct, data)
    harness.call(FREE_FIELD, struct)
    require(harness.client_frees == [(data, FILE_NAME, 0x9A, 0)] and harness.frees == [(struct, FILE_NAME, 0x9B, 0)],
            f"free-field {harness.client_frees} {harness.frees}")
    cases += 2

    # the txt splitter against a model, on hand-made and random buffers
    def run_split(raw):
        """Run 0x6bd640 on raw bytes; returns the emulator's view of the result."""
        data = machine.put(raw)
        try:
            result = harness.call(SPLIT, data, len(raw))
        except Halt as halt:
            return ("halt", halt.args[0])
        if result == 0:
            require(harness.client_frees == [(data, FILE_NAME, 0x9A, 0)] and len(harness.frees) == 1
                    and harness.frees[0][1:] == (FILE_NAME, 0x9B, 0), "split failure frees both")
            return ("fail", bytearray(machine.read(data, len(raw))))
        require(harness.allocs[0] == (0x10, FILE_NAME, 0x4D, 0), f"split alloc {harness.allocs}")
        require(machine.r32(result) == data and machine.r32(result + 4) != 0, "split struct")
        return ("ok", bytearray(machine.read(data, len(raw))), machine.r32(result + 8), machine.r32(result + 0xC),
                machine.r32(result + 4) - data)

    samples = [
        b"a\tb\tc\r\n1\t2\t3\r\n4\t5\t6\r\n",
        b"a\tb\r\nExpansion\t\t\r\n1\t2\r\n",
        b"a\tb\r\n1\t2\r\nExpansion\r\n3\t4\r\n",
        b"a\tb\r\n1\t2\r\n3\t4",
        b"a\tb\r\n1\t2\r\nExpansion",
        b"a\tb\r\nexpansion\t1\r\nExpansionX\t2\r\nExpansion\r\n",
        b"a\tb\r\n",
        b"a\tb",
        b"",
        b"a\rb\r\n",
        b"a\tb\r\n1\r2\r\n",
        b"\r\n1\r\n",
        b"\t".join(b"c" for _ in range(0x118)) + b"\r\n1\r\n",
        b"\t".join(b"c" for _ in range(0x119)) + b"\r\n1\r\n",
    ]
    soup = [b"a", b"\t", b"\r\n", b"Expansion", b"\t", b"b", b"\r\n", b"\r"]
    for _ in range(600):
        samples.append(b"".join(rng.choice(soup) for _ in range(rng.randint(1, 14))))
    for raw in samples:
        got = run_split(raw)
        want = model_split(raw, len(raw))
        if want[0] == "halt" or got[0] == "halt":
            require(got == want, f"split {raw!r}: {got} vs {want}")
        else:
            require(got[0] == want[0], f"split {raw!r}: {got[0]} vs {want[0]}")
            if want[0] == "ok":
                require(got[1:] == (want[1], want[2], want[3], want[4]), f"split {raw!r}: {got[1:]} vs {want[1:]}")
            else:
                require(got[1] == want[1], f"split {raw!r} buffer")
        cases += 1
    print(f"{cases} cases: add name / lookup / add-or-find / add code / free node, link, field / split; "
          "a repeated name is freed but counted, an unterminated last row is not counted")
    print("ok")


if __name__ == "__main__":
    main()
