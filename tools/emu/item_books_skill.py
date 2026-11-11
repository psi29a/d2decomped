# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the book/scroll skill lookup FUN_0055bff0 in stock game.exe 1.14d.

Run: python item_books_skill.py --exe /path/to/game.exe
Type, suffix and Books-record lookups are hooked. Null-item assertion is not run.
"""
import argparse
import hashlib
from itertools import product
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(args.exe))
    item, books = (machine.alloc(0x100) for _ in range(2))
    machine.w32(books + 8, 0x12345678)
    machine.w32(books + 12, 0x87654321)
    calls = []
    scripted = {"scroll": 0, "book": 0, "table": 0, "suffix": 0}

    def item_type(current):
        unit, kind = current.arg(0), current.arg(1)
        calls.append(("type", unit, kind))
        return scripted["scroll"] if kind == 0x16 else scripted["book"]

    def suffix(current):
        calls.append(("suffix", current.arg(0), current.arg(1)))
        return scripted["suffix"]

    def table(current):
        calls.append(("books", current.arg(0)))
        return books if scripted["table"] else 0

    machine.hook(0x629bb0, item_type, 2)
    machine.hook(0x627f80, suffix, 2)
    machine.hook(0x6374b0, table, 1)
    count = 0
    for scroll, book, table_ok, suffix_value in product((0, 1), (0, 1), (0, 1), (0, 0x12345678)):
        calls.clear()
        scripted.update(scroll=scroll, book=book, table=table_ok, suffix=suffix_value)
        result = machine.call(0x55bff0, regs={"esi": item}, limit=1000)
        expected = [("type", item, 0x16)]
        if not scroll:
            expected.append(("type", item, 0x12))
        if scroll or book:
            expected += [("suffix", item, 0), ("books", suffix_value & 0xffff)]
            if table_ok:
                expected.append(("type", item, 0x16))
                if not scroll:
                    expected.append(("type", item, 0x12))
        value = (0x12345678 if scroll else 0x87654321) if table_ok and (scroll or book) else 0xffffffff
        if (result != value or calls != expected or machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC or
                machine.mu.reg_read(UC_X86_REG_ESP) != emu.STACK + emu.STACK_SIZE - 0x1000):
            raise AssertionError(f"case {(scroll, book, table_ok, suffix_value)}: {result:x}, {calls!r}")
        count += 1
    print(f"ok: {count} book/scroll skill lookup cases (callees hooked)")


if __name__ == "__main__":
    main()
