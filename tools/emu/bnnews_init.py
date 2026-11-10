# SPDX-License-Identifier: GPL-3.0-or-later
"""Check stock BNNews subscription initialization with scripted callees.

Run: D2_MPQ_DIR=<game-data> uv run python bnnews_init.py
Cache load, notification, packet send and free are intercepted; no cache,
network connection or game process is started.
"""
import hashlib
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP

SHA = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
ACTIVE, ITEM, HEADER = 0x881C24, 0x881C28, 0x881C2C
CAP, COUNT, DATA, FIRST, NEWEST = 0x881C30, 0x881C34, 0x881C38, 0x881C40, 0x881C44


def require(condition, message):
    """Fail when a stock observation differs from the expected sequence."""
    if not condition:
        raise AssertionError(message)


def run():
    """Verify reset, optional cached notification and the request packet."""
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == SHA, "requires stock 1.14d game.exe")
    m = emu.Emu(str(exe))
    events = []
    loaded = [0]

    def free(current):
        """Record release of the previous list buffer."""
        events.append(("free", current.arg(0)))
        return 0

    def load(current):
        """Check reset state and script success and newest cache timestamp."""
        events.append(("load", tuple(current.r32(a) for a in (CAP, COUNT, DATA, FIRST, NEWEST))))
        if loaded[0]:
            current.w32(NEWEST, 0x12345678)
        return loaded[0]

    def notify(_current):
        """Record the request to deliver cached entries."""
        events.append(("notify",))
        return 0

    def send(current):
        """Record the packet id, payload and length without opening a socket."""
        ident = current.mu.reg_read(UC_X86_REG_ECX) & 0xFF
        data = current.mu.reg_read(UC_X86_REG_EDX)
        events.append(("send", ident, current.read(data, current.arg(0))))
        return 0

    m.hook(0x412650, free, 4)
    m.hook(0x51DA30, load, 0)
    m.hook(0x51D510, notify, 0)
    m.hook(0x51C5C0, send, 1)
    for success in (0, 1):
        events.clear()
        loaded[0] = success
        old = m.alloc(8)
        for address, value in ((CAP, 8), (COUNT, 3), (DATA, old), (FIRST, 2), (NEWEST, 99)):
            m.w32(address, value)
        result = m.call(0x51DBF0, ecx=0x1234, edx=0x5678, limit=100000)
        require(m.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, "init did not return")
        require(m.mu.reg_read(UC_X86_REG_ESP) == emu.STACK + emu.STACK_SIZE - 0x1000, "stack cleanup")
        require(result == 1 and (m.r32(ACTIVE), m.r32(ITEM), m.r32(HEADER)) == (1, 0x1234, 0x5678),
                "active state or callbacks")
        expected = [("free", old), ("load", (0, 0, 0, 0, 0))]
        if success:
            expected.append(("notify",))
        expected.append(("send", 0x46, (0x12345678 if success else 0).to_bytes(4, "little")))
        require(events == expected, str(events))
    print("ok: news init reset, cached notification and request packet")


if __name__ == "__main__":
    run()
