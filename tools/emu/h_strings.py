# SPDX-License-Identifier: GPL-3.0-or-later
"""Check 19 H/stpl.h string and ignore-list routines in stock 1.14d.

Run: D2_MPQ_DIR=/path/to/game-data python h_strings.py
Only game.exe's mapped code executes. String allocation, Storm string calls,
registry, file I/O, hash-table operations and virtual methods are synthetic
hooks; apart from reading the local executable, no registry, user ignore
file, MPQ or game process is accessed. See
docs/research/re/h-strings.md for the exact claims and limitations.
"""

import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

import emu


GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CLAIMED = {0x60dcb0, 0x60dd00, 0x60dd20, 0x60dd50, 0x60dd60, 0x60dd90,
           0x60ddb0, 0x60ddf0, 0x60de00, 0x60de30, 0x60dea0, 0x60df10,
           0x60dff0, 0x60e130, 0x60f870, 0x610d60, 0x610eb0, 0x610f90, 0x611050}
STPL_FILE, DELETE_TAG = 0x6e6218, 0x6cc824
VIRTUAL = 0x6615e4


def require(condition, message):
    """Reject a mismatching observation even with Python assertions disabled."""
    if not condition:
        raise AssertionError(message)


class Rig:
    """One clean mapped image, bounded calls and explicit external hooks."""

    def __init__(self, exe):
        """Load the selected stock image and initialize call accounting."""
        self.machine = emu.Emu(str(exe))
        self.log = []
        self.calls = 0
        self.covered = set()
        self.duplicate_fails = False
        self.machine.hook(0x414310, self.duplicate, 1)
        self.machine.hook(0x412650, self.free, 4)
        self.machine.hook(0x4135d0, self.copy, 3)
        self.machine.hook(0x413800, self.append, 3)
        self.machine.hook(0x413750, lambda current: len(self.raw(current.arg(0))), 1)

    def raw(self, pointer):
        """Read a bounded synthetic C string without decoding its bytes."""
        for length in range(4096):
            if self.machine.read(pointer + length, 1) == b"\0":
                return self.machine.read(pointer, length)
        raise AssertionError("synthetic string lacks a bounded terminator")

    def put(self, raw):
        """Allocate a synthetic, NUL-terminated string."""
        return self.machine.put(raw + b"\0")

    def duplicate(self, current):
        """Supply SStrDup storage, retaining its exact pointer and payload log."""
        source = current.arg(0)
        result = 0 if self.duplicate_fails else self.put(self.raw(source))
        self.log.append(("dup", source, result))
        return result

    def free(self, current):
        """Record SMemFree arguments without invalidating synthetic storage."""
        self.log.append(("free", *(current.arg(i) for i in range(4))))
        return 1

    def copy(self, current):
        """Model the bounded string-copy boundary, not the Storm implementation."""
        target, source, capacity = (current.arg(i) for i in range(3))
        self.log.append(("copy", target, source, capacity))
        if capacity:
            current.mu.mem_write(target, self.raw(source)[:capacity - 1] + b"\0")
        return target

    def append(self, current):
        """Model bounded concatenation for the caller's local text buffer."""
        target, source, capacity = (current.arg(i) for i in range(3))
        self.log.append(("append", target, source, capacity))
        if capacity:
            current.mu.mem_write(target, (self.raw(target) + self.raw(source))[:capacity - 1] + b"\0")
        return target

    def run(self, address, *args, ecx=0):
        """Require normal return and exact RET cleanup before accepting a case."""
        self.log.clear()
        result = self.machine.call(address, *args, ecx=ecx, limit=100000)
        require(self.machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC,
                f"{address:08x}: instruction budget exhausted")
        require(self.machine.mu.reg_read(UC_X86_REG_ESP) == emu.STACK + emu.STACK_SIZE - 0x1000,
                f"{address:08x}: wrong RET stack cleanup")
        self.covered.add(address)
        self.calls += 1
        return result


