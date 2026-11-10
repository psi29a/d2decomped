# SPDX-License-Identifier: GPL-3.0-or-later
"""Check BnMessQueue.cpp's four routines against stock game.exe 1.14d.

Run: D2_MPQ_DIR=/path/to/data uv run python bnqueue.py
The allocator, free and Win32 critical-section calls are scripted and logged;
the queue routines and bounded string copy execute as stock x86 code. No game
process or network is started. These serial tests do not prove thread safety.
"""
import hashlib
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
PUSH, STRING, POP, CLEAR = 0x520210, 0x5202F0, 0x5203B0, 0x520440
HEAD, TAIL, LOCK = 0x882020, 0x882024, 0x882004


def require(ok, message):
    """Raise on a stock-code observation that differs from the expected result."""
    if not ok:
        raise AssertionError(message)


class Queue:
    """Run real queue instructions with a recorded heap and critical section."""

    def __init__(self, exe):
        """Create the emulator and install bounded allocator and lock hooks."""
        self.m = emu.Emu(str(exe))
        self.events = []
        self.live = set()
        self.locked = False
        self.m.hook(0x40B380, self.allocate, 2)
        self.m.hook(0x40B3C0, self.free, 2)
        for slot, (name, _) in list(self.m.slots.items()):
            if name in ("EnterCriticalSection", "LeaveCriticalSection"):
                self.m.slots[slot] = (name, self.enter if name.startswith("Enter") else self.leave)

    def allocate(self, m):
        """Allocate a poisoned node and record the size requested by stock."""
        size = m.mu.reg_read(UC_X86_REG_ECX)
        require(size == 0x12C, f"allocation size {size:#x}")
        ptr = m.alloc(size)
        m.mu.mem_write(ptr, b"\xcc" * size)
        self.live.add(ptr)
        self.events.append(("alloc", ptr))
        return ptr

    def free(self, m):
        """Record a node release and check it happens exactly once under lock."""
        ptr = m.mu.reg_read(UC_X86_REG_ECX)
        require(self.locked and ptr in self.live, f"invalid or unlocked free {ptr:#x}")
        self.live.remove(ptr)
        self.events.append(("free", ptr))
        return 1

    def enter(self, m):
        """Check the lock address and record one non-reentrant acquisition."""
        require(m.arg(0) == LOCK and not self.locked, "bad lock acquisition")
        self.locked = True
        self.events.append(("enter", LOCK))
        return 0, 1

    def leave(self, m):
        """Check and record the matching critical-section release."""
        require(m.arg(0) == LOCK and self.locked, "bad lock release")
        self.locked = False
        self.events.append(("leave", LOCK))
        return 0, 1

    def call(self, address, *args, ecx=0, edx=0):
        """Run one bounded call and check return, stack cleanup and lock balance."""
        result = self.m.call(address, *args, ecx=ecx, edx=edx, limit=200000)
        require(self.m.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "instruction limit")
        require(self.m.mu.reg_read(UC_X86_REG_ESP) == emu.STACK + emu.STACK_SIZE - 0x1000,
                f"stack cleanup at {address:#x}")
        require(not self.locked, "critical section left locked")
        return result

    def push(self, ident, data, string=False, length=None):
        """Enqueue bytes or a NUL-terminated string through the original entry."""
        ptr = self.m.put(data + (b"\0" if string else b""))
        if string:
            ret = self.call(STRING, ecx=ident, edx=ptr)
        else:
            ret = self.call(PUSH, len(data) if length is None else length, ecx=ident, edx=ptr)
        require(ret == 1, "push failed")

    def pop(self):
        """Return length, id and a poisoned output buffer after one stock pop."""
        ident = self.m.alloc(4)
        out = self.m.alloc(0x121)
        self.m.w32(ident, 0xDEADBEEF)
        self.m.mu.mem_write(out, b"\xdd" * 0x121)
        n = self.call(POP, ecx=ident, edx=out)
        return n, self.m.r32(ident), self.m.read(out, 0x121)


def main():
    """Exercise FIFO, bounds, empty and clear/reuse paths on the stock binary."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == SHA256, "requires stock game.exe 1.14d")
    q = Queue(exe)
    require(q.pop() == (0, 0xDEADBEEF, b"\xdd" * 0x121), "empty pop changed output")
    messages = [(11, b""), (12, b"abc"), (13, bytes(range(256)) * 2)]
    for ident, data in messages:
        q.push(ident, data)
    q.push(14, b"x" * 400, string=True)
    q.push(15, b"hi\0ignored", string=True)
    q.push(19, b"", string=True)
    q.push(20, b"z" * 0x120)
    expected = [(11, b""), (12, b"abc"), (13, messages[2][1][:0x120]),
                (14, b"x" * 0x11f + b"\0"), (15, b"hi\0"), (19, b"\0"),
                (20, b"z" * 0x120)]
    for ident, data in expected:
        n, got_id, out = q.pop()
        require((n, got_id, out[:n], out[n:]) == (len(data), ident, data, b"\xdd" * (0x121 - n)),
                f"FIFO or payload mismatch for id {ident}")
    require(not q.m.r32(HEAD) and not q.live, "drain leaked a node")
    old_tail = q.m.r32(TAIL)
    require(old_tail != 0, "drain unexpectedly cleared tail")
    q.push(16, b"again")
    require(q.m.r32(HEAD) == q.m.r32(TAIL) != old_tail, "enqueue after drain")
    q.push(17, b"more")
    require(q.call(CLEAR) == 1 and not q.m.r32(HEAD) and not q.live, "clear failed")
    require(q.call(CLEAR) == 1, "empty clear failed")
    q.push(18, b"reuse")
    n, ident, out = q.pop()
    require((n, ident, out[:n]) == (5, 18, b"reuse") and not q.live, "reuse after clear")
    require(sum(event[0] == "alloc" for event in q.events) == 10, "allocation count")
    require(sum(event[0] == "free" for event in q.events) == 10, "free count")
    print("ok: queue FIFO, zero length, limits, string truncation, clear and reuse")


if __name__ == "__main__":
    main()
