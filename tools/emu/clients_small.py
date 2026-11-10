# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the GAME\\Clients.cpp server client-record functions in stock game.exe 1.14d.

    uv run python clients_small.py [--mutation-check]

A client record is 0x518 bytes from the game pool (".\\GAME\\Clients.cpp", line 0x347): +0 id,
+4 a state dword (stored whole by FUN_005386d0, compared as a dword by FUN_00538930), +8 the class byte
and +0xc the raw type byte, +0xa flag word, +0xd name (16 bytes) and +0x1d second name,
+0x17c / +0x180 the uploaded save buffer and its byte count, +0x184 / +0x188 a state pair,
+0x1a8 the game, +0x1ac act, +0x1b4 area, +0x1b8 / +0x1bc packet queue head and tail
(0x208-byte nodes, next at +0x204), +0x3d4 flags, +0x484 / +0x488 two words, +0x4ac / +0x4b0
the links of the id table (256 buckets at 0x8842a8, id & 0xff) and the name table (256
buckets at 0x883ea8, the low byte of FUN_004112c0's hash). The tables are enabled by
0x8846d8; the id table is locked with the critical section 0x8846a8, the name table with
0x8846c0 (the oracle logs every Enter and Leave).

Runs game.exe's own code on random client sets and checks, against a model written from the
decompile and the disassembly: the return value (or the fatal source line), every byte of
the arena after the call (records, game, nodes, buffers), the ordered log of the hooked callees
(registers and stack arguments), the critical section calls and the callee-saved registers.
A fatal error (a call to 0x40_8a60 with a source line) is a "halt" with that line; the call site must lie in
the function under test or in a callee the case names (`fatal_from`), so a callee's fatal with the same line
is not taken for the function's own. 0x6cc837, the report's first argument, is an empty string: only the call site names the file.

The name functions run the stock hash 0x4112c0, the case-insensitive compare 0x413590 and the
copy 0x4135d0; the model carries its own versions (the hash reads the CRC table at 0x708140
from the image). The copy is a bounded copy that always ends with a NUL and never pads.

Hooked (their work is not claimed): 0x40_8a60 (fatal report), 0x53_7b50 (the area-change
fan-out, logged), 0x53_fc70 (logged), 0x62_0bb0 (a unit's room, scripted), 0x53_1e30 (the save
checksum, scripted), 0x66_57c0 (class map, scripted), the pool entries (allocation from the arena,
logged), GetTickCount (scripted); FUN_0053fe70 / 0053fea0 run as stock.

Covered: FUN_005381f0, the getter at 0x538230 (inside no Ghidra function), 005382b0, 005382e0, 00538590, 005385f0, 00538620, 00538630,
00538670, 005386d0, 00538700, 00538760, 00538790, 005387c0, 005387f0, 00538830, 00538860, 005388c0,
00538930, 00538b70, 00538bf0, 00538c60, 00538ce0, 00538e00, 00538e30, 00538f10, 00538f80,
00538fc0, 00539240, 005392d0, 00539a30.

`--mutation-check` breaks the model on purpose, one defect at a time, and requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
from _game_world import ANY, Arena, Rig, require, signed, verify

ENABLED, ID_TABLE, NAME_TABLE = 0x88_46D8, 0x88_42A8, 0x88_3EA8
ID_CS, NAME_CS = 0x88_46A8, 0x88_46C0
CRC_TABLE = 0x70_8140
CRT_ERROR = 0x77_90F4              # FUN_0041c340 stores the error code here before SetLastError
FILE = ".\\GAME\\Clients.cpp"
COVERED = (0x53_81F0, 0x53_8230, 0x53_82B0, 0x53_82E0, 0x53_8590, 0x53_85F0, 0x53_8620, 0x53_8630, 0x53_8670, 0x53_86D0,
           0x53_8700, 0x53_8760, 0x53_8790, 0x53_87C0, 0x53_87F0, 0x53_8830, 0x53_8860, 0x53_88C0, 0x53_8930, 0x53_8B70, 0x53_8BF0,
           0x53_8C60, 0x53_8CE0, 0x53_8E00, 0x53_8E30, 0x53_8F10, 0x53_8F80, 0x53_8FC0, 0x53_9240, 0x53_92D0, 0x53_9A30)
MUTATIONS = (
    "area-set-always-fans-out", "act-limit-6", "act-limit-4", "act-reads-next-byte", "act-null-ok",
    "class-reads-word", "class-sign-extended", "w4-setter-byte", "recv-unsigned-compare", "room-count-signed", "queue-tests-head", "mask-reads-dword", "flags-write-byte", "w484-w488-swapped", "name-offset-0xc",
    "room-scan-skips-last", "room-null-found", "bit2-set-always-notifies", "bit2-clear-wrong-mask",
    "bit2-read-bit3", "state-compares-id", "disabled-state-locks", "lookup-id-low-byte-only",
    "lookup-name-skips-copy", "lookup-copy-17", "lookup-disabled-zero", "name-case-sensitive", "name-compare-15",
    "name-zero-id-stops", "name-hash-unlowered", "recv-no-complete-flag", "recv-overflow-equal",
    "recv-alloc-per-chunk", "recv-alloc-only-without-buffer", "recv-offset-wrong", "recv-param-not-stored", "recv-not-found-one", "recv-disabled-one",
    "recv-hash-not-stored", "free-keeps-flag", "free-keeps-count", "free-skips-release", "start-state-4",
    "start-keeps-count", "ready-ignores-buffer", "ready-ignores-flag", "queue-first-not-head", "queue-next-not-cleared",
    "queue-tail-link-skipped", "create-skips-limit", "create-limit-9", "create-no-memset", "create-tick-18000",
    "create-id-table-tail", "create-name-table-skipped", "create-class-map-ignored", "create-name2-skipped",
    "create-count-not-bumped", "create-null-name2-no-error", "create-slots-not-reset", "create-p7-word", "create-game-list-skipped",
)
mutation = None
LINES = {"area": 0x79F, "act": 0x7DF, "act-set": 0x7EA, "act-range": 0x7EB, "id": 0x870, "class": 0x8B8, "w4": 0x973,
         "w484": 0x97E, "w488": 0x994, "w488r": 0x99F, "name": 0x9ED, "bit2": 0x9F7, "bit2r": 0xA05, "save": 0xB55,
         "size": 0xB5F, "free": 0xB7D, "start": 0xB91, "ready": 0xB9E, "area-get": 0x7AE, "overflow": 0xB2F,
         "info": 0x27, "game": 0x9AA}
CLIENT, GAME, INFO, STRS, SRC, ROOM, ROOMARR, NODES = 0x600, 0x3000, 0x5000, 0x5400, 0x5800, 0x5E00, 0x5F00, 0x6000
POOL = 0xB000


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Machine(Rig):
    """The rig with the callees of the client functions hooked."""

    def __init__(self):
        """Hook the fan-outs, the scripted callees, the allocator, the critical sections and the clock."""
        super().__init__()
        self.arena = None
        self.script = {"room": 0, "hash": 0, "class": 0, "tick": 0}
        lg = self.logger
        lg(0x53_7B50, 1, "area-change", regs=("esi",))
        lg(0x53_FC70, 0, "bit2-notify", regs=("ecx", "edx"))
        lg(0x62_0BB0, 1, "unit-room", ret=lambda c: self.script["room"], regs=())
        lg(0x53_1E30, 0, "save-hash", ret=lambda c: self.script["hash"], regs=("ecx",))
        lg(0x66_57C0, 0, "class-map", ret=lambda c: self.script["class"], regs=("ecx", "edx"))
        self.m.hook(mw.POOL_ALLOC, self.alloc, 3)
        for address, (name, _) in list(self.m.slots.items()):
            if name in ("EnterCriticalSection", "LeaveCriticalSection"):
                self.m.slots[address] = (name, self.cs(name))
            elif name == "GetTickCount":
                self.m.slots[address] = (name, lambda current: (self.script["tick"], 0))

    def cs(self, name):
        """A critical section import: log Enter/Leave with the section address."""
        tag = "cs-enter" if name.startswith("Enter") else "cs-leave"

        def handler(current):
            """Log the call and pop its one argument."""
            self.log.append((tag, current.arg(0)))
            return 0, 1
        return handler

    def alloc(self, current):
        """The pool allocator (ECX pool, EDX size, stack: file, line, flag): log it, return the next arena block."""
        regs = mw.REGISTERS
        self.log.append(("alloc", current.mu.reg_read(regs["ecx"]), current.mu.reg_read(regs["edx"]),
                         self.file_of(current.arg(0)), current.arg(1), current.arg(2)))
        return self.arena.hook_alloc(current.mu.reg_read(regs["edx"]))

    def write_global(self, address, value):
        """Write a game global (outside the arena)."""
        self.m.w32(address, value)


def lower(byte):
    """The C-locale tolower the hash and the compare use."""
    return byte + 0x20 if 0x41 <= byte <= 0x5A else byte


class Clients:
    """Client set-up in emulator memory and a model copy, plus the model of each covered function."""

    def __init__(self, rig, rng):
        """Build a random client set in the id and name tables, a game block and a room."""
        self.rig, self.rng, self.log, self.errno = rig, rng, [], 0
        self.a = a = Arena(rig, rng, size=0x10000, pool=POOL)
        rig.arena = a
        self.crc = struct.unpack("<256H", rig.m.read(CRC_TABLE, 512))
        self.enabled = 1 if rng.random() < 0.9 else 0
        rig.write_global(ENABLED, self.enabled)
        self.game = a.at(GAME)
        a.put(self.game + 0x1C, rng.getrandbits(8))
        a.put(self.game + 0x88, 0)
        a.put(self.game + 0x8C, rng.choice([0, 1, 7, 8, 9, 0xFFFFFFFF]))
        a.put(self.game + 0x1D28, a.at(INFO) if rng.random() < 0.8 else 0)
        a.put(a.at(INFO) + 8, rng.choice([0x100, 0, 0xFF, 0x1FF, 0x300]))
        a.put(a.at(INFO) + 0xC, rng.getrandbits(8), 1)
        self.idtab, self.nametab = [0] * 256, [0] * 256
        names = ["Alice", "aLiCe", "Bob", "BOB", "bob", "Carol", "", "Z" * 15, "Dave1", "dave1", "ABCDEFGHIJKLMNOP"]
        self.names = names
        self.clients = []
        for k in range(rng.randint(0, 6)):
            c = a.at(CLIENT * k)
            cid = rng.choice([rng.getrandbits(32), 0x100 * rng.randint(0, 3) + 5, 0, 0xFFFFFFFF, 0x105 + 0x100 * k])
            name = rng.choice(names)
            self.clients.append(c)
            a.put(c, cid)
            raw = name.encode() + b"\0"
            a.put_bytes(c + 0xD, raw.ljust(16, b"\0"))
            a.put_bytes(c + 0x1D, (name.encode() + b"\0").ljust(16, b"\0"))
            a.put(c + 0x1A8, self.game)
            a.put(c + 0x180, rng.choice([0, 0, 5, 20]))
            a.put(c + 0x17C, a.at(0x7000 + 0x100 * k) if rng.random() < 0.8 else 0)
            a.put(c + 0x1BC, 0)
            a.put(c + 0x1B8, 0)
            self.insert(c, cid, name)
        self.write_tables()
        a.sync_pool()

    def insert(self, c, cid, name):
        """Put a client at the head of its id bucket and its name bucket (as FUN_00539a30 does)."""
        a = self.a
        a.put(c + 0x4AC, self.idtab[cid & 0xFF])
        self.idtab[cid & 0xFF] = c
        # the 16-letter name sits in the bucket of a one-letter-different query: only the 16th compared letter tells them apart
        bucket = self.hash(b"ABCDEFGHIJKLMNOX" if name == "ABCDEFGHIJKLMNOP" else name.encode()) & 0xFF
        a.put(c + 0x4B0, self.nametab[bucket])
        self.nametab[bucket] = c

    def write_tables(self):
        """Mirror the model's two bucket arrays into the game globals."""
        self.rig.m.mu.mem_write(ID_TABLE, struct.pack("<256I", *self.idtab))
        self.rig.m.mu.mem_write(NAME_TABLE, struct.pack("<256I", *self.nametab))

    # --- model helpers ---------------------------------------------------------------------------
    def hash(self, text):
        """FUN_004112c0: the 16-bit name hash over the lowered bytes up to the first NUL."""
        value = 0xFFFF
        for byte in text:
            low = byte if mut("name-hash-unlowered") else lower(byte)
            if low == 0:
                break
            value = ((value >> 2) ^ self.crc[(low ^ value) & 0xFF]) & 0xFFFF
        return value

    def cstr(self, address):
        """The NUL-terminated string at an arena address (at most 0x40 bytes)."""
        out = bytearray()
        for i in range(0x40):
            byte = self.a.r8(address + i)
            if byte == 0:
                break
            out.append(byte)
        return bytes(out)

    def copy(self, dest, src, limit):
        """FUN_004135d0: copy at most limit-1 bytes of the string, then a NUL (nothing else is written)."""
        if not src:                                      # FUN_0041c340(0x57): SetLastError and the CRT error word
            self.errno = 0 if mut("create-null-name2-no-error") else 0x57
            return
        text = self.cstr(src)[:limit - 1]
        for i, byte in enumerate(text + b"\0"):
            self.a.w(dest + i, byte, 1)

    def equal_nocase(self, one, two):
        """FUN_00413590 with n = 16: the case-insensitive compare of two 16-byte fields is zero."""
        a = self.a
        for i in range(15 if mut("name-compare-15") else 16):
            x, y = a.r8(one + i), a.r8(two + i)
            if (x if mut("name-case-sensitive") else lower(x)) != (y if mut("name-case-sensitive") else lower(y)):
                return False
            if x == 0:
                return True
        return True

    def enter(self, section):
        """Log a critical section Enter."""
        self.log.append(("cs-enter", section))

    def leave(self, section):
        """Log a critical section Leave."""
        self.log.append(("cs-leave", section))

    def find_id(self, cid):
        """The id table walk shared by the lookups."""
        c = self.idtab[cid & 0xFF]
        while c:
            theirs = self.a.r32(c)
            if (theirs & 0xFF) == (cid & 0xFF) if mut("lookup-id-low-byte-only") else theirs == cid:
                return c
            c = self.a.r32(c + 0x4AC)
        return 0

    # --- the lookups -----------------------------------------------------------------------------
    def has_state(self, cid, state):
        """FUN_00538930: whether the client with this id has [+4] == state (0 when the table is off)."""
        if not self.enabled and not mut("disabled-state-locks"):
            return 0
        self.enter(ID_CS)
        c = self.find_id(cid)
        result = 0
        if c:
            result = 1 if self.a.r32(c + (0 if mut("state-compares-id") else 4)) == state else 0
        self.leave(ID_CS)
        return result

    def name_of_id(self, cid, dest):
        """FUN_00538b70: 0 and the 16-byte name copied to dest (when non-null) for a found id, 1 otherwise."""
        if not self.enabled and not mut("lookup-disabled-zero"):
            return 1
        if not self.enabled:
            return 0
        self.enter(ID_CS)
        c = self.find_id(cid)
        result = 1
        if c:
            if dest:
                self.copy(dest, c + (0xC if mut("name-offset-0xc") else 0xD), 17 if mut("lookup-copy-17") else 16)
            result = 0
        self.leave(ID_CS)
        return result

    def id_of_name(self, name):
        """FUN_00538bf0: the id of the first client in the name bucket whose name matches and whose id is nonzero."""
        found = 0
        self.enter(NAME_CS)
        c = self.nametab[self.hash(self.cstr(name)) & 0xFF]
        while c and (found == 0 or mut("name-zero-id-stops")):
            if self.equal_nocase(c + 0xD, name):
                found = self.a.r32(c)
            c = self.a.r32(c + 0x4B0)
        self.leave(NAME_CS)
        return found

    def canonical_name(self, name, dest):
        """FUN_00538c60: 0 and the record's 16-byte name copied to dest for a found name, 1 otherwise."""
        self.enter(NAME_CS)
        c = self.nametab[self.hash(self.cstr(name)) & 0xFF]
        result = 1
        while c:
            if self.equal_nocase(c + 0xD, name):
                if dest and not mut("lookup-name-skips-copy"):
                    self.copy(dest, c + 0xD, 16)
                result = 0
                break
            c = self.a.r32(c + 0x4B0)
        self.leave(NAME_CS)
        return result

    # --- the upload buffer ------------------------------------------------------------------------
    def receive(self, cid, src, length, total, tag):
        """FUN_00538ce0: append a chunk to the client's save buffer; 1 when the buffer is complete."""
        a = self.a
        if not self.enabled:
            return ("ret", 1 if mut("recv-disabled-one") else 0)
        self.enter(ID_CS)
        c = self.find_id(cid)
        result = ("ret", 1 if mut("recv-not-found-one") else 0)
        if c:
            count = a.r32(c + 0x180)
            if (a.r32(c + 0x17C) == 0 if mut("recv-alloc-only-without-buffer") else count == 0) or mut("recv-alloc-per-chunk"):
                pool = a.r32(a.r32(c + 0x1A8) + 0x1C)
                buf = a.model_alloc(total)
                self.log.append(("alloc", pool, total & 0xFFFFFFFF, FILE, 0xB2A, 0))
                a.w(c + 0x17C, buf)
                dest = buf
            else:
                dest = a.r32(c + 0x17C) + (count if not mut("recv-offset-wrong") else count + 1)
            end = count + length
            order = (lambda value: value & 0xFFFFFFFF) if mut("recv-unsigned-compare") else signed      # `cmp edx, ebx; jle`
            if order(end) > order(total) or (mut("recv-overflow-equal") and order(end) == order(total)):
                return ("halt", LINES["overflow"])
            for i in range(length):
                a.w(dest + i, a.r8(src + i), 1)
            a.w(c + 0x180, end)
            if not mut("recv-param-not-stored"):
                a.w(c + 0x178, tag)
            if end == total:
                if not mut("recv-no-complete-flag"):
                    a.w(c + 0x3D4, a.r32(c + 0x3D4) | 8)
                self.log.append(("save-hash", a.r32(c + 0x17C)))
                if not mut("recv-hash-not-stored"):
                    a.w(c + 0x18C, self.rig.script["hash"])
                result = ("ret", 1)
        self.leave(ID_CS)
        return result

    # --- creation ---------------------------------------------------------------------------------
    def create(self, game, cid, kind, name, name2, p6, p7):
        """FUN_00539a30: build a client record and link it into the game list, the id table and the name table."""
        a = self.a
        if not self.enabled or not game:
            return ("ret", 0)
        count = a.r32(game + 0x8C)
        if signed(count) >= (9 if mut("create-limit-9") else 8) and not mut("create-skips-limit"):
            return ("ret", 0)
        pool = a.r32(game + 0x1C)
        c = a.model_alloc(0x518)
        self.log.append(("alloc", pool, 0x518, FILE, 0x347, 0))
        if not mut("create-no-memset"):
            for i in range(0x518):
                a.w(c + i, 0, 1)
        a.w(c, cid)
        a.w(c + 0x3D8, (self.rig.script["tick"] + (18000 if mut("create-tick-18000") else 0x2BF20)))
        a.w(c + 0x1A8, game)
        info = a.r32(game + 0x1D28)
        if not info:
            return ("halt", LINES["info"])
        if (a.r32(info + 8) >> 8) & 1:
            a.w(c + 0xC, kind, 1)
            mapped = self.rig.script["class"] & 0xFF
            self.log.append(("class-map", kind & 0xFF, a.r8(info + 0xC)))
            a.w(c + 8, kind if mut("create-class-map-ignored") else mapped, 1)
        else:
            a.w(c + 8, kind, 1)
        self.copy(c + 0xD, name, 0x7FFFFFFF)
        if not mut("create-name2-skipped"):
            self.copy(c + 0x1D, name2, 0x7FFFFFFF)
        a.w(c + 0x60, p6)
        a.w(c + 0x68, 0)
        a.w(c + 0x50C, p7 & (0xFFFF if mut("create-p7-word") else 0xFF))
        a.w(c + 4, 0)
        for off in (0x1B8, 0x1BC, 0x1C0):
            a.w(c + off, 0)
        if not mut("create-slots-not-reset"):
            for i in range(16):
                a.w(c + 0x3DC + 8 * i, 0xFFFF, 2)
                a.w(c + 0x3DE + 8 * i, 0, 1)
                a.w(c + 0x3E0 + 8 * i, 0xFFFFFFFF)
        if not mut("create-game-list-skipped"):
            a.w(c + 0x4A8, a.r32(game + 0x88))
            a.w(game + 0x88, c)
        self.enter(ID_CS)
        a.w(c + 0x4AC, self.idtab[cid & 0xFF])
        if not mut("create-id-table-tail"):
            self.idtab[cid & 0xFF] = c
        self.leave(ID_CS)
        bucket = self.hash(self.cstr(name)) & 0xFF
        self.enter(NAME_CS)
        a.w(c + 0x4B0, self.nametab[bucket])
        if not mut("create-name-table-skipped"):
            self.nametab[bucket] = c
        self.leave(NAME_CS)
        if not mut("create-count-not-bumped"):
            a.w(game + 0x8C, count + 1)
        return ("ret", c)


def new_string(fx, rng, slot):
    """A query string in the arena strings region; returns its address."""
    text = rng.choice(fx.names + [n.upper() for n in fx.names] + ["Nobody", "ALICE", "Alice1", "x" * 20, "abcdefghijklmnop", "ABCDEFGHIJKLMNOX", "ABCDEFGHIJKLMNOX"])
    address = fx.a.at(STRS + 0x20 * slot)
    fx.a.put_bytes(address, text.encode() + b"\0")
    return address


def check_fields(rig, rng):
    """Plain field accessors: FUN_005382b0, 005382e0, 00538590, 005385f0, 00538620, 00538630, 00538670, 005386d0, 00538700,
    00538760, 00538790, 00538830, 00538e00, 00538e30, 005392d0, 00538230."""
    fx = Clients(rig, rng)
    a, count = fx.a, 0
    for c in fx.clients + [0]:
        for fn, off, width, line in (
                (0x53_82B0, 0x1AC, 1, "act"), (0x53_8590, 0, 4, "id"), (0x53_85F0, 8, 1, "class"),
                (0x53_8790, 0x488, 4, "w488r"), (0x53_8E00, 0x17C, 4, "save"), (0x53_8E30, 0x180, 4, "size"),
                (0x53_8230, 0x1B4, 4, "area-get"), (0x53_87C0, 0x1A8, 4, "game"), (0x53_92D0, 0x1BC, 4, None)):
            if not c:
                if line is None:
                    continue
                want = ("halt", LINES[line])
            else:
                if fn == 0x53_82B0 and mut("act-reads-next-byte"):
                    off += 1
                if fn == 0x53_85F0 and mut("class-reads-word"):
                    width = 2
                value = a.r32(c + off) if width == 4 else a.r16(c + off) if width == 2 else a.r8(c + off)
                if fn == 0x53_85F0 and mut("class-sign-extended") and value & 0x80:
                    value |= 0xFFFFFF00
                want = ("ret", value)
            # FUN_005382b0 is `mov al, [ecx+0x1ac]`: the rest of EAX is undefined, so only AL is compared; FUN_005385f0
            # is `movzx eax, byte [ecx+8]` and its whole EAX is compared
            count += verify(rig, fx, "read", fn, want, ecx=c, mask=0xFF if fn == 0x53_82B0 else None)
        if c:
            count += verify(rig, fx, "name", 0x53_8830, ("ret", c + (0xC if mut("name-offset-0xc") else 0xD)), ecx=c)
            count += verify(rig, fx, "mask", 0x53_8670, ("ret", (a.r16(c + 0xA) & 0xFFFF) if not mut("mask-reads-dword") else a.r32(c + 0xA) & 0xFFFFFFFF), ecx=c, edx=0xFFFF)
        else:
            count += verify(rig, fx, "name", 0x53_8830, ("halt", LINES["name"]), ecx=c)
        for edx in (0, 5, 0x8004, 0xABCD1234):
            if c:
                mask = edx & 0xFFFF
                count += verify(rig, fx, "mask", 0x53_8670, ("ret", (a.r16(c + 0xA) & mask) if not mut("mask-reads-dword") else a.r32(c + 0xA) & mask), ecx=c, edx=edx)
                # setters without a null check: 00538620 (byte +8), 00538630 (word +0xa)
                a.w(c + 8, edx & 0xFF, 1)
                count += verify(rig, fx, "set-8", 0x53_8620, ("ret", ANY), ecx=c, edx=edx)
                a.w(c + 0xA, edx & (0xFF if mut("flags-write-byte") else 0xFFFF), 2)
                count += verify(rig, fx, "set-a", 0x53_8630, ("ret", ANY), ecx=c, edx=edx)
            # setters with a null check
            for fn, off, line, tweak in ((0x53_86D0, 4, "w4", "w4-setter-byte"), (0x53_8700, 0x484, "w484", "w484-w488-swapped"),
                                         (0x53_8760, 0x488, "w488", "w484-w488-swapped")):
                if not c:
                    want = ("halt", LINES[line])
                else:
                    target = off
                    if tweak == "w484-w488-swapped" and mut(tweak) and off in (0x484, 0x488):
                        target = 0x484 + 0x488 - off
                    a.w(c + target, edx, 1 if tweak == "w4-setter-byte" and mut(tweak) else 4)
                    want = ("ret", ANY)
                count += verify(rig, fx, "set", fn, want, ecx=c, edx=edx)
            # act setter
            for act in (0, 3, 4, 5, 6, 0x80, 0xFF, 0x105):
                limit = 6 if mut("act-limit-6") else 4 if mut("act-limit-4") else 5
                if not c:
                    want = ("halt", LINES["act-set"] if not mut("act-null-ok") else LINES["act-range"])
                    if mut("act-null-ok"):
                        want = ("ret", ANY)
                elif (act & 0xFF) >= limit:
                    want = ("halt", LINES["act-range"])
                else:
                    a.w(c + 0x1AC, act & 0xFF, 1)
                    want = ("ret", ANY)
                count += verify(rig, fx, "set-act", 0x53_82E0, want, ecx=c, edx=act)
    return count


def check_area_and_room(rig, rng):
    """FUN_005381f0 (set the area, fan out on a change) and FUN_005387f0 (is the client in the unit's room)."""
    fx = Clients(rig, rng)
    a, count = fx.a, 0
    for c in fx.clients + [0]:
        for new in (0, 1, 5, None, None, 0xFFFFFFFF):
            if new is None:
                new = a.r32(c + 0x1B4) if c else 0          # the same area again: no fan-out
            if not c:
                want = ("halt", LINES["area"])
            else:
                if new != a.r32(c + 0x1B4) or mut("area-set-always-fans-out"):
                    fx.log.append(("area-change", c, new))
                a.w(c + 0x1B4, new)
                want = ("ret", ANY)
            count += verify(rig, fx, "area", 0x53_81F0, want, ecx=c, edx=new)
        # the room scan
        # the count is an unsigned dword (`jbe` / `jb`): the huge ones only stop on a hit in the first entries
        for n in (0, 1, 3, 0x80000000, 0xFFFFFFFF):
            for target in (c, 0x1234, a.at(0x7F00)):
                room = a.at(ROOM)
                a.put(room + 0x48, a.at(ROOMARR))
                a.put(room + 0x78, n)
                entries = min(n, 3)
                for i in range(entries):
                    a.put(a.at(ROOMARR) + 4 * i, (c if (i == entries - 1 and c) else rng.getrandbits(32)))
                if n > 3 and (not c or target != c):
                    continue                                 # a miss would scan on past the arena
                unit = rng.getrandbits(32)
                rig.script["room"] = room
                fx.log.append(("unit-room", unit))
                hit = 0
                limit = signed(n) if mut("room-count-signed") else n
                for i in range(limit - 1 if mut("room-scan-skips-last") else limit):
                    if a.r32(a.at(ROOMARR) + 4 * i) == target:
                        hit = 1
                        break
                count += verify(rig, fx, "room", 0x53_87F0, ("ret", hit), ecx=unit, edx=target)
        rig.script["room"] = 0
        fx.log.append(("unit-room", 7))
        count += verify(rig, fx, "room-none", 0x53_87F0, ("ret", 1 if mut("room-null-found") else 0), ecx=7, edx=c)
    return count


def check_flags(rig, rng):
    """FUN_00538860 / 005388c0 (bit 2 of +0x3d4), 00538f10 / 00538f80 / 00538fc0 (upload buffer state)."""
    fx = Clients(rig, rng)
    a, count = fx.a, 0
    for c in fx.clients + [0]:
        for state in (1, 0, 5, 0x80000000, 0):
            if not c:
                want = ("halt", LINES["bit2"])
            else:
                flags = a.r32(c + 0x3D4)
                if state:
                    if mut("bit2-set-always-notifies") or not flags & 4:
                        fx.log.append(("bit2-notify", a.r32(c + 0x1A8), c))
                    a.w(c + 0x3D4, flags | 4)
                else:
                    a.w(c + 0x3D4, flags & (0xFFFFFFFB if not mut("bit2-clear-wrong-mask") else 0xFFFFFFF3))
                want = ("ret", ANY)
            count += verify(rig, fx, "bit2", 0x53_8860, want, ecx=c, edx=state)
            want = ("halt", LINES["bit2r"]) if not c else ("ret", (a.r8(c + 0x3D4) >> (3 if mut("bit2-read-bit3") else 2)) & 1)
            count += verify(rig, fx, "bit2-get", 0x53_88C0, want, ecx=c)
        # upload buffer: ready, start, free
        for _ in range(2):
            if c:
                a.put(c + 0x3D4, a.r32(c + 0x3D4) ^ (rng.getrandbits(32) & 0x10))
                a.put(c + 0x17C, rng.choice([0, a.at(0x7000)]))
            ready = ("halt", LINES["ready"]) if not c else (
                "ret", 1 if (((a.r32(c + 0x3D4) & 0x10) or mut("ready-ignores-flag")) and (a.r32(c + 0x17C) or mut("ready-ignores-buffer"))) else 0)
            count += verify(rig, fx, "ready", 0x53_8FC0, ready, ecx=c)
            if not c:
                want = ("halt", LINES["start"])
            else:
                a.w(c + 0x3D4, a.r32(c + 0x3D4) | 0x10)
                a.w(c + 0x184, 4 if mut("start-state-4") else 5)
                if not mut("start-keeps-count"):
                    a.w(c + 0x188, 0)
                want = ("ret", ANY)
            count += verify(rig, fx, "start", 0x53_8F80, want, ecx=c)
            if not c:
                want = ("halt", LINES["free"])
            else:
                buf = a.r32(c + 0x17C)
                if buf:
                    pool = a.r32(a.r32(c + 0x1A8) + 0x1C)
                    if not mut("free-skips-release"):
                        fx.log.append(("free", pool, buf, FILE, 0xB80, 0))
                    a.w(c + 0x17C, 0)
                if not mut("free-keeps-flag"):
                    a.w(c + 0x3D4, a.r32(c + 0x3D4) & 0xFFFFFFEF)
                if not mut("free-keeps-count"):
                    a.w(c + 0x180, 0)
                a.w(c + 0x188, 0)
                a.w(c + 0x184, 0)
                want = ("ret", ANY)
            count += verify(rig, fx, "free", 0x53_8F10, want, ecx=c)
            if c:
                a.put(c + 0x17C, rng.choice([0, a.at(0x7000 + 0x100)]))
    return count


def check_queue(rig, rng):
    """FUN_00539240 (append a packet node to the client's queue) and FUN_005392d0 (the tail)."""
    fx = Clients(rig, rng)
    a, count = fx.a, 0
    for c in fx.clients:
        # the game tests the tail (`mov eax, [ecx+0x1bc]` at 0x53924a), never the head: all four head/tail states
        for head, tail in ((0, 0), (a.at(NODES + 0x210 * 6), 0), (0, a.at(NODES + 0x210 * 7)), (a.at(NODES + 0x210 * 6), a.at(NODES + 0x210 * 7))):
            a.put(c + 0x1B8, head)
            a.put(c + 0x1BC, tail)
            for k in range(4):
                node = a.at(NODES + 0x210 * k)
                a.put(node + 0x204, 0x1234)                      # a stale link the append must clear
                tail = a.r32(c + 0x1BC)
                if not mut("queue-next-not-cleared"):
                    a.w(node + 0x204, 0)
                if (a.r32(c + 0x1B8) == 0) if mut("queue-tests-head") else (tail == 0):
                    a.w(c + 0x1B8, node)
                    if mut("queue-first-not-head"):
                        a.w(c + 0x1B8, 0)
                elif not mut("queue-tail-link-skipped"):
                    a.w(tail + 0x204, node)
                a.w(c + 0x1BC, node)
                count += verify(rig, fx, "queue", 0x53_9240, ("ret", ANY), ecx=c, edx=node)
                count += verify(rig, fx, "tail", 0x53_92D0, ("ret", a.r32(c + 0x1BC)), ecx=c)
    return count


def check_lookups(rig, rng):
    """FUN_00538930, 00538b70, 00538bf0, 00538c60: id and name table lookups with their critical sections."""
    fx = Clients(rig, rng)
    a, count = fx.a, 0
    ids = {0, 0xFFFFFFFF, 0x1234, 0x105, 0x205, 0x305, 5} | {a.r32(c) for c in fx.clients} | {(a.r32(c) & 0xFF) | 0x7700 for c in fx.clients}
    for cid in sorted(ids):
        for state in (0, 1, a.r32(fx.find_id(cid) + 4) if fx.find_id(cid) else 9):
            count += verify(rig, fx, "state", 0x53_8930, ("ret", fx.has_state(cid, state)), ecx=cid, edx=state)
        for dest in (0, a.at(0x7E00)):
            if dest:
                a.put_bytes(dest, b"\xee" * 24)
            want = ("ret", fx.name_of_id(cid, dest))
            count += verify(rig, fx, "name-of-id", 0x53_8B70, want, ecx=cid, edx=dest)
    for slot in range(8):
        name = new_string(fx, rng, slot)
        want = ("ret", fx.id_of_name(name))
        count += verify(rig, fx, "id-of-name", 0x53_8BF0, want, ecx=name)
        for dest in (0, a.at(0x7E40)):
            if dest:
                a.put_bytes(dest, b"\xee" * 24)
            want = ("ret", fx.canonical_name(name, dest))
            count += verify(rig, fx, "canonical", 0x53_8C60, want, ecx=name, edx=dest)
    return count


def check_receive(rig, rng):
    """FUN_00538ce0: the save upload, chunk by chunk, with its allocation, completion and overflow rules."""
    fx = Clients(rig, rng)
    a, count = fx.a, 0
    if not fx.clients:
        return 0
    for c in fx.clients:
        a.put(c + 0x180, 0)
    src = a.at(SRC)
    a.put_bytes(src, rng.randbytes(0x400))
    c = fx.clients[0]
    cid = a.r32(c)
    if fx.find_id(cid) != c:
        return 0
    rig.script["hash"] = rng.getrandbits(32)
    total = rng.choice([8, 0x20, 0x61])
    done = 0
    while done < total + 4:
        length = rng.choice([1, 4, 7, 16, total - done if total > done else 3])
        tag = rng.getrandbits(32)
        count += verify(rig, fx, "recv", 0x53_8CE0, fx.receive(cid, src + done, length, total, tag),
                        ecx=cid, edx=src + done, args=(length, total, tag, rng.getrandbits(32), rng.getrandbits(32)))
        done += length
        if a.r32(c + 0x180) >= total:
            break
    for bad in (0xFFFFFFFE, 0x7777):
        count += verify(rig, fx, "recv-absent", 0x53_8CE0, fx.receive(bad, src, 4, 8, 1), ecx=bad, edx=src, args=(4, 8, 1, 0, 0))
    return count


def check_receive_exact(rig, rng):
    """FUN_00538ce0 with the argument layout fixed: ECX id, EDX source, stack length, total, tag, two unused words."""
    fx = Clients(rig, rng)
    a, count = fx.a, 0
    if not fx.clients:
        return 0
    c = fx.clients[0]
    cid = a.r32(c)
    if fx.find_id(cid) != c:
        return 0
    src = a.at(SRC)
    a.put_bytes(src, rng.randbytes(0x100))
    rig.script["hash"] = rng.getrandbits(32)
    for tag in (0, 7, 0xFFFFFFFF):
        a.put(c + 0x180, 0)
        a.put(c + 0x3D4, 0)
        fx.log = []
        count += verify(rig, fx, "recv-whole", 0x53_8CE0, fx.receive(cid, src, 0x20, 0x20, tag), ecx=cid, edx=src,
                        args=(0x20, 0x20, tag, rng.getrandbits(32), rng.getrandbits(32)))
    # bugs.md 95: the buffer is allocated whenever the count is 0, so zero-length chunks leak a block each
    a.put(c + 0x180, 0)
    a.put(c + 0x3D4, 0)
    fx.log = []
    for _ in range(3):
        count += verify(rig, fx, "recv-empty", 0x53_8CE0, fx.receive(cid, src, 0, 0x20, 1), ecx=cid, edx=src, args=(0, 0x20, 1, 0, 0))
    count += verify(rig, fx, "recv-total-zero", 0x53_8CE0, fx.receive(cid, src, 0, 0, 2), ecx=cid, edx=src, args=(0, 0, 2, 0, 0))
    a.put(c + 0x180, 0)
    # FUN_00538ce0 compares `count + length > total` as signed (`jle`): total 0x80000000 is negative, so even a
    # chunk of length 0 at count 0 is the overflow fatal; an unsigned compare would accept it and store nothing
    a.put(c + 0x180, 0)
    count += verify(rig, fx, "recv-total-negative", 0x53_8CE0, fx.receive(cid, src, 0, 0x80000000, 3), ecx=cid, edx=src,
                    args=(0, 0x80000000, 3, 0, 0))
    a.put(c + 0x180, 0)
    count += verify(rig, fx, "recv-over", 0x53_8CE0, fx.receive(cid, src, 0x21, 0x20, 1), ecx=cid, edx=src, args=(0x21, 0x20, 1, 0, 0))
    return count


def check_create(rig, rng):
    """FUN_00539a30: the client constructor, with its limits, class mapping and links."""
    fx = Clients(rig, rng)
    a, count = fx.a, 0
    for _ in range(5):
        cid = rng.choice([rng.getrandbits(32), 0x105, 0, 0xFFFFFFFF, 0x1234])
        kind = rng.choice([0, 1, 5, 0x7F, 0xFF, 0x1FF])
        name, name2 = new_string(fx, rng, 9), new_string(fx, rng, 10)
        a.put_bytes(name2, b"Second\0")
        rig.script["tick"] = rng.getrandbits(31)
        rig.script["class"] = rng.getrandbits(8) | rng.getrandbits(24) << 8
        p6, p7 = rng.getrandbits(32), rng.getrandbits(32)
        a.put(fx.game + 0x8C, rng.choice([0, 3, 7, 8, 9, 0xFFFFFFFF, 0x80000000]))
        a.put(a.at(INFO) + 8, rng.choice([0x100, 0, 0x1FF, 0xFF]))
        a.put(fx.game + 0x1D28, a.at(INFO) if rng.random() < 0.85 else 0)
        if rng.random() < 0.3:
            name2 = 0                                    # the host's own client passes no second name
        rig.m.w32(CRT_ERROR, 0)
        fx.errno = 0
        want = fx.create(fx.game, cid, kind, name, name2, p6, p7)
        count += verify(rig, fx, "create", 0x53_9A30, want, ecx=fx.game, edx=cid, args=(kind, name, name2, p6, p7, 0, 0),
                        fatal_from=(0x53_FE70,))           # the missing game info is FUN_0053fe70's fatal (line 0x27)
        require(rig.m.r32(CRT_ERROR) == fx.errno, f"create: CRT error word {rig.m.r32(CRT_ERROR):#x}, model {fx.errno:#x}")
    want = fx.create(0, 5, 1, name, name2, 1, 1)
    count += verify(rig, fx, "create-null-game", 0x53_9A30, want, ecx=0, edx=5, args=(1, name, name2, 1, 1, 0, 0))
    return count


def run_all():
    """All check rounds on fresh random client sets; the number of calls checked."""
    rig = Machine()
    count = 0
    for seed in range(1, 100):
        rng = random.Random(seed * 104729)
        count += (check_fields(rig, rng) + check_area_and_room(rig, rng) + check_flags(rig, rng) + check_queue(rig, rng)
                  + check_lookups(rig, rng) + check_receive(rig, rng) + check_receive_exact(rig, rng) + check_create(rig, rng))
    require(rig.called == set(COVERED), f"covered set differs: {sorted(hex(x) for x in set(COVERED) ^ rig.called)}")
    return count


def main():
    """Command line: run the checks, or with --mutation-check require every defect to be caught."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "CLIENTS record, lookup, upload and constructor calls checked")


if __name__ == "__main__":
    main()