def objects(exe):
    """Check string wrappers, vtables, singleton deletion and persistent flags."""
    rig = Rig(exe)
    machine = rig.machine
    target, source = machine.alloc(0x200), machine.alloc(8)
    text = rig.put(b"a name")
    machine.w32(source, text)
    for address in (0x60dcb0, 0x60e130):
        for old in (0, rig.put(b"old")):
            for fails in (False, True):
                rig.duplicate_fails = fails
                machine.w32(target, old)
                result = rig.run(address, source, ecx=target)
                require(result == target, "assignment return")
                expected_prefix = [("free", old, STPL_FILE, 0x5e9, 0)] if old else []
                require(rig.log[:-1] == expected_prefix and rig.log[-1][:2] == ("dup", text),
                        f"assignment call sequence {rig.log}")
                require(machine.r32(target) == rig.log[-1][2], "assignment write")
                require(bool(machine.r32(target)) != fails, "assignment duplicate-failure propagation")
    rig.duplicate_fails = False
    require(rig.run(0x60dd00, text, ecx=target) == target and rig.log[0][:2] == ("dup", text),
            "string constructor")
    require(machine.r32(target) == rig.log[0][2], "constructor stored pointer")

    def compare(current):
        """Supply signed comparison results while checking the complete API arguments."""
        require(tuple(current.arg(i) for i in range(3)) == (text, text, 0x7fffffff), "comparison args")
        return comparison[0]

    comparison = [0]
    machine.hook(0x413590, compare, 3)
    machine.w32(target, text)
    for value in (0, 1, -1, 0x80000000):
        comparison[0] = value
        require(rig.run(0x60dd20, source, ecx=target) == int(value == 0), "string equality")
    rig.run(0x60dd50, ecx=target)
    require(machine.r32(target) == 0x6e6248, "base vtable")
    for flags in (0, 1, 2, 3, 0x100):
        machine.w32(target, 0x12345678)
        require(rig.run(0x60dd60, flags, ecx=target) == target, "deleting destructor return")
        require(machine.r32(target) == 0x6e6248, "deleting destructor vtable")
        expected = [("free", target, DELETE_TAG, 0xffffffff, 0)] if flags & 1 else []
        require(rig.log == expected, "deleting destructor flags")
    for pointer in (0, text):
        machine.w32(target, pointer)
        rig.run(0x60dd90, ecx=target)
        require(machine.r32(target) == pointer, "string destructor must not clear field")
        require(rig.log == ([("free", pointer, STPL_FILE, 0x5e4, 0)] if pointer else []), "string free")
    vtable = machine.alloc(0x40)
    machine.w32(vtable + 0x2c, VIRTUAL)
    machine.w32(target, vtable)

    def delete(current):
        """Record the singleton's virtual deleting-destructor invocation."""
        rig.log.append(("delete", current.mu.reg_read(UC_X86_REG_ECX), current.arg(0)))
        return 0

    machine.hook(VIRTUAL, delete, 1)
    for pointer in (0, target):
        machine.w32(0x96bc20, pointer)
        rig.run(0x60ddb0)
        require(machine.r32(0x96bc20) == 0 and rig.log == ([("delete", target, 1)] if pointer else []),
                "singleton deletion")

    def registry(current):
        """Record registry writes; no host registry is accessed."""
        rig.log.append(("registry", *(current.arg(i) for i in range(4))))
        return 1

    machine.hook(0x4150e0, registry, 4)
    for value in (0, 1, 2, 0xffffffff):
        rig.run(0x60de00, value, ecx=target)
        require(machine.r32(target + 0x184) == value, "persistent setter")
        require(rig.log == [("registry", 0x6cc8b8, 0x6e6278, 0, value)], "registry args")
        require(rig.run(0x60ddf0, ecx=target) == value, "persistent getter")
    return rig


