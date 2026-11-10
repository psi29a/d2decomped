# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the Battle.net client's BnSend.cpp in stock game.exe 1.14d.

Run: uv run python bnsend.py [--mutation-check]

Each of the 28 BnSend functions (FUN_0051ac20 ... FUN_0051be20) runs for real
in the emulator against a Python model of the packet it should send. Hooked:
the socket send FUN_0051c5c0 (logs id, bytes and length, snapshots the reply
arrays), the reply wait FUN_00520b70 (logs, returns at once), the expansion
test FUN_0051bec0 (a switch), the Storm allocator and free (FUN_0040b380,
FUN_0040b3c0: logged), time() (fixed), the old-SHA hash FUN_005209d0 (replaced
by SHA-1 and logged), the crash-dump reader FUN_0040d290 (scripted) and the
fatal exit. The string copy FUN_004135d0, strlen FUN_00413750 and the reply
array reset/read helpers run for real. The Windows locale calls behind
FUN_0051b540 are scripted. Not covered: the sockets, FUN_0051c5c0 itself, the
hash FUN_005209d0 and the crash-dump reader (other functions).

`--mutation-check` plants deliberate defects in the model and requires each
one to be caught.
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
LIMIT = 400000

SEND = 0x51C5C0
WAIT = 0x520B70
EXPANSION = 0x51BEC0
ALLOC, FREE = 0x40B380, 0x40B3C0
TIME = 0x6850E6
HASH = 0x5209D0
CRASH_DUMP = 0x40D290
FATAL, FATAL_EXIT = 0x408A60, 0x681E09
NTOHS_ORD = "ntohs"

REPLY_A, REPLY_B = 0x882128, 0x8822A8        # per-id reply arrays (dwords)
CHANNEL_COPY, REALM_COPY, ADV_COPY = 0x8818C8, 0x8818CC, 0x8818D0
LOGON_COPY, USERDATA_COPY = 0x8818D4, 0x8818D8
TOKEN, COUNTER, BANNER = 0x8818BC, 0x8818C4, 0x88252C
FILE_NAME = 0x6DEF84

PRODUCT = (0x44324456, 0x44325850)           # 'VD2D' / 'PX2D' (expansion adds 0x13fa)

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


def u32(*values):
    """Little-endian dwords."""
    return b"".join(struct.pack("<I", v & 0xFFFFFFFF) for v in values)


def copied(text):
    """What FUN_004135d0(dst, src, 0x100) leaves: at most 0xFF bytes, then a NUL; a null source copies nothing."""
    if text is None:
        return b""
    return text[:0xFE if mut("copy_limit") else 0xFF]


def cstrings(*texts):
    """Each string cut at 0xFF and NUL-terminated, one after another."""
    return b"".join(copied(t) + b"\0" for t in texts)


