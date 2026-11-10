# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the Battle.net client's CACHE.CPP (bncache.dat) in stock game.exe 1.14d.

Run: uv run python bncache.py [--mutation-check]

The 16 functions FUN_00518de0 ... FUN_00519db0 run for real in the emulator
against an independent Python model of the file format (an MPQ-like archive:
header, encrypted hash and block tables, 0x144-byte entry headers). Hooked: the
Storm allocator and free (FUN_00413020 / FUN_00412650, logged and tracked for
leaks and double frees), the log writer FUN_00518d40, the fatal report and
exit, and the Win32 imports (a dictionary file system with exclusive opens,
a scripted clock, SHGetFolderPathA, critical sections counted). The CRT's
toupper, memset, memcpy, _stricmp and the string copy FUN_00413800 run for
real. Not covered: the real file system, the callers (BNNews, Warden, BNFTP),
the logger's formatting, and thread contention on the critical section.

The model is compared with the game's output after every operation: the whole
file image byte for byte, and the in-memory tables and header fields.

`--mutation-check` plants deliberate defects in the model and requires each
one to be caught.
"""
import argparse
import datetime
import hashlib
import random
import struct
import zlib
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_EIP

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
LIMIT = 400_000_000
M = 0xFFFFFFFF

CRC, DECRYPT, ENCRYPT, TIME = 0x518DE0, 0x518E60, 0x518EC0, 0x518F30
HASH, CRYPT_TABLE, FIND, DELETE = 0x518FA0, 0x519000, 0x519110, 0x5191D0
PATH, REMOVE_FILE, CLOSE, FREE_BUF = 0x519220, 0x5192A0, 0x519300, 0x5193C0
OPEN, REBUILD, READ, WRITE = 0x5193E0, 0x5198C0, 0x519920, 0x519DB0
ALLOC, FREE, LOG = 0x413020, 0x412650, 0x518D40
FATAL, FATAL_EXIT = 0x408A60, 0x681E09

HDR_PTR, BLOCK_PTR, CRYPT_PTR, HASH_PTR, OPEN_FLAG, CS = 0x881880, 0x881884, 0x881888, 0x88188C, 0x881890, 0x881894
HANDLE = 0x72FB78
T_FT, T_SECS, T_TICK = 0x8818AC, 0x8818B8, 0x8818B4
ENTRY = 0x144
CACHE_FILE = "bncache.dat"

mutation = None


class Mismatch(Exception):
    """The emulated function and the model disagree."""


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def require(condition, message):
    """Stop the probe when an observation differs from the model."""
    if not condition:
        raise Mismatch(message)


def u32(*v):
    """Little-endian dwords."""
    return b"".join(struct.pack("<I", x & M) for x in v)


def signed8(c):
    """A char as the game's movsx sees it."""
    return c - 256 if c >= 128 else c


# ---------------------------------------------------------------- the model

def make_crypt_table():
    """The 0x500-entry MPQ crypt table (FUN_00519000): seed 0x100001, * 0x7d + 3 % 0x2aaaab."""
    mul = 0x7C if mut("crypt_mul") else 0x7D
    seed, table = 0x100001, [0] * 0x500
    for i in range(0x100):
        for k in range(5):
            seed = (seed * mul + 3) % 0x2AAAAB
            hi = seed & 0xFFFF
            seed = (seed * mul + 3) % 0x2AAAAB
            table[i + 0x100 * k] = (hi << 16) | (seed & 0xFFFF)
    return table


TABLE = make_crypt_table()


def below_table(index):
    """What lies below the crypt table for a high-bit char hashed as type 0: the test arena's pattern (the game's heap in play)."""
    return 0xA5000000 + 0x400 + index


BELOW = below_table


def mcrc(data):
    """CRC-32 as FUN_00518de0 (zlib's)."""
    if mut("crc_init"):
        return zlib.crc32(data, 1) & M
    return zlib.crc32(bytes(data)) & M


def name_hash(name, kind, table=None):
    """FUN_00518fa0: MPQ name hash; chars index the table signed (high-bit chars read the previous block)."""
    table = table or TABLE
    s1, s2 = (0x7FED7FEE if mut("hash_seed") else 0x7FED7FED), 0xEEEEEEEE
    for c in name:
        ch = c - 32 if 97 <= c <= 122 else c
        index = kind * 256 + signed8(ch)
        s1 = ((table[index] if index >= 0 else BELOW(index)) ^ (s1 + s2)) & M
        s2 = (ch if ch < 128 else ch - 256) + s1 + s2 + ((s2 << 5) & M) + 3 & M
        s2 &= M
    return s1


