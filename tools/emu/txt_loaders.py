# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's two table loaders: LoadTxt (D2Comp.cpp) and CompileTxt (DataTbls.cpp).

    uv run python txt_loaders.py

Both turn a table name into a record array: read the file, split it
(0x6bd640, run for real here), allocate rows * record size, zero it, fill it
with the column parser (0x6bd780, hooked: it logs its arguments), free the
split. They run here from game.exe with the file readers, StoreDataTables, the
column parser, the allocator and the error handler hooked.

* LoadTxt 0x504430 (stdcall pool, name, fields, &rows, record size; flag
  0x72ef2c = read .bin): with the flag off, "leveldefs" (any case) is read as
  "levels", the .txt comes from 0x5043e0, a miss halts 0x9e3, the records are
  allocated at D2Comp.cpp line 0x9fa; with it on, `DATA\\GLOBAL\\EXCEL\\<name>.bin`
  is read at line 0x958 with the name **not** mapped, its first dword is the
  row count and the records follow, nothing is parsed or copied.
* CompileTxt 0x6122f0 (the same arguments; flags 0x96c8b4 = compile the .txt,
  0x744308 = read the .bin): compile first reads the .txt (mapped name; lines
  0x8b9 / 0x8c0, a miss halts 0x8c2), parses it (alloc line 0x8c9) and hands
  `<name>.bin` (the **unmapped** name) to StoreDataTables 0x6121f0 with the
  records, byte size and row count, then frees them (0x8d6). It then loads
  the table for use: the .bin (line 0x8de, name unmapped) or, with that flag
  off, the .txt again (0x8e2 mapped / 0x8e4) parsed a second time (alloc
  line 0x904); a miss halts 0x8e7.
* A table with a header and no data row, or no line break, makes the splitter
  return 0 and both loaders then read row count at address 8 (null struct):
  a crash in the original.
No MPQs are read.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
D2COMP_FILE, DATATBLS_FILE = 0x6DCC20, 0x6E6370
ALLOC, FREE, FREE_CLIENT, FATAL = 0x40B430, 0x40B480, 0x40B3C0, 0x408A60
LOAD_TXT, COMPILE_TXT = 0x504430, 0x6122F0
LOAD_FILE, LOAD_EXCEL_TXT, LOAD_DATATBLS, STORE, PARSE = 0x517079, 0x5043E0, 0x612280, 0x6121F0, 0x6BD780
BIN_FLAG, USE_TXT, MISC_BIN = 0x72EF2C, 0x96C8B4, 0x744308
WSPRINTF_SLOT = 0x6CC468
LIMIT = 400000
TXT = b"name\tx\r\nA\t1\r\nB\t2\r\nExpansion\t\r\nC\t3\r\n"
ROWS = 3


class Halt(Exception):
    """game.exe's unrecoverable-error call, with the source line it passed."""


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