def splits(exe):
    """Check name splitting, caps and the star-suffix source-index terminator."""
    rig = Rig(exe)
    machine = rig.machine
    destination = machine.alloc(320)
    fill = b"\xa5" * 320
    samples = (b"", b"plain", b"*acct", b"abc*acct", b"abc*", b"**", b"x" * 16,
               b"x" * 17, b"x" * 18, b"x" * 30 + b"*tail", b"abc*" + b"q" * 25)
    for raw in samples:
        pointer = rig.put(raw)
        for capacity in (0, 1, 2, 17, 18, 256):
            bound = (capacity - 1) & 0xffffffff
            prefix = raw.split(b"*", 1)[0][:min(17, bound)]
            expected = bytearray(fill)
            expected[:len(prefix) + 1] = prefix + b"\0"
            machine.mu.mem_write(destination, fill)
            rig.run(0x60de30, pointer, destination, capacity)
            require(machine.read(destination, 320) == expected, f"prefix {raw!r}/{capacity}")
            expected = bytearray(fill)
            star = raw.find(b"*")
            if star >= 0:
                take = min(len(raw) - star, 17, max(0, bound - star))
                expected[:take] = raw[star:star + take]
                expected[star + take] = 0
            machine.mu.mem_write(destination, fill)
            rig.run(0x60dea0, pointer, destination, capacity)
            require(machine.read(destination, 320) == expected, f"suffix source-index terminator {raw!r}/{capacity}")
            machine.mu.mem_write(destination, fill)
            rig.run(0x60dff0, pointer, destination, capacity)
            key = raw[star:star + 17] if star >= 0 else raw[:17]
            expected = bytearray(fill)
            expected[0] = 0
            if capacity:
                copied = key[:capacity - 1]
                expected[:len(copied) + 1] = copied + b"\0"
            require(machine.read(destination, 320) == expected, f"key {raw!r}/{capacity}")
    return rig


def table_wrappers(exe):
    """Check account-value copy and temporary string ownership on hash insertion."""
    rig = Rig(exe)
    machine = rig.machine
    owner, node, output = machine.alloc(0x200), machine.alloc(0x40), machine.alloc(80)
    name, value = rig.put(b"account"), rig.put(b"*character")
    machine.w32(node + 0x18, value)
    found = [0]

    def lookup(current):
        """Account table lookup is a call boundary, not a hash-table claim."""
        require(current.mu.reg_read(UC_X86_REG_ECX) == owner + 0x54 and current.arg(0) == name,
                "account lookup arguments")
        rig.log.append(("lookup", found[0]))
        return found[0]

    machine.hook(0x60f510, lookup, 1)
    for present in (False, True):
        for capacity in (0, 1, 5, 80):
            found[0] = node if present else 0
            machine.mu.mem_write(output, b"\xa5" * 80)
            require(rig.run(0x60f870, name, output, capacity, ecx=owner) == int(present), "account value found")
            expected = bytearray(b"\xa5" * 80)
            if present and capacity:
                copied = b"*character"[:capacity - 1]
                expected[:len(copied) + 1] = copied + b"\0"
            require(machine.read(output, 80) == expected, "account value output")
            require(len(rig.log) == (2 if present else 1), "account copy call gate")

    def hash_name(current):
        """Check hash input and return a controlled hash without running Storm."""
        require(tuple(current.arg(i) for i in range(3)) == (name, 0, 0), "hash arguments")
        rig.log.append(("hash", name))
        return 0x12345678

    def insert(current):
        """Observe the wrapper's owned temporary string and complete insert arguments."""
        temporary = current.arg(1)
        require(current.mu.reg_read(UC_X86_REG_ECX) == owner, "insert this")
        require((current.arg(0), current.arg(2), current.arg(3)) == (0x12345678, 7, 9), "insert arguments")
        rig.log.append(("insert", machine.r32(temporary)))
        return 0x12344321

    machine.hook(0x413ce0, hash_name, 3)
    machine.hook(0x610be0, insert, 4)
    for fails in (False, True):
        rig.duplicate_fails = fails
        require(rig.run(0x610d60, name, 7, 9, ecx=owner) == 0x12344321, "insert wrapper return")
        temporary = rig.log[0][2]
        expected = [("dup", name, temporary), ("hash", name), ("insert", temporary)]
        if temporary:
            expected.append(("free", temporary, STPL_FILE, 0x5e4, 0))
        require(rig.log == expected, "temporary string lifetime")
    return rig


