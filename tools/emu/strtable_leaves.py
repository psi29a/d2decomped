# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's string table leaves (StrTable/strtable.cpp) against line-for-line models.

    uv run python strtable_leaves.py                  # prints `ok: N ... cases`
    uv run python strtable_leaves.py --mutation-check  # each deliberate model defect must fail

Runs the stock game.exe's own code in Unicorn. The allocator (0x40_b380),
the client free (0x40_b3c0), the fatal handler (0x40_8a60) and the registry
imports are hooked and logged; the string-by-id lookup (0x52_4930) and the
patch-string lookup (0x52_4d30) are hooked as callees. Each claimed function
then runs for real and its result, written memory, callee log and halt line
are compared with a Python model of the same code:

* 0x524fd0 set a CRC-16 word to 0xffff; 0x524fe0 one CRC-16 byte step (table
  0x7301d8); 0x525020 CRC-16 over bytes [start, end) (halt 0x3ea on an empty range).
* 0x524e20 returns the string pointer that 0x52_4d30 writes through its second argument.
* 0x525090 replaces a language code in place with the row's code (ENG when unknown);
  0x525100 returns the row id of a code (0 when unknown); 0x525260 copies one
  field of the row picked by an id (modes 1..4).
* 0x5251c0 reads HKCU\\Control Panel\\International\\sLanguage and returns the row id.
* 0x524c60 finds a key in a TBL hash table (linear probing, 0xffff when absent,
  halt 0x296 for an empty pointer slot).
* 0x524ac0 decodes a key into a cached UTF-16 string (key + the 23 bytes at 0x730520).
* 0x524e40 frees the loaded tables and the cache, counting down 0x8829d8 first.
* 0x525770 fills the K/M/B/T suffix words from string ids 0x14d0..0x14d3.

Callees 0x52_49e0 (hash), 0x52_7d50 (character count), 0x52_6320 (UTF-8 decode)
and 0x52_6f70 run for real.
"""
import argparse
import hashlib
import random
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FILE = 0x6E0334            # ".\StrTable\strtable.cpp"
CRC_TABLE = 0x7301D8       # 256 u16
SUFFIX = 0x730520          # the string appended to every key by 0x524ac0
ROWS = 0x7303D8           # 13 language rows of 0x18: +0 code, +4 code, +8 fallback, +0xc country, +0x10 locale, +0x14 id
ROW_COUNT = 13
G_CACHE, G_STR_LOADED, G_STR_OFFS, G_STR_DATA = 0x8829B0, 0x8829B4, 0x8829B8, 0x8829C4
G_PATCH_OFFS, G_PATCH_DATA, G_PATCH_LOADED = 0x8829BC, 0x8829C8, 0x8829D0
G_EXP_OFFS, G_EXP_DATA, G_EXP_LOADED, G_COUNTER = 0x8829C0, 0x8829CC, 0x8829D4, 0x8829D8
G_ABBREV = 0x730510        # K, M, B, T words
MUTATION = None            # set by --mutation-check: one of MUTATIONS


class Halt(Exception):
    """game.exe's unrecoverable-error call, with the source line it passed."""


MUTATIONS = ("crc_init_zero", "row_default_1", "probe_short", "unload_skip_d0", "abbrev_default_x",
             "hash_no_mask")


def broken(name):
    """True when the model under test carries this deliberate defect."""
    return MUTATION == name


def require(condition, message):
    """Stop when game.exe's code disagrees with the model."""
    if not condition:
        raise RuntimeError(message)