class World:
    """One emulated game.exe with BnSend's callees hooked and logged."""

    def __init__(self, exe):
        """Build the machine, scripted imports and every hook."""
        self.sends, self.waits, self.allocs, self.frees = [], [], [], []
        self.hashes, self.crash_calls = [], []
        self.fatals, self.exited = [], False
        self.gs_failed = False
        self.send_ret = 0
        self.expansion = 0
        self.crash = (0, b"")
        self.locale_fail = False
        machine = self.m = emu.Emu(exe)
        self.locale_calls = []
        self.alloc_ptr = {}
        for name, handler in self._imports().items():
            for address, (slot, _) in list(machine.slots.items()):
                if slot == name:
                    machine.slots[address] = (slot, handler)
        machine.hook(SEND, self._send, 1)
        machine.hook(WAIT, self._wait, 0)
        machine.hook(EXPANSION, lambda current: self.expansion, 0)
        machine.hook(ALLOC, self._alloc, 2)
        machine.hook(FREE, self._free, 2)
        machine.hook(TIME, lambda current: 0x5F000000, 0)
        machine.hook(HASH, self._hash, 1)
        machine.hook(CRASH_DUMP, self._crash, 0)
        machine.hook(FATAL, self._fatal, 0)
        machine.hook(FATAL_EXIT, self._exit, 0)

    def _imports(self):
        """Scripted Windows locale and time calls (FUN_0051b540)."""
        machine = self.m

        def put_systemtime(base, stamp):
            """Write a SYSTEMTIME of 8 words."""
            machine.mu.mem_write(base, struct.pack("<8H", *stamp))

        def tz(current):
            """GetTimeZoneInformation: Bias -120 at +0."""
            machine.w32(current.arg(0), (-120) & 0xFFFFFFFF)
            return 1, 1

        def systime(current):
            """GetSystemTime: a fixed UTC moment."""
            self.locale_calls.append("sys")
            put_systemtime(current.arg(0), (2026, 11, 1, 9, 12, 34, 56, 789))
            return 0, 1

        def localtime(current):
            """GetLocalTime: two hours later."""
            self.locale_calls.append("local")
            put_systemtime(current.arg(0), (2026, 11, 1, 9, 14, 34, 56, 789))
            return 0, 1

        def to_filetime(current):
            """SystemTimeToFileTime for the fixed moments."""
            fields = struct.unpack("<8H", machine.read(current.arg(0), 16))
            value = 0x01D9_0000_0000_0000 + sum(f * (31 + i) * 1000003 for i, f in enumerate(fields))
            machine.mu.mem_write(current.arg(1), struct.pack("<Q", value))
            return 1, 2

        def locale_info(current):
            """GetLocaleInfoA(0x400, type, buf, 0x40): one string per type."""
            lcid, kind, buf, size = (current.arg(i) for i in range(4))
            self.locale_calls.append((lcid, kind, size))
            if self.locale_fail:
                return 0, 4
            text = locale_text(kind).encode() + b"\0"
            machine.mu.mem_write(buf, text)
            return len(text), 4

        def gs_failure(current):
            """IsDebuggerPresent, first call of the CRT's stack-cookie failure report: stop the run."""
            self.gs_failed = True
            machine.mu.emu_stop()
            return 0, 0

        return {
            "IsDebuggerPresent": gs_failure,
            "GetTimeZoneInformation": tz,
            "GetSystemTime": systime,
            "GetLocalTime": localtime,
            "SystemTimeToFileTime": to_filetime,
            "GetSystemDefaultLCID": lambda current: (0x0409, 0),
            "GetUserDefaultLCID": lambda current: (0x0816, 0),
            "GetUserDefaultLangID": lambda current: (0xFEED0416 & 0xFFFFFFFF, 0),
            "GetLocaleInfoA": locale_info,
            NTOHS_ORD: lambda current: (((current.arg(0) & 0xFF) << 8) | ((current.arg(0) >> 8) & 0xFF), 1),
        }

    def _send(self, current):
        """FUN_0051c5c0 (fastcall: id in CL, data in EDX, length on the stack): log, snapshot the reply arrays."""
        ident = current.mu.reg_read(UC_X86_REG_ECX) & 0xFF
        data = current.read(current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)) if current.arg(0) else b""
        snapshot = (current.r32(REPLY_A + 4 * ident), current.r32(REPLY_B + 4 * ident))
        self.sends.append((ident, data, current.arg(0), snapshot))
        ret = self.send_ret
        return ret.pop(0) if isinstance(ret, list) else ret

    def _wait(self, current):
        """FUN_00520b70 (fastcall: id in CL): log, report the reply as arrived."""
        self.waits.append(current.mu.reg_read(UC_X86_REG_ECX) & 0xFF)
        return 1

    def _alloc(self, current):
        """Storm allocator: log (size, file, line, flags), hand out poisoned memory."""
        size = current.mu.reg_read(UC_X86_REG_ECX)
        file_name = current.mu.reg_read(UC_X86_REG_EDX)
        self.allocs.append((size, file_name, current.arg(0), current.arg(1)))
        pointer = current.alloc(max(size, 1) + 16, zero=False)
        current.mu.mem_write(pointer, b"\xCD" * (max(size, 1) + 16))
        return pointer

    def _free(self, current):
        """Storm free: log (pointer, file, line, flags)."""
        self.frees.append((current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                           current.arg(0), current.arg(1)))
        return 1

    def _hash(self, current):
        """FUN_005209d0 (fastcall: out in ECX, data in EDX, length on the stack): log, write SHA-1."""
        out, data = current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)
        length = current.arg(0)
        blob = current.read(data, length) if length else b""
        self.hashes.append(blob)
        current.mu.mem_write(out, hashlib.sha1(blob).digest())
        return 0

    def _crash(self, current):
        """FUN_0040d290 (ECX buffer, EDX &length): scripted result and bytes."""
        buf, plen = current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)
        self.crash_calls.append(current.r32(plen))
        count, blob = self.crash
        current.mu.mem_write(buf, blob)
        current.w32(plen, count)
        return 1 if blob or count else 0

    def _fatal(self, current):
        """The fatal report: log its line argument."""
        self.fatals.append(current.arg(2))
        return 0

    def _exit(self, current):
        """exit(-1): stop the run."""
        self.exited = True
        current.mu.emu_stop()
        return 0

    def reset(self):
        """Clear logs and reply arrays between cases."""
        for log in (self.sends, self.waits, self.allocs, self.frees, self.hashes, self.fatals,
                    self.locale_calls, self.crash_calls):
            log.clear()
        self.exited = False
        self.gs_failed = False
        self.send_ret = 0
        self.m.mu.mem_write(REPLY_A, b"\xA5" * 0x400)
        self.m.mu.mem_write(REPLY_B, b"\x5A" * 0x400)

    def run(self, address, *stack, ecx=0, edx=0):
        """Call one function; return EAX (None when it never returned)."""
        before = len(self.sends)
        eax = self.m.call(address, *stack, ecx=ecx, edx=edx, limit=LIMIT)
        done = self.m.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC
        require(done or self.exited or self.gs_failed, f"{address:#x}: instruction cap")
        self.ran = len(self.sends) - before
        return eax if done else None  # None: fatal exit or stack-cookie failure

    def cstr_arg(self, text):
        """Pointer to a NUL-terminated string (None gives a null pointer)."""
        return 0 if text is None else self.m.put(text + b"\0")


def locale_text(kind):
    """The scripted GetLocaleInfoA string for an LCTYPE."""
    return f"L{kind:x}" + "x" * (kind % 5)