def ignore_operations(exe):
    """Check canonical/as-given ignore dispatch and the synthetic append/read paths."""
    rig = Rig(exe)
    machine = rig.machine
    owner, vtable = machine.alloc(0x200), machine.alloc(0x40)
    machine.w32(owner, vtable)
    machine.w32(vtable + 0x20, VIRTUAL)
    machine.mu.mem_write(owner + 0x80, b"synthetic-ignore.txt\0")
    state = {"persistent": 1, "found": 0, "open": 0x1234, "close": 0, "lines": []}

    def persistent(current):
        """Observe virtual persistence checks without invoking host settings."""
        require(current.mu.reg_read(UC_X86_REG_ECX) == owner, "persistence this")
        rig.log.append(("persistent", state["persistent"]))
        return state["persistent"]

    def lookup(current):
        """Return a controlled table lookup result for the provided key."""
        rig.log.append(("lookup", current.mu.reg_read(UC_X86_REG_ECX), rig.raw(current.arg(0))))
        return state["found"]

    def insert(current):
        """Record which table receives which key; the actual hash insertion is outside this claim."""
        require((current.arg(1), current.arg(2)) == (0, 0), "ignore insert trailing args")
        rig.log.append(("insert", current.mu.reg_read(UC_X86_REG_ECX), rig.raw(current.arg(0))))
        return 0x4321

    def append_file(current):
        """Capture requested persistence text without opening a file."""
        require(current.mu.reg_read(UC_X86_REG_ECX) == owner, "append this")
        rig.log.append(("persist", rig.raw(current.arg(0))))
        return 1

    machine.hook(VIRTUAL, persistent, 0)
    machine.hook(0x60f8b0, lookup, 1)
    machine.hook(0x610d60, insert, 3)
    machine.hook(0x60df10, append_file, 1)
    for address, table_offset in ((0x610eb0, 4), (0x610f90, 0x2c)):
        for raw in (None, b"", b"abc*account", b"plain", b"*acct"):
            pointer = rig.put(raw) if raw is not None else 0
            for enabled in (0, 1):
                for found in (0, 0x9999):
                    state.update(persistent=enabled, found=found)
                    result = rig.run(address, pointer, ecx=owner)
                    active = bool(raw) and bool(enabled)
                    require(result == int(active), "ignore operation return")
                    semantic = [entry for entry in rig.log if entry[0] in ("lookup", "insert", "persist")]
                    key = raw[raw.find(b"*"):][:17] if raw and b"*" in raw and address == 0x610eb0 else raw
                    expected = []
                    if active:
                        expected.append(("lookup", owner + table_offset, key))
                        if not found:
                            expected.append(("insert", owner + table_offset, key))
                            expected.append(("persist", key if address == 0x610eb0 else b"!" + raw))
                    require(semantic == expected, f"ignore dispatch {address:08x}/{raw!r}/{enabled}/{found}")
    return rig