class Harness:
    """game.exe with hooked allocator, free, fatal handler, callees and registry."""

    def __init__(self, machine):
        """Install the hooks; the logs and the test state are cleared per call."""
        self.m = machine
        self.allocs, self.frees, self.halt_line = [], [], None
        self.patch_out = {}           # 0x52_4d30's stub: key address -> string pointer
        self.lang_id = 0              # 0x52_5150's stub result
        self.string_by_id = {}        # 0x52_4930's stub: id -> u16 string address
        self.exit_calls = 0
        self.reg = {"open": 0, "query": 0, "data": b"", "closes": [], "opens": 0}
        m = machine
        m.hook(0x40_B380, self._alloc, 2)
        m.hook(0x40_B3C0, self._free, 2)
        m.hook(0x40_8A60, self._fatal, 0)
        m.hook(0x52_7720, self._tail, 0)
        m.hook(0x52_4930, self._string_by_id, 1)
        m.hook(0x52_4D30, self._patch_lookup, 2)
        m.hook(0x52_5150, self._lang_id, 0)
        for name, handler in (("RegOpenKeyExA", self._reg_open), ("RegQueryValueExA", self._reg_query),
                              ("RegCloseKey", self._reg_close)):
            addr = [a for a, (n, _) in m.slots.items() if n == name][0]
            m.slots[addr] = (name, handler)

    def _alloc(self, e):
        """Log (size, file, line, flag) and return zeroed memory."""
        size = e.mu.reg_read(UC_X86_REG_ECX)
        self.allocs.append((size, e.mu.reg_read(UC_X86_REG_EDX), e.arg(0), e.arg(1)))
        return e.alloc(size)

    def _free(self, e):
        """Log (block, file, line, flag)."""
        self.frees.append((e.mu.reg_read(UC_X86_REG_ECX), e.mu.reg_read(UC_X86_REG_EDX), e.arg(0), e.arg(1)))
        return 1

    def _fatal(self, e):
        """The unrecoverable-error call: its third argument is the source line."""
        raise Halt(e.arg(2))

    def _tail(self, e):
        """0x52_7720 is the unload tail call; its work is not claimed here."""
        self.exit_calls += 1
        return 0

    def _string_by_id(self, e):
        """Stub of 0x52_4930: the u16 string registered for the id."""
        return self.string_by_id[e.arg(0)]

    def _patch_lookup(self, e):
        """Stub of 0x52_4d30 (key, &out): writes the registered pointer for this key."""
        e.w32(e.arg(1), self.patch_out.get(e.arg(0), 0))
        return 0

    def _lang_id(self, e):
        """Stub of 0x52_5150: the id the caller gets."""
        return self.lang_id

    def _reg_open(self, e):
        """Stub of RegOpenKeyExA: success writes a handle to the result slot."""
        self.reg["opens"] += 1
        if self.reg["open"]:
            return self.reg["open"], 5
        e.w32(e.arg(4), 0x1234)
        return 0, 5

    def _reg_query(self, e):
        """Stub of RegQueryValueExA: copies the chosen value and its NUL into lpData."""
        if self.reg["query"]:
            return self.reg["query"], 6
        e.mu.mem_write(e.arg(4), self.reg["data"] + b"\0")
        return 0, 6

    def _reg_close(self, e):
        """Stub of RegCloseKey: logs the handle."""
        self.reg["closes"].append(e.arg(0))
        return 0, 1

    def call(self, addr, *args, ecx=0, edx=0, regs=None):
        """Run a function, clear the logs first, and fail if it did not return."""
        self.allocs.clear()
        self.frees.clear()
        self.exit_calls = 0
        self.reg["closes"] = []
        self.reg["opens"] = 0
        result = self.m.call(addr, *args, ecx=ecx, edx=edx, regs=regs, limit=2_000_000)
        return result

    def halts(self, line, addr, *args, **kwargs):
        """Run a function that must reach the fatal handler with this line."""
        try:
            self.call(addr, *args, **kwargs)
        except Halt as halt:
            require(halt.args[0] == line, f"{addr:#x}: halt {halt.args[0]:#x}, expected {line:#x}")
            return
        raise RuntimeError(f"{addr:#x}: no halt, expected {line:#x}")


def cstr_bytes(machine, addr):
    """The bytes of a NUL-terminated ASCII string at addr (without the NUL)."""
    return machine.cstr(addr).encode("latin-1") if isinstance(machine.cstr(addr), str) else machine.cstr(addr)


def units(machine, addr):
    """The UTF-16 units of a u16 string at addr, up to its NUL."""
    out, i = [], 0
    while True:
        v = machine.r16(addr + 2 * i)
        if v == 0:
            return out
        out.append(v)
        i += 1