def strings_for(rng):
    """A spread of lengths around the 0xFF copy limit, plus empty and null."""
    pool = [None, b"", b"a", b"Hello", b"u" * 0xFD, b"x" * 0xFE, b"y" * 0xFF, b"z" * 0x100, b"w" * 0x101, b"v" * 300]
    for _ in range(3):
        pool.append(bytes(rng.randrange(1, 255) for _ in range(rng.randrange(1, 60))))
    return pool


def expect_sends(world, expected, what):
    """The logged sends must equal [(id, data)] with the reply arrays reset first."""
    got = [(i, d) for i, d, n, snap in world.sends]
    require(got == expected, f"{what}: sent {[(i, d[:24]) for i, d in got]} != {[(i, d[:24]) for i, d in expected]}")
    for _, data, length, _ in world.sends:
        require(length == len(data), f"{what}: length {length}")


def expect_cookie(world, total, what):
    """A 0x100-byte local string area: more than 0x100 bytes smashes the stack cookie (0x101: the NUL lands on the cookie low byte, untested)."""
    if total == 0x101:
        return
    limit = 0xFF if mut("cookie_limit") else 0x100
    require(world.gs_failed == (total > limit), f"{what}: {total:#x} string bytes, cookie failure {world.gs_failed}")


def product(world):
    """The product dword the senders write."""
    return PRODUCT[1] if world.expansion else PRODUCT[0]


def check_get_channel_list(world):
    """FUN_0051ac50: the 0x0B request and its reply pointer."""
    n = 0
    for expansion in (0, 1):
        for flag in (0, 1, 7):
            world.reset()
            world.expansion = expansion
            world.m.w32(CHANNEL_COPY, 0xC0DE0000 + flag)
            eax = world.run(0x51AC50, ecx=flag)
            want = 1 if (expansion and flag == 0) else 0
            if mut("channel_flag"):
                want = 1 if expansion else 0
            expect_sends(world, [(0x0B, u32(want))], "channel list")
            require(world.sends[0][3] == (0, 0), f"reply arrays not reset before send: {world.sends[0][3]}")
            require(world.waits == [0x0B], f"wait {world.waits}")
            require(eax == 0xC0DE0000 + flag, f"channel list pointer {eax:#x}")
            n += 1
    return n


def check_join_channel(world):
    """FUN_0051aca0: the 0x0C join request: flags then the channel name."""
    n = 0
    rng = random.Random(1)
    for name in strings_for(rng):
        for expansion in (0, 1):
            for second in (0, 1):
                for third in (0, 1):
                    world.reset()
                    world.expansion = expansion
                    pointer = world.cstr_arg(name)
                    world.run(0x51ACA0, third, ecx=pointer, edx=second)
                    if name:
                        flags = 2 if second else 0
                        text = name
                    else:
                        flags = 1 | (4 if expansion and not third else 0)
                        if mut("join_default_flags"):
                            flags = 1
                        text = b"Diablo II"
                    expect_sends(world, [(0x0C, u32(flags) + copied(text) + b"\0")], f"join {name!r}")
                    n += 1
    return n


def stuff_model(buf, length):
    """FUN_0051ad70's loop on a 0x800-byte image: returns (result, buffer)."""
    buf = bytearray(buf)
    position, size = 0, length
    while position < size:
        if position == 0xFF:
            return 0, bytes(buf)
        value = buf[position]
        if value in (0, 0xFF):
            window = bytes(buf[position + 1:position + 0x101])
            buf[position + 2:position + 2 + 0x100] = window
            buf[position + 1] = 1 if value == 0 else 0xFF
            buf[position] = 0xFF
            size += 1
            position += 2
        else:
            position += 1
    return 1, bytes(buf)


def check_byte_stuff(world):
    """FUN_0051ad70: in-place 00 -> FF 01 and FF -> FF FF, refused when it reaches offset 0xFF."""
    rng = random.Random(7)
    n = 0
    cases = [b"", b"abc", b"\0", b"\xff", b"\0\xff\0\xff", bytes(range(1, 40)), b"\x00" * 100, b"\xff" * 127,
             b"\x00" * 128, b"\x00" * 200, bytes([1] * 253 + [0]), bytes([1] * 254 + [0, 5]),
             bytes([1] * 254 + [2, 3]), bytes([1] * 255), bytes([1] * 255 + [0]), bytes([1] * 253 + [0, 0])]
    for _ in range(60):
        size = rng.choice((1, 2, 5, 30, 120, 126, 127, 128, 200, 250, 254, 255, 256))
        cases.append(bytes(rng.choice((0, 0xFF, 0x41, 0x7F)) for _ in range(size)))
    for data in cases:
        world.reset()
        image = bytes(data) + bytes((0x80 + i) % 256 for i in range(0x800 - len(data)))
        pointer = world.m.alloc(0x800 + 0x20, zero=False)
        world.m.mu.mem_write(pointer, image)
        eax = world.run(0x51AD70, ecx=pointer, edx=len(data))
        want, wbuf = stuff_model(image, len(data)) if len(data) else (1, image)
        if mut("stuff_limit") and want == 0:
            want = 1
        require(eax == want, f"stuff {data[:12].hex()} len {len(data)}: result {eax} != {want}")
        got = world.m.read(pointer, 0x800)
        require(got == wbuf, f"stuff {data[:12].hex()} len {len(data)}: buffer differs")
        n += 1
    return n