def files(exe):
    """Check append outcome and loader routing using only in-memory FILE stand-ins."""
    rig = Rig(exe)
    machine = rig.machine
    owner, vtable = machine.alloc(0x200), machine.alloc(0x40)
    machine.w32(owner, vtable)
    machine.w32(vtable + 0x20, VIRTUAL)
    machine.mu.mem_write(owner + 0x80, b"synthetic-ignore.txt\0")
    state = {"persistent": 1, "open": 0x1234, "close": 0, "lines": []}

    def persistent(current):
        """Return synthetic persistence and check the virtual this pointer."""
        require(current.mu.reg_read(UC_X86_REG_ECX) == owner, "file persistence this")
        return state["persistent"]

    def open_file(current):
        """Record fopen text and mode without touching a filesystem."""
        rig.log.append(("open", rig.raw(current.arg(0)), rig.raw(current.arg(1))))
        return state["open"]

    def write_file(current):
        """Capture the fwrite payload, dimensions and synthetic stream."""
        size, count, stream = (current.arg(i) for i in (1, 2, 3))
        rig.log.append(("write", current.read(current.arg(0), size), size, count, stream))
        return 0

    def close_file(current):
        """Supply fclose status; append returns this success rather than fwrite's count."""
        rig.log.append(("close", current.arg(0)))
        return state["close"]

    def read_line(current):
        """Supply one preselected fgets chunk and record the stream contract."""
        target, capacity, stream = (current.arg(i) for i in range(3))
        require((capacity, stream) == (256, state["open"]), "fgets arguments")
        if not state["lines"]:
            return 0
        raw = state["lines"].pop(0)
        require(len(raw) < capacity, "synthetic fgets chunk too long")
        current.mu.mem_write(target, raw + b"\0")
        return target

    def insert(current):
        """Capture the loader's selected table and text, without real insertion."""
        require((current.arg(1), current.arg(2)) == (0, 0), "loader trailing insert args")
        rig.log.append(("insert", current.mu.reg_read(UC_X86_REG_ECX), rig.raw(current.arg(0))))
        return 1

    machine.hook(VIRTUAL, persistent, 0)
    machine.hook(0x68239a, open_file, 0)
    machine.hook(0x684449, write_file, 0)
    machine.hook(0x681fdc, close_file, 0)
    machine.hook(0x688271, read_line, 0)
    machine.hook(0x610d60, insert, 3)
    newline = rig.raw(0x6ffcb0)
    for raw in (None, b"", b"plain", b"x" * 300):
        pointer = rig.put(raw) if raw is not None else 0
        for enabled in (0, 1):
            for opened in (0, 0x1234):
                for close_status in (0, -1):
                    state.update(persistent=enabled, open=opened, close=close_status)
                    machine.w32(owner + 0x7c, 0x7777)
                    result = rig.run(0x60df10, pointer, ecx=owner)
                    active = bool(raw) and bool(enabled)
                    require(result == int(active and bool(opened) and close_status == 0), "append result")
                    expected = []
                    if active:
                        expected.append(("open", b"synthetic-ignore.txt", b"at+"))
                        if opened:
                            payload = (raw[:255] + newline)[:255]
                            expected.extend((("write", payload, len(payload), 1, opened), ("close", opened)))
                    semantic = [entry for entry in rig.log if entry[0] in ("open", "write", "close")]
                    require(semantic == expected, "append file sequence")
                    require(machine.r32(owner + 0x7c) == (opened if active else 0x7777), "stored file handle")
    chunks = [b"foo\n", b"!acct\r\n", b"!\n", b"\n", b"bar\rjunk\n", b"x" * 255]
    for opened in (0, 0x1234):
        for lines in ([], chunks):
            state.update(open=opened, lines=list(lines))
            before = machine.read(owner, 0x200)
            rig.run(0x611050, ecx=owner)
            expected = [("open", b"synthetic-ignore.txt", b"rt")]
            if opened:
                for raw in lines:
                    stripped = raw.split(b"\r", 1)[0].split(b"\n", 1)[0]
                    special = stripped.startswith(b"!") and len(stripped) > 1
                    expected.append(("insert", owner + (0x2c if special else 4), stripped[1:] if special else stripped))
                expected.append(("close", opened))
            require(rig.log == expected, f"loader dispatch {opened}/{lines}")
            require(machine.read(owner, 0x200) == before, "loader changed owner record behind hooked insertion")
    return rig


def main():
    """Run the fixed synthetic corpus and require coverage of all nineteen entry points."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d")
    cases, covered = 0, set()
    for check in (objects, splits, table_wrappers, ignore_operations, files):
        rig = check(exe)
        cases += rig.calls
        covered.update(rig.covered)
    require(covered == CLAIMED, f"wrong entry coverage: missing {CLAIMED-covered}, extra {covered-CLAIMED}")
    print(f"PASS H strings: {len(covered)} functions, {cases} cases; normal return and RET cleanup checked")


if __name__ == "__main__":
    main()
