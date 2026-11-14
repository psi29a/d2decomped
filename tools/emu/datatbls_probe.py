# SPDX-License-Identifier: GPL-3.0-or-later
"""Check 22 DATATBLS entry paths in stock game.exe 1.14d.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py [hex-address]
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py --populated [hex-address]
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py --populated-check
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py --writeback
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py --writeback-fail
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py --readback-present
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py --readback-null
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py --strings
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py --strings-missing
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py --superuniques
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py --monseq
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_probe.py --matrix

The executable is hash-checked by _monster_world. CompileTxt and link creation
are hooked, so this reads no MPQ. Nineteen functions use zero rows, the monster
freer uses zeroed globals, the excel opener has its file call hooked, and the
readback path has its binary reader hooked. Populated rows and nonzero flags
need separate oracles.
`--populated` is a diagnostic pass with one zero-filled row and no assertions.
`--populated-check` asserts the stable one-row paths in POPULATED_RETURNS.
`--writeback` checks the compile-mode write and readback with file I/O hooked.
`--writeback-fail` checks the failed-open branch and unconditional free/readback.
`--readback-present` and `--readback-null` check both sides of the write gate.
`--strings` checks the two-row string-field loop with the resolver and copier hooked.
`--strings-missing` exercises the missing-key fallback with the same row shape.
`--superuniques` checks all 66 hcIdx slots and a duplicate using synthetic rows.
`--monseq` checks two sequence groups across three synthetic rows.
`--matrix` compares two-row ItemTypes and MonType is-a bit matrices.
`--schema-digests` prints canonical hashes of every CompileTxt column list.
"""
import hashlib
import json
import struct
import sys

import monster_loaders as loaders
from _monster_world import require