def check_enter_chat(world):
    """FUN_0051ae10: the product dword (id 0x0B), then the 0x0A user and statstring."""
    rng = random.Random(2)
    n = 0
    pool = strings_for(rng)
    for first in pool:
        for second in pool[::2]:
            for expansion in (0, 1):
                for ok in (True, False):
                    world.reset()
                    world.expansion = expansion
                    world.send_ret = [0, 0 if ok else -1]
                    eax = world.run(0x51AE10, 0, ecx=world.cstr_arg(first), edx=world.cstr_arg(second))
                    body = copied(first) + b"\0" + copied(second) + b"\0"
                    expect_sends(world, [(0x0B, u32(product(world))), (0x0A, body)], f"enter chat {first!r} {second!r}")
                    require(eax == (1 if ok else 0), f"enter chat result {eax} for send ok={ok}")
                    n += 1
    return n


def check_leave_chat(world):
    """FUN_0051af20: the empty 0x10 packet and its >= 0 result."""
    n = 0
    for ret in (0, 5, -1, -7):
        world.reset()
        world.send_ret = ret
        eax = world.run(0x51AF20)
        expect_sends(world, [(0x10, b"")], "leave chat")
        require(eax == (1 if ret >= 0 else 0), f"leave chat {ret}: {eax}")
        n += 1
    return n


CP1252_HOLES = (0x81, 0x8D, 0x8F, 0x90, 0x9D)


def chat_model(text):
    """FUN_0051af40's packet: ANSI (cp1252) text to UTF-16 and on to UTF-8, NUL included; None when a buffer overflows.

    The five bytes cp1252 leaves undefined become U+FFFF. Both conversions
    take at most 0x1FF units including the NUL.
    """
    wide = []
    for byte in text:
        wide.append(0xFFFF if byte in CP1252_HOLES else ord(bytes([byte]).decode("cp1252")))
    limit = 0x1FE if mut("chat_limit") else 0x1FF
    if len(wide) + 1 > limit:
        return None
    out = b"".join(chr(c).encode("utf-8", "surrogatepass") for c in wide)
    if len(out) + 1 > limit:
        return None
    return out + b"\0"


def check_chat_command(world):
    """FUN_0051af40: the 0x0E chat line through its cp1252 -> UTF-16 -> UTF-8 round trip and its size limits."""
    rng = random.Random(9)
    n = 0
    texts = [None, b"", b"/w bob hi", b"caf\xe9 \xff!", b"\xc3\xa9", b"\xe2\x82\xac"]
    texts += [bytes([b, 0x41]) for b in range(1, 256)]
    texts += [b"x" * k for k in (0x1FD, 0x1FE, 0x1FF, 0x200, 600)]
    texts += [b"\xe9" * k for k in (254, 255, 256)] + [b"\x80" * k for k in (169, 170, 171)]
    texts += [bytes(rng.randrange(1, 256) for _ in range(rng.randrange(1, 300))) for _ in range(40)]
    for text in texts:
        world.reset()
        world.run(0x51AF40, ecx=world.cstr_arg(text))
        want = chat_model(text) if text else None
        expect_sends(world, [(0x0E, want)] if want is not None else [], f"chat command {text!r}"[:80])
        n += 1
    return n


def check_check_ad(world):
    """FUN_0051b010 and FUN_0051b060: the 0x15 and 0x21 ad packets."""
    n = 0
    for expansion in (0, 1):
        for banner in (0, 0x12345678):
            world.reset()
            world.expansion = expansion
            world.m.w32(BANNER, banner)
            eax = world.run(0x51B010)
            expect_sends(world, [(0x15, u32(0x49583836, product(world), banner, 0x5F000000))], "check ad")
            require(eax == 1, f"check ad result {eax}")
            world.reset()
            world.expansion = expansion
            world.m.w32(BANNER, banner)
            world.run(0x51B060)
            expect_sends(world, [(0x21, u32(0x49583836, product(world), banner)[:12] + b"\0\0")], "display ad")
            n += 2
    return n


def check_start_game(world):
    """FUN_0051b0e0: the 0x1C start-game packet: flags, field, name, password, description."""
    rng = random.Random(3)
    n = 0
    pool = strings_for(rng)
    for name in pool[::2]:
        for password in pool[:6]:
            for desc in pool[::3]:
                world.reset()
                field = rng.getrandbits(32)
                world.run(0x51B0E0, world.cstr_arg(desc), field, ecx=world.cstr_arg(name), edx=world.cstr_arg(password))
                has_password = 1 if password and password[:1] != b"\0" and len(password) > 0 else 0
                if mut("start_password_flag"):
                    has_password = 1 if password is not None else 0
                body = u32(has_password, 0, field, 0, 0) + cstrings(name, password, desc)
                expect_sends(world, [(0x1C, body)], f"start game {name!r}")
                expect_cookie(world, len(cstrings(name, password, desc)), f"start game {name!r}")
                n += 1
    return n