def crc_step(table_word, crc, byte):
    """One CRC-16 byte step as game.exe's 0x524fe0 does it: high byte of crc in the table index."""
    return ((crc & 0xFF) << 8) ^ table_word[((crc >> 8) ^ byte) & 0xFF]


def crc_range(table_word, data, start, end):
    """The CRC-16 of data[start:end] from 0xffff, as 0x525020 computes it."""
    crc = 0xFFFF if not broken("crc_init_zero") else 0
    for byte in data[start:end]:
        crc = crc_step(table_word, crc, byte)
    return crc


def hash_key(key, size):
    """0x52_49e0: the string hash modulo size (a signed-char hash, top nibble folded in)."""
    h = 0
    for byte in key:
        c = byte - 256 if byte >= 128 else byte
        h = ((h << 4) + c) & 0xFFFFFFFF
        top = h & 0xF0000000
        if top:
            h = (((top >> 24) ^ h) & 0x0FFFFFFF) if not broken("hash_no_mask") else ((top >> 24) ^ h)
    return h % size


def check_crc(machine, harness, rng, table_word, cases):
    """0x524fd0, 0x524fe0 and 0x525020: the word init, one byte step and the range CRC."""
    cell = machine.alloc(4)
    for _ in range(300):
        upper = rng.getrandbits(16)
        machine.w32(cell, rng.getrandbits(16) | upper << 16)
        harness.call(0x524FD0, cell)
        require(machine.r16(cell) == 0xFFFF and machine.r16(cell + 2) == upper, "init word")
        cases += 1
    for _ in range(3000):
        crc = rng.getrandbits(16)
        byte = rng.getrandbits(8)
        machine.w32(cell, crc)
        harness.call(0x524FE0, cell, byte)
        want = crc_step(table_word, crc, byte)
        require(machine.r16(cell) == want, f"step {crc:04x} {byte:02x}: {machine.r16(cell):04x} vs {want:04x}")
        cases += 1
    for _ in range(400):
        n = rng.randint(1, 64)
        data = bytes(rng.getrandbits(8) for _ in range(n))
        start = rng.randrange(n)
        end = rng.randint(start + 1, n)
        buf = machine.put(data)
        harness.call(0x525020, cell, buf, start, end)
        want = crc_range(table_word, data, start, end)
        require(machine.r16(cell) == want, f"range crc {start}:{end}: {machine.r16(cell):04x} vs {want:04x}")
        cases += 1
    buf = machine.put(b"abc")
    harness.halts(0x3EA, 0x525020, cell, buf, 2, 2)
    harness.halts(0x3EA, 0x525020, cell, buf, 3, 1)
    cases += 2
    return cases


def row_fields(machine, index):
    """The row's key, code, fallback, country and id as game.exe stores them."""
    base = ROWS + 24 * index
    return {"key": machine.cstr(machine.r32(base)), "code": machine.cstr(machine.r32(base + 4)),
            "fallback": machine.cstr(machine.r32(base + 8)), "country": machine.cstr(machine.r32(base + 0xC)),
            "id": machine.r32(base + 0x14), "base": base}