# address: (ordered (table name, row size) calls, last schema size, link line)
SPECS = {
    0x612600: ((), 0, None),
    0x612750: ((('compcode', 4),), 2, 0x73),
    0x613260: ((('charstats', 0xC4),), 72, None),
    0x613E90: ((), 0, None),
    0x61DE00: ((('leveldefs', 0x9C),), 43, None),
    0x61EBB0: ((('lvlprest', 0x1B0),), 24, None),
    0x61F500: ((('lvlsub', 0x15C),), 23, None),
    0x633730: ((('magicsuffix', 0x90), ('magicprefix', 0x90), ('automagic', 0x90)), 41, None),
    0x633F40: ((('raresuffix', 0x48), ('rareprefix', 0x48)), 14, None),
    0x6342B0: ((('uniqueitems', 0x14C),), 68, 0x37A),
    0x636B70: ((('gems', 0xC0),), 42, None),
    0x637A00: ((('itemstatcost', 0x144),), 52, 0x6B4),
    0x638630: ((('properties', 0x2E),), 30, 0x796),
    0x638AE0: ((('gamble', 0xC),), 2, None),
    0x638D80: ((('itemtypes', 0xE4),), 36, 0x884),
    0x639420: ((), 0, None),
    0x6552E0: ((('superuniques', 0x34),), 21, 0x744),
    0x656930: ((('monsounds', 0x94),), 41, 0x96F),
    0x658520: ((('montype', 0xC),), 7, 0xAF6),
    0x658720: ((('monprop', 0x138),), 92, 0xB89),
    0x659C60: ((('monseq', 6),), 6, 0xCC3),
    0x65A190: ((), 0, None),
}
STACK_ARGS = {0x613260: (0,), 0x61F500: (0,)}
# In the order FUN_0065a190 calls them.
MONSTER_FREES = (0x6562B0, 0x659AF0, 0x659DE0, 0x654D50, 0x653E20)
POPULATED_RETURNS = {
    0x612750: "table", 0x613260: "table", 0x61DE00: 0, 0x61EBB0: 1,
    0x61F500: "table+4", 0x633730: 0, 0x633F40: 0, 0x6342B0: 1,
    0x636B70: 0x1A8, 0x637A00: "sorted", 0x638630: "table", 0x638AE0: ("halt", 0x7E0),
    0x638D80: "matrix", 0x6552E0: ("halt", 0x782), 0x656930: "table",
    0x658520: 0, 0x658720: "table", 0x659C60: 0,
}
# SHA-256 of canonical JSON for each full CompileTxt column list, read from
# the stock 1.14d executable. Combined affix loaders use the same list each call.
SCHEMA_DIGESTS = {
    0x612750: "ed8902f1cb3a05d14e817cdaef352b02bfc97238fdf87316ccf684543d5531aa",
    0x613260: "66eabe1d77f97c82958ef30a1ae2b9d01e6f909cac63304b82eaf114bfe75b80",
    0x61DE00: "78bddd31763ce9bf6a24f6c3e2247a21625e208f1e6a2f9cbb500a8555f026ee",
    0x61EBB0: "7594ce1500f0693dba91ab0a97a0c8e012d1f44c676ce56a542e65d32b5551a0",
    0x61F500: "3090798d4ce59ccb1a0c38cc8a037bc6d48d23e8037259dcf96ad75b25bdcc1d",
    0x633730: "994c25c4f1fe03caa54da090546902b43181a66583edbe18af9d57333a580902",
    0x633F40: "33693cdb18650799f7d6bd3076385a51c704556a04a8b625bc329caeb4799c78",
    0x6342B0: "4ebadc6b6015bc24cb85ba3aef99dfd14c2ccdaba24964bbd76761163f54df3b",
    0x636B70: "a32c1ffc95de008b8c89962c3f7ac67596db2063a24b54e2d5ec3ea0dd1846f2",
    0x637A00: "a9c10dbbc3aad9283dbebfd798421ad292de35f3ef829f2f45585b930e5584fc",
    0x638630: "41670c2d183356f461d37def1de0d0b95183ae812ea6dfa4624557fd2ec12f9b",
    0x638AE0: "13e7190373d4051989a8e6d08de899057f78631a72fbfec99190fcd9de14bb08",
    0x638D80: "08073824f07b31acaf769a858cb042fa9c742c2a7d8b56dd7026099f188ff203",
    0x6552E0: "2392bf4802228f62488f8815137eb8b21d295349bdbd008c3280a7c335bd1f4c",
    0x656930: "5207c0e29fd919f0feee249bb5c7f7445f45e7faa7a24174bce2d421c3224d7d",
    0x658520: "3f009622e187faf58112d873b8243f16cdb43b27f29836eff90ba8f7d443553e",
    0x658720: "9bde5d8631532b1203ac4bae8d0ed14d266b243db5fae1c58a6a8975d1fa6abf",
    0x659C60: "471355918f8603a0a61f1c86df837dff7c8e8cdc4dc686e47122d7a62f024304",
}

# Direct CompileTxt destination slots (table pointer, row count, link pointer).
# Combined loaders and derived arrays have their own checks below or in the
# populated follow-up oracles.
DESTINATIONS = {
    0x612750: (0x96BCAC, 0x96BCB4, 0x96BCB0),
    0x613260: (0x96C7F4, 0x96C7F8, None),
    0x61DE00: (0x96C890, None, None),
    0x61EBB0: (0x96C894, 0x96C898, None),
    0x61F500: (0x96CA10, 0x96CA14, None),
    0x6342B0: (0x96C854, 0x96C858, 0x96C850),
    0x636B70: (0x96CA94, 0x96CA90, None),
    0x637A00: (0x96C7FC, 0x96C804, 0x96C800),
    0x638630: (0x96BCD4, 0x96BCDC, 0x96BCD8),
    0x638D80: (0x96C828, 0x96C82C, 0x96C824),
    0x6552E0: (0x96C704, 0x96C70C, 0x96C708),
    0x656930: (0x96C6B4, 0x96C6BC, 0x96C6B8),
    0x658520: (0x96C86C, 0x96C870, 0x96C868),
    0x658720: (0x96C860, 0x96C864, 0x96C85C),
    0x659C60: (0x96C7A8, 0x96C7B0, 0x96C7AC),
}


def cstring(rig, value):
    """Allocate a terminated string for a real game.exe call."""
    address = rig.m.alloc(len(value) + 1)
    rig.write(address, value + b"\0")
    return address