def check_reply_arrays(world):
    """FUN_0051b1f0: ECX out and EAX are the two per-id reply arrays at 0x1C."""
    world.reset()
    world.m.w32(REPLY_B + 4 * 0x1C, 0x11112222)
    world.m.w32(REPLY_A + 4 * 0x1C, 0x33334444)
    out = world.m.alloc(8)
    eax = world.run(0x51B1F0, ecx=out)
    require(world.m.r32(out) == 0x11112222 and eax == 0x33334444, f"1C reply out {world.m.r32(out):#x}, eax {eax:#x}")
    return 1


def check_frees(world):
    """FUN_0051ac20 and FUN_0051b210: free the saved reply copy once, then clear the pointer."""
    n = 0
    for address, slot, line in ((0x51AC20, CHANNEL_COPY, 0x1EF), (0x51B210, ADV_COPY, 0x36A),
                                (0x51B3B0, LOGON_COPY, 0x3B2)):
        for old in (0, 0x00ABC000):
            world.reset()
            world.m.w32(slot, old)
            world.run(address)
            want = [(old, FILE_NAME, line, 0)] if old else []
            require(world.frees == want, f"free {address:#x} old={old:#x}: {world.frees}")
            require(world.m.r32(slot) == 0, f"{address:#x} did not clear its pointer")
            n += 1
    return n


def check_get_adv_list(world):
    """FUN_0051b240: the 0x09 game list request."""
    rng = random.Random(4)
    n = 0
    pool = strings_for(rng)
    for first in pool:
        for second in pool[::2]:
            world.reset()
            world.m.w32(ADV_COPY, 0xADD00000)
            eax = world.run(0x51B240, 0, 0, ecx=world.cstr_arg(first), edx=world.cstr_arg(second))
            body = u32(0, 0, 0, 0x19) + cstrings(first, second, b"")
            expect_sends(world, [(0x09, body)], f"adv list {first!r} {second!r}")
            expect_cookie(world, len(cstrings(first, second, b"")), f"adv list {first!r}")
            require(world.sends[0][3] == (0, 0), "adv list reset")
            if not world.gs_failed:
                require(world.waits == [0x09] and eax == 0xADD00000, f"adv list wait/pointer {world.waits} {eax}")
            n += 1
    return n


def check_notify_join(world):
    """FUN_0051b320: the 0x22 packet: product, 0x0E, game name, user."""
    rng = random.Random(5)
    n = 0
    pool = [t for t in strings_for(rng) if t is not None]   # a null leaves stack garbage in the packet
    for first in pool:
        for second in pool[::2]:
            for expansion in (0, 1):
                world.reset()
                world.expansion = expansion
                world.run(0x51B320, ecx=world.cstr_arg(first), edx=world.cstr_arg(second))
                body = u32(product(world), 0x0E) + cstrings(first, second)
                expect_sends(world, [(0x22, body)], f"notify join {first!r} {second!r}")
                n += 1
    return n


def check_logon_copy(world):
    """FUN_0051b3e0: keep a copy of the 0x3E reply with its size, swap the status word."""
    n = 0
    for size in (0, 4, 8, 0x47, 0x48, 0x49, 0x80):
        world.reset()
        world.m.w32(LOGON_COPY, 0xBADBAD00)
        blob = bytes((i * 7 + 3) % 256 for i in range(size))
        pointer = world.m.put(blob + b"\0" * 8)
        world.run(0x51B3E0, ecx=pointer, edx=size)
        want_free = [(0xBADBAD00, FILE_NAME, 0x3B2, 0)]
        require(world.frees == want_free, f"logon copy free {world.frees}")
        require(len(world.allocs) == 1 and world.allocs[0][:1] == (size + 4,) and world.allocs[0][2] == 0x3BB,
                f"logon copy alloc {world.allocs}")
        copy = world.m.r32(LOGON_COPY)
        want = bytearray(u32(size) + blob)
        if size != 8 and size >= 0x48:
            want[0x18:0x1C] = u32(((blob[0x14] << 8) | blob[0x15]) if not mut("ntohs") else blob[0x14] | (blob[0x15] << 8))
        require(world.m.read(copy, size + 4) == bytes(want), f"logon copy size {size}: {world.m.read(copy, size + 4).hex()}")
        n += 1
    return n


def check_logon_realm(world):
    """FUN_0051b460: the 0x3E realm logon: counter, hash of the password, realm title."""
    rng = random.Random(6)
    n = 0
    pool = strings_for(rng)
    for title in pool:
        for password in [p for p in pool[:7] if p is not None]:   # a null password is read by strlen and faults
            world.reset()
            token = rng.getrandbits(32)
            counter = rng.getrandbits(24)
            world.m.w32(TOKEN, token)
            world.m.w32(COUNTER, counter)
            world.m.w32(LOGON_COPY, 0xFACE0000)
            title_ptr = world.cstr_arg(title)
            eax = world.run(0x51B460, ecx=title_ptr, edx=world.cstr_arg(password))
            if not title:
                if title is None:
                    require(not world.sends and eax == 0, "null realm title still sent")
                    n += 1
                    continue
            new_counter = (counter + 1) & 0xFFFFFFFF
            pw = password if password is not None else b""
            first = hashlib.sha1(pw).digest()
            second_in = u32(new_counter, token) + first
            second = hashlib.sha1(second_in).digest()
            body = u32(new_counter) + second + copied(title) + b"\0"
            if mut("realm_hash_order"):
                body = u32(new_counter) + first + copied(title) + b"\0"
            expect_sends(world, [(0x3E, body)], f"realm logon {title!r}")
            require(world.hashes == [pw, second_in], f"realm hashes {world.hashes}")
            require(world.m.r32(COUNTER) == new_counter, "counter not incremented")
            require(world.sends[0][3] == (0, 0) and world.waits == [0x3E], "realm reset/wait")
            require(eax == 0xFACE0000, f"realm logon returned {eax:#x}")
            n += 1
    return n