def check_lang(machine, harness, rng, cases):
    """0x525090 (code to its row's code, else ENG), 0x525100 (code to row id), 0x525260 (row field by mode)."""
    rows = [row_fields(machine, i) for i in range(ROW_COUNT)]
    for _ in range(600):
        if rng.random() < 0.7:
            text = rng.choice(rows)["key"]
        else:
            text = "".join(rng.choice("ENUFRADESPOLIT") for _ in range(rng.randint(0, 4)))
        buf = machine.alloc(16)
        machine.mu.mem_write(buf, text.encode() + b"\0")
        harness.call(0x525090, ecx=buf)
        want = next((r["code"] for r in rows if r["key"] == text), rows[0]["code"] if broken("row_default_1") is False else rows[1]["code"])
        require(machine.cstr(buf) == want, f"normalise {text!r}: {machine.cstr(buf)!r} vs {want!r}")
        buf2 = machine.alloc(16)
        machine.mu.mem_write(buf2, text.encode() + b"\0")
        got = harness.call(0x525100, ecx=buf2)
        want_id = next((r["id"] for r in rows if r["key"] == text), 0)
        require(got == want_id, f"row id {text!r}: {got} vs {want_id}")
        cases += 2
    for ident in list(range(ROW_COUNT)) + [14, 20, 99]:
        for mode in range(5):
            out = machine.alloc(64)
            harness.lang_id = ident
            harness.call(0x525260, out, mode)
            index = 0 if ident >= 14 else ident          # the row whose id is ident (rows are in id order)
            row = rows[index]
            if mode == 3:
                want = machine.r32(row["base"] + 0x10)
                require(machine.r32(out) == want, f"id {ident} mode 3: {machine.r32(out):#x}")
            else:
                field = {1: 8, 2: 0xC}.get(mode, 4)
                want = machine.cstr(machine.r32(row["base"] + field))
                require(machine.cstr(out) == want, f"id {ident} mode {mode}: {machine.cstr(out)!r} vs {want!r}")
            cases += 1
    return cases


def check_registry(machine, harness, cases):
    """0x5251c0: open HKCU Control Panel\\International, read sLanguage, row id if 1..3 chars."""
    rows = [row_fields(machine, i) for i in range(ROW_COUNT)]
    scenarios = [("open", b"", 2, 0), ("query", b"", 0, 2), ("data", b"", 0, 0)]
    scenarios += [("data", s, 0, 0) for s in (b"", b"A", b"DEU", b"FRA", b"ENU", b"ESP", b"XYZ", b"ENGL", b"RUS", b"KOR", b"nope")]
    for kind, data, open_status, query_status in scenarios:
        harness.reg.update(open=open_status, query=query_status, data=data)
        got = harness.call(0x5251C0)
        if open_status or query_status:
            want, closes = 0, []
        elif len(data) == 0 or len(data) >= 4:
            want, closes = 0, []
        else:
            want = next((r["id"] for r in rows if r["key"].encode() == data), 0)
            closes = [0x1234]
        require(got == want, f"registry {kind} {data!r}: {got} vs {want}")
        require(harness.reg["closes"] == closes, f"registry {kind} {data!r}: closes {harness.reg['closes']} vs {closes}")
        cases += 1
    harness.reg.update(open=0, query=0, data=b"")
    return cases


def write_tbl(machine, rng, count1, size, count, keys):
    """A synthetic TBL: header, u16 index array, 0x11-byte entries (valid or empty), key strings."""
    tbl = machine.alloc(0x2000)
    machine.w32(tbl + 4, size)
    machine.mu.mem_write(tbl + 2, count1.to_bytes(2, "little"))
    machine.w32(tbl + 0xD, count)
    base = tbl + 0x15 + 2 * count1
    key_area = base + 17 * size + 0x10
    entries = []
    strings = {}
    for i in range(size):
        entry = base + 17 * i
        valid = i < len(keys) and rng.random() < 0.8
        machine.mu.mem_write(entry, bytes([1 if valid else 0]))
        if valid:
            key = keys[i]
            machine.mu.mem_write(key_area + 0x20 * i, key + b"\0")
            machine.w32(entry + 7, key_area + 0x20 * i - tbl)
            machine.mu.mem_write(entry + 1, (0x100 + i).to_bytes(2, "little"))
            strings[i] = key
            entries.append((i, key))
    return tbl, base, entries


