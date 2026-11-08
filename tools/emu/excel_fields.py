# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the rest of game.exe 1.14d's Excel/Excel.cpp: reverse links, code search, bin field map.

    uv run python excel_fields.py

excel_links.py covers the eight functions that build a link and split a txt
buffer; this covers the six that read one (a link is +0 record count, +4
capacity, +8 array of {code, row}, +0xc name tree; a tree node is +0 name,
+0x20 row, +0x24 left, +0x28 right):

* 0x6bd040 (stdcall file, line): allocate a link (16 bytes, pool 0, flag 0)
  and zero its four fields.
* 0x6bd130 (stdcall link, code, unused): binary search of the code array by
  unsigned compare; returns the row or -1. Run on sorted arrays (matches a
  dictionary) and on unsorted ones (matches the same search).
* 0x6bd180 (stdcall link, row, out): the reverse: a linear scan of the array
  for a row; writes the four code characters, a space becoming NUL, then a
  NUL; returns 1, or 0 with `out` untouched (no array, row outside 0..count,
  not found).
* 0x6bd330 (fastcall node, row): the tree node holding a row, searched
  node first, then the whole left subtree, then the whole right; the "not
  found" value is the pointer stored at 0x74c3d8.
* 0x6bd380 (stdcall link, row, flag): that, after a range check on the row;
  anything absent is the 0x74c3d8 pointer, and the third argument changes
  nothing.
* 0x6bce20 (stdcall out, excel, fields): maps txt columns to the 20-byte
  field table of a bin loader. Fields hash by name (0x413ce0, case-blind);
  for each column, in order, the field with the same name (a later field
  first when two share a name; used once) gives its index, else 0x7fff; then
  every unused field follows in table order. Returns the number of entries.
  More than 0x118 fields halts 0xcc. A type-5 (bool) field of size above 1
  starts a run: each next entry must be type 5 (else halt 0xc4), and the run
  ends when the count reaches the size of the entry just read.
No MPQs are read. The error handler and allocator are hooked as in
excel_links.py.
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

import emu
from excel_links import FILE_NAME, GAME_SHA256, Halt, Harness, require

CREATE, CLASS_ID, RECORD, FIELD, INT_FIELD, BIN_FIELD = 0x6BD040, 0x6BD130, 0x6BD180, 0x6BD330, 0x6BD380, 0x6BCE20
SENTINEL_VAR, HASH = 0x74C3D8, 0x413CE0


def model_class_id(codes, target):
    """0x6bd130: the binary search as game.exe runs it, over [(code, row)] in array order."""
    low, high = 0, len(codes)
    while low < high:
        mid = (high - low) // 2 + low
        code = codes[mid][0]
        if code < target:
            low = mid + 1
            mid = high
        elif code == target:
            return codes[mid][1]
        high = mid
    return 0xFFFFFFFF


def model_record(raw, count, row):
    """0x6bd180: (found, 5 output bytes) for a link whose array is raw (8 bytes an element)."""
    if raw is None or not 0 <= row < count:
        return (False, None)
    for i in range(count):
        if struct.unpack_from("<I", raw, 8 * i + 4)[0] == row:
            chars = bytes(0 if c == 0x20 else c for c in raw[8 * i:8 * i + 4])
            return (True, chars + b"\0")
    return (False, None)