def check_realm_send_fail(world):
    """FUN_0051b460: a failed send returns 0 without waiting."""
    world.reset()
    world.send_ret = -1
    world.m.w32(LOGON_COPY, 0xFACE0000)
    eax = world.run(0x51B460, ecx=world.cstr_arg(b"realm"), edx=world.cstr_arg(b"pw"))
    require(eax == 0 and not world.waits, f"realm send failure: eax {eax} waits {world.waits}")
    return 1


def check_locale(world):
    """FUN_0051b540: the 0x12 locale packet from the scripted Windows calls."""
    world.reset()
    world.run(0x51B540)
    require(len(world.sends) == 1 and world.sends[0][0] == 0x12, f"locale sends {world.sends}")
    data = world.sends[0][1]
    require(len(data) >= 0x20, f"locale packet too short: {len(data)}")
    texts = b"".join(locale_text(k).encode() + b"\0" for k in (3, 5, 7, 0x1002))
    require(len(data) == 0x20 + len(texts), f"locale length {len(data)}")
    require(data[0x20:] == texts, f"locale strings {data[0x20:]!r}")
    fields = struct.unpack("<QQiIII", data[:0x20])
    langid = 0x0415 if mut("langid") else 0x0416
    require(fields[2:] == (-120, 0x0409, 0x0816, langid), f"locale header words {fields[2:]}")
    require(fields[0] != fields[1] and fields[0] and fields[1], f"locale filetimes {fields[:2]}")
    require(world.locale_calls.count((0x400, 3, 0x40)) == 1 and (0x400, 0x1002, 0x40) in world.locale_calls,
            f"locale calls {world.locale_calls}")
    world.reset()
    world.locale_fail = True
    world.run(0x51B540)
    world.locale_fail = False
    require(world.sends[0][1][0x20] == 0, "locale buffer's first byte not cleared")
    return 2


def check_click_ad(world):
    """FUN_0051b6c0: the 0x16 click: ad id, then whether EDX is non-zero."""
    n = 0
    for ad in (0, 1, 0x7FFFFFFF, 0xFFFFFFFF):
        for flag in (0, 1, 0x100, 0x80000000):
            world.reset()
            eax = world.run(0x51B6C0, ecx=ad, edx=flag)
            expect_sends(world, [(0x16, u32(ad, 1 if flag else 0))], "click ad")
            require(eax == 1, "click ad result")
            n += 1
    return n


def pointer_array(world, items):
    """An array of C-string pointers."""
    return world.m.put(u32(*[world.cstr_arg(i) for i in items]) if items else b"\0" * 4)


def check_user_data(world):
    """FUN_0051b6f0 (0x27 write) and FUN_0051b870 (0x26 read)."""
    rng = random.Random(8)
    n = 0
    names = [b"acct", b"a" * 40, b"A" * 0x30]
    keysets = [[], [b"k"], [b"System\\Age", b"Profile\\Location"], [b"a", b"b", b"c", b"d"]]
    for account in names:
        for keys in keysets:
            for ok in (True, False):
                world.reset()
                values = [bytes(rng.randrange(1, 255) for _ in range(rng.randrange(0, 12))) for _ in keys]
                world.send_ret = 0 if ok else -1
                eax = world.run(0x51B6F0, pointer_array(world, keys), pointer_array(world, values),
                                ecx=world.cstr_arg(account), edx=len(keys))
                body = u32(1, len(keys)) + account + b"\0" + b"".join(k + b"\0" for k in keys) + b"".join(v + b"\0" for v in values)
                expect_sends(world, [(0x27, body)], f"write userdata {account!r} {keys}")
                require(world.sends[0][3] == (0, 0), "write userdata arrays not reset")
                require(len(world.allocs) == 1 and world.allocs[0][0] == len(body) and world.allocs[0][2:] == (0x4C4, 0)
                        and len(world.frees) == 1
                        and world.frees[0][2:] == (0x4E0, 0), f"write userdata alloc/free {world.allocs} {world.frees}")
                require(world.frees[0][0] != 0 and world.frees[0][0] <= 0xFFFFFFFF, "free pointer")
                require(eax == (1 if ok else 0), f"write userdata result {eax}")
                n += 1
                world.reset()
                request = rng.getrandbits(32)
                world.send_ret = 0 if ok else -1
                eax = world.run(0x51B870, request, pointer_array(world, keys), ecx=world.cstr_arg(account), edx=len(keys))
                body = u32(1, len(keys), request) + account + b"\0" + b"".join(k + b"\0" for k in keys)
                expect_sends(world, [(0x26, body)], f"read userdata {account!r} {keys}")
                require(world.sends[0][3] == (0, 0), "read userdata arrays not reset")
                require(len(world.allocs) == 1 and world.allocs[0][0] == len(body) and world.allocs[0][2:] == (0x506, 0)
                        and len(world.frees) == 1
                        and world.frees[0][2:] == (0x521, 0), f"read userdata alloc/free {world.allocs} {world.frees}")
                require(eax == (1 if ok else 0), f"read userdata result {eax}")
                n += 1
    return n