class Harness:
    """game.exe with logging file readers, allocator and column parser."""

    def __init__(self, machine):
        """Hook the allocator, frees, file readers, StoreDataTables, parser, wsprintfA and the error handler."""
        self.machine = machine
        self.log = []
        self.missing = False
        self.sizes = {}
        machine.hook(ALLOC, self.alloc, 3)
        machine.hook(FREE, self.free, 3)
        machine.hook(FREE_CLIENT, self.free_client, 2)
        machine.hook(FATAL, self.fatal, 0)
        machine.hook(LOAD_FILE, self.load_file, 3)
        machine.hook(LOAD_EXCEL_TXT, self.load_excel_txt, 4)
        machine.hook(LOAD_DATATBLS, self.load_datatbls, 4)
        machine.hook(STORE, self.store, 4)
        machine.hook(PARSE, self.parse, 5)
        machine.slots[machine.r32(WSPRINTF_SLOT)] = ("wsprintfA", self.wsprintf)

    def wsprintf(self, current):
        """wsprintfA(buffer, "%s\\%s%s", a, b, c): only that format is used."""
        fmt = current.cstr(current.arg(1))
        require(fmt == "%s\\%s%s", f"unexpected format {fmt!r}")
        text = current.cstr(current.arg(2)) + "\\" + current.cstr(current.arg(3)) + current.cstr(current.arg(4))
        current.mu.mem_write(current.arg(0), text.encode() + b"\0")
        return len(text), 0

    def alloc(self, current):
        """Log the size and line; return a block poisoned with 0xab."""
        size = current.mu.reg_read(UC_X86_REG_EDX)
        self.log.append(("alloc", size, current.arg(0), current.arg(1)))
        block = current.alloc(size)
        current.mu.mem_write(block, b"\xab" * size)
        self.sizes[block] = size
        return block

    def free(self, current):
        """Log (block, file, line)."""
        self.log.append(("free", current.mu.reg_read(UC_X86_REG_EDX), current.arg(0), current.arg(1)))
        return 0

    def free_client(self, current):
        """Log (block, file, line) of a client free."""
        self.log.append(("free-client", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX), current.arg(0)))
        return 1

    def fatal(self, current):
        """Stop at the unrecoverable-error call: arg 2 is the line."""
        raise Halt(current.arg(2))

    def file(self, size_ptr, data):
        """Hand back a file: its size stored at size_ptr, a miss is 0."""
        if self.missing:
            return 0
        block = self.machine.put(data)
        self.machine.w32(size_ptr, len(data))
        return block

    def load_file(self, current):
        """0x517079 (ECX pool, EDX path; &size, file, line): reads a .bin: count, then records."""
        path = current.cstr(current.mu.reg_read(UC_X86_REG_EDX))
        self.log.append(("load-bin", path, current.arg(1), current.arg(2)))
        return self.file(current.arg(0), (ROWS).to_bytes(4, "little") + b"\x11" * 12)

    def load_excel_txt(self, current):
        """0x5043e0 (pool, name, ext, &size): LoadTxt's .txt reader."""
        self.log.append(("load-txt", current.cstr(current.arg(1)), current.cstr(current.arg(2))))
        return self.file(current.arg(3), TXT)

    def load_datatbls(self, current):
        """0x612280 (ECX pool, EDX name; ext, &size, file, line): CompileTxt's reader."""
        name, ext = current.cstr(current.mu.reg_read(UC_X86_REG_EDX)), current.cstr(current.arg(0))
        self.log.append(("load", name, ext, current.arg(2), current.arg(3)))
        return self.file(current.arg(1), (ROWS).to_bytes(4, "little") + b"\x22" * 12 if ext == ".bin" else TXT)

    def store(self, current):
        """0x6121f0 (name, data, bytes, rows): the .bin cache write."""
        self.log.append(("store", current.cstr(current.arg(0)), current.arg(2), current.arg(3)))
        return 0

    def parse(self, current):
        """0x6bd780 (struct, fields, out, rows, record size): log, and check the buffer arrives zeroed."""
        out, rows, size = current.arg(2), current.arg(3), current.arg(4)
        zeroed = current.read(out, rows * size) == b"\0" * (rows * size)
        self.log.append(("parse", current.arg(1), out, rows, size, zeroed))
        return 0

    def run(self, address, *args):
        """Run a loader on the current flags with a fresh log; returns (result, log)."""
        self.log.clear()
        result = self.machine.call(address, *args, limit=LIMIT)
        require(self.machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x}: stopped at the cap")
        return result, list(self.log)

    def halts(self, line, address, *args):
        """Run a loader that must reach the error handler with this line."""
        try:
            self.run(address, *args)
        except Halt as halt:
            require(halt.args[0] == line, f"{address:#x}: halt {halt.args[0]:#x}, expected {line:#x}")
            return
        raise RuntimeError(f"{address:#x}: no halt, expected {line:#x}")