def check_lookup(machine, harness, rng, cases):
    """0x524c60: the TBL hash lookup with linear probing, the id, and the array entry."""
    for _ in range(120):
        size = rng.randint(3, 9)
        count1 = rng.randint(0, 4)
        count = rng.randint(1, size)
        keys = [bytes(rng.choice(b"abcXYZ_09") for _ in range(rng.randint(1, 12))) for _ in range(size)]
        tbl, base, entries = write_tbl(machine, rng, count1, size, count, keys)
        arr = machine.alloc(4 * size)
        for i in range(size):
            machine.w32(arr + 4 * i, 0x9000 + i if rng.random() < 0.9 else 0)
        outp = machine.alloc(4)
        for probe in [k for _, k in entries] + [b"nope", b"zz"]:
            key_buf = machine.put(probe + b"\0")
            want_id, want_out, want_halt = 0xFFFF, None, None
            h = hash_key(probe, size)
            probes = count - 1 if broken("probe_short") else count
            for _ in range(max(0, probes)):
                ent = base + 17 * h
                if machine.mu.mem_read(ent, 1)[0] == 1 and machine.cstr(tbl + machine.r32(ent + 7)) == probe.decode():
                    want_id = machine.r16(ent + 1)
                    want_out = arr + 4 * h
                    break
                h = (h + 1) % size
            machine.w32(outp, 0)
            if want_out is not None and machine.r32(want_out) == 0:
                want_halt = 0x296
            got = None
            if want_halt:
                harness.halts(want_halt, 0x524C60, arr, key_buf, outp, regs={"EDI": tbl})
            else:
                got = harness.call(0x524C60, arr, key_buf, outp, regs={"EDI": tbl})
                require((got & 0xFFFF) == want_id, f"lookup {probe!r}: {got & 0xFFFF:#x} vs {want_id:#x}")
                if want_out is not None:
                    require(machine.r32(outp) == machine.r32(want_out), f"lookup {probe!r}: out {machine.r32(outp):#x}")
            cases += 1
    return cases


SUFFIX_TEXT = None


def check_cache(machine, harness, rng, cases):
    """0x524ac0: the cache of decoded keys, miss = key + SUFFIX decoded, hit = same pointer."""
    suffix = machine.cstr(SUFFIX)
    machine.w32(G_CACHE, 0)
    model = []                       # newest first: (key, decoded address, expected units)
    for _ in range(160):
        if model and rng.random() < 0.4:
            key = rng.choice(model)[0]
        else:
            key = "".join(rng.choice("abcXYZ_09 ") for _ in range(rng.randint(0, 9)))
        key_buf = machine.put(key.encode() + b"\0")
        hit = next((n for n in model if n[0] == key), None)
        got = harness.call(0x524AC0, ecx=key_buf)
        if hit:
            require(got == hit[1], f"cache hit {key!r}: {got:#x} vs {hit[1]:#x}")
            require(harness.allocs == [] and harness.frees == [], "a hit allocates nothing")
        else:
            text = key + suffix
            total = len(text)
            # 0x52_7d50 counts the whole key + suffix; 0x52_6320 writes count - 1 units, so the last character is dropped
            want = [ord(c) for c in text][:total - 1]
            require(units(machine, got) == want, f"decoded {key!r}: {units(machine, got)} vs {want}")
            require(harness.allocs[0] == (0xC, FILE, 0x259, 0), f"node alloc {harness.allocs}")
            require(harness.allocs[1] == (len(key) + 1, FILE, 0x264, 0), f"key copy alloc {harness.allocs}")
            require(harness.allocs[2] == (total + 1, FILE, 0x26C, 0), f"buffer alloc {harness.allocs}")
            require(harness.allocs[3] == (2 * total + 2, FILE, 0x271, 0), f"decoded alloc {harness.allocs}")
            require(len(harness.frees) == 1 and harness.frees[0][1:] == (FILE, 0x277, 0),
                    f"buffer free {harness.frees}")
            model.insert(0, (key, got, want))
        # the list at 0x8829b0 mirrors the model, newest first
        node, walked = machine.r32(G_CACHE), []
        while node:
            walked.append((machine.cstr(machine.r32(node + 4)), machine.r32(node)))
            node = machine.r32(node + 8)
        require([(k, d) for k, d, _ in model] == walked, "cache list order")
        cases += 1
    return cases