def check_user_data_null(world):
    """FUN_0051b6f0 and FUN_0051b870 with a null string: strlen reports error 0x57 and the packet sees a zero length."""
    world.reset()
    world.run(0x51B870, 3, pointer_array(world, [None]), ecx=world.cstr_arg(b"acct"), edx=1)
    require(world.sends and world.sends[0][0] == 0x26, f"null key read: {world.sends}")
    return 1


def check_userdata_reply(world):
    """FUN_0051b820 copy and FUN_0051b960 parse of the 0x26 reply."""
    n = 0
    for old in (0, 0x00123000):
        for size in (0, 1, 17, 40):
            world.reset()
            world.m.w32(USERDATA_COPY, old)
            blob = bytes((5 * i + 1) % 256 for i in range(size))
            world.run(0x51B820, ecx=world.m.put(blob + b"\0" * 8), edx=size)
            require(world.frees == ([(old, FILE_NAME, 0x4EC, 0)] if old else []), f"userdata copy free {world.frees}")
            require(len(world.allocs) == 1 and world.allocs[0][2] == 0x4ED, f"userdata copy alloc {world.allocs}")
            require(world.m.read(world.m.r32(USERDATA_COPY), size) == blob, "userdata copy bytes")
            n += 1
    values = [b"one", b"", b"three" * 9, b"4"]
    for count in (0, 1, 2, 3, 4):
        for request in (0x77, 0x78):
            for ready in (0, 1):
                for accounts in (1, 2):
                    for length_limit in (2, 6, 60):
                        world.reset()
                        world.m.w32(REPLY_A + 4 * 0x26, ready)
                        blob = u32(accounts, count, 0x77) + b"".join(v + b"\0" for v in values[:count])
                        world.m.w32(USERDATA_COPY, world.m.put(blob + b"\0" * 8))
                        buffers = [world.m.alloc(128, zero=False) for _ in range(count)]
                        for buffer in buffers:
                            world.m.mu.mem_write(buffer, b"\xEE" * 128)
                        eax = world.run(0x51B960, world.m.put(u32(*buffers) + b"\0" * 4),
                                        world.m.put(u32(*([length_limit] * count)) + b"\0" * 4),
                                        ecx=count, edx=request)
                        want = 1 if (ready and accounts == 1 and request == 0x77) else 0
                        require(eax == want, f"userdata parse result {eax} != {want}")
                        if want:
                            for index, buffer in enumerate(buffers):
                                value = values[index]
                                got = world.m.read(buffer, length_limit + 2)
                                if value:
                                    expected = value[:length_limit].ljust(length_limit, b"\0") + b"\0" + b"\xEE"
                                else:
                                    expected = b"\0" + b"\xEE" * (length_limit + 1)
                                require(got == expected, f"userdata parse value {index} {got!r} vs {expected!r}")
                        n += 1
    return n


def account_model(texts, first_lines, long_lines, fatal_shift=0):
    """The shared shape of FUN_0051baa0 and FUN_0051bb90: ('fatal', line), ('quiet',) or ('send', bytes).

    A null or empty first string is fatal (line by null/empty); a null or empty
    later one returns quietly; only then are lengths checked in order (strlen + 1
    over 0x100 is fatal), so a quiet return beats a too-long first string.
    """
    first = texts[0]
    if first is None:
        return ("fatal", first_lines[0])
    if first == b"":
        return ("fatal", first_lines[1])
    if any(not t for t in texts[1:]):
        return ("quiet",)
    for text, line in zip(texts, long_lines):
        if len(text) + 1 > (0x101 if mut("mail_limit") else 0x100):
            return ("fatal", line)
    return ("send", b"".join(t + b"\0" for t in texts))


def check_account_mail(world):
    """FUN_0051ba00, FUN_0051baa0, FUN_0051bb90: the 0x59, 0x5A and 0x5B account strings, their fatal lines and quiet returns."""
    n = 0
    for text in (b"user@x", b"j" * 0xFF, b"q" * 0x100, b"q" * 0x101, None):
        for ok in (True, False):
            world.reset()
            world.send_ret = 0 if ok else -1
            eax = world.run(0x51BA00, ecx=world.cstr_arg(text))
            if text is None:
                require(world.fatals == [0x55D] and not world.sends, f"set email null {world.fatals}")
            elif len(text) >= 0x100:
                require(world.fatals == [0x565] and not world.sends, f"set email long {world.fatals}")
            else:
                expect_sends(world, [(0x59, text + b"\0")], "set email")
                require(not world.fatals and eax == (1 if ok else 0), f"set email result {eax}")
            n += 1
    sixty = [None, b"", b"a", b"mail@x", b"a" * 0xFE, b"b" * 0xFF, b"c" * 0x100, b"d" * 0x101]
    for first in sixty:
        for second in sixty[::1]:
            for ok in (True, False):
                world.reset()
                world.send_ret = 0 if ok else -1
                eax = world.run(0x51BAA0, ecx=world.cstr_arg(first), edx=world.cstr_arg(second))
                kind = account_model((first, second), (1400, 1401), (1409, 1416))
                n += check_account_outcome(world, kind, 0x5A, eax, ok, f"reset password {first!r:.20} {second!r:.20}")
    trio_pool = [None, b"", b"a", b"b" * 0xFF, b"c" * 0x100]
    for first in trio_pool:
        for second in trio_pool:
            for third in trio_pool:
                for ok in (True, False):
                    world.reset()
                    world.send_ret = 0 if ok else -1
                    eax = world.run(0x51BB90, world.cstr_arg(third), ecx=world.cstr_arg(first), edx=world.cstr_arg(second))
                    kind = account_model((first, second, third), (1433, 1434), (1444, 1452, 1460))
                    n += check_account_outcome(world, kind, 0x5B, eax, ok, f"change email {first!r:.12} {second!r:.12} {third!r:.12}")
    return n