def main():
    """Build links, trees and field tables in emulated memory and compare with the models."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    require(machine.cstr(FILE_NAME) == ".\\Src\\Excel\\Excel.cpp", "0x6fc448 is not .\\Src\\Excel\\Excel.cpp")
    harness = Harness(machine)
    rng = random.Random(1140)
    cases = 0

    # 0x6bd040: a zeroed 16-byte link from the pool-0 allocator
    for line in (0x123, 0, 0xFFFF):
        link = harness.call(CREATE, FILE_NAME, line)
        require(harness.allocs == [(0x10, FILE_NAME, line, 0)], f"create link allocation {harness.allocs}")
        require(machine.read(link, 16) == b"\0" * 16, "created link is not zeroed")
        cases += 1

    # 0x6bd130: the code search, on sorted arrays and on arbitrary ones
    def code_link(pairs):
        """A link whose array holds the (code, row) pairs in this order."""
        array = machine.alloc(max(8 * len(pairs), 8))
        for i, (code, row) in enumerate(pairs):
            machine.w32(array + 8 * i, code)
            machine.w32(array + 8 * i + 4, row)
        return harness.link(count=len(pairs), capacity=len(pairs), array=array)

    for size in (0, 1, 2, 3, 7, 40, 129):
        for sorted_array in (True, False):
            codes = rng.sample(range(0, 1 << 32, 3), size)
            if sorted_array:
                codes.sort()
            pairs = [(code, rng.randrange(1000)) for code in codes]
            link = code_link(pairs)
            probes = [c for c, _ in pairs] + [0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF] + \
                [rng.randrange(1 << 32) for _ in range(20)]
            for target in probes:
                want = model_class_id(pairs, target)
                got = harness.call(CLASS_ID, link, target, 0x55AA)
                require(got == want, f"class id {target:#x} in {size}/{sorted_array}: {got:#x}, model {want:#x}")
                if sorted_array:
                    require(want == dict(pairs).get(target, 0xFFFFFFFF), "sorted search is not a dictionary lookup")
                cases += 1
    require(harness.call(CLASS_ID, code_link([]), 0x61626364, 0) == 0xFFFFFFFF, "empty array")
    negative = harness.link(count=0xFFFFFFFF, array=machine.alloc(16))     # a negative count searches nothing
    require(harness.call(CLASS_ID, negative, 1, 0) == 0xFFFFFFFF, "negative count")
    cases += 2

    # 0x6bd180: row -> code text
    alphabet = b"abcXYZ_09 ?"
    for size in (1, 2, 5, 33):
        raw = bytearray()
        rows = rng.sample(range(0, 300), size)
        if size > 2:
            rows[1] = rows[0]                       # a repeated row: the first one is found
        for row in rows:
            raw += bytes(rng.choice(alphabet) for _ in range(4)) + struct.pack("<I", row)
        array = machine.alloc(len(raw))
        machine.mu.mem_write(array, bytes(raw))
        link = harness.link(count=size, capacity=size, array=array)
        for row in rows + [-1 & 0xFFFFFFFF, size, 300, 0x7FFFFFFF, 0x80000000]:
            signed = row - (1 << 32) if row >> 31 else row
            out = machine.alloc(16)
            machine.mu.mem_write(out, b"\xee" * 16)
            got = harness.call(RECORD, link, row, out)
            found, text = model_record(bytes(raw), size, signed)
            require(got == int(found), f"record {row:#x}: {got}, model {found}")
            want = text + b"\xee" * 11 if found else b"\xee" * 16
            require(machine.read(out, 16) == want, f"record {row:#x} output {machine.read(out, 16)!r}")
            cases += 1
    nothing = harness.link(count=3)                  # no array: nothing, whatever the row
    out = machine.alloc(8)
    machine.mu.mem_write(out, b"\xee" * 8)
    require(harness.call(RECORD, nothing, 1, out) == 0 and machine.read(out, 8) == b"\xee" * 8, "record without array")
    cases += 1

    # 0x6bd330 and 0x6bd380: the tree search by row
    sentinel = machine.alloc(16)
    machine.w32(SENTINEL_VAR, sentinel)

    def build(depth):
        """A random tree; returns (node, [(node, row)] in preorder)."""
        if depth == 0 or rng.random() < 0.2:
            return 0, []
        node = machine.alloc(0x2C)
        row = rng.randrange(16)
        machine.w32(node + 0x20, row)
        left, lorder = build(depth - 1)
        right, rorder = build(depth - 1)
        machine.w32(node + 0x24, left)
        machine.w32(node + 0x28, right)
        return node, [(node, row)] + lorder + rorder

    for _ in range(60):
        root, order = build(5)
        link = harness.link(count=12, root=root)
        for row in range(-2, 15):
            first = next((n for n, r in order if r == row), sentinel)
            require(harness.call(FIELD, ecx=root, edx=row & 0xFFFFFFFF) == first,
                    f"tree search row {row}: wrong node")
            want = first if 0 <= row < 12 else sentinel
            for flag in (0, 1, 7):
                require(harness.call(INT_FIELD, link, row & 0xFFFFFFFF, flag) == want,
                        f"link search row {row} flag {flag}")
                cases += 1
            cases += 1
    require(harness.call(FIELD, ecx=0, edx=3) == sentinel, "null node")
    require(harness.call(INT_FIELD, 0, 0, 0) == sentinel and harness.call(INT_FIELD, 0, 0, 1) == sentinel,
            "null link")
    machine.w32(SENTINEL_VAR, 0x6CCB30)              # the value it starts with
    require(harness.call(FIELD, ecx=0, edx=3) == 0x6CCB30, "initial sentinel")
    cases += 3

    # 0x6bce20: the bin field map
    def name_hash(text):
        """game.exe's own 0x413ce0 (case-blind), used to order a bucket as the code does."""
        return harness.call(HASH, machine.put(text.encode() + b"\0"), 0, 0)

    def run_map(columns, fields):
        """Run the map; fields is [(name, type, size)]. Returns the output list or a halt line."""
        names = b"".join(c.encode() + b"\0" for c in columns)
        archive = machine.alloc(len(names) + 8)
        machine.mu.mem_write(archive, names)
        excel = machine.alloc(16)
        machine.w32(excel, archive)
        machine.w32(excel + 0xC, len(columns))
        table = machine.alloc(20 * (len(fields) + 1))
        for i, (name, kind, size) in enumerate(fields):
            machine.w32(table + 20 * i, machine.put(name.encode() + b"\0"))
            machine.w32(table + 20 * i + 4, kind)
            machine.w32(table + 20 * i + 8, size)
        out = machine.alloc(2 * (len(columns) + len(fields) + 4))
        machine.mu.mem_write(out, b"\xee" * (2 * (len(columns) + len(fields) + 4)))
        try:
            count = harness.call(BIN_FIELD, out, excel, table)
        except Halt as halt:
            return ("halt", halt.args[0])
        return count, list(struct.unpack(f"<{count}H", machine.read(out, 2 * count)))

    def model_map(columns, fields):
        """The mapping the claim describes."""
        i = 0
        while i < len(fields):                       # bool runs: sized by each entry in turn, then skipped
            if fields[i][1] == 5 and fields[i][2] > 1:
                seen, cur = 1, i
                while True:
                    cur += 1
                    if cur >= len(fields) or fields[cur][1] != 5:
                        return ("halt", 0xC4)
                    seen += 1
                    if seen >= fields[cur][2]:
                        break
                i = cur
            i += 1
        if len(fields) > 0x118:
            return ("halt", 0xCC)
        used = [False] * len(fields)
        by_hash = {}
        for i, (name, _, _) in enumerate(fields):
            by_hash.setdefault(name_hash(name), []).append(i)
        result = []
        for column in columns:
            hit = None
            for i in reversed(by_hash.get(name_hash(column), [])):      # a later field is found first
                if not used[i] and fields[i][0].lower() == column.lower():
                    hit = i
                    break
            if hit is None:
                result.append(0x7FFF)
            else:
                used[hit] = True
                result.append(hit)
        result += [i for i in range(len(fields)) if not used[i]]
        return len(result), result

    letters = ["str", "dex", "Vit", "ENERGY", "life", "Mana", "level", "name", "code", "type", "rarity"]
    for _ in range(80):
        names = rng.sample(letters, rng.randrange(1, len(letters) + 1))
        fields = [(n if rng.random() < 0.7 else n.upper(), 1 + rng.randrange(4), 4) for n in names]
        if rng.random() < 0.3:
            fields.append((rng.choice(letters), 2, 4))      # a repeated name
        columns = [c for c in rng.sample(letters + ["extra", "other"], rng.randrange(0, 9))]
        if rng.random() < 0.3 and columns:
            columns.append(columns[0].upper())              # a repeated column
        require(run_map(columns, fields) == model_map(columns, fields), f"bin map {columns} {fields}")
        cases += 1
    # the field limit, and bool runs
    many = [(f"f{i}", 1, 4) for i in range(0x118)]
    require(run_map([], many) == model_map([], many) and run_map([], many)[0] == 0x118, "0x118 fields")
    require(run_map([], many + [("f0x118", 1, 4)]) == ("halt", 0xCC), "0x119 fields")
    for fields, line in (([("a", 5, 3), ("b", 5, 3), ("c", 5, 3)], None),
                         ([("a", 5, 3), ("b", 5, 3), ("c", 1, 3)], 0xC4),
                         ([("a", 5, 2)], 0xC4), ([("a", 5, 1), ("b", 1, 4)], None),
                         ([("a", 1, 2), ("b", 5, 4)], 0xC4)):
        got = run_map(["b"], fields)
        want = ("halt", line) if line else model_map(["b"], fields)
        require(got == want, f"bool run {fields}: {got}, model {want}")
        cases += 1

    print(f"ok: {cases} excel link read, code search, tree search and bin field map cases")


if __name__ == "__main__":
    main()