def check_unload(machine, harness, rng, cases):
    """0x524e40: nothing without a table; a count-down; then the frees in code order and the list."""
    globals_all = (G_CACHE, G_STR_LOADED, G_STR_OFFS, G_STR_DATA, G_PATCH_OFFS, G_PATCH_DATA, G_PATCH_LOADED,
                   G_EXP_OFFS, G_EXP_DATA, G_EXP_LOADED, G_COUNTER)

    def set_state(values):
        """Write every global the function reads, then return the dict."""
        for addr in globals_all:
            machine.w32(addr, values.get(addr, 0))

    def blocks(n):
        """n fresh block addresses."""
        return [machine.alloc(16) for _ in range(n)]

    for _ in range(60):
        b = blocks(12)
        nodes = []
        for _ in range(rng.randint(0, 3)):
            node = machine.alloc(12)
            decoded, key = blocks(2)
            machine.w32(node, decoded)
            machine.w32(node + 4, key)
            nodes.append((node, decoded, key))
        for i in range(len(nodes)):
            machine.w32(nodes[i][0] + 8, nodes[i + 1][0] if i + 1 < len(nodes) else 0)
        loaded = rng.random() < 0.8
        counter = rng.choice([0, 1, 2, 3])
        state = {G_STR_LOADED: b[0] if loaded else 0, G_STR_OFFS: b[1] if rng.random() < 0.7 else 0,
                 G_STR_DATA: b[2], G_PATCH_OFFS: b[3] if rng.random() < 0.5 else 0, G_PATCH_DATA: b[4] if rng.random() < 0.5 else 0,
                 G_PATCH_LOADED: b[5], G_EXP_OFFS: b[6] if rng.random() < 0.5 else 0, G_EXP_DATA: b[7] if rng.random() < 0.5 else 0,
                 G_EXP_LOADED: b[8], G_COUNTER: counter, G_CACHE: nodes[0][0] if nodes else 0}
        set_state(state)
        if not loaded:
            want_frees, want_counter, want_exit = [], counter, 0
        else:
            want_counter = (counter - 1) & 0xFFFFFFFF
            if want_counter != 0:
                want_frees, want_exit = [], 0
            else:
                want_frees = []
                if state[G_STR_OFFS]:
                    want_frees += [(state[G_STR_DATA], FILE, 0x329, 0), (state[G_STR_OFFS], FILE, 0x32E, 0)]
                want_frees += [(state[G_STR_LOADED], FILE, 0x121, 0)]
                if state[G_PATCH_OFFS]:
                    want_frees += [(state[G_PATCH_OFFS], FILE, 0x33C, 0)]
                if state[G_PATCH_DATA]:
                    want_frees += [(state[G_PATCH_DATA], FILE, 0x341, 0)]
                if not broken("unload_skip_d0"):
                    want_frees += [(state[G_PATCH_LOADED], FILE, 0x121, 0)]
                if state[G_EXP_OFFS]:
                    want_frees += [(state[G_EXP_OFFS], FILE, 0x34A, 0)]
                if state[G_EXP_DATA]:
                    want_frees += [(state[G_EXP_DATA], FILE, 0x34F, 0)]
                if state[G_EXP_LOADED]:
                    want_frees += [(state[G_EXP_LOADED], FILE, 0x121, 0)]
                for node, decoded, key in nodes:
                    want_frees += [(decoded, FILE, 0x377, 0), (key, FILE, 0x378, 0), (node, FILE, 0x379, 0)]
                want_exit = 1
        harness.call(0x524E40)
        require(machine.r32(G_COUNTER) == want_counter, f"counter {machine.r32(G_COUNTER)} vs {want_counter}")
        if not loaded:
            require(not harness.frees, "unloaded: nothing to free")
        else:
            require(harness.frees == want_frees, f"frees {harness.frees} vs {want_frees}")
            require(harness.exit_calls == want_exit, "tail call")
            if want_counter == 0:
                for addr in (G_STR_LOADED, G_STR_OFFS, G_PATCH_OFFS, G_PATCH_DATA, G_PATCH_LOADED,
                             G_EXP_OFFS, G_EXP_DATA, G_EXP_LOADED, G_CACHE):
                    require(machine.r32(addr) == 0, f"global {addr:#x} cleared")
                # the string data pointer is cleared only inside the offset-table branch
                want_data = 0 if state[G_STR_OFFS] else state[G_STR_DATA]
                require(machine.r32(G_STR_DATA) == want_data, "string data pointer")
        cases += 1
    set_state({})
    return cases


