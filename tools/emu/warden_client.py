# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the Warden client manager (WardenClient.cpp) in stock game.exe 1.14d.

Run: uv run python warden_client.py [--mutation-check]

The functions FUN_0047fa20 ... FUN_004803c0 run for real in the emulator against
a Python model: the module signature check (SHA-1 of the data plus a salt, an RSA
"NGIS" trailer), the load pipeline FUN_0047fc70 (copy, RC4, signature, zlib, image
loader, entry lookup), the load-by-name and cache helpers, the seven callbacks of
the table at 0x6d8e68, and the module life cycle (start, unload, packet, tick,
shutdown, init). FUN_00478830 (C->S 0x66 framing) runs for real in a third machine.

SAFETY: no real Warden module (Blizzard's built-in one or one a server sends) is
loaded, decrypted or run. Every module here is a synthetic byte string packed by
this file (the manager does RC4 and inflate them, as bytes); the image loader FUN_004e0730, its entry
lookup FUN_004e0700 and its destructor FUN_004e06a0 are hooked (they log their
arguments), so nothing is mapped or executed. The module "code" a started module
calls is a set of Python stubs. The built-in module (data 0x6d74b0, key 0x6d8778) is
never read: in the manager machine FUN_0047fc70 is hooked and only its arguments
are checked. The signature check runs with a self-generated test RSA key written over
the public key in the emulated image (the shipped key is also tried against forged
trailers). The cache API (bncache.dat) and the network send are scripted; no socket
and no server is involved.

Hooked: Storm alloc FUN_00413020 and free FUN_00412650 (logged), the fatal report
and exit, FUN_004e06a0, FUN_004e0730, FUN_004e0700, GetSystemTimeAsFileTime, FUN_00478350
(the packet sender), and in the manager machine FUN_0047fc70. The SHA-1, RC4, zlib,
bignum, sprintf, strupr and memcpy callees run for real.

`--mutation-check` plants deliberate defects in the model and requires each one to be caught.
"""
import argparse
import hashlib
import random
import struct
import zlib
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
LIMIT = 300_000_000

ALLOC, FREE, REALLOC = 0x413020, 0x412650, 0x4133D0
FATAL, FATAL_EXIT = 0x408A60, 0x681E09
DTOR, LOADER, ENTRY = 0x4E06A0, 0x4E0730, 0x4E0700
SEND_PACKET, SEND_WRAP = 0x478350, 0x478830
LOAD_MODULE, LOAD_BY_NAME = 0x47FC70, 0x47FEE0
GET_API = 0x518C10

SAVED, SAVED_SIZE = 0x7BC988, 0x7BC98C
RUNNING, PENDING, API = 0x7BC990, 0x7BC994, 0x7BC998
SEED, TABLE_PTR = 0x721C94, 0x721C98
CALLBACKS = 0x6D8E68
WARDEN_FILE = 0x6D8E18
BUILTIN_NAME, BUILTIN_KEY, BUILTIN_DATA, BUILTIN_SIZE = 0x6D8768, 0x6D8778, 0x6D74B0, 0x12B2
RSA_E, RSA_N = 0x6D8D14, 0x6D8D18
VTABLE_BASE = 0x6D8E38

OBFUSCATED_SALT = bytes([0x5F, 0x7B, 0x53, 0xE7, 0xF2, 0x5A, 0xBF, 0x75, 0x8C, 0x00])
FIXED_FILETIME = 0x01DC_0000_1234_5678

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


# ---- models ---------------------------------------------------------------------

def salt_text():
    """The salt FUN_0047fc70 builds on its stack: each byte times 0xd3, low byte kept."""
    return bytes((b * 0xD3) & 0xFF for b in OBFUSCATED_SALT).split(b"\0")[0]


def rc4(key, data):
    """Plain RC4 (FUN_004e09b0 key setup, FUN_0047f8c0 keystream xor)."""
    if mut("rc4_key_len"):
        key = key[:15]
    state, j = list(range(256)), 0
    for i in range(256):
        j = (j + state[i] + key[i % len(key)]) & 0xFF
        state[i], state[j] = state[j], state[i]
    out, i, j = bytearray(), 0, 0
    for byte in data:
        i = (i + 1) & 0xFF
        j = (j + state[i]) & 0xFF
        state[i], state[j] = state[j], state[i]
        out.append(byte ^ state[(state[i] + state[j]) & 0xFF])
    return bytes(out)


def digest(data, skip, salt):
    """FUN_0047f990: SHA-1 over data[skip:], data[:skip], then the upper-cased salt."""
    body = data if (skip == 0 or mut("no_skip")) else data[skip:] + data[:skip]
    text = salt.lower() if mut("salt_case") else salt.upper()
    return hashlib.sha1(body + text).digest()


def padded_block(digest_bytes):
    """The 0x100-byte expected RSA value: digest, 0xbb filler, final byte 0x0b."""
    return digest_bytes + bytes([0xBA if mut("pad_byte") else 0xBB]) * (0x100 - 21) + bytes([0x0C if mut("end_byte") else 0x0B])


def order():
    """Byte order of the emulated bignum import/export (found in the trace: little)."""
    return "big" if mut("endian") else "little"


def is_prime(n, rng):
    """Miller-Rabin with 24 fixed-seed rounds (test key generation only)."""
    if n % 2 == 0:
        return False
    d, r = n - 1, 0
    while d % 2 == 0:
        d //= 2
        r += 1
    for _ in range(24):
        x = pow(rng.randrange(2, n - 1), d, n)
        if x in (1, n - 1):
            continue
        for _ in range(r - 1):
            x = x * x % n
            if x == n - 1:
                break
        else:
            return False
    return True


TEST_P = 0xf5c775b3bf912631ccf34a81c9c5bda5960d2cabd09912f8129580e2a800ad22bd5d413003b12299fc4bf7a4dc5d58744b7461509eeb48af097dc8db5f7bf8722801869cb1b9f5d68f0ea2bda573c76e3bb30f266eca2d2b2cc63bf13236d291ba7528cb45de76c82f7f176c2d88c5b358645f1800d265fdb28049bef1715c67
TEST_Q = 0xc69e9e58722d50646808e32efddc79a217308f81fcc0123cd49338f7c3cbfb67cca2b1d1275cdcb2e83d0616819e449595f419ad7db9a7d9732d3d2bb644738e716b3c5bb1ff232d8d2cbc55f522147c2e156cc353474425f5dd5208330edd8eecd2a95937382f0f61ae4742981b2ae594933db4d8c2a83910b0439a0564f0f7


def test_key():
    """A 2048-bit RSA key (e = 65537) from two primes found with random.Random(0x57A2DE) for this file; not any real Warden key."""
    return TEST_P * TEST_Q, 65537, pow(65537, -1, (TEST_P - 1) * (TEST_Q - 1))


def sign(plain, skip, salt, key):
    """The 'NGIS' trailer: marker plus RSA-private of the padded digest block."""
    n, _, d = key
    block = int.from_bytes(padded_block(digest(plain, skip, salt)), "little")
    return b"NGIS" + pow(block, d, n).to_bytes(0x100, "little")


def verify_model(data, skip, salt, key):
    """What FUN_0047fa20 should answer: 1 for a good trailer, 0 otherwise; None when it must abort."""
    length = len(data)
    if length < skip:
        return None
    if length < (0x105 if mut("short_len") else 0x104):
        return 0
    marker = b"SIGN" if mut("marker") else b"NGIS"
    if data[length - 0x104:length - 0x100] != marker:
        return 0
    n, e, _ = key
    sig = int.from_bytes(data[length - 0x100:], order())
    want = padded_block(digest(data[:length - 0x104], skip, salt))
    got = pow(sig, e, n).to_bytes(0x100, order()) if sig < (1 << 2048) else b""
    return int(got == want)


def pack_module(payload, key, rc4_key, declared=None, tamper=None, skip_sign=False):
    """A synthetic encrypted module: size dword, zlib stream, 'NGIS' trailer, RC4 over the lot."""
    plain = struct.pack("<I", len(payload) if declared is None else declared) + zlib.compress(payload)
    signed = plain + sign(plain, 0, salt_text(), key)
    if tamper is not None:
        signed = tamper(bytearray(signed))
    return rc4(rc4_key, bytes(signed))


def zlib_model(stream, declared):
    """What the uncompress call yields: the bytes, or None when it fails (the trailer follows the stream)."""
    try:
        decoder = zlib.decompressobj()
        out = decoder.decompress(stream, declared + 1)
    except zlib.error:
        return None
    return out if decoder.eof and len(out) <= declared else None


# ---- the emulated world ----------------------------------------------------------

class World:
    """One emulated game.exe with WardenClient's callees hooked and logged."""

    def __init__(self, exe, manager=False):
        """Build the machine; manager=True hooks FUN_0047fc70 too (the built-in module is never decrypted)."""
        machine = self.m = emu.Emu(exe)
        machine.crt_init()                      # the CRT heap zlib's allocator needs
        self.events, self.labels, self.sizes = [], {}, {}
        self.fatals, self.exited = [], False
        self.sent = []
        self.loader_ok = True
        self.entry = 0
        self.null_alloc_line = None
        self.api_reads, self.api_writes, self.api_frees = [], [], []
        self.cache = {}
        self.write_ret = 1
        self.load_ret = 1
        self.hooked_loads = []
        self.on_init = None
        self.null_get_api = False
        self.saved_original = {}
        for slot, (name, _) in list(machine.slots.items()):
            if name == "GetSystemTimeAsFileTime":
                machine.slots[slot] = (name, self._filetime)
        machine.hook(ALLOC, self._alloc, 4)
        machine.hook(FREE, self._free, 4)
        machine.hook(REALLOC, self._realloc, 5)
        machine.hook(FATAL, self._fatal, 0)
        machine.hook(FATAL_EXIT, self._exit, 0)
        machine.hook(DTOR, self._dtor, 0)
        machine.hook(LOADER, self._loader, 2)
        machine.hook(ENTRY, self._entry, 1)
        machine.hook(SEND_PACKET, self._send_packet, 1)
        if manager:
            machine.hook(LOAD_MODULE, self._hooked_load, 2)
        self.api = self._build_api()

    def _filetime(self, current):
        """GetSystemTimeAsFileTime: a fixed moment."""
        current.mu.mem_write(current.arg(0), struct.pack("<Q", FIXED_FILETIME))
        return 0, 1

    def label(self, pointer):
        """The test's name for a pointer, else the pointer."""
        return self.labels.get(pointer, pointer)

    def _alloc(self, current):
        """Storm SMemAlloc(size, file, line, flags): log, hand out poisoned memory (null on request)."""
        size, line = current.arg(0), current.arg(2)
        warden = current.arg(1) == WARDEN_FILE        # WardenClient.cpp's own allocations are logged and poisoned; the bignum library's are not
        if warden:
            self.events.append(("alloc", size, line))
        if warden and line == self.null_alloc_line:
            return 0
        pointer = current.alloc(max(size, 1) + 16, zero=False)
        current.mu.mem_write(pointer, (b"\xCD" if warden else b"\0") * (max(size, 1) + 16))
        self.sizes[pointer] = size
        self.labels[pointer] = {0x16E: "S", 0x182: "D", 0x10D: "N"}.get(line, f"a{line:x}")
        return pointer

    def _realloc(self, current):
        """Storm SMemReAlloc(ptr, size, file, line, flags) for the bignum vectors: a zeroed new block, old bytes copied."""
        old, size = current.arg(0), current.arg(1)
        pointer = current.alloc(max(size, 1) + 16)
        if old:
            current.mu.mem_write(pointer, current.read(old, min(size, self.sizes.get(old, size))))
        self.sizes[pointer] = size
        return pointer

    def _free(self, current):
        """Storm SMemFree(ptr, file, line, flags): log."""
        if current.arg(1) == WARDEN_FILE:
            self.events.append(("free", current.arg(2), self.label(current.arg(0))))
        return 1

    def _fatal(self, current):
        """The fatal report: log its line argument."""
        self.fatals.append(current.arg(2))
        return 0

    def _exit(self, current):
        """exit(-1): stop the run."""
        self.exited = True
        current.mu.emu_stop()
        return 0

    def _dtor(self, current):
        """FUN_004e06a0 (ECX module record): log only."""
        self.events.append(("dtor", self.label(current.mu.reg_read(UC_X86_REG_ECX))))
        return 0

    def _loader(self, current):
        """FUN_004e0730 (ECX record; image, size): log the bytes it would map; scripted success."""
        record = current.mu.reg_read(UC_X86_REG_ECX)
        image, size = current.arg(0), current.arg(1)
        self.events.append(("load", self.label(record), size, hashlib.sha1(current.read(image, size)).hexdigest()))
        return int(self.loader_ok)

    def _entry(self, current):
        """FUN_004e0700 (ECX record; ordinal): log; scripted entry address."""
        self.events.append(("ord", current.arg(0)))
        return self.entry

    def _send_packet(self, current):
        """FUN_00478350 (packet pointer): log the 0x66 packet (id, u16 length, bytes)."""
        packet = current.arg(0)
        length = current.r16(packet + 1)
        self.sent.append(current.read(packet, 3 + length))
        return 0

    def _hooked_load(self, current):
        """FUN_0047fc70 in the manager machine (EAX length; data, key): log pointers only; scripted result."""
        length = current.mu.reg_read(UC_X86_REG_EAX)
        self.hooked_loads.append((length, current.arg(0), current.arg(1)))
        return self.load_ret

    def stub(self, name, nstack, fn):
        """A callable address in the emulated image that runs a Python function (stands in for module or API code)."""
        machine = self.m
        address = emu.STUB + 16 * (len(machine.slots) + 1)
        machine.slots[address] = (name, lambda current: (fn(current), nstack))
        return address

    def _build_api(self):
        """A fake Bnclient API table: +0xc8 free, +0xcc cache read, +0xd0 cache write (bncache.dat is never touched)."""
        table = self.m.alloc(0x200)

        def read(current):
            """API +0xcc: ECX name, EDX scratch; stack &data, &size."""
            name = current.cstr(current.mu.reg_read(UC_X86_REG_ECX))
            self.api_reads.append(name)
            if name not in self.cache:
                return 0
            blob = self.cache[name]
            pointer = current.put(blob)
            current.w32(current.arg(0), pointer)
            current.w32(current.arg(1), len(blob))
            self.labels[pointer] = "cache"
            return 1

        def release(current):
            """API +0xc8: ECX data, EDX size."""
            self.api_frees.append((self.label(current.mu.reg_read(UC_X86_REG_ECX)), current.mu.reg_read(UC_X86_REG_EDX)))
            return 0

        def write(current):
            """API +0xd0: ECX name, EDX data; stack size, FILETIME*, two lifetimes."""
            name = current.cstr(current.mu.reg_read(UC_X86_REG_ECX))
            data = current.mu.reg_read(UC_X86_REG_EDX)
            size = current.arg(0)
            stamp = struct.unpack("<Q", current.read(current.arg(1), 8))[0]
            self.api_writes.append((name, current.read(data, size), stamp, current.arg(2), current.arg(3)))
            return self.write_ret

        self.m.w32(table + 0xC8, self.stub("api_free", 0, release))
        self.m.w32(table + 0xCC, self.stub("api_read", 2, read))
        self.m.w32(table + 0xD0, self.stub("api_write", 4, write))
        return table

    def reset(self):
        """Clear logs and the Warden globals between cases."""
        for log in (self.events, self.sent, self.fatals, self.api_reads, self.api_writes, self.api_frees, self.hooked_loads):
            log.clear()
        self.exited = False
        self.loader_ok = True
        self.entry = 0x00C0FFEE
        self.null_alloc_line = None
        self.write_ret = 1
        self.load_ret = 1
        self.on_init = None
        for address in (SAVED, SAVED_SIZE, RUNNING, PENDING, API):
            self.m.w32(address, 0)
        self.m.w32(SEED, 0xFFFFFFFF)

    def run(self, address, *stack, ecx=0, edx=0, regs=None):
        """Call one function; return EAX (None when it never returned)."""
        eax = self.m.call(address, *stack, ecx=ecx, edx=edx, regs=regs, limit=LIMIT)
        done = self.m.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC
        require(done or self.exited, f"{address:#x}: instruction cap")
        return eax if done else None

    def cstr_arg(self, text):
        """Pointer to a NUL-terminated string."""
        return self.m.put(text + b"\0")

    def make_module(self, label, started):
        """A fake module record (0x14 bytes) whose factory and object methods are Python stubs; started=True fills +0xc."""
        machine = self.m
        record = machine.alloc(0x14)
        obj = machine.alloc(8)
        vtable = machine.alloc(16)
        self.labels[record] = label
        self.labels[obj] = label.lower()

        def factory(current):
            """Record +0x10: ECX = &0x721c98; returns the object."""
            self.events.append(("factory", label, current.mu.reg_read(UC_X86_REG_ECX)))
            return obj

        def init(current):
            """Object method 0 (seed pointer, size 4): log the seed value."""
            self.events.append(("init", label, current.mu.reg_read(UC_X86_REG_ECX) == obj, current.r32(current.arg(0)), current.arg(1)))
            if self.on_init:
                self.on_init(label)
            return 0

        def unload(current):
            """Object method 1: no arguments."""
            self.events.append(("unload", label, current.mu.reg_read(UC_X86_REG_ECX) == obj))
            return 0

        def packet(current):
            """Object method 2 (stack: packet pointer, length, out pointer)."""
            self.events.append(("packet", label, current.arg(0), current.arg(1), current.r32(current.arg(2))))
            return 0

        def tick(current):
            """Object method 3 (one stack argument)."""
            self.events.append(("tick", label, current.arg(0)))
            return 0

        for index, (name, count, fn) in enumerate((("init", 2, init), ("unload", 0, unload), ("packet", 3, packet), ("tick", 1, tick))):
            machine.w32(vtable + 4 * index, self.stub(f"{label}_{name}", count, fn))
        machine.w32(obj, vtable)
        machine.w32(record + 0x10, self.stub(f"{label}_factory", 0, factory))
        if started:
            machine.w32(record + 0xC, obj)
        return record


# ---- checks ----------------------------------------------------------------------

def events_of(world, kinds=None):
    """The logged events, optionally only some kinds."""
    return [e for e in world.events if kinds is None or e[0] in kinds]


def check_static(loader):
    """The callback table, the table pointer, the seed default, the strings and the built-in module's metadata."""
    machine = loader.m
    want = [0x480000, 0x480020, 0x480040, 0x4800C0, 0x4800E0, 0x480100, 0x480150]
    require([machine.r32(CALLBACKS + 4 * i) for i in range(7)] == want, "callback table")
    require(machine.r32(TABLE_PTR) == CALLBACKS, "table pointer 0x721c98")
    require(machine.r32(SEED) == 0xFFFFFFFF, "seed default")
    require(machine.cstr(0x6D8E5C) == "%02x" and machine.cstr(0x6D8E54) == ".mod", "format strings")
    require(machine.cstr(0x6D8E18) == ".\\WARDEN\\WardenClient.cpp", "file name")
    require(salt_text() == b"Maiev.mod", f"salt {salt_text()!r}")
    rng = random.Random(1)
    require(is_prime(TEST_P, rng) and is_prime(TEST_Q, rng) and test_key()[0].bit_length() == 2048, "test key")
    require(machine.r32(RSA_E) == 0x10001, "public exponent")
    require(machine.read(GET_API, 6) == b"\xB8\xA0\xFA\x72\x00\xC3", "FUN_00518c10 returns 0x72faa0")
    return 8


def check_rc4(loader):
    """FUN_004e09b0 key setup and FUN_0047f8c0 keystream against the RC4 model."""
    machine, rng, n = loader.m, random.Random(4), 0
    for key_len in (16, 16, 5):
        for size in (0, 1, 255, 256, 257, 1000):
            key = bytes(rng.randrange(256) for _ in range(key_len))
            data = bytes(rng.randrange(256) for _ in range(size))
            state = machine.alloc(0x200)
            machine.call(0x4E09B0, machine.put(key), key_len, ecx=state, limit=LIMIT)
            buf = machine.put(data)
            machine.call(0x47F8C0, buf, size, ecx=state, limit=LIMIT)
            want = rc4(key, data) if key_len == 16 else rc4_plain(key, data)
            require(machine.read(buf, size) == want, f"rc4 key {key_len} size {size}")
            n += 1
    return n


def rc4_plain(key, data):
    """RC4 without the planted defect (used for key lengths the defect does not target)."""
    global mutation
    kept, mutation = mutation, None
    try:
        return rc4(key, data)
    finally:
        mutation = kept


def check_digest(loader):
    """FUN_0047f990: SHA-1 over data (rotated at skip) plus the upper-cased salt."""
    machine, rng, n = loader.m, random.Random(5), 0
    for size in (0, 1, 55, 56, 63, 64, 65, 119, 200, 513):
        for skip in sorted({0, 1, size // 2, size}):
            if skip > size:
                continue
            for salt in (b"Maiev.mod", b"abC"):
                data = bytes(rng.randrange(256) for _ in range(size))
                out = machine.alloc(32)
                loader.run(0x47F990, loader.cstr_arg(salt), out, regs={"eax": size, "ebx": machine.put(data), "esi": skip})
                require(machine.read(out, 20) == digest(data, skip, salt), f"digest size {size} skip {skip} salt {salt!r}")
                n += 1
    return n


def install_key(loader, key):
    """Write the test public key over the shipped one in the emulated image (exponent dword, 256-byte modulus)."""
    machine = loader.m
    n, e, _ = key
    machine.mu.mem_write(RSA_E, struct.pack("<I", e))
    machine.mu.mem_write(RSA_N, n.to_bytes(0x100, "little"))


def verify_call(loader, data, skip, salt):
    """Run FUN_0047fa20 (ECX 0x100, EAX length; salt, data, modulus, exponent, 4, skip)."""
    machine = loader.m
    loader.reset()
    result = loader.run(0x47FA20, loader.cstr_arg(salt), machine.put(data), RSA_N, RSA_E, 4, skip, ecx=0x100, regs={"eax": len(data)})
    return result


def check_signature(loader, key):
    """FUN_0047fa20: trailer layout, marker, hash, RSA compare, short data, skip and fatal arguments."""
    rng, n = random.Random(6), 0
    install_key(loader, key)
    for size, skip, salt in ((0, 0, b"Maiev.mod"), (100, 0, b"Maiev.mod"), (100, 33, b"Maiev.mod"), (1500, 0, b"Other")):
        plain = bytes(rng.randrange(256) for _ in range(size))
        good = plain + sign(plain, skip, salt, key)
        cases = [("good", good)]
        if size:
            bad = bytearray(good)
            bad[size // 2] ^= 1
            cases.append(("data bit", bytes(bad)))
        bad = bytearray(good)
        bad[size] ^= 0x20
        cases.append(("marker", bytes(bad)))
        bad = bytearray(good)
        bad[-1] ^= 1
        cases.append(("sig last", bytes(bad)))
        bad = bytearray(good)
        bad[size + 10] ^= 0x80
        cases.append(("sig mid", bytes(bad)))
        cases.append(("extra byte", good + b"\0"))
        cases.append(("one short", good[1:]))
        for name, data in cases:
            want = verify_model(data, skip, salt, key)
            got = verify_call(loader, data, skip, salt)
            require(got == want, f"signature {name} size {size} skip {skip} salt {salt!r}: got {got} want {want}")
            n += 1
    for size in (0, 0x100, 0x103, 0x104):
        data = bytes(range(256))[:size] if size <= 256 else bytes(size)
        got = verify_call(loader, data, 0, b"Maiev.mod")
        require(got == verify_model(data, 0, b"Maiev.mod", key), f"short {size}")
        n += 1
    got = verify_call(loader, bytes(0x110), 0x111, b"Maiev.mod")
    require(got is None and loader.fatals == [0xA3], f"skip beyond length: {got} {loader.fatals}")
    n += 1
    # the shipped key against a forged trailer
    machine = loader.m
    machine.mu.mem_write(RSA_N, REAL_KEY[1])
    machine.mu.mem_write(RSA_E, REAL_KEY[0])
    forged = bytes(300) + b"NGIS" + bytes(range(256))
    require(verify_call(loader, forged, 0, b"Maiev.mod") == 0, "shipped key accepted a forged trailer")
    n += 1
    install_key(loader, key)
    return n


REAL_KEY = (b"", b"")


def expect_load(world, cipher, length, rc4_key, payload, declared, had_pending, outcome, tag):
    """Check one FUN_0047fc70 run's events against the pipeline model."""
    prefix = [("dtor", "P")] * (1 if mut("dtor_once") else 2) + [("free", 0x117, "P")] if had_pending else []
    plain = cipher_plain(cipher, rc4_key)
    trailer = length >= 0x104 and plain[length - 0x104:length - 0x100] == b"NGIS"
    checked = [("alloc", 0x100, 0xAB), ("alloc", 0x100, 0xB5), ("free", 0xCC, "aab"), ("free", 0xCD, "ab5")] if trailer else []
    head = prefix + [("alloc", length, 0x16E)] + checked
    if outcome == "sig":
        want = head + [("free", 0x17C, "S")]
    else:
        out = zlib_model(plain[4:], declared)
        mid = head + [("alloc", declared, 0x182)]
        if out is None:
            want = mid + [("free", 0x18D, "S"), ("free", 0x18E, "D")]
        else:
            want = mid + [("free", 0x193, "S"), ("alloc", 0x14, 0x10D)]
            want.append(("load", "N", len(out), hashlib.sha1(out).hexdigest()))
            if outcome == "loader":
                want += [("free", 0x1A0, "D"), ("dtor", "N"), ("free", 0x117, "N")]
            elif outcome == "entry":
                want += [("ord", 1), ("dtor", "N"), ("free", 0x1AA, "D"), ("dtor", "N"), ("free", 0x117, "N")]
            else:
                want += [("ord", 1), ("free", 0x1B0, "D")]
    require(world.events == want, f"{tag}: events\n got {world.events}\nwant {want}")


def cipher_plain(cipher, rc4_key):
    """The decrypted form FUN_0047fc70 works on."""
    return rc4_plain(rc4_key, cipher)


def check_load_module(loader, key):
    """FUN_0047fc70: copy, RC4, signature, zlib, image loader, entry lookup, and the pending slot."""
    machine, rng, n = loader.m, random.Random(7), 0
    install_key(loader, key)
    rc4_key = bytes(rng.randrange(256) for _ in range(16))
    payload = bytes(rng.randrange(256) for _ in range(300)) + b"\0" * 400

    def attempt(cipher, outcome, tag, had_pending, payload_=payload, declared=None):
        """Run one load and check events, result, pending slot and the untouched input."""
        loader.reset()
        pending = loader.make_module("P", False) if had_pending else 0
        machine.w32(PENDING, pending)
        buf = machine.put(cipher)
        result = loader.run(LOAD_MODULE, buf, machine.put(rc4_key), regs={"eax": len(cipher)})
        size = declared if declared is not None else len(payload_)
        expect_load(loader, cipher, len(cipher), rc4_key, payload_, size, had_pending, outcome, tag)
        require(machine.read(buf, len(cipher)) == cipher, f"{tag}: input buffer modified")
        now = machine.r32(PENDING)
        if outcome == "ok" and zlib_model(cipher_plain(cipher, rc4_key)[4:], size) is not None:
            require(result == 1 and loader.label(now) == "N", f"{tag}: result {result} pending {now:#x}")
            require(machine.r32(now + 0x10) == loader.entry, f"{tag}: entry slot")
            require(machine.read(now, 0x10) == b"\0" * 16, f"{tag}: record fields not zeroed")
        else:
            require(result == 0, f"{tag}: result {result}")
            require(now == 0, f"{tag}: pending slot {now:#x} after a failed load")

    for had_pending in (False, True):
        attempt(pack_module(payload, key, rc4_key), "ok", "good", had_pending)
        attempt(pack_module(b"", key, rc4_key), "ok", "empty image", had_pending, payload_=b"")
        attempt(pack_module(payload, key, rc4_key, declared=len(payload) + 100), "ok", "oversize declaration", had_pending, declared=len(payload) + 100)
        attempt(pack_module(payload, key, rc4_key, declared=len(payload) - 1), "ok", "undersize declaration", had_pending, declared=len(payload) - 1)
        attempt(pack_module(payload, key, rc4_key, declared=0), "ok", "zero declaration", had_pending, declared=0)

        def corrupt_stream(raw):
            """Break the zlib stream, then re-sign (so the signature passes)."""
            plain = bytes(raw[:-0x104])
            bad = plain[:8] + bytes([plain[8] ^ 0xFF]) + plain[9:]
            return bytearray(bad + sign(bad, 0, salt_text(), key))

        attempt(pack_module(payload, key, rc4_key, tamper=corrupt_stream), "ok", "bad zlib", had_pending)

        def flip(offset):
            """A tamper that flips one bit of the signed bytes."""
            def apply(raw):
                """Flip a bit at offset."""
                raw[offset] ^= 1
                return raw
            return apply

        attempt(pack_module(payload, key, rc4_key, tamper=flip(5)), "sig", "data bit", had_pending)
        attempt(pack_module(payload, key, rc4_key, tamper=flip(-1)), "sig", "sig bit", had_pending)
        attempt(rc4_plain(rc4_key, b"\0" * 0x103), "sig", "short 0x103", had_pending)
        attempt(b"", "sig", "empty", had_pending)
        attempt(bytes(range(256)) * 2, "sig", "junk", had_pending)
        n += 13
    for outcome, tweak in (("loader", "loader_ok"), ("entry", "entry")):
        loader.reset()
        loader.loader_ok = tweak != "loader_ok"
        loader.entry = 0 if tweak == "entry" else loader.entry
        cipher = pack_module(payload, key, rc4_key)
        result = loader.run(LOAD_MODULE, machine.put(cipher), machine.put(rc4_key), regs={"eax": len(cipher)})
        expect_load(loader, cipher, len(cipher), rc4_key, payload, len(payload), False, outcome, outcome)
        require(result == 0 and machine.r32(PENDING) == 0, f"{outcome}: result")
        n += 1
    loader.reset()
    loader.null_alloc_line = 0x10D
    cipher = pack_module(payload, key, rc4_key)
    result = loader.run(LOAD_MODULE, machine.put(cipher), machine.put(rc4_key), regs={"eax": len(cipher)})
    require(result == 0 and loader.events[-1][:2] == ("free", 0x199), f"module alloc failure: {loader.events[-2:]}")
    n += 1
    loader.reset()
    loader.null_alloc_line = 0x182
    result = loader.run(LOAD_MODULE, machine.put(cipher), machine.put(rc4_key), regs={"eax": len(cipher)})
    require(result == 0 and loader.events[-1][:2] == ("free", 0x185), f"image alloc failure: {loader.events[-2:]}")
    return n + 1


def check_load_helpers(loader):
    """The tiny helpers: FUN_0047fbc0 zero fill, 0047fbe0, 0047fbf0 alloc, 0047fc10 free, 0047fc30 vtable, 0047fc40 deleting destructor."""
    machine = loader.m
    obj = machine.put(b"\xAA" * 0x20)
    require(loader.run(0x47FBC0, ecx=obj) == obj and machine.read(obj, 0x14) == b"\0" * 0x14 and machine.read(obj + 0x14, 4) == b"\xAA" * 4, "47fbc0")
    loader.reset()
    loader.run(0x47FBE0, ecx=0x1234)
    require(loader.events == [("dtor", 0x1234)], f"47fbe0 {loader.events}")
    loader.reset()
    loader.run(0x47FBF0, ecx=0x14)
    require(loader.events == [("alloc", 0x14, 0x10D)], f"47fbf0 {loader.events}")
    loader.reset()
    loader.run(0x47FC10, ecx=0x5555)
    require(loader.events == [("free", 0x117, 0x5555)], f"47fc10 {loader.events}")
    require(loader.run(0x47FC30, ecx=obj) == obj and machine.r32(obj) == VTABLE_BASE, "47fc30")
    for flags in (0, 1, 2, 3):
        loader.reset()
        result = loader.run(0x47FC40, flags, ecx=0x7777)
        want = [("dtor", 0x7777)] + ([("free", 0x117, 0x7777)] if flags & 1 else [])
        require(result == 0x7777 and loader.events == want, f"47fc40 flags {flags}: {loader.events}")
    return 5 + 4


def check_cache_write(world):
    """FUN_0047ffb0 (orphan) and callback 2 FUN_00480040: bncache write under the 32-hex-digit name, fixed FILETIME, lifetimes -1."""
    machine, rng, n = world.m, random.Random(8), 0
    for api in (0, world.api):
        for size in (0, 1, 5000):
            blob = bytes(rng.randrange(256) for _ in range(size))
            name = bytes(rng.randrange(256) for _ in range(16))
            hexname = ("".join(f"{b:X}" if mut("hex_case") else f"{b:02x}" for b in name)) + ".mod"
            world.reset()
            machine.w32(API, api)
            world.run(0x480040, world.m.put(name), machine.put(blob), size)
            if api:
                require(world.api_writes == [(hexname, blob, FIXED_FILETIME, 0xFFFFFFFF, 0xFFFFFFFF)], f"callback 2 {world.api_writes}")
            else:
                require(not world.api_writes, "callback 2 wrote with no API")
            world.reset()
            machine.w32(API, api)
            world.write_ret = 1
            result = world.run(0x47FFB0, world.cstr_arg(hexname.encode()), machine.put(blob), size)
            if api:
                require(world.api_writes == [(hexname, blob, FIXED_FILETIME, 0xFFFFFFFF, 0xFFFFFFFF)] and result == 1, f"47ffb0 {world.api_writes} {result}")
            else:
                require(result == 0 and not world.api_writes, "47ffb0 with no API")
            n += 2
    return n


def check_load_by_name(manager):
    """FUN_0047fee0 (EBX name; key): the built-in name uses the built-in blob, others come from the cache by hex name."""
    machine, rng, n = manager.m, random.Random(9), 0
    builtin = machine.read(BUILTIN_NAME, 16)
    for api in (0, manager.api):
        for ret in (0, 1, 0x55):
            manager.reset()
            manager.load_ret = ret
            machine.w32(API, api)
            result = manager.run(LOAD_BY_NAME, 0xABCD, regs={"ebx": BUILTIN_NAME})
            require(result == ret and manager.hooked_loads == [(BUILTIN_SIZE, BUILTIN_DATA, BUILTIN_KEY)], f"built-in {manager.hooked_loads} {result}")
            require(not manager.api_reads, "built-in module read the cache")
            copy = machine.put(builtin)
            manager.reset()
            manager.load_ret = ret
            machine.w32(API, api)
            require(manager.run(LOAD_BY_NAME, 0xABCD, regs={"ebx": copy}) == ret and manager.hooked_loads, "built-in name by value")
            n += 2
    near = bytearray(builtin)
    near[15] ^= 1
    for name in (bytes(near), bytes(16), bytes(rng.randrange(256) for _ in range(16))):
        hexname = "".join(f"{b:02x}" for b in name) + ".mod"
        blob = bytes(rng.randrange(256) for _ in range(40))
        key = machine.put(bytes(range(16)))
        for have in (True, False):
            for api in (0, manager.api):
                manager.reset()
                manager.cache = {hexname: blob} if have else {}
                manager.load_ret = 1
                machine.w32(API, api)
                result = manager.run(LOAD_BY_NAME, key, regs={"ebx": machine.put(name)})
                if not api:
                    require(result == 0 and not manager.hooked_loads and not manager.api_reads, "no API")
                elif not have:
                    require(result == 0 and manager.api_reads == [hexname] and not manager.hooked_loads, f"cache miss {manager.api_reads}")
                else:
                    length, data, passed_key = manager.hooked_loads[0]
                    require(result == 1 and manager.api_reads == [hexname] and length == len(blob) and passed_key == key, f"cache hit {manager.hooked_loads}")
                    require(machine.read(data, length) == blob and manager.api_frees == [("cache", len(blob))], f"cache buffer release {manager.api_frees}")
                n += 1
    return n


def check_callbacks(net):
    """Callbacks 0 (send), 3 (alloc) and 4 (free), and the 0x66 framing of FUN_00478830."""
    machine, rng, n = net.m, random.Random(10), 0
    limit = 0x1FC if mut("send_limit") else 0x1FD
    for size in (0, 1, 300, 0x1FC, 0x1FD):
        data = bytes(rng.randrange(256) for _ in range(size))
        net.reset()
        net.run(0x480000, machine.put(data), size)
        want = bytes([0x66]) + struct.pack(">H" if mut("len_endian") else "<H", size) + data
        require(net.sent == ([want] if size <= limit else []), f"callback 0 size {size}")
        net.reset()
        net.run(SEND_WRAP, ecx=machine.put(data), edx=size)
        require(net.sent == [bytes([0x66]) + struct.pack("<H", size) + data], f"FUN_00478830 size {size}")
        n += 2
    for size in (0x1FE, 0x1FF, 0x400, 0xFFFF):
        net.reset()
        require(net.run(0x480000, machine.put(bytes(size)), size) is None and net.fatals == [0x452] and not net.sent, f"callback 0 size {size:#x}")
        n += 1
    for size in (0, 1, 64):
        net.reset()
        result = net.run(0x4800C0, size)
        require(net.events == [("alloc", size, 0x274)] and result, f"callback 3 {net.events}")
        pointer = result
        net.reset()
        net.run(0x4800E0, pointer)
        require(net.events == [("free", 0x27B, net.label(pointer))], f"callback 4 {net.events}")
        n += 2
    return n


def check_callback_load(manager):
    """Callback 1 (FUN_00480020): FUN_0047fee0's result as 0 or 1 in AL."""
    machine, n = manager.m, 0
    for ret in (0, 1, 0x55):
        manager.reset()
        manager.load_ret = ret
        machine.w32(API, manager.api)
        key = machine.put(bytes(range(16)))
        name = machine.put(machine.read(BUILTIN_NAME, 16))
        result = manager.run(0x480020, name, key)
        require(result & 0xFF == int(ret != 0), f"callback 1 returned {result:#x} for {ret:#x}")
        require(manager.hooked_loads == [(BUILTIN_SIZE, BUILTIN_DATA, BUILTIN_KEY)], "callback 1 built-in load")
        manager.reset()
        result = manager.run(0x480020, machine.put(bytes(range(16))), key)
        require(result & 0xFF == 0 and not manager.hooked_loads, "callback 1 with a name not in the cache")
        n += 2
    return n


def check_keep_take(net):
    """Callbacks 5 and 6: one saved buffer, replaced on every keep, truncated and released by take."""
    machine, rng, n = net.m, random.Random(11), 0
    net.reset()
    out, size = machine.alloc(64), machine.alloc(4)
    machine.w32(size, 64)
    require(net.run(0x480150, out, size) & 0xFF == 0 and machine.r32(size) == 64, "take with nothing kept")
    n += 1
    for keep in (0, 1, 33):
        for want in (0, 1, 33, 64):
            blob = bytes(rng.randrange(256) for _ in range(keep))
            net.reset()
            net.run(0x480100, machine.put(blob), keep)
            first = machine.r32(SAVED)
            require(net.events == [("alloc", keep, 0x284)] and machine.r32(SAVED_SIZE) == keep and machine.read(first, keep) == blob, f"keep {keep}")
            net.events.clear()
            net.run(0x480100, machine.put(blob), keep)
            require(net.events == [("free", 0x283, "a284"), ("alloc", keep, 0x284)], f"second keep {net.events}")
            net.events.clear()
            out, size = machine.alloc(80), machine.alloc(4)
            machine.mu.mem_write(out, b"\xEE" * 80)
            machine.w32(size, want)
            result = net.run(0x480150, out, size)
            taken = want if mut("no_truncate") else min(want, keep)
            require(result & 0xFF == 1 and machine.r32(size) == taken, f"take size {machine.r32(size)} want {taken}")
            require(machine.read(out, 80) == blob[:taken] + b"\xEE" * (80 - taken), f"take bytes keep {keep} want {want}")
            require(net.events == [("free", 0x292, "a284")] and machine.r32(SAVED) == 0 and machine.r32(SAVED_SIZE) == 0, f"take release {net.events}")
            n += 1
            if want < keep:
                size2 = machine.alloc(4)
                machine.w32(size2, 64)
                require(net.run(0x480150, out, size2) & 0xFF == 0, "the unread remainder is gone")
    return n


def check_life_cycle(manager):
    """FUN_00480230 start, 004801d0 unload, 004802e0 packet, 00480320 tick, 00480340 shutdown, 004803c0 init."""
    machine, n = manager.m, 0

    def globals_are(running, pending):
        """The running and pending slots by label."""
        require((manager.label(machine.r32(RUNNING)), manager.label(machine.r32(PENDING))) == (running, pending),
                f"slots {manager.label(machine.r32(RUNNING))} {manager.label(machine.r32(PENDING))} want {running} {pending}")

    # start: nothing pending
    manager.reset()
    machine.w32(RUNNING, manager.make_module("R", True))
    manager.run(0x480230)
    require(not manager.events, "start with nothing pending")
    globals_are("R", 0)
    n += 1
    # start with pending, nothing running
    manager.reset()
    machine.w32(SEED, 0xDEADBEEF)
    machine.w32(PENDING, manager.make_module("P", False))
    manager.run(0x480230)
    require(manager.events == [("factory", "P", TABLE_PTR), ("init", "P", True, 0xDEADBEEF, 4)], f"start {manager.events}")
    globals_are("P", 0)
    n += 1
    # start with a running module: unloaded first
    manager.reset()
    machine.w32(SEED, 7)
    machine.w32(RUNNING, manager.make_module("R", True))
    machine.w32(PENDING, manager.make_module("P", False))
    manager.run(0x480230)
    want = [("unload", "R", True), ("dtor", "R"), ("dtor", "R"), ("free", 0x117, "R"), ("factory", "P", TABLE_PTR), ("init", "P", True, 7, 4)]
    require(manager.events == want, f"replace {manager.events}")
    globals_are("P", 0)
    n += 1
    # a module's init loads the next one: the loop keeps going
    manager.reset()
    machine.w32(SEED, 9)
    queue = [manager.make_module("Q", False), manager.make_module("S2", False)]

    def chain(label):
        """During init, queue the next pending module (as callback 1 would)."""
        if queue:
            machine.w32(PENDING, queue.pop(0))

    manager.on_init = chain
    machine.w32(PENDING, manager.make_module("P", False))
    manager.run(0x480230)
    kinds = [(e[0], e[1]) for e in manager.events]
    want = [("factory", "P"), ("init", "P"), ("unload", "P"), ("dtor", "P"), ("dtor", "P"), ("free", 0x117), ("factory", "Q"), ("init", "Q"),
            ("unload", "Q"), ("dtor", "Q"), ("dtor", "Q"), ("free", 0x117), ("factory", "S2"), ("init", "S2")]
    require([(k, label if k != "free" else 0x117) for k, label in kinds] == [(k, label if k != "free" else 0x117) for k, label in want], f"chain {kinds}")
    globals_are("S2", 0)
    n += 1
    # unload
    for running in (False, True):
        manager.reset()
        if running:
            machine.w32(RUNNING, manager.make_module("R", True))
        manager.run(0x4801D0)
        want = [("unload", "R", True), ("dtor", "R"), ("dtor", "R"), ("free", 0x117, "R")] if running else []
        require(manager.events == want, f"unload {manager.events}")
        globals_are(0, 0)
        n += 1
    # packet and tick
    for running in (False, True):
        for pending in (False, True):
            manager.reset()
            machine.w32(SEED, 3)
            if running:
                machine.w32(RUNNING, manager.make_module("R", True))
            if pending:
                machine.w32(PENDING, manager.make_module("P", False))
            manager.run(0x4802E0, ecx=0x5000, edx=0x77)
            if not running:
                require(not manager.events, "packet with no running module")
                globals_are(0, "P" if pending else 0)
            else:
                want = [("packet", "R", 0x5000, 0x77, 0)]
                if pending:
                    want += [("unload", "R", True), ("dtor", "R"), ("dtor", "R"), ("free", 0x117, "R"), ("factory", "P", TABLE_PTR), ("init", "P", True, 3, 4)]
                require(manager.events == want, f"packet {manager.events}")
            manager.reset()
            if running:
                machine.w32(RUNNING, manager.make_module("R", True))
            if pending:
                machine.w32(PENDING, manager.make_module("P", False))
            manager.run(0x480320, ecx=0x1234)
            if not running:
                require(not manager.events, "tick with no running module")
            else:
                want = [("tick", "R", 0x1234)]
                if pending:
                    want += [("unload", "R", True), ("dtor", "R"), ("dtor", "R"), ("free", 0x117, "R"), ("factory", "P", TABLE_PTR), ("init", "P", True, 0xFFFFFFFF, 4)]
                require(manager.events == want, f"tick {manager.events}")
            n += 2
    # shutdown
    for running in (False, True):
        for pending in (False, True):
            for kept in (False, True):
                manager.reset()
                machine.w32(SEED, 5)
                machine.w32(API, manager.api)
                if running:
                    machine.w32(RUNNING, manager.make_module("R", True))
                if pending:
                    machine.w32(PENDING, manager.make_module("P", False))
                if kept:
                    machine.w32(SAVED, machine.put(b"kept"))
                    machine.w32(SAVED_SIZE, 4)
                    manager.labels[machine.r32(SAVED)] = "kept"
                manager.run(0x480340)
                want = []
                if pending:
                    want += [("dtor", "P"), ("dtor", "P"), ("free", 0x117, "P")]
                if running:
                    want += [("unload", "R", True), ("dtor", "R"), ("dtor", "R"), ("free", 0x117, "R")]
                if kept:
                    want += [("free", 0x24F, "kept")]
                require(manager.events == want, f"shutdown {manager.events}")
                require(machine.r32(SEED) == 0xFFFFFFFF and machine.r32(API) == 0 and machine.r32(SAVED) == 0 and machine.r32(SAVED_SIZE) == 0, "shutdown globals")
                globals_are(0, 0)
                n += 1
    # init: built-in module through the hooked loader, then start
    manager.reset()
    machine.w32(RUNNING, manager.make_module("R", True))
    machine.w32(SAVED, 0)

    def hooked_loader_sets_pending(current):
        """The manager machine's FUN_0047fc70: pretend the built-in module became pending."""
        manager.hooked_loads.append((current.mu.reg_read(UC_X86_REG_EAX), current.arg(0), current.arg(1)))
        machine.w32(PENDING, manager.make_module("B", False))
        return 1

    manager.m.hook(LOAD_MODULE, hooked_loader_sets_pending, 2)
    result = manager.run(0x4803C0, ecx=0xCAFE0001) & 0xFFFFFFFF
    require(result == 1, f"init result {result}")
    require(machine.r32(SEED) == 0xCAFE0001 and machine.r32(API) == 0x72FAA0, "init globals")
    require(manager.hooked_loads == [(BUILTIN_SIZE, BUILTIN_DATA, BUILTIN_KEY)], f"init built-in load {manager.hooked_loads}")
    want = [("unload", "R", True), ("dtor", "R"), ("dtor", "R"), ("free", 0x117, "R"), ("factory", "B", TABLE_PTR), ("init", "B", True, 0xCAFE0001, 4)]
    require(manager.events == want, f"init events {manager.events}")
    globals_are("B", 0)
    n += 1
    machine.mu.mem_write(GET_API + 1, struct.pack("<I", 0))
    machine.mu.ctl_remove_cache(GET_API, GET_API + 6)
    manager.reset()
    manager.run(0x4803C0, ecx=0x11)
    require(machine.r32(API) == 0 and not manager.hooked_loads and machine.r32(SEED) == 0x11, "init without an API")
    machine.mu.mem_write(GET_API + 1, struct.pack("<I", 0x72FAA0))
    machine.mu.ctl_remove_cache(GET_API, GET_API + 6)
    return n + 1


MUTATION_GROUPS = ("static", "rc4", "digest", "signature", "load", "helpers", "cache", "byname", "callbacks", "keeptake", "lifecycle")
MUTATIONS = {
    "salt_case": "digest", "no_skip": "digest", "pad_byte": "signature", "end_byte": "signature", "marker": "signature",
    "endian": "signature", "short_len": "signature", "dtor_once": "load", "no_truncate": "keeptake", "rc4_key_len": "load", "hex_case": "cache", "len_endian": "callbacks", "send_limit": "callbacks",
}


def build_world(manager=False):
    """A new World from the stock game.exe."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    return World(str(exe), manager)


def run_all(names=None):
    """Run the checks (all, or those in names); returns the case count."""
    global REAL_KEY
    loader, manager, net = build_world(), build_world(True), build_world()
    REAL_KEY = (loader.m.read(RSA_E, 4), loader.m.read(RSA_N, 0x100))
    key = TEST_KEY
    plan = [
        ("static", lambda: check_static(loader)),
        ("rc4", lambda: check_rc4(loader)),
        ("digest", lambda: check_digest(loader)),
        ("signature", lambda: check_signature(loader, key)),
        ("load", lambda: check_load_module(loader, key)),
        ("helpers", lambda: check_load_helpers(loader)),
        ("cache", lambda: check_cache_write(loader)),
        ("byname", lambda: check_load_by_name(manager)),
        ("callbacks", lambda: check_callbacks(net) + check_callback_load(manager)),
        ("keeptake", lambda: check_keep_take(net)),
        ("lifecycle", lambda: check_life_cycle(manager)),
    ]
    total = 0
    for name, check in plan:
        if names is None or name in names:
            total += check()
    return total, len(plan)


TEST_KEY = None


def main():
    """Run every check, or with --mutation-check prove each model defect is caught."""
    global mutation, TEST_KEY
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", nargs="*", help="run only these checks")
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate model defect to fail")
    options = parser.parse_args()
    TEST_KEY = test_key()
    if options.mutation_check:
        for name, group in MUTATIONS.items():
            mutation = name
            try:
                run_all({group})
            except Mismatch:
                print(f"caught: {name}")
            else:
                raise SystemExit(f"mutation {name} was not caught")
            finally:
                mutation = None
        print(f"ok: {len(MUTATIONS)} model defects caught")
        return
    only = set(options.only) if options.only else None
    if only and only - {name for name in MUTATION_GROUPS}:
        raise SystemExit(f"unknown check(s): {sorted(only - set(MUTATION_GROUPS))}; known: {sorted(MUTATION_GROUPS)}")
    total, checks = run_all(only)
    print(f"ok: {total} WardenClient cases over {checks} checks")


if __name__ == "__main__":
    main()