def encrypt(data, key):
    """FUN_00518ec0 (a whole number of dwords; the tail stays plain)."""
    out, seed = bytearray(data), 0xEEEEEEEE
    for i in range(len(out) // 4):
        seed = (seed + TABLE[0x400 + (key & 0xFF)]) & M
        plain = struct.unpack_from("<I", out, 4 * i)[0]
        struct.pack_into("<I", out, 4 * i, plain ^ ((seed + key) & M))
        seed = (plain + seed + ((seed << 5) & M) + 3) & M
        key = ((((~key) & M) << 21) & M) + 0x11111111 & M | key >> 11
    return bytes(out)


def decrypt(data, key):
    """FUN_00518e60."""
    out, seed = bytearray(data), 0xEEEEEEEE
    for i in range(len(out) // 4):
        seed = (seed + TABLE[0x400 + (key & 0xFF)]) & M
        word = struct.unpack_from("<I", out, 4 * i)[0] ^ ((seed + key) & M)
        struct.pack_into("<I", out, 4 * i, word)
        seed = (word + seed + ((seed << 5) & M) + 3) & M
        key = ((((~key) & M) << 21) & M) + 0x11111111 & M | key >> 11
    return bytes(out)


class Clock:
    """A scripted clock: milliseconds since 1601; each GetSystemTime reading advances it by `step`."""

    def __init__(self, step=0):
        """Start at 2026-11-10 12:00."""
        delta = datetime.datetime(2026, 11, 10, 12) - datetime.datetime(1601, 1, 1)
        self.ms = delta.days * 86400000 + delta.seconds * 1000
        self.step = step

    def read(self):
        """One GetSystemTime + SystemTimeToFileTime: a FILETIME; the clock then moves on."""
        value = self.ms * 10000
        self.ms += self.step
        return value

    def tick(self):
        """GetTickCount."""
        return (self.ms // 1) & M


class TimeModel:
    """FUN_00518f30: now + secs, cached per (secs, tick count) in one static FILETIME."""

    def __init__(self, clock):
        """Empty cache, as at start-up."""
        self.clock, self.ft, self.secs, self.tick = clock, 0, 0, 0

    def now(self, secs):
        """The FILETIME the game's static holds after FUN_00518f30(ESI = secs)."""
        if secs != self.secs or self.clock.tick() != self.tick:
            self.ft = (self.clock.read() + (secs & M) * 10_000_000) & 0xFFFFFFFFFFFFFFFF
            self.secs = secs
            self.tick = self.clock.tick()
        return self.ft


class Model:
    """The bncache.dat format and the open/read/write rules, read from the disassembly."""

    def __init__(self, clock):
        """No cache open yet."""
        self.times = TimeModel(clock)
        self.log = []
        self.opened = False
        self.file = bytearray()

    # -- tables
    def fresh(self):
        """A new empty cache: 0x400 empty hash entries (block -1), 0x400 zero block entries."""
        self.hdr = {"end": 0x802C, "hoff": 0x2C, "boff": 0x402C, "hcnt": 0x400, "bcnt": 0}
        self.hash = [[0, 0, 0, M] for _ in range(0x400)]
        self.blocks = [[0, 0, 0, 0] for _ in range(0x400)]
        self.opened = True

    def find(self, name, locale=0):
        """FUN_00519110: slot of the entry, or the locale-0 slot, or -1."""
        h0, h1, h2 = (name_hash(name, k) for k in range(3))
        mask = (self.hdr["hcnt"] - (2 if mut("find_mask") else 1)) & M
        start = i = h0 & mask
        fallback = -1
        while True:
            e = self.hash[i]
            if e[3] == M:
                return fallback
            if e[0] == h1 and e[1] == h2 and e[3] != 0xFFFFFFFE:
                if e[2] == locale:
                    return i
                if e[2] == 0:
                    fallback = i
            i = (i + 1) & mask
            if i == start:
                return fallback

    def delete(self, name):
        """FUN_005191d0: every entry of the name: block cleared (size, flags), hash slot -2."""
        while (i := self.find(name)) != -1:
            b = self.hash[i][3]
            if b < self.hdr["bcnt"]:
                self.blocks[b][2] = 0
                if not mut("delete_flags"):
                    self.blocks[b][3] = 0
            self.hash[i][3] = 0xFFFFFFFE

    # -- open
    def open(self, data):
        """FUN_005193e0 on a file: None (loaded), 'silent' (replaced without a log line) or the logged code."""
        self.fresh_file = False
        size = len(data)
        limit = 0xA00001 if mut("open_limit") else 0xA00000
        if size == 0 or size > limit:
            return self._new("silent")
        if size < 0x2C:
            return self._new(1)
        head = bytes(data[:0x2C])
        magic = struct.unpack_from("<I", head)[0]
        if magic == 0x1A324E42:
            return self._new("silent")
        if magic != 0x1A334E42:
            return self._new(3)
        stored = struct.unpack_from("<I", head, 0x20)[0]
        if mcrc(head[:0x20] + b"\0\0\0\0" + head[0x24:]) != stored:
            return self._new(4)
        _, _, end, _, hoff, boff, hcnt, bcnt, _, hcrc, bcrc = struct.unpack("<11I", head)
        if hoff == 0 or hcnt == 0:
            return self._new(5)
        if boff == 0 or bcnt == 0:
            return self._new(6)
        hlen = (max(hcnt, 0x400) << 4) & M
        if (hoff + hlen) & M > size:
            return self._new(7)
        raw = bytes(data[hoff:hoff + hlen])
        if len(raw) != hlen:
            return self._new(8)
        if mcrc(raw) != hcrc:
            return self._new(9)
        plain = decrypt(raw, name_hash(b"(hash table)", 3))
        blen = (max(bcnt, 0x400) << 4) & M
        if (boff + blen) & M > size:
            return self._new(10)
        raw2 = bytes(data[boff:boff + blen])
        if len(raw2) != blen:
            return self._new(11)
        if mcrc(raw2) != bcrc:
            return self._new(12)
        blocks = decrypt(raw2, name_hash(b"(block table)", 3))
        self.hdr = {"end": end, "hoff": hoff, "boff": boff, "hcnt": hcnt, "bcnt": bcnt}
        self.hash = [list(struct.unpack_from("<4I", plain, 16 * i)) for i in range(hlen // 16)]
        self.blocks = [list(struct.unpack_from("<4I", blocks, 16 * i)) for i in range(blen // 16)]
        self.opened = True
        self.file = bytearray(data)
        return None

    def _new(self, code):
        """Replace the cache: log (unless silent), empty file, fresh tables."""
        if code != "silent":
            self.log.append(("invalid", code))
        self.file = bytearray()
        self.fresh()
        self.fresh_file = True
        return code

    # -- file image helpers
    def put(self, offset, data):
        """WriteFile at an offset, zero-extending."""
        if len(self.file) < offset:
            self.file.extend(b"\0" * (offset - len(self.file)))
        self.file[offset:offset + len(data)] = data

    def entry_header(self, off):
        """The 0x144-byte entry header at a file offset, or None for a short read."""
        raw = bytes(self.file[off:off + ENTRY])
        return bytearray(raw) if len(raw) == ENTRY else None

    def flush(self):
        """The end of FUN_00519db0: both tables encrypted and written, their CRCs, then the header."""
        hn = max(self.hdr["hcnt"], 0x400) * 16
        bn = max(self.hdr["bcnt"], 0x400) * 16
        hraw = encrypt(b"".join(u32(*e) for e in self.hash[:hn // 16]), name_hash(b"(hash table)", 3))
        self.put(self.hdr["hoff"], hraw)
        braw = encrypt(b"".join(u32(*e) for e in self.blocks[:bn // 16]), name_hash(b"(block table)", 3))
        self.put(self.hdr["boff"], braw)
        head = bytearray(struct.pack("<11I", 0x1A334E42, 0x2C, self.hdr["end"], 0, self.hdr["hoff"], self.hdr["boff"],
                                     self.hdr["hcnt"], self.hdr["bcnt"], 0, mcrc(hraw), mcrc(braw)))
        if mut("header_crc"):
            head[0x20:0x24] = b"\xff\xff\xff\xff"     # the CRC taken with the field not zeroed
        struct.pack_into("<I", head, 0x20, mcrc(bytes(head)))
        self.put(0, bytes(head))

    # -- write
    def write(self, name, data, ft, life1, life2):
        """FUN_00519db0: result 1/0; any failure past the checks logs and rebuilds the cache."""
        self.rejected = not name or not data or len(data) > 0x1000000 or not life2 or ft is None or not self.opened
        if self.rejected:
            return 0
        self.delete(name)
        need = len(data) + ENTRY
        n = self.hdr["bcnt"]
        idx = n
        for i in range(n):
            blk = self.blocks[i]
            if blk[3] == 0 and blk[1] >= need:
                idx = i
                break
        if idx == n:
            for i in range(n):
                blk = self.blocks[i]
                if blk[1] < need:
                    continue
                head = self.entry_header(blk[0])
                if head is None or struct.unpack_from("<I", head)[0] != ENTRY:
                    idx = i
                    break
                now = self.times.now(0)
                reusable = struct.unpack_from("<Q", head, 0x30)[0]
                if (now < reusable) if mut("reuse_dir") else (now > reusable):
                    idx = i
                    break
        if idx < n:
            for e in self.hash[:self.hdr["hcnt"]]:
                if e[3] == idx:
                    e[3] = 0xFFFFFFFE
        else:
            if idx >= 0x400:
                return self._corrupt(0xB)
            self.blocks[idx] = [self.hdr["end"], need, 0, 0]
            self.hdr["end"] += need
            self.hdr["bcnt"] = idx + 1
        h0, h1, h2 = (name_hash(name, k) for k in range(3))
        mask = self.hdr["hcnt"] - 1
        start = slot = h0 & mask
        while self.hash[slot][3] not in (M, 0xFFFFFFFE):
            slot = (slot + 1) & mask
            if slot == start:
                break
        if self.hash[slot][3] not in (M, 0xFFFFFFFE):
            return self._corrupt(0xC)
        self.hash[slot] = [h2, h1, 0, idx] if mut("write_hash_order") else [h1, h2, 0, idx]
        self.blocks[idx][2] = len(data)
        self.blocks[idx][3] = 0xC0000000
        t = self.times
        written = t.now(0)
        head = bytearray(ENTRY)
        struct.pack_into("<I", head, 0, ENTRY)
        struct.pack_into("<Q", head, 0x28, written)
        struct.pack_into("<Q", head, 0x20, ft)
        struct.pack_into("<Q", head, 0x30, t.now(life1))
        struct.pack_into("<Q", head, 0x38, t.now(life2))
        struct.pack_into("<Q", head, 0x18, t.now(0))
        head[0x40:0x40 + len(name) + 1] = name + b"\0"
        struct.pack_into("<I", head, 0x10, mcrc(data))
        struct.pack_into("<I", head, 0xC, mcrc(bytes(head)))
        self.put(self.blocks[idx][0], bytes(head))
        self.put(self.blocks[idx][0] + ENTRY, data)
        self.flush()
        return 1

    def _corrupt(self, code):
        """A write/read failure on the file: log and replace the whole cache."""
        self.log.append(("rebuild", code))
        self.file = bytearray()
        self.fresh()
        return 0

    # -- read
    def read(self, name):
        """FUN_00519920: (result, data, filetime, size). Entry failures delete the entry, file failures rebuild."""
        if not self.opened or not name:
            return 0, None, None, 0
        i = self.find(name)
        if i == -1:
            return 0, None, None, 0
        b = self.hash[i][3]
        if b >= self.hdr["bcnt"]:
            self._corrupt(1)
            return 0, None, None, 0
        blk = self.blocks[b]
        head = bytearray(ENTRY)
        entry_err = 0
        if blk[3] & 0xC0000000:
            raw4 = bytes(self.file[blk[0]:blk[0] + 4])
            if len(raw4) != 4:
                self._corrupt(2)
                return 0, None, None, 0
            if struct.unpack("<I", raw4)[0] != ENTRY:
                self._corrupt(3)
                return 0, None, None, 0
            rest = bytes(self.file[blk[0] + 4:blk[0] + ENTRY])
            if len(rest) != 0x140:
                self._corrupt(4)
                return 0, None, None, 0
            head = bytearray(raw4 + rest)
            stored = struct.unpack_from("<I", head, 0xC)[0]
            struct.pack_into("<I", head, 0xC, 0)
            if mcrc(bytes(head)) != stored:
                entry_err = 1
        if not entry_err:
            hname = bytes(head[0x40:]).split(b"\0")[0]
            if hname.lower() != name.lower():
                entry_err = 2
        if not entry_err:
            now = self.times.now(0)
            expires = struct.unpack_from("<Q", head, 0x38)[0]
            if (now < expires) if mut("expire_dir") else (now > expires):
                entry_err = 3
        if entry_err:
            return self._entry_fail(name, entry_err)
        if blk[2] > 0x1000000:
            self._corrupt(5)
            return 0, None, None, 0
        out_ft = struct.unpack_from("<Q", head, 0x20)[0]
        struct.pack_into("<Q", head, 0x18, self.times.now(0))
        struct.pack_into("<I", head, 0xC, 0)
        struct.pack_into("<I", head, 0xC, mcrc(bytes(head)))
        if getattr(self, "fail_write_back", False):
            self._corrupt(6)
            return 0, None, out_ft, 0
        self.put(blk[0], bytes(head))
        data = bytes(self.file[blk[0] + ENTRY:blk[0] + ENTRY + blk[2]])
        if len(data) != blk[2]:
            self._corrupt(7)
            return 0, None, out_ft, 0
        if mcrc(data) != struct.unpack_from("<I", head, 0x10)[0]:
            self.log.append(("entry", name, 4))
            self.delete(name)
            return 0, None, out_ft, 0
        return 1, data, out_ft, blk[2]

    def _entry_fail(self, name, code):
        """One entry failed its checks: log, drop it from the tables (memory only)."""
        self.log.append(("entry", name, code))
        self.delete(name)
        return 0, None, None, 0

    def image(self):
        """The file bytes the model expects on disk."""
        return bytes(self.file)


# ---------------------------------------------------------------- the world

class World:
    """One emulated game.exe with the Win32 file calls, allocator and logger scripted."""

    def __init__(self, exe):
        """Build the machine and install every hook."""
        self.m = machine = emu.Emu(exe)
        self.fs, self.handles, self.locked = {}, {}, set()
        self.reset_logs()
        for name, handler in self._imports().items():
            for address, (slot, _) in list(machine.slots.items()):
                if slot == name:
                    machine.slots[address] = (slot, handler)
        machine.hook(ALLOC, self._alloc, 4)
        machine.hook(FREE, self._free, 4)
        machine.hook(LOG, self._log, 0)
        machine.hook(FATAL, self._fatal, 0)
        machine.hook(FATAL_EXIT, self._exit, 0)

    def reset_logs(self):
        """Clear every observation log (not the file system)."""
        self.live, self.allocs, self.frees, self.bad_frees = {}, [], [], []
        self.logs, self.fatals, self.exited, self.gs_failed = [], [], False, False
        self.created, self.dirs, self.deleted, self.violations = [], [], [], []
        self.enter, self.leave = 0, 0
        self.fail_create, self.short_write_after = 0, None
        self.writes, self.reads, self.short_read_at = 0, 0, None
        self.folder, self.folder_fail = b"C:\\ProgramData", False
        self.clock = Clock()

    def _imports(self):
        """The Win32 calls CACHE.CPP makes, on a dictionary file system."""
        machine = self.m

        def sh_folder(c):
            """SHGetFolderPathA(hwnd, csidl, token, flags, buf): the scripted folder, or an error."""
            self.csidl = c.arg(1)
            if self.folder_fail:
                return 1, 5
            machine.mu.mem_write(c.arg(4), self.folder + b"\0")
            return 0, 5

        def mkdir(c):
            """CreateDirectoryA: logged."""
            self.dirs.append(machine.cstr(c.arg(0)))
            return 1, 2

        def create(c):
            """CreateFileA(name, access, share, sec, disposition, flags, template): exclusive, OPEN_ALWAYS."""
            name = machine.cstr(c.arg(0))
            self.created.append((name, c.arg(1), c.arg(2), c.arg(4), c.arg(5)))
            if self.fail_create:
                self.fail_create -= 1
                return M, 7
            if name in self.locked:
                return M, 7
            if c.arg(4) == 4:
                self.fs.setdefault(name, bytearray())
            elif name not in self.fs:
                return M, 7
            handle = 0x100 + len(self.handles)
            self.handles[handle] = [name, 0]
            if c.arg(2) == 0:
                self.locked.add(name)
            return handle, 7

        def file_size(c):
            """GetFileSize."""
            return len(self.fs[self.handles[c.arg(0)][0]]), 2

        def read_file(c):
            """ReadFile(h, buf, n, &got, 0)."""
            h = self.handles[c.arg(0)]
            self.reads += 1
            data = bytes(self.fs[h[0]][h[1]:h[1] + c.arg(2)])
            if self.reads == self.short_read_at:
                data = data[:-1]
            machine.mu.mem_write(c.arg(1), data) if data else None
            h[1] += len(data)
            machine.w32(c.arg(3), len(data))
            return 1, 5

        def write_file(c):
            """WriteFile(h, buf, n, &done, 0); an optional budget of successful calls."""
            h = self.handles[c.arg(0)]
            n = c.arg(2)
            self.writes += 1
            if self.short_write_after is not None and self.writes > self.short_write_after:
                machine.w32(c.arg(3), n - 1 if n else 0)
                return 1, 5
            buf = self.fs[h[0]]
            if len(buf) < h[1]:
                buf.extend(b"\0" * (h[1] - len(buf)))
            buf[h[1]:h[1] + n] = machine.read(c.arg(1), n)
            h[1] += n
            machine.w32(c.arg(3), n)
            return 1, 5

        def seek(c):
            """SetFilePointer(h, lo, hi*, method): absolute only."""
            require(c.arg(3) == 0 and c.arg(2) == 0, "SetFilePointer: unexpected method or high word")
            self.handles[c.arg(0)][1] = c.arg(1)
            return c.arg(1), 4

        def close(c):
            """CloseHandle: releases the exclusive open."""
            h = c.arg(0)
            self.locked.discard(self.handles[h][0])
            self.handles[h].append("closed")
            return 1, 1

        def delete(c):
            """DeleteFileA: refused while the file is open (the sharing violation Windows reports)."""
            name = machine.cstr(c.arg(0))
            if name in self.locked:
                self.violations.append(name)
                return 0, 1
            self.deleted.append(name)
            return (1 if self.fs.pop(name, None) is not None else 0), 1

        def compare(c):
            """CompareFileTime."""
            a = struct.unpack("<Q", machine.read(c.arg(0), 8))[0]
            b = struct.unpack("<Q", machine.read(c.arg(1), 8))[0]
            return (M if a < b else (1 if a > b else 0)), 2

        def system_time(c):
            """GetSystemTime: the clock as a SYSTEMTIME (read here, converted back below)."""
            value = self.clock.read() // 10000
            self.pending_ms = value
            moment = datetime.datetime(1601, 1, 1) + datetime.timedelta(milliseconds=value)
            machine.mu.mem_write(c.arg(0), struct.pack("<8H", moment.year, moment.month, moment.weekday(), moment.day,
                                                       moment.hour, moment.minute, moment.second, moment.microsecond // 1000))
            return 0, 1

        def to_filetime(c):
            """SystemTimeToFileTime: the exact inverse of system_time."""
            y, mo, _, d, h, mi, s, ms = struct.unpack("<8H", machine.read(c.arg(0), 16))
            delta = datetime.datetime(y, mo, d, h, mi, s) - datetime.datetime(1601, 1, 1)
            total = (delta.days * 86400 + delta.seconds) * 1000 + ms
            machine.mu.mem_write(c.arg(1), struct.pack("<Q", total * 10000))
            return 1, 2

        def debugger(c):
            """IsDebuggerPresent: the CRT's stack-cookie failure report; stop the run."""
            self.gs_failed = True
            machine.mu.emu_stop()
            return 0, 0

        def enter(c):
            """EnterCriticalSection."""
            self.enter += 1
            return 0, 1

        def leave(c):
            """LeaveCriticalSection."""
            self.leave += 1
            return 0, 1

        return {
            "SHGetFolderPathA": sh_folder, "CreateDirectoryA": mkdir, "CreateFileA": create, "GetFileSize": file_size,
            "ReadFile": read_file, "WriteFile": write_file, "SetFilePointer": seek, "CloseHandle": close,
            "DeleteFileA": delete, "CompareFileTime": compare, "GetSystemTime": system_time,
            "SystemTimeToFileTime": to_filetime, "GetTickCount": lambda c: (self.clock.tick(), 0),
            "IsDebuggerPresent": debugger, "EnterCriticalSection": enter, "LeaveCriticalSection": leave,
        }

    def _alloc(self, c):
        """SMemAlloc(size, file, line, flags): flag 8 zeroes, else poison; tracked."""
        size, line, flags = c.arg(0), c.arg(2), c.arg(3)
        pointer = c.alloc(max(size, 1) + 16, zero=False)
        c.mu.mem_write(pointer, (b"\0" if flags & 8 else b"\xCD") * (max(size, 1) + 16))
        self.allocs.append((size, line, flags))
        self.live[pointer] = (size, line)
        return pointer

    def _free(self, c):
        """SMemFree(ptr, file, line, flags): a pointer not live is a double or wild free."""
        pointer, line = c.arg(0), c.arg(2)
        self.frees.append((pointer, line))
        if pointer not in self.live:
            self.bad_frees.append((pointer, line))
        self.live.pop(pointer, None)
        return 1

    def _log(self, c):
        """FUN_00518d40(fmt, args...): record the format and the first argument."""
        fmt = c.cstr(c.arg(0))
        first = c.arg(1)
        self.logs.append((fmt, c.cstr(first) if "%s" in fmt else first, c.arg(2)))
        return 0

    def _fatal(self, c):
        """The fatal report: log its line argument."""
        self.fatals.append(c.arg(2))
        return 0

    def _exit(self, c):
        """exit(-1): stop the run."""
        self.exited = True
        c.mu.emu_stop()
        return 0

    def run(self, address, *stack, ecx=0, edx=0, regs=None):
        """Call one function; return EAX (None when it never returned)."""
        eax = self.m.call(address, *stack, ecx=ecx, edx=edx, limit=LIMIT, regs=regs)
        done = self.m.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC
        require(done or self.exited or self.gs_failed, f"{address:#x}: instruction cap")
        return eax if done else None

    def cstr(self, text):
        """Pointer to a NUL-terminated copy."""
        return self.m.put(text + b"\0")

    def fresh_game(self):
        """Reset the cache's globals and the file system: a game that never opened the cache."""
        self.reset_logs()
        self.fs.clear()
        self.handles.clear()
        self.locked.clear()
        for address in (HDR_PTR, BLOCK_PTR, CRYPT_PTR, HASH_PTR, OPEN_FLAG, T_FT, T_FT + 4, T_SECS, T_TICK, 0x8818B0):
            self.m.w32(address, 0)
        self.m.w32(HANDLE, M)

    @property
    def path(self):
        """The cache path the game builds from the scripted folder."""
        return (self.folder + b"\\Blizzard Entertainment\\Diablo II\\bncache.dat").decode()


class Rig:
    """A game plus a model of its cache, driven in lockstep."""

    def __init__(self, world, seed_file=None, step=0):
        """A fresh game whose cache file (if any) is `seed_file`, opened by FUN_005193e0."""
        self.w = world
        world.fresh_game()
        world.clock = Clock(step)
        self.model = Model(Clock(step))
        if seed_file is not None:
            world.fs[world.path] = bytearray(seed_file)
        self.expected_open = self.model.open(seed_file if seed_file is not None else b"")
        self.opened_with = seed_file
        self.log_seen = 0
        world.run(OPEN)
        self.check_open()

    def check_open(self):
        """After FUN_005193e0: logged code, file contents, memory tables, leaks, critical section."""
        w = self.w
        invalid = [code for fmt, code, _ in w.logs if fmt.startswith("WARNING- cache file is invalid")]
        want = [] if self.expected_open in (None, "silent") else [self.expected_open]
        require(invalid == want, f"open logged {invalid}, expected {want}")
        require(w.m.r32(OPEN_FLAG) == 1, "open flag")
        require(w.enter == w.leave, f"critical section {w.enter}/{w.leave}")
        self.compare_state("open")
        lines = sorted(line for _, line in w.live.values())
        require(len(lines) == 4, f"live allocations after open: {lines}")

    def compare_state(self, what):
        """The file image and the in-memory tables must equal the model's."""
        w, m = self.w, self.model
        a, b = bytes(w.fs[w.path]), m.image()
        diff = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), None)
        require(a == b, f"{what}: file image differs ({len(a)} vs {len(b)} bytes, first difference at {diff})")
        head = w.m.r32(HDR_PTR)
        got = {"end": w.m.r32(head + 8), "hoff": w.m.r32(head + 0x10), "boff": w.m.r32(head + 0x14),
               "hcnt": w.m.r32(head + 0x18), "bcnt": w.m.r32(head + 0x1C)}
        require(got == m.hdr, f"{what}: header fields {got} vs {m.hdr}")
        hp, bp = w.m.r32(HASH_PTR), w.m.r32(BLOCK_PTR)
        hn = max(m.hdr["hcnt"], 0x400)
        bn = max(m.hdr["bcnt"], 0x400)
        hm = [list(struct.unpack("<4I", w.m.read(hp + 16 * i, 16))) for i in range(hn)]
        bm = [list(struct.unpack("<4I", w.m.read(bp + 16 * i, 16))) for i in range(bn)]
        require(hm == m.hash[:hn], f"{what}: hash table differs")
        require(bm == m.blocks[:bn], f"{what}: block table differs")
        require(w.bad_frees == [], f"{what}: freed what was not allocated: {w.bad_frees}")

    def write(self, name, data, ft=0x01D0_0000_0000_0001, life1=3600, life2=7200):
        """FUN_00519db0 against the model; returns its result."""
        w = self.w
        before = len(w.logs)
        w.enter = w.leave = 0
        ptr = w.m.put(data) if data is not None else 0
        eax = w.run(WRITE, len(data or b""), w.m.put(struct.pack("<Q", ft)) if ft is not None else 0, life1, life2,
                    ecx=w.cstr(name) if name is not None else 0, edx=ptr)
        want = self.model.write(name or b"", data or b"", ft, life1, life2)
        require(eax == want, f"write {name!r}: result {eax} want {want}")
        self.after_op("write", before)
        return eax

    def read(self, name):
        """FUN_00519920 against the model; returns (result, data, filetime)."""
        w = self.w
        before = len(w.logs)
        w.enter = w.leave = 0
        out_ft, out_data, out_size = w.m.alloc(8), w.m.alloc(4), w.m.alloc(4)
        w.m.mu.mem_write(out_ft, b"\xEE" * 8)
        w.m.w32(out_data, 0xDEADBEEF)
        w.m.w32(out_size, 0xDEADBEEF)
        eax = w.run(READ, out_data, out_size, ecx=w.cstr(name), edx=out_ft)
        want, data, ft, size = self.model.read(name)
        require(eax == want, f"read {name!r}: result {eax} want {want}")
        got_ptr, got_size = w.m.r32(out_data), w.m.r32(out_size)
        if want:
            require(got_size == size and w.m.read(got_ptr, got_size) == data, f"read {name!r}: data differs")
            require(got_ptr in w.live, "read: data buffer not an allocation")
            w.live.pop(got_ptr)
        else:
            require(got_ptr == 0 and got_size == 0, f"read {name!r}: outputs {got_ptr:#x} {got_size:#x} after failure")
        got_ft = struct.unpack("<Q", w.m.read(out_ft, 8))[0]
        if ft is not None:
            require(got_ft == ft, f"read {name!r}: filetime {got_ft:#x} want {ft:#x}")
        else:
            require(got_ft == 0, f"read {name!r}: filetime {got_ft:#x} after failure")
        self.after_op("read", before)
        return want, data, ft

    def after_op(self, what, before):
        """Common post-conditions: logs, state, critical section, no leaks beyond the four tables."""
        w, m = self.w, self.model
        got = [(fmt, code) for fmt, code, _ in w.logs[before:]]
        want = []
        for entry in m.log[self.log_seen:]:
            if entry[0] == "invalid":
                want.append(("WARNING- cache file is invalid. Replacing with new cache file", entry[1]))
            elif entry[0] == "rebuild":
                want.append(("bncache.dat is corrupt (%d) - rebuilding archive", entry[1]))
            else:
                want.append(("File: '%s' in bncache.dat is corrupt (%d) - deleting from cache", entry[1]))
        self.log_seen = len(m.log)
        require([f for f, _ in got] == [f for f, _ in want], f"{what}: logged {got}, expected {want}")
        for (f, c, extra), entry in zip(w.logs[before:], [e for e in m.log[self.log_seen - len(want):]]):
            code = entry[-1]
            value = extra if "%s" in f else c
            require(value == code, f"{what}: logged code {value}, expected {code}")
        locked = not (what == "write" and getattr(m, "rejected", False))
        require(w.enter == w.leave and (w.enter > 0) == locked, f"{what}: critical section {w.enter}/{w.leave}")
        self.compare_state(what)
        require(len(w.live) == 4, f"{what}: live allocations {sorted((l, s) for s, l in w.live.values())}")

    def close(self):
        """FUN_00519300 and its leak/handle post-conditions."""
        w = self.w
        w.enter = w.leave = 0
        w.run(CLOSE)
        require(w.live == {}, f"close left allocations {w.live}")
        require(w.m.r32(HANDLE) == M and w.m.r32(OPEN_FLAG) == 0, "close: handle or open flag")
        require(w.enter == w.leave == 1, "close: critical section")
        require(w.bad_frees == [], f"close freed {w.bad_frees}")


# ---------------------------------------------------------------- file builders

def build_file(model_tables, hcnt=0x400, bcnt=0x400, pad_to=0, entries=()):
    """A valid bncache.dat image: header, tables from model lists, entry data placed by the caller."""
    hash_list, block_list = model_tables
    hraw = encrypt(b"".join(u32(*e) for e in hash_list), name_hash(b"(hash table)", 3))
    braw = encrypt(b"".join(u32(*e) for e in block_list), name_hash(b"(block table)", 3))
    end = 0x2C + len(hraw) + len(braw)
    image = bytearray(0x2C) + hraw + braw
    head = bytearray(struct.pack("<11I", 0x1A334E42, 0x2C, end, 0, 0x2C, 0x2C + len(hraw), hcnt, bcnt, 0, mcrc(hraw), mcrc(braw)))
    struct.pack_into("<I", head, 0x20, mcrc(bytes(head)))
    image[:0x2C] = head
    if len(image) < pad_to:
        image.extend(b"\0" * (pad_to - len(image)))
    return bytearray(image)


def empty_tables(hcnt=0x400, bcnt=0x400):
    """Fresh hash and block lists."""
    return [[0, 0, 0, M] for _ in range(max(hcnt, 0x400))], [[0, 0, 0, 0] for _ in range(max(bcnt, 0x400))]


def resign(image):
    """Recompute a mutated image's header CRC."""
    image[0x20:0x24] = b"\0\0\0\0"
    struct.pack_into("<I", image, 0x20, mcrc(bytes(image[:0x2C])))
    return image


def names(rng, count=12, high=True):
    """A spread of cache names: the game's own, mixed case, high bytes, long."""
    pool = [b"BNNews-D2DV", b"bnnews-d2dv", b"0123456789abcdef0123456789abcdef.mod", b"a", b"Z", b"x" * 100,
            b"patch.mpq", b"ver-IX86-1.mpq"] + ([b"caf\xe9", b"\xff\xfeName"] if high else [])
    while len(pool) < count:
        pool.append(bytes(rng.randrange(33, 127) for _ in range(rng.randrange(1, 40))))
    return pool


# ---------------------------------------------------------------- checks

def check_crc(world):
    """FUN_00518de0: zlib's CRC-32 on any length; null buffer with length 0 is 0, with a length is fatal."""
    n, rng = 0, random.Random(7)
    for length in list(range(0, 70)) + [255, 256, 257, 1000, 4096]:
        data = bytes(rng.randrange(256) for _ in range(length))
        world.reset_logs()
        eax = world.run(CRC, ecx=length, regs={"eax": world.m.put(data)})
        require(eax == mcrc(data), f"crc length {length}: {eax:#x} want {mcrc(data):#x}")
        n += 1
    world.reset_logs()
    require(world.run(CRC, ecx=0, regs={"eax": 0}) == 0 and not world.fatals, "crc null+0")
    world.reset_logs()
    world.run(CRC, ecx=5, regs={"eax": 0})
    require(world.fatals == [0x91] and world.exited, f"crc null+5 fatal {world.fatals}")
    return n + 2


def install_table(world):
    """A crypt table in an arena with known bytes before it (type-0 high-bit names index below it)."""
    arena = world.m.alloc(0x1400 + 0x1000)
    world.m.mu.mem_write(arena, b"".join(u32(0xA5000000 + i) for i in range(0x400)))
    table = arena + 0x1000
    world.m.w32(CRYPT_PTR, table)
    return arena, table


def check_crypt_table(world):
    """FUN_00519000: the 0x500 entries from the seed; a null table pointer does nothing."""
    world.fresh_game()
    arena, table = install_table(world)
    world.run(CRYPT_TABLE)
    got = struct.unpack("<1280I", world.m.read(table, 0x1400))
    require(list(got) == make_crypt_table(), "crypt table differs from the model")
    world.m.w32(CRYPT_PTR, 0)
    world.run(CRYPT_TABLE)
    return 2


def check_hash(world):
    """FUN_00518fa0: the MPQ name hash, types 0 to 4, upper-cased, signed chars index below their block."""
    world.fresh_game()
    arena, table = install_table(world)
    world.run(CRYPT_TABLE)
    flat = list(struct.unpack("<%dI" % (0x1000 // 4 + 0x500), world.m.read(arena, 0x1000 + 0x1400)))
    n, rng = 0, random.Random(3)
    for name in names(rng, 30) + [b"", b"\x80", b"\x80abc", b"\xff"]:
        for kind in range(5):
            eax = world.run(HASH, kind, regs={"eax": world.cstr(name)})
            index = [(kind * 256 + signed8(c if not 97 <= c <= 122 else c - 32)) for c in name]
            s1, s2 = (0x7FED7FEE if mut("hash_seed") else 0x7FED7FED), 0xEEEEEEEE
            for c, ix in zip(name, index):
                ch = c - 32 if 97 <= c <= 122 else c
                s1 = (flat[0x400 + ix] ^ (s1 + s2)) & M
                s2 = (signed8(ch) + s1 + s2 + (s2 << 5) + 3) & M
            require(eax == s1, f"hash {name!r} type {kind}: {eax:#x} want {s1:#x}")
            if all(c < 128 for c in name):
                require(s1 == name_hash(name, kind) or mut("hash_seed"), "model self-check")
            n += 1
    return n


def check_encrypt(world):
    """FUN_00518ec0 / FUN_00518e60: the MPQ block cipher; the tail below four bytes stays plain."""
    world.fresh_game()
    install_table(world)
    world.run(CRYPT_TABLE)
    n, rng = 0, random.Random(5)
    for length in list(range(0, 40)) + [64, 1000]:
        data = bytes(rng.randrange(256) for _ in range(length))
        key = rng.randrange(1 << 32)
        buf = world.m.put(data)
        world.run(ENCRYPT, ecx=key, edx=length, regs={"eax": buf})
        enc = world.m.read(buf, length)
        require(enc == encrypt(data, key), f"encrypt length {length}")
        world.run(DECRYPT, ecx=key, edx=buf, regs={"eax": length})
        require(world.m.read(buf, length) == data, f"decrypt length {length}")
        require(decrypt(enc, key) == data, "model round trip")
        n += 1
    return n


def check_time(world):
    """FUN_00518f30: now + secs, one static FILETIME cached per (secs, tick count)."""
    n = 0
    for step in (0, 1, 15):
        for sequence in ([0], [0, 0, 5, 5, 0, 3600], [0xFFFFFFFF, 0xFFFFFFFF, 0, 0x7FFFFFFF], [10, 20, 10, 20]):
            world.fresh_game()
            world.clock = Clock(step)
            model = TimeModel(Clock(step))
            for secs in sequence:
                eax = world.run(TIME, regs={"esi": secs})
                require(eax == T_FT, "time returns the static")
                got = struct.unpack("<Q", world.m.read(T_FT, 8))[0]
                want = model.now(secs)
                require(got == want, f"time step {step} secs {secs:#x}: {got:#x} want {want:#x}")
                n += 1
    return n


def seeded_state(world, rng, hcnt=0x400, density=300, deleted=60):
    """Memory tables with entries (some deleted) and the model alongside, for find and delete."""
    world.fresh_game()
    clock = Clock()
    model = Model(clock)
    model.open(b"")
    model.hdr["hcnt"] = hcnt
    model.hash = [[0, 0, 0, M] for _ in range(max(hcnt, 0x400))]
    pool = names(rng, 50)
    for i in range(density):
        name = pool[i % len(pool)] + (b"-%d" % i if i >= len(pool) else b"")
        j = model.find(name)
        if j != -1 and i < len(pool):
            continue
        h0, h1, h2 = (name_hash(name, k) for k in range(3))
        mask = hcnt - 1
        slot = h0 & mask
        for _ in range(hcnt):
            if model.hash[slot][3] in (M, 0xFFFFFFFE):
                break
            slot = (slot + 1) & mask
        else:
            continue
        model.hash[slot] = [h1, h2, rng.choice([0, 0, 0, 9]), min(i, 0x3FF)]
    model.hdr["bcnt"] = 0x400
    for b in model.blocks:
        b[0], b[1], b[2], b[3] = 0x1000, 0x200, 0x10, 0xC0000000
    for _ in range(deleted):
        e = rng.choice(model.hash)
        if e[3] != M:
            e[3] = 0xFFFFFFFE
    # load into the game's memory
    arena, table = install_table(world)
    world.run(CRYPT_TABLE)
    head = world.m.alloc(0x2C)
    world.m.w32(head + 0x18, hcnt)
    world.m.w32(head + 0x1C, 0x400)
    hbuf = world.m.put(b"".join(u32(*e) for e in model.hash))
    bbuf = world.m.put(b"".join(u32(*e) for e in model.blocks))
    world.m.w32(HDR_PTR, head)
    world.m.w32(HASH_PTR, hbuf)
    world.m.w32(BLOCK_PTR, bbuf)
    return model, pool, hbuf, bbuf


def check_find_delete(world):
    """FUN_00519110 / FUN_005191d0: probe from hash & (count - 1) to an empty slot; delete clears every match."""
    n, rng = 0, random.Random(11)
    for hcnt in (0x400, 0x10, 0x401):
        model, pool, hbuf, bbuf = seeded_state(world, rng, hcnt=hcnt, density=min(300, hcnt - 1) if hcnt != 0x10 else 9)
        probes = pool + [b"missing-%d" % i for i in range(10)]
        for name in probes:
            for locale in (0, 9, 1):
                eax = world.run(FIND, locale, regs={"eax": world.cstr(name)})
                want = model.find(name, locale) & M
                require(eax == want, f"find {name!r} locale {locale} count {hcnt:#x}: {eax:#x} want {want:#x}")
                n += 1
        for name in probes[:25]:
            world.run(DELETE, regs={"esi": world.cstr(name)})
            model.delete(name)
            hm = [list(struct.unpack("<4I", world.m.read(hbuf + 16 * i, 16))) for i in range(max(hcnt, 0x400))]
            bm = [list(struct.unpack("<4I", world.m.read(bbuf + 16 * i, 16))) for i in range(0x400)]
            require(hm == model.hash and bm == model.blocks, f"delete {name!r}: tables differ (count {hcnt:#x})")
            n += 1
    return n


def check_path(world):
    """FUN_00519220: SHGetFolderPathA(0x23) + the two folders and the file name, creating each folder."""
    n = 0
    for folder in (b"C:\\ProgramData", b"", b"D:\\x"):
        world.fresh_game()
        world.folder = folder
        buf = world.m.alloc(0x120)
        world.m.mu.mem_write(buf, b"\xAA" * 0x120)
        world.run(PATH, regs={"esi": buf})
        text = world.m.cstr(buf).encode("latin-1")
        want = folder + b"\\Blizzard Entertainment\\Diablo II\\bncache.dat"
        require(text == want, f"path {text!r} want {want!r}")
        require(world.csidl == 0x23, "csidl")
        require([d.encode() for d in world.dirs] == [folder + b"\\Blizzard Entertainment", folder + b"\\Blizzard Entertainment\\Diablo II"],
                f"folders created {world.dirs}")
        n += 1
    world.fresh_game()
    world.folder = b"E:\\" + b"p" * 240
    buf = world.m.alloc(0x120)
    world.m.mu.mem_write(buf, b"\xAA" * 0x120)
    world.run(PATH, regs={"esi": buf})
    require(world.m.read(buf + 0x104, 0x1C) == b"\xAA" * 0x1C, "path wrote past 0x104 bytes")
    require(b"\0" in world.m.read(buf, 0x104), "path unterminated")
    world.fresh_game()
    world.folder_fail = True
    world.run(PATH, regs={"esi": world.m.alloc(0x120)})
    require(world.fatals == [0x112] and world.exited, f"folder failure fatal {world.fatals}")
    return n + 2


def check_close_free(world):
    """FUN_00519300 / FUN_005193c0 / FUN_005192a0: close frees the four blocks in order; free(null) is 0."""
    rig = Rig(world)
    before = list(world.live.values())
    world.frees.clear()
    rig.close()
    require([line for _, line in world.frees] == [0x155, 0x159, 0x15D, 0x165], f"close free lines {world.frees}")
    world.run(CLOSE)                       # closing twice is harmless
    require(world.bad_frees == [] and world.m.r32(HANDLE) == M, "second close")
    world.frees.clear()
    pointer = world.m.put(b"x")
    world.live[pointer] = (1, 0)
    require(world.run(FREE_BUF, ecx=pointer) == 1 and world.frees == [(pointer, 0x16F)], f"free {world.frees}")
    world.frees.clear()
    require(world.run(FREE_BUF, ecx=0) == 0 and not world.frees, "free(null) must do nothing")
    rig = Rig(world)
    world.deleted.clear()
    world.run(REMOVE_FILE)
    require(world.deleted == [world.path] and world.m.r32(HANDLE) == M, f"remove file {world.deleted} {world.violations} {world.m.r32(HANDLE):#x}")
    return len(before) + 5


def check_open(world):
    """FUN_005193e0: every rejection code 1 to 12, the silent replacements, a valid file, exclusive open."""
    n = 0
    base_h, base_b = empty_tables()
    good = build_file((base_h, base_b))
    cases = {}
    cases["valid empty"] = good
    cases["size 0"] = b""
    cases["size 0x2b"] = bytes(good[:0x2B])                                             # 1
    cases["bad magic"] = b"XXXX" + bytes(good[4:])                                      # 3
    cases["BN2 magic"] = struct.pack("<I", 0x1A324E42) + bytes(good[4:])
    img = bytearray(good); img[0x20] ^= 1; cases["header crc"] = bytes(img)             # 4
    img = bytearray(good); struct.pack_into("<I", img, 0x10, 0); cases["hash offset 0"] = bytes(resign(img))   # 5
    img = bytearray(good); struct.pack_into("<I", img, 0x18, 0); cases["hash count 0"] = bytes(resign(img))   # 5
    img = bytearray(good); struct.pack_into("<I", img, 0x14, 0); cases["block offset 0"] = bytes(resign(img))   # 6
    img = bytearray(good); struct.pack_into("<I", img, 0x1C, 0); cases["block count 0"] = bytes(resign(img))   # 6
    img = bytearray(good); struct.pack_into("<I", img, 0x10, len(good) - 0x100); cases["hash past end"] = bytes(resign(img))   # 7
    img = bytearray(good); img[0x2C + 5] ^= 1; cases["hash table crc"] = bytes(img)     # 9
    img = bytearray(good); struct.pack_into("<I", img, 0x14, len(good) - 0x100); cases["block past end"] = bytes(resign(img))   # 10
    img = bytearray(good); img[0x402C + 5] ^= 1; cases["block table crc"] = bytes(img)  # 12
    img = bytearray(good); struct.pack_into("<I", img, 0x18, 0x10000000); cases["hash count wraps"] = bytes(resign(img))
    cases["10 MB exactly"] = bytes(build_file((base_h, base_b), pad_to=0xA00000))
    cases["10 MB + 1"] = bytes(build_file((base_h, base_b), pad_to=0xA00001))
    for label, image in cases.items():
        rig = Rig(world, image)
        n += 1
        if label.startswith("10 MB"):
            want_loaded = label == "10 MB exactly"
            require(want_loaded == (rig.expected_open is None), f"{label}: model")
        rig.close()
    # a loaded file keeps its entries
    rig = Rig(world)
    rig.write(b"keep", b"data" * 10)
    image = bytes(world.fs[world.path])
    rig.close()
    rig = Rig(world, image)
    require(rig.expected_open is None and rig.read(b"keep")[0] == 1, "reopen keeps the entry")
    rig.close()
    # exclusive: a second open while the file is held falls back to no cache (result 0, nothing allocated)
    world.fresh_game()
    world.locked.add(world.path)
    world.fs[world.path] = bytearray()
    world.enter = world.leave = 0
    require(world.run(OPEN) == 0 and world.m.r32(OPEN_FLAG) == 0 and world.live == {}, "open while locked")
    require(world.enter == world.leave, "critical section after failed open")
    # open twice: the second closes the first (no leaks)
    rig = Rig(world)
    world.run(OPEN)
    require(len(world.live) == 4 and world.bad_frees == [], f"second open leaked {len(world.live)}")
    rig.close()
    return n + 4


def check_open_failures(world):
    """FUN_005193e0 when the replacement file cannot be created: the tables it freed are freed again by FUN_00519300."""
    base_h, base_b = empty_tables()
    n = 0
    for flip, code, doubles in ((0x2C + 5, 9, [0x159]), (0x402C + 5, 12, [0x155, 0x159])):
        img = bytearray(build_file((base_h, base_b)))
        img[flip] ^= 1
        world.fresh_game()
        world.fs[world.path] = img
        count = {"n": 0}
        (address,) = [a for a, (slot, _) in world.m.slots.items() if slot == "CreateFileA"]
        slot, real = world.m.slots[address]

        def second_fails(c, real=real, count=count):
            """Fail the second CreateFileA only (the one after the bad file was deleted)."""
            count["n"] += 1
            if count["n"] == 2:
                world.created.append((world.m.cstr(c.arg(0)), 0, 0, 0, 0))
                return M, 7
            return real(c)

        world.m.slots[address] = (slot, second_fails)
        try:
            require(world.run(OPEN) == 0, "open must fail when the replacement cannot be created")
        finally:
            world.m.slots[address] = (slot, real)
        require([c for f, c, _ in world.logs] == [code], f"logged {world.logs}")
        require(world.m.r32(OPEN_FLAG) == 0 and world.enter == world.leave, "state after the failed open")
        require([line for _, line in world.bad_frees] == doubles, f"freed twice (lines): {world.bad_frees}, expected {doubles}")
        n += 1
    return n


def check_short_reads(world):
    """A short ReadFile in FUN_005193e0 (codes 2, 8, 11) and a short header write-back in FUN_00519920 (code 6) rebuild the cache."""
    base_h, base_b = empty_tables()
    good = build_file((base_h, base_b))
    for nth, code in ((1, 2), (2, 8), (3, 11)):
        world.fresh_game()
        world.fs[world.path] = bytearray(good)
        world.short_read_at = nth
        world.run(OPEN)
        require([c for f, c, _ in world.logs if f.startswith("WARNING- cache file is invalid")] == [code], f"short read {nth}: {world.logs}")
        require(world.fs[world.path] == bytearray() and world.m.r32(OPEN_FLAG) == 1 and world.bad_frees == [], f"short read {nth}: state")
        require(len(world.live) == 4 and world.enter == world.leave, f"short read {nth}: leaks or lock")
        world.short_read_at = None
        world.run(CLOSE)
    rig = Rig(world)
    rig.write(b"entry", b"payload" * 10)
    rig.model.fail_write_back = True
    world.writes, world.short_write_after = 0, 0
    try:
        want, _, _ = rig.read(b"entry")
    finally:
        world.short_write_after = None
    require(want == 0, "a failed write-back must fail the read")
    rig.close()
    return 4


def check_write_read(world):
    """FUN_00519db0 / FUN_00519920: sequences of writes, rewrites, reads and deletes against the model."""
    n, rng = 0, random.Random(21)
    for step in (0, 1):
        rig = Rig(world, step=step)
        pool = names(rng, 14, high=False)
        sizes = [1, 3, 4, 5, 100, 1000, 4096]
        for _ in range(60):
            op = rng.choice(["w", "w", "w", "r", "r", "w"])
            name = rng.choice(pool)
            if op == "w":
                rig.write(name, bytes(rng.randrange(256) for _ in range(rng.choice(sizes))),
                          ft=rng.randrange(1 << 60), life1=rng.choice([0, 1, 60, 3600, M]), life2=rng.choice([1, 60, M]))
            else:
                rig.read(name)
            n += 1
            rig.w.clock.ms += rng.choice([0, 0, 1000, 3_600_000])
            rig.model.times.clock.ms = rig.w.clock.ms
        rig.close()
    return n


def check_write_args(world):
    """FUN_00519db0 refuses a null/empty name, null data or FILETIME, size 0 or over 0x1000000, a zero second lifetime."""
    rig = Rig(world)
    n = 0
    for name, data, ft, l1, l2 in ((None, b"x", 1, 1, 1), (b"", b"x", 1, 1, 1), (b"n", None, 1, 1, 1), (b"n", b"x", None, 1, 1),
                                   (b"n", b"x", 1, 1, 0), (b"n", b"x", 1, 0, 1)):
        rig.write(name, data, ft, l1, l2)
        n += 1
    world.enter = world.leave = 0
    require(world.run(WRITE, 0x1000001, world.m.put(b"\0" * 8), 1, 1, ecx=world.cstr(b"n"), edx=world.m.put(b"x")) == 0, "size over limit")
    require(world.run(WRITE, 0, world.m.put(b"\0" * 8), 1, 1, ecx=world.cstr(b"n"), edx=world.m.put(b"x")) == 0, "size 0")
    # not open: both calls refuse and touch nothing
    rig.close()
    require(world.run(WRITE, 1, world.m.put(b"\0" * 8), 1, 1, ecx=world.cstr(b"n"), edx=world.m.put(b"x")) == 0, "write when closed")
    out = (world.m.alloc(4), world.m.alloc(4))
    world.m.w32(out[0], 7)
    require(world.run(READ, out[0], out[1], ecx=world.cstr(b"n"), edx=0) == 0 and world.m.r32(out[0]) == 0, "read when closed")
    return n + 4


def check_reuse(world):
    """FUN_00519db0 block reuse: a freed block that fits, else the first fitting block past its reusable time."""
    n = 0
    rig = Rig(world)
    rig.write(b"a", b"A" * 1000, life1=60, life2=M)
    rig.write(b"b", b"B" * 500, life1=M, life2=M)
    rig.write(b"c", b"C" * 1000, life1=M, life2=M)
    rig.write(b"a", b"x" * 200, life1=M, life2=M)         # rewrite: its old block is freed first and reused
    rig.w.clock.ms += 120_000
    rig.model.times.clock.ms = rig.w.clock.ms
    rig.write(b"d", b"D" * 900, life1=M, life2=M)         # no free block: "a" is gone, nothing past its reusable time but its block
    rig.write(b"e", b"E" * 1400, life1=60, life2=M)       # appended
    rig.w.clock.ms += 120_000
    rig.model.times.clock.ms = rig.w.clock.ms
    rig.write(b"f", b"F" * 1300, life1=M, life2=M)        # reuses e's block (past reusable, big enough), evicting e
    rig.read(b"e")
    rig.read(b"f")
    rig.read(b"b")
    n += 10
    # a damaged entry header in a candidate block counts as reusable
    rig.write(b"g", b"G" * 2000, life1=M, life2=M)
    blk = rig.model.blocks[rig.model.hash[rig.model.find(b"g")][3]]
    for image in (rig.w.fs[rig.w.path], rig.model.file):
        image[blk[0]] ^= 0x55
    rig.write(b"h", b"H" * 1800, life1=M, life2=M)
    rig.read(b"g")
    rig.close()
    return n + 3


def check_expiry_and_corruption(world):
    """FUN_00519920's entry checks: header CRC, name, expiry, data CRC; and the file-level rebuilds."""
    n = 0
    for fault in ("hdr_crc", "name", "expired", "data_crc", "no_flags", "block_range", "hdr_size", "short_hdr", "data_len", "short_data",
                  "case"):
        rig = Rig(world)
        rig.write(b"Target", b"payload-" * 20, life1=60, life2=120)
        rig.write(b"other", b"zzz", life1=60, life2=M)
        b = rig.model.hash[rig.model.find(b"Target")][3]
        blk = rig.model.blocks[b]

        def poke(offset, value, rig=rig):
            """Damage a byte in game and model files alike."""
            for img in (rig.w.fs[rig.w.path], rig.model.file):
                img[offset] ^= value

        probe = b"Target"
        if fault == "hdr_crc":
            poke(blk[0] + 0x50, 1)
        elif fault == "name":
            for img in (rig.w.fs[rig.w.path], rig.model.file):
                img[blk[0] + 0x40] = ord("X")
                # keep the header CRC valid so only the name differs
                head = bytearray(img[blk[0]:blk[0] + ENTRY]); head[0xC:0x10] = b"\0\0\0\0"
                img[blk[0] + 0xC:blk[0] + 0x10] = struct.pack("<I", mcrc(bytes(head)))
        elif fault == "expired":
            rig.w.clock.ms += 200_000
            rig.model.times.clock.ms = rig.w.clock.ms
        elif fault == "data_crc":
            poke(blk[0] + ENTRY + 3, 0x20)
        elif fault == "no_flags":
            rig.model.blocks[b][3] = 0
            bp = rig.w.m.r32(BLOCK_PTR)
            rig.w.m.w32(bp + 16 * b + 12, 0)
        elif fault == "block_range":
            rig.model.hdr["bcnt"] = b
            rig.w.m.w32(rig.w.m.r32(HDR_PTR) + 0x1C, b)
        elif fault == "hdr_size":
            poke(blk[0], 1)
        elif fault == "short_hdr":
            del rig.w.fs[rig.w.path][blk[0] + 2:]
            del rig.model.file[blk[0] + 2:]
        elif fault == "data_len":
            rig.model.blocks[b][2] = 0x1000001
            bp = rig.w.m.r32(BLOCK_PTR)
            rig.w.m.w32(bp + 16 * b + 8, 0x1000001)
        elif fault == "short_data":
            del rig.w.fs[rig.w.path][blk[0] + ENTRY + 10:]
            del rig.model.file[blk[0] + ENTRY + 10:]
        elif fault == "case":
            probe = b"tARGET"
        rig.read(probe)
        rig.read(b"other")
        n += 1
        rig.close()
    return n


def check_failures(world):
    """FUN_00519db0 error paths: table full, no free hash slot, failing writes; each logs, rebuilds and returns 0."""
    n = 0
    # block table full (0x400 blocks in use): code 0xb
    hash_list, block_list = empty_tables()
    for i in range(0x400):
        block_list[i] = [0x802C + i * 0x200, 0x200, 0x10, 0xC0000000]
    image = build_file((hash_list, block_list), bcnt=0x400)
    struct.pack_into("<I", image, 8, 0x802C + 0x400 * 0x200)
    rig = Rig(world, resign(image))
    rig.write(b"new", b"x" * 1000)                       # every block is 0x200 bytes: none fits 1000 + 0x144, so code 0xb
    rig.close()
    n += 1
    # hash table with one usable slot at hcnt 0x401: only slots {0, 0x400} are ever probed (non power of two count)
    hash_list, block_list = empty_tables(0x401)
    image = build_file((hash_list, block_list), hcnt=0x401, bcnt=1)
    rig = Rig(world, image)
    for name in (b"one", b"two", b"three", b"four"):
        rig.write(name, b"d" * 10, life1=M, life2=M)
        n += 1
    rig.close()
    # failing WriteFile on the n-th call: the write logs code 0xd..0x11 or the entry is half written
    for budget in range(0, 6):
        rig = Rig(world)
        world.writes = 0
        world.short_write_after = budget
        before = len(world.logs)
        eax = world.run(WRITE, 10, world.m.put(b"\0" * 8), 1, 1, ecx=world.cstr(b"k"), edx=world.m.put(b"d" * 10))
        world.short_write_after = None
        codes = [c for f, c, _ in world.logs[before:] if "rebuilding" in f]
        want = {0: 0xD, 1: 0xE, 2: 0xF, 3: 0x10, 4: 0x11}.get(budget)
        require(eax == (0 if want else 1) and (codes == [want] if want else codes == []), f"write budget {budget}: {eax} {codes}")
        if want:
            require(world.fs[world.path] == bytearray(), "failed write must leave an empty rebuilt file")
        world.run(CLOSE)
        n += 1
    return n


def check_name_cookie(world):
    """FUN_00519db0 copies the name unbounded into the 260-byte entry-name field: 261 bytes or more overwrite the stack cookie."""
    n = 0
    for length in (1, 100, 258, 259, 261, 263):
        rig = Rig(world)
        name = bytes(65 + (i % 26) for i in range(length))
        limit = 258 if mut("cookie_limit") else 259
        if length <= limit:
            require(rig.write(name, b"datum") == 1 and rig.read(name)[0] == 1, f"name length {length}")
            require(not world.gs_failed, f"name length {length}: cookie failure")
        else:
            eax = world.run(WRITE, 5, world.m.put(b"\0" * 8), 1, 1, ecx=world.cstr(name), edx=world.m.put(b"datum"))
            require(world.gs_failed and eax is None, f"name length {length}: no cookie failure")
        n += 1
    return n


def check_size_cap(world):
    """The write limit (16 MB per entry) exceeds the 10 MB file limit of the open: a big cache is dropped silently."""
    rig = Rig(world)
    blob = bytes((i * 7) & 0xFF for i in range(0xA00000))
    rig.write(b"big", blob, life1=M, life2=M)
    require(len(world.fs[world.path]) > 0xA00000, "file over 10 MB")
    image = bytes(world.fs[world.path])
    rig.read(b"big")
    rig.close()
    rig = Rig(world, image)
    require(rig.expected_open == "silent" and rig.read(b"big")[0] == 0, "an oversized cache is replaced without a log line")
    rig.close()
    return 2


CHECKS = [check_crc, check_crypt_table, check_hash, check_encrypt, check_time, check_find_delete, check_path, check_close_free,
          check_open, check_open_failures, check_short_reads, check_write_args, check_write_read, check_reuse, check_expiry_and_corruption,
          check_failures, check_name_cookie, check_size_cap]

MUTATIONS = {
    "crc_init": check_write_read,
    "hash_seed": check_hash,
    "crypt_mul": check_crypt_table,
    "find_mask": check_find_delete,
    "delete_flags": check_find_delete,
    "open_limit": check_open,
    "reuse_dir": check_reuse,
    "expire_dir": check_expiry_and_corruption,
    "write_hash_order": check_write_read,
    "header_crc": check_write_read,
    "cookie_limit": check_name_cookie,
}


def build_world():
    """A new World from the stock game.exe."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    return World(str(exe))


def main():
    """Run every check, or with --mutation-check prove each model defect is caught."""
    global mutation, TABLE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate model defect to fail")
    options = parser.parse_args()
    world = build_world()
    if options.mutation_check:
        for name, check in MUTATIONS.items():
            mutation = name
            TABLE = make_crypt_table()
            try:
                check(world)
            except Mismatch:
                print(f"caught: {name}")
            else:
                raise SystemExit(f"mutation {name} was not caught")
            finally:
                mutation = None
                TABLE = make_crypt_table()
        print(f"ok: {len(MUTATIONS)} model defects caught")
        return
    total = 0
    for check in CHECKS:
        count = check(world)
        total += count
    print(f"ok: {total} CACHE.CPP cases over {len(CHECKS)} checks")


if __name__ == "__main__":
    main()