def check_suffix(machine, harness, rng, cases):
    """0x525770: K, M, B, T from the first four characters of string ids 0x14d0..0x14d3."""
    saved = machine.read(G_ABBREV, 0x10)
    defaults = (0x4B, 0x4D, 0x42, 0x54)
    if broken("abbrev_default_x"):
        defaults = (0x58, 0x4D, 0x42, 0x54)
    for _ in range(120):
        strings = {}
        for ident in (0x14D0, 0x14D1, 0x14D2, 0x14D3):
            text = "".join(chr(rng.randint(0x41, 0x5A)) for _ in range(rng.randint(0, 6)))
            addr = machine.alloc(64)
            machine.mu.mem_write(addr, b"".join(ord(c).to_bytes(2, "little") for c in text) + b"\0\0")
            strings[ident] = (text, addr)
            harness.string_by_id[ident] = addr
        machine.w32(G_STR_LOADED, machine.alloc(4))
        machine.w32(G_STR_OFFS, machine.alloc(4))
        harness.call(0x525770)
        for n, ident in enumerate((0x14D0, 0x14D1, 0x14D2, 0x14D3)):
            text = strings[ident][0][:4]
            want = ord(text[0]) if len(text) <= 1 and text else (0 if len(text) == 0 else defaults[n])
            if len(text) > 1:
                want = defaults[n]
            got = machine.r16(G_ABBREV + 4 * n)
            require(got == want, f"abbrev {n} from {text!r}: {got:#x} vs {want:#x}")
            cases += 1
    machine.w32(G_STR_LOADED, 0)
    harness.halts(0x246, 0x525770)
    cases += 1
    machine.mu.mem_write(G_ABBREV, saved)
    machine.w32(G_STR_LOADED, 0)
    machine.w32(G_STR_OFFS, 0)
    return cases


def main():
    """Verify the leaves, or with --mutation-check prove each model defect is caught."""
    global MUTATION
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate model defect to fail")
    options = parser.parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d (1.14.3.71)")
    if options.mutation_check:
        baseline = emu.Emu(str(exe))
        run_all(baseline, Harness(baseline))  # the unmutated model must pass before any defect is attributed
        for name in MUTATIONS:
            MUTATION = name
            machine = emu.Emu(str(exe))
            harness = Harness(machine)
            try:
                run_all(machine, harness)
            except (RuntimeError, SystemExit, Halt):
                print(f"mutation {name}: caught")
                continue
            raise SystemExit(f"mutation {name}: not caught")
        print("mutation check: every defect caught")
        return
    machine = emu.Emu(str(exe))
    harness = Harness(machine)
    print(f"ok: {run_all(machine, harness)} string table leaf cases")


def check_wrapper(machine, harness, rng, cases):
    """0x524e20: passes its ECX key to 0x52_4d30 with a local out slot and returns what 0x52_4d30 stored."""
    for _ in range(50):
        key = machine.put(b"key%d\0" % rng.randint(0, 999))
        value = 0x7000 + rng.randrange(0x1000) * 4
        harness.patch_out[key] = value
        got = harness.call(0x524E20, ecx=key)
        require(got == value, f"wrapper returned {got:#x} vs {value:#x}")
        cases += 1
    return cases


def run_all(machine, harness):
    """Run every check and return the case count."""
    require(machine.cstr(FILE) == ".\\StrTable\\strtable.cpp", "0x6e0334 is not .\\StrTable\\strtable.cpp")
    rng = random.Random(0x5712)
    table_word = [machine.r16(CRC_TABLE + 2 * i) for i in range(256)]
    cases = 0
    cases = check_crc(machine, harness, rng, table_word, cases)
    cases = check_wrapper(machine, harness, rng, cases)
    cases = check_lang(machine, harness, rng, cases)
    cases = check_registry(machine, harness, cases)
    cases = check_lookup(machine, harness, rng, cases)
    cases = check_cache(machine, harness, rng, cases)
    cases = check_unload(machine, harness, rng, cases)
    cases = check_suffix(machine, harness, rng, cases)
    return cases


if __name__ == "__main__":
    main()