def check_account_outcome(world, kind, ident, eax, ok, what):
    """Compare one run to account_model's outcome; returns 1."""
    if kind[0] == "fatal":
        require(world.fatals == [kind[1]] and not world.sends and world.exited, f"{what}: fatal {world.fatals}, want {kind[1]}")
    elif kind[0] == "quiet":
        require(not world.fatals and not world.sends and eax == 0, f"{what}: quiet return gave {world.fatals} {world.sends} {eax}")
    else:
        expect_sends(world, [(ident, kind[1])], what)
        require(not world.fatals and eax == (1 if ok else 0), f"{what}: result {eax}")
    return 1


def check_crash_report(world):
    """FUN_0051bcf0: the 0x5D crash report: sent only when the reader found something, with its length."""
    n = 0
    for count, blob in ((0, b""), (5, b"hello"), (0x5DC, bytes(range(256)) * 5 + bytes(range(220)))):
        world.reset()
        world.crash = (count, blob)
        world.run(0x51BCF0)
        require(world.crash_calls == [0x5DC], f"crash reader capacity {world.crash_calls}")
        if blob:
            expect_sends(world, [(0x5D, blob[:count])], "crash report")
        else:
            require(not world.sends, "crash report sent without data")
        n += 1
    return n


def check_reply_copies(world):
    """FUN_0051bd60, FUN_0051bdc0, FUN_0051be20: keep a copy of the realm, channel and game list replies."""
    n = 0
    for address, slot, free_line, alloc_line in ((0x51BD60, REALM_COPY, 0x19A, 0x16B),
                                                  (0x51BDC0, CHANNEL_COPY, 0x1EF, 0x1E6),
                                                  (0x51BE20, ADV_COPY, 0x36A, 0x326)):
        for old in (0, 0x00ABC000):
            for size in (0, 3, 70):
                world.reset()
                world.m.w32(slot, old)
                blob = bytes((9 * i + 2) % 256 for i in range(size))
                world.run(address, ecx=world.m.put(blob + b"\0" * 8), edx=size)
                require(world.frees == ([(old, FILE_NAME, free_line, 0)] if old else []),
                        f"{address:#x} free {world.frees}")
                require(len(world.allocs) == 1 and world.allocs[0][2] == alloc_line, f"{address:#x} alloc {world.allocs}")
                copy = world.m.r32(slot)
                require(copy != 0 and world.m.read(copy, size) == blob, f"{address:#x} bytes")
                n += 1
    return n


CHECKS = [
    check_get_channel_list, check_join_channel, check_byte_stuff, check_enter_chat, check_leave_chat,
    check_chat_command, check_check_ad, check_start_game, check_reply_arrays, check_frees, check_get_adv_list,
    check_notify_join, check_logon_copy, check_logon_realm, check_realm_send_fail, check_locale, check_click_ad,
    check_user_data, check_user_data_null, check_userdata_reply, check_account_mail, check_crash_report,
    check_reply_copies,
]

MUTATIONS = {
    "copy_limit": check_enter_chat,
    "channel_flag": check_get_channel_list,
    "join_default_flags": check_join_channel,
    "stuff_limit": check_byte_stuff,
    "start_password_flag": check_start_game,
    "ntohs": check_logon_copy,
    "realm_hash_order": check_logon_realm,
    "langid": check_locale,
    "chat_limit": check_chat_command,
    "cookie_limit": check_start_game,
    "mail_limit": check_account_mail,
}


def build_world():
    """A new World from the stock game.exe."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    world = World(str(exe))
    world.chat_lengths = []
    return world


def main():
    """Run every check, or with --mutation-check prove each model defect is caught."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate model defect to fail")
    options = parser.parse_args()
    world = build_world()
    if options.mutation_check:
        for name, check in MUTATIONS.items():
            mutation = name
            try:
                check(world)
            except Mismatch:
                print(f"caught: {name}")
            else:
                raise SystemExit(f"mutation {name} was not caught")
            finally:
                mutation = None
        print(f"ok: {len(MUTATIONS)} model defects caught")
        return
    total = 0
    for check in CHECKS:
        count = check(world)
        total += count
    print(f"ok: {total} BnSend cases over {len(CHECKS)} checks")


if __name__ == "__main__":
    main()