def main():
    """Run both loaders on every flag combination and compare with the claims."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    harness = Harness(machine)
    cases = 0
    pool, fields = 0x1234ABCD, 0x5000
    rows_out = machine.alloc(4)

    def flags(bin_flag=0, use_txt=0, misc_bin=0):
        """Set the three global flags."""
        machine.w32(BIN_FLAG, bin_flag)
        machine.w32(USE_TXT, use_txt)
        machine.w32(MISC_BIN, misc_bin)

    def kind(log, name):
        """The log entries of one kind."""
        return [entry for entry in log if entry[0] == name]

    for record in (4, 0x38, 0x1B0):
        # LoadTxt, .txt: mapped name, rows * record, zeroed before the parse, split freed
        for name, want in (("skills", "skills"), ("leveldefs", "levels"), ("LevelDefs", "levels")):
            flags()
            harness.missing = False
            result, log = harness.run(LOAD_TXT, pool, machine.put(name.encode() + b"\0"), fields, rows_out, record)
            require(kind(log, "load-txt") == [("load-txt", want, ".txt")], f"load-txt {name}: {log}")
            require(("alloc", ROWS * record, D2COMP_FILE, 0x9FA) in kind(log, "alloc"), f"alloc {log}")
            parse = kind(log, "parse")
            require(parse == [("parse", fields, result, ROWS, record, True)], f"parse {parse}")
            require(machine.r32(rows_out) == ROWS, "rows out")
            require([e[3] for e in kind(log, "free-client") + kind(log, "free")] == [0x9A, 0x9B], f"split freed {log}")
            cases += 1
        harness.run(LOAD_TXT, pool, machine.put(b"skills\0"), fields, 0, record)    # a null row-count pointer is fine
        # LoadTxt, .bin: path, no parse, count then records, name unmapped
        for name in ("skills", "leveldefs"):
            flags(bin_flag=1)
            result, log = harness.run(LOAD_TXT, pool, machine.put(name.encode() + b"\0"), fields, rows_out, record)
            require(kind(log, "load-bin") == [("load-bin", f"DATA\\GLOBAL\\EXCEL\\{name}.bin", D2COMP_FILE, 0x958)], f"bin {log}")
            require(not kind(log, "parse") and not kind(log, "alloc"), "bin: no parse, no copy")
            require(machine.r32(rows_out) == ROWS and machine.read(result, 4) == b"\x11" * 4, "bin: count, records after it")
            cases += 1
        # CompileTxt: the four flag combinations
        for name, mapped, line_a, line_b in (("skills", "skills", 0x8C0, 0x8E4), ("leveldefs", "levels", 0x8B9, 0x8E2)):
            for use_txt, misc_bin in ((0, 0), (0, 1), (1, 0), (1, 1)):
                flags(0, use_txt, misc_bin)
                result, log = harness.run(COMPILE_TXT, pool, machine.put(name.encode() + b"\0"), fields, rows_out, record)
                loads = kind(log, "load")
                want = []
                if use_txt:
                    want.append(("load", mapped, ".txt", DATATBLS_FILE, line_a))
                want.append(("load", name, ".bin", DATATBLS_FILE, 0x8DE) if misc_bin
                            else ("load", mapped, ".txt", DATATBLS_FILE, line_b))
                require(loads == want, f"compile {name} {use_txt}{misc_bin}: {loads} vs {want}")
                stores = kind(log, "store")
                if use_txt:
                    require(stores == [("store", f"{name}.bin", ROWS * record, ROWS)], f"store {stores}")
                    require(("alloc", ROWS * record, DATATBLS_FILE, 0x8C9) in kind(log, "alloc"), "compile alloc")
                    require(("free", kind(log, "parse")[0][2], DATATBLS_FILE, 0x8D6) in log, "compile frees the records")
                else:
                    require(not stores, "no store without the compile flag")
                parses = kind(log, "parse")
                require(len(parses) == (1 if use_txt else 0) + (0 if misc_bin else 1), f"parse count {parses}")
                require(all(p[1:] == (fields, p[2], ROWS, record, True) for p in parses), f"parse args {parses}")
                if not misc_bin:
                    require(("alloc", ROWS * record, DATATBLS_FILE, 0x904) in log and result == parses[-1][2], "second parse result")
                else:
                    require(machine.read(result, 4) == b"\x22" * 4, "bin result after the count")
                require(machine.r32(rows_out) == ROWS, "compile rows out")
                cases += 1
    # halts and the missing-file lines
    harness.missing = True
    flags()
    harness.halts(0x9E3, LOAD_TXT, pool, machine.put(b"skills\0"), fields, rows_out, 4)
    flags(bin_flag=1)
    harness.halts(0x9E3, LOAD_TXT, pool, machine.put(b"skills\0"), fields, rows_out, 4)
    flags(0, 1, 0)
    harness.halts(0x8C2, COMPILE_TXT, pool, machine.put(b"skills\0"), fields, rows_out, 4)
    flags(0, 0, 0)
    harness.halts(0x8E7, COMPILE_TXT, pool, machine.put(b"skills\0"), fields, rows_out, 4)
    flags(0, 0, 1)
    harness.halts(0x8E7, COMPILE_TXT, pool, machine.put(b"skills\0"), fields, rows_out, 4)
    harness.missing = False
    cases += 5
    # a header with no data row: the splitter returns 0 and the row count is read at address 8
    for loader in (LOAD_TXT, COMPILE_TXT):
        flags(0, 0, 0)
        original = TXT
        try:
            globals()["TXT"] = b"name\tx\r\n"
            harness.run(loader, pool, machine.put(b"skills\0"), fields, rows_out, 4)
        except RuntimeError as error:
            require("emu fault at 006bcde6" in str(error), f"header-only fault elsewhere: {error}")
        else:
            raise RuntimeError("a header-only table did not fault")
        finally:
            globals()["TXT"] = original
        cases += 1
    print(f"{cases} cases: LoadTxt (.txt / .bin) and CompileTxt (4 flag sets) on 3 record sizes; "
          "leveldefs is read as levels except in .bin names, the cache is written under the unmapped name")
    print("ok")


if __name__ == "__main__":
    main()