def schema_digest(fields):
    """Hash all field names, types, sizes, offsets and linker addresses."""
    encoded = json.dumps(fields, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def main():
    """Run each function independently and compare its scripted entry path."""
    check_populated = "--populated-check" in sys.argv[1:]
    populated = "--populated" in sys.argv[1:] or check_populated
    open_failure = "--writeback-fail" in sys.argv[1:]
    writeback = "--writeback" in sys.argv[1:] or open_failure
    readback_present = "--readback-present" in sys.argv[1:]
    readback_null = "--readback-null" in sys.argv[1:]
    missing_strings = "--strings-missing" in sys.argv[1:]
    strings = "--strings" in sys.argv[1:] or missing_strings
    superuniques = "--superuniques" in sys.argv[1:]
    monseq = "--monseq" in sys.argv[1:]
    matrix_check = "--matrix" in sys.argv[1:]
    schema_digests = "--schema-digests" in sys.argv[1:]
    addresses = [value for value in sys.argv[1:] if value not in
                 ("--populated", "--populated-check", "--writeback", "--strings", "--superuniques",
                  "--monseq", "--matrix", "--schema-digests", "--writeback-fail", "--strings-missing",
                  "--readback-present", "--readback-null")]
    selected = [int(value, 16) for value in addresses] if addresses else (
        [0x638D80, 0x658520] if matrix_check else [0x659C60] if monseq else
        [0x6552E0] if superuniques else [0x612600] if strings else
        [0x613E90] if writeback or readback_present or readback_null else
        POPULATED_RETURNS if check_populated else SPECS)
    for address in selected:
        tables, columns, link_line = SPECS[address]
        if populated and not tables:
            continue
        rig = loaders.Rig()
        if (populated or matrix_check) and address == 0x638D80:
            rig.logger(0x6BD230, 2, "link-fill", regs=())
        rig.rows = 2 if matrix_check else 3 if monseq else 67 if superuniques else 1 if populated else 0
        rig.table = rig.m.alloc(0x5000 if populated or superuniques or matrix_check else 0x200)
        rig.links = [rig.m.alloc(4) for _ in range(128 if populated else 8)]
        first_link = rig.links[0]
        rig.ids = [0] * 128 if populated else []
        if superuniques and address == 0x6552E0:
            for index in range(67):
                rig.m.w32(rig.table + 0x34 * index + 8, index if index < 66 else 0)
        if monseq and address == 0x659C60:
            rig.write(rig.table, struct.pack("<h4xh4xh4x", 0, 0, 1))
        if strings and address == 0x612600:
            rig.write(rig.table, bytes([0xA5]) * 0x100)
            def resolve(current, rig=rig):
                """Return a synthetic string for each record field."""
                target = current.mu.reg_read(loaders.mw.REGISTERS["ecx"])
                rig.log.append(("resolve", target))
                return 0 if missing_strings else rig.table + 0x180
            def copy_text(current, rig=rig):
                """Record destination, source and size without copying game data."""
                target = current.mu.reg_read(loaders.mw.REGISTERS["ecx"])
                source = current.mu.reg_read(loaders.mw.REGISTERS["edx"])
                rig.log.append(("copy", target, source, current.arg(0), rig.m.read(source, 16)))
                return target
            rig.m.hook(0x524E20, resolve, 0)
            rig.m.hook(0x526790, copy_text, 1)
        if populated and address == 0x636B70:
            items = rig.m.alloc(0x1A8 * 2)
            rig.m.w32(0x96CA5C, items)
            rig.m.w32(rig.table + 0x28, 1)
            rig.ids[0] = 0x1234
        if address == 0x65A190:
            rig.write(loaders.REGION, bytes(loaders.REGION_END - loaders.REGION))
            for freed in MONSTER_FREES:
                rig.logger(freed, 0, f"nested-{freed:08x}", regs=())
        if address == 0x639420:
            rig.logger(0x516E46, 2, "open", ret=0)
        if address == 0x613E90:
            def readback(current, rig=rig):
                """Return a scripted binary table and size to the caller."""
                size_pointer = current.arg(1)
                current.w32(size_pointer, 0x1234)
                rig.log.append(("readback", current.arg(0), size_pointer, current.arg(3)))
                return rig.table
            rig.m.hook(0x612280, readback, 4)
            if writeback or readback_present or readback_null:
                def fopen(current, rig=rig):
                    """Record the path and return a synthetic writable stream."""
                    rig.log.append(("fopen", current.cstr(current.arg(0)), current.cstr(current.arg(1))))
                    return 0 if open_failure or readback_present or readback_null else rig.table + 0x100
                def fwrite(current, rig=rig):
                    """Record the byte count and stream without touching the disk."""
                    rig.log.append(("fwrite", current.arg(0), current.arg(1), current.arg(2), current.arg(3)))
                    return current.arg(2)
                def fclose(current, rig=rig):
                    """Record the stream close without touching the disk."""
                    rig.log.append(("fclose", current.arg(0)))
                    return 0
                rig.m.hook(0x68239A, fopen, 0)
                rig.m.hook(0x684449, fwrite, 0)
                rig.m.hook(0x681FDC, fclose, 0)
        args = (0x40, 0, 0x10, 8) if strings and address == 0x612600 else (
            (16, 0, 0, 16) if address == 0x612600 else STACK_ARGS.get(address, ()))
        ecx = rig.table if address == 0x612600 else 0
        if address == 0x639420:
            args = (cstring(rig, b"demo"),)
            ecx = cstring(rig, b".bin")
        edx = 0
        if address == 0x613E90:
            name = cstring(rig, b"demo")
            outputs = [rig.m.alloc(4) for _ in range(3)]
            for output in outputs:
                rig.m.w32(output, 0)
            if writeback or readback_present:
                rig.m.w32(outputs[0], rig.table)
                rig.m.w32(outputs[1], 0x20)
            rig.m.w32(0x96C8B4, int(writeback or readback_null))
            args, edx = tuple(outputs), name
        if strings and address == 0x612600:
            edx = 2
        try:
            result = rig.go(address, *args, ecx=ecx, edx=edx)
        except Exception as error:
            if not populated:
                raise
            print(f"{address:08x}: one row: {type(error).__name__}: {error}")
            continue
        if address in DESTINATIONS:
            table_slot, count_slot, link_slot = DESTINATIONS[address]
            require(rig.m.r32(table_slot) == rig.table,
                    f"{address:08x}: table destination {table_slot:#x} differs")
            if count_slot is not None:
                require(rig.m.r32(count_slot) == rig.rows,
                        f"{address:08x}: count destination {count_slot:#x} differs")
            if link_slot is not None:
                require(rig.m.r32(link_slot) == first_link,
                        f"{address:08x}: link destination {link_slot:#x} differs")
        if populated:
            calls = [entry for entry in rig.log if entry[0] in ("compile", "link", "unload", "link-free")]
            if check_populated:
                expected = POPULATED_RETURNS[address]
                if expected == "table":
                    expected = rig.table
                elif expected == "table+4":
                    expected = rig.table + 4
                elif expected == "sorted":
                    expected = rig.m.r32(0x96C808)
                elif expected == "matrix":
                    expected = rig.m.r32(0x96C834)
                expected_result = expected if isinstance(expected, tuple) else ("ret", expected)
                observed = tuple((entry[2], entry[4]) for entry in calls if entry[0] == "compile")
                links = [entry[2] for entry in calls if entry[0] == "link"]
                require(result == expected_result and observed == tables and len(rig.fields) == columns and
                        links == ([link_line] if link_line is not None else []),
                        f"{address:08x}: one-row result {result}, tables {observed}, columns {len(rig.fields)}, links {links}")
                digests = [schema_digest(fields) for fields in rig.field_history]
                require(digests == [SCHEMA_DIGESTS[address]] * len(tables),
                        f"{address:08x}: full field schemas differ: {digests}")
                if address == 0x636B70:
                    require((rig.m.r32(items + 0xF0), rig.m.r32(items + 0x1A8 + 0xF0),
                             rig.m.r32(rig.table + 0x2C) & 0xFFFF) == (0xFFFFFFFF, 0, 0x1234),
                            f"{address:08x}: gem-to-item assignment differs")
                if address == 0x637A00:
                    allocations = [entry for entry in rig.log if entry[0] == "alloc"]
                    require(rig.m.r32(0x96C80C) == 0 and allocations ==
                            [("alloc", 0, 0, ".\\DATATBLS\\ItemTbls.cpp", 0x748, 0)],
                            f"{address:08x}: sorted index allocation {allocations}")
                if address == 0x638D80:
                    matrix = rig.m.r32(0x96C834)
                    fills = [entry for entry in rig.log if entry[0] == "link-fill"]
                    require(rig.m.r32(0x96C830) == 1 and rig.m.r32(matrix) == 1 and len(fills) == 1,
                            f"{address:08x}: one-row type matrix {matrix:#x} / {fills}")
                if address == 0x658520:
                    matrix = rig.m.r32(0x96C874)
                    require(rig.m.r32(0x96C878) == 1 and rig.m.r32(matrix) == 0,
                            f"{address:08x}: one-row monster type matrix stride={rig.m.r32(0x96C878)} bits={rig.m.r32(matrix)}")
                if address == 0x659C60:
                    sequence_index = rig.m.r32(0x96C7B4)
                    require(rig.m.r32(0x96C7B8) == 1 and
                            (rig.m.r32(sequence_index), rig.m.r32(sequence_index + 4),
                             rig.m.r32(sequence_index + 8)) == (rig.table, 1, 1),
                            f"{address:08x}: one-row sequence index differs")
                print(f"ok: {address:08x} one scripted row")
                continue
            print(f"{address:08x}: one row: {result}; calls={calls}")
            if address == 0x637A00:
                allocations = [entry for entry in rig.log if entry[0] == "alloc"]
                print(f"{address:08x}: allocations={allocations}, sorted-count={rig.m.r32(0x96C80C)}")
            continue
        calls = [entry for entry in rig.log if entry[0] == "compile"]
        observed = tuple((entry[2], entry[4]) for entry in calls)
        links = [entry[2] for entry in rig.log if entry[0] == "link"]
        expected = ("halt", 0x782) if address == 0x6552E0 and not superuniques else ("ret",)
        require(observed == tables, f"{address:08x}: compile calls {observed} != {tables}")
        require(len(rig.fields) == columns, f"{address:08x}: {len(rig.fields)} columns != {columns}")
        if tables:
            digests = [schema_digest(fields) for fields in rig.field_history]
            require(digests == [SCHEMA_DIGESTS[address]] * len(tables),
                    f"{address:08x}: full field schemas differ: {digests}")
        require(links == ([link_line] if link_line is not None else []),
                f"{address:08x}: link lines {links} != {link_line}")
        require(result == expected if len(expected) == 2 else result[0] == expected[0],
                f"{address:08x}: result {result} != {expected}")
        if superuniques and address == 0x6552E0:
            raw_slots = rig.m.read(0x96C710, 132)
            require(len(raw_slots) == 132, f"{address:08x}: short hcIdx index")
            slots = struct.unpack("<66h", raw_slots)
            require(rig.m.r32(0x96C70C) == 67 and slots == tuple(range(66)),
                    f"{address:08x}: hcIdx first-row index differs")
        if monseq and address == 0x659C60:
            index = rig.m.r32(0x96C7B4)
            entries = tuple(rig.m.r32(index + 4 * offset) for offset in range(6))
            require(rig.m.r32(0x96C7B8) == 2 and entries ==
                    (rig.table, 2, 2, rig.table + 12, 1, 1),
                    f"{address:08x}: grouped MonSeq index {entries}")
        if matrix_check:
            matrix_address, stride_address = ((0x96C834, 0x96C830) if address == 0x638D80 else
                                              (0x96C874, 0x96C878))
            matrix = rig.m.r32(matrix_address)
            words = (rig.m.r32(matrix), rig.m.r32(matrix + 4))
            expected_words = (1, 3) if address == 0x638D80 else (0, 2)
            require(rig.m.r32(stride_address) == 1 and words == expected_words,
                    f"{address:08x}: two-row type matrix {words} != {expected_words}")
        if schema_digests and tables:
            print(f"{address:08x}: {','.join(schema_digest(fields) for fields in rig.field_history)}")
        if address == 0x65A190:
            nested = tuple(int(entry[0].split("-")[1], 16) for entry in rig.log if entry[0].startswith("nested-"))
            require(nested == MONSTER_FREES, f"{address:08x}: nested free order {nested}")
            require([entry for entry in rig.log if entry[0] == "unload"] == [("unload", 0)],
                    f"{address:08x}: unconditional npc table unload {rig.log}")
        if strings and address == 0x612600:
            resolver = [entry for entry in rig.log if entry[0] == "resolve"]
            copies = [entry for entry in rig.log if entry[0] == "copy"]
            require(len(resolver) == 2 and
                    [(entry[1], entry[3]) for entry in copies] ==
                    [(rig.table + 0x10, 8), (rig.table + 0x50, 8)],
                    f"{address:08x}: field loop calls {rig.log}")
            require(all(entry[2] != 0 and (missing_strings or entry[2] == rig.table + 0x180)
                        for entry in copies),
                    f"{address:08x}: string source pointers {copies}")
            if missing_strings:
                require(all(entry[4].startswith(b"<\0>\0\0\0") for entry in copies),
                        f"{address:08x}: missing-key fallback strings {copies}")
            require(rig.m.read(rig.table + 0x10, 8) == bytes(8) and
                    rig.m.read(rig.table + 0x50, 8) == bytes(8) and
                    rig.m.read(rig.table, 0x10) == bytes([0xA5]) * 0x10,
                    f"{address:08x}: field clearing differs")
        if address == 0x639420:
            opened = [entry for entry in rig.log if entry[0] == "open"]
            require(len(opened) == 1, f"{address:08x}: open calls {opened}")
            path = rig.m.cstr(opened[0][2])
            require(path == "DATA\\GLOBAL\\EXCEL\\demo.bin" and opened[0][4] == 1,
                    f"{address:08x}: open path {path!r} / mode {opened[0][4]}")
        if address == 0x613E90:
            values = tuple(rig.m.r32(output) for output in outputs)
            require(values == (rig.table, 0x1234, 0x1234), f"{address:08x}: readback outputs {values}; log {rig.log}")
            reads = [entry for entry in rig.log if entry[0] == "readback"]
            require(len(reads) == 1 and reads[0][1] == 0x6DCC68 and reads[0][3] == 0x27A,
                    f"{address:08x}: readback call {reads}")
            if writeback:
                want = [("fopen", "DATA\\GLOBAL\\EXCEL\\demo.bin", "wb")]
                if not open_failure:
                    want += [("fwrite", rig.table, 0x20, 1, rig.table + 0x100),
                             ("fclose", rig.table + 0x100)]
                want += [("free", 0, rig.table, ".\\DATATBLS\\DataTbls.cpp", 0x275, 0), reads[0]]
                require(rig.log == want, f"{address:08x}: writeback calls {rig.log} != {want}")
            elif readback_present or readback_null:
                require(rig.log == [reads[0]],
                        f"{address:08x}: write gate should skip file calls: {rig.log}")
        branch = {0x613E90: "binary readback", 0x639420: "excel file open",
                  0x65A190: "zero-state unload"}.get(address, "zero rows")
        if address == 0x613E90 and writeback:
            branch = "failed-open writeback and readback" if open_failure else "writeback and readback"
        if address == 0x613E90 and (readback_present or readback_null):
            branch = "readback with write gate closed"
        if strings and address == 0x612600:
            branch = "two missing string fields" if missing_strings else "two string fields"
        if superuniques and address == 0x6552E0:
            branch = "66 hcIdx slots and duplicate"
        if monseq and address == 0x659C60:
            branch = "three rows in two sequences"
        if matrix_check:
            branch = "two-row type matrix"
        names = ", ".join(name for name, _ in tables)
        print(f"ok: {address:08x} {names} {branch}".strip())


if __name__ == "__main__":
    main()
