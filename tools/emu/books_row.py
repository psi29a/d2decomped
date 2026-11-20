# SPDX-License-Identifier: GPL-3.0-or-later
"""A normal item's fix-ups (FUN_00556f30 -> FUN_00556e80) on every Items row, docs/research/re/drops.md.

    uv run python books_row.py          # a line per item: code, then what the fix-ups did
    uv run python books_row.py inc      # writes tests/books_row_cases.inc (code, tome)

game.exe boots with every data table loaded (drlg.boot). FUN_00556f30 runs
for real on a fake item unit (+0 type 4, +4 the Items index, +0x14 item
data with version 101 at +0x30), the drop record zeroed. So do
FUN_0062a670 (the version), FUN_00629bb0 (the ItemTypes tests, rows 0xd,
0x28, 7, 0x16, 0x12) and FUN_005c2540 (the Books row, off the loaded
table). Hooked and logged: FUN_00556a60 (the charm's affix), FUN_00629df0
(item data +0x28), FUN_006280d0 (item flags), FUN_00627fb0 (a suffix slot),
FUN_005c1260 (the staff mods, the tail jump) and FUN_00556d80 (the
version-0 branch, which a line shows as `classic` if it is ever taken).
The line: the code, then `tome <n>` (suffix slot 0, or -1 when nothing set
it), `charm`, `sub <n>`, `flag <hex>` and `staff` as they came.
"""
import sys

from unicorn.x86_const import UC_X86_REG_ECX

import drlg
import emu

# gaps-runs: 00556f30 00556e80 005c2540


def item_codes(e):
    """Every item's code by index (FUN_006335f0 record +0x80), read up front."""
    codes = []
    while rec := e.call(0x6335f0, len(codes)):
        codes.append(e.read(rec + 0x80, 4).decode("latin-1").rstrip(" \0"))
    return codes


def run():
    e = drlg.boot()
    codes = item_codes(e)
    item, data, record = e.alloc(0x100), e.alloc(0x100), e.alloc(0x100)
    log = []
    e.hook(0x556a60, lambda m: log.append("charm") or 1, 1)
    e.hook(0x629df0, lambda m: log.append(f"sub {m.arg(1) - (1 << 32) if m.arg(1) >> 31 else m.arg(1)}") or 0, 2)
    e.hook(0x6280d0, lambda m: log.append(f"flag {m.arg(1):x}={m.arg(2)}") or 0, 3)
    e.hook(0x627fb0, lambda m: log.append(("suffix", m.arg(2), m.arg(1) & 0xffff)) or 0, 3)
    e.hook(0x5c1260, lambda m: log.append("staff" if m.mu.reg_read(UC_X86_REG_ECX) == item else "staff?") or 0, 0)
    e.hook(0x556d80, lambda m: log.append("classic") or 0, 1)
    lines = []
    for index, code in enumerate(codes):
        e.w32(item, 4); e.w32(item + 4, index); e.w32(item + 0x14, data)
        e.w32(data + 0x30, 101)
        log.clear()
        e.call(0x556f30, 0, regs={"esi": item, "eax": record}, limit=100000)
        tome = -1                                      # the tome's test runs after the scroll's: the last slot-0 store wins
        for entry in log:
            if isinstance(entry, tuple) and entry[1] == 0: tome = entry[2]
        rest = [entry for entry in log if not isinstance(entry, tuple)]
        lines.append((code, tome, rest))
    return lines


def main():
    lines = run()
    if len(sys.argv) > 1 and sys.argv[1] == "inc":
        with open(emu.ROOT / "tests" / "books_row_cases.inc", "w", newline="\n") as out:
            out.write("// tools/emu/books_row.py inc: every Items row's code and the Books row FUN_00556e80 puts in suffix slot 0 (-1 none)\n")
            for code, tome, _ in lines:
                out.write(f'{{ "{code}", {tome} }},\n')
        return
    for code, tome, rest in lines:
        print(code, "tome", tome, *rest)


if __name__ == "__main__":
    main()
