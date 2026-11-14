# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the automap index FUN_00457f40 (UI\automap.cpp, the `<name>.map` / `.ma0..3` files) in stock game.exe 1.14d.

    uv run python automap_index.py --exe /path/to/game.exe [--mutation-check]

FUN_00457f40 opens `<dir><sub>\<name>.map` (falling back to `<dir><name>.map`),
reads its 24-byte header (magic 0xc, `next`, four seeds), and opens
`<name>.ma<i>` for the current map seed: the slot already holding the seed, or
the slot `next` (then `next` = (next + 1) mod 4, the slot's old file deleted).
A header that isn't 0x18 bytes with magic 0xc starts over: magic 0xc, next 1,
seed[0] = the seed, and `.ma0`..`.ma3` deleted.

The file calls (FUN_00406960 open, 004069b0 read, 004069d0 write, 004069f0
seek, 004069a0 close, 00406990 delete, 00406a10 make directory), the directory
getter FUN_00407050 and the C string calls (FUN_00413a40, 006823fd, 004135d0)
are hooked over a scripted file system, so the run needs no files. A `next`
past 3 stores the seed past `seed[3]` and deletes `.ma<next>`
(docs/research/re/bugs.md 40); a negative `next` stores the seed before the
seed array; both are run and compared with the model.

`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

import unicorn.x86_const as uc

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("found-slot-ignored", "next-unwrapped", "next-checked", "no-subfolder-fallback", "magic-ignored", "reset-keeps-files",
             "no-header-write", "negative-next-mod")
mutation = None
DIR = "C:\\D2\\Save\\"
SUB, NAME, SEED = 0x7A0500, 0x7A05C4, 0x7A0638
INVALID = 0xFFFFFFFF


def cformat(machine, fmt, args):
    """The few printf conversions these calls use (%s, %d)."""
    out, index, position = "", 0, 0
    while position < len(fmt):
        char = fmt[position]
        if char != "%":
            out, position = out + char, position + 1
            continue
        conv = fmt[position + 1]
        value = args[index]
        index += 1
        out += machine.cstr(value) if conv == "s" else str(struct.unpack("<i", struct.pack("<I", value & 0xFFFFFFFF))[0])
        position += 2
    return out


class Fs:
    """The scripted file system and call log."""

    def __init__(self, machine):
        """Hook the file, directory and string calls."""
        self.m = machine
        self.files = {}
        self.deny = set()
        self.log = []
        self.handles = {}
        self.position = {}
        machine.hook(0x407050, self._dir, 0)
        machine.hook(0x413A40, self._snprintf, 0)
        machine.hook(0x6823FD, self._sprintf, 0)
        machine.hook(0x4135D0, self._strncpy, 0)
        machine.hook(0x406A10, self._mkdir, 0)
        machine.hook(0x406960, self._open, 5)
        machine.hook(0x4069B0, self._read, 4)
        machine.hook(0x4069D0, self._write, 3)
        machine.hook(0x4069F0, self._seek, 2)
        machine.hook(0x4069A0, self._close, 0)
        machine.hook(0x406990, self._delete, 0)

    def reg(self, name):
        """A register at the hooked call."""
        return self.m.mu.reg_read(getattr(uc, "UC_X86_REG_" + name))

    def _dir(self, current):
        """FUN_00407050 (ECX buffer, EDX size): the save directory."""
        self.m.mu.mem_write(self.reg("ECX"), DIR.encode() + b"\0")
        return 0

    def _snprintf(self, current):
        """FUN_00413a40 (cdecl: buffer, size, format, ...)."""
        text = cformat(self.m, self.m.cstr(current.arg(2)), [current.arg(index) for index in range(3, 8)])
        self.m.mu.mem_write(current.arg(0), text.encode() + b"\0")
        return len(text)

    def _sprintf(self, current):
        """FUN_006823fd (cdecl: buffer, format, ...)."""
        text = cformat(self.m, self.m.cstr(current.arg(1)), [current.arg(index) for index in range(2, 7)])
        self.m.mu.mem_write(current.arg(0), text.encode() + b"\0")
        return len(text)

    def _strncpy(self, current):
        """FUN_004135d0 (cdecl: destination, source, size)."""
        self.m.mu.mem_write(current.arg(0), self.m.cstr(current.arg(1)).encode() + b"\0")
        return 0

    def _mkdir(self, current):
        """FUN_00406a10 (ECX path)."""
        self.log.append(("mkdir", self.m.cstr(self.reg("ECX"))))
        return 0

    def _open(self, current):
        """FUN_00406960 (ECX path, EDX access, five words): open or create; INVALID for a denied path."""
        path = self.m.cstr(self.reg("ECX"))
        self.log.append(("open", path))
        if path in self.deny:
            return INVALID
        self.files.setdefault(path, bytearray())
        handle = 0x100 + len(self.handles)
        self.handles[handle], self.position[handle] = path, 0
        return handle

    def _read(self, current):
        """FUN_004069b0 (ECX handle, EDX buffer; count, &bytes read, two more words)."""
        handle, count = self.reg("ECX"), current.arg(0)
        data = bytes(self.files[self.handles[handle]][self.position[handle]:self.position[handle] + count])
        self.m.mu.mem_write(self.reg("EDX"), data)
        self.m.w32(current.arg(1), len(data))
        self.position[handle] += len(data)
        return 1

    def _write(self, current):
        """FUN_004069d0 (ECX handle, EDX buffer; count, &bytes written, 0)."""
        handle, count = self.reg("ECX"), current.arg(0)
        path = self.handles[handle]
        data = self.m.read(self.reg("EDX"), count)
        self.files[path][self.position[handle]:self.position[handle] + count] = data
        self.position[handle] += count
        self.log.append(("write", path, data))
        return 1

    def _seek(self, current):
        """FUN_004069f0 (ECX handle, EDX offset, two words)."""
        self.position[self.reg("ECX")] = self.reg("EDX")
        return 0

    def _close(self, current):
        """FUN_004069a0 (ECX handle)."""
        self.log.append(("close", self.handles[self.reg("ECX")]))
        return 0

    def _delete(self, current):
        """FUN_00406990 (ECX path)."""
        path = self.m.cstr(self.reg("ECX"))
        self.log.append(("delete", path))
        self.files.pop(path, None)
        return 0


def header(magic, nxt, seeds):
    """The 24-byte header as the file stores it."""
    return struct.pack("<II4I", magic, nxt & 0xFFFFFFFF, *[seed & 0xFFFFFFFF for seed in seeds])


def signed(value):
    """A 32-bit word as a signed int."""
    return value - (1 << 32) if value & 0x80000000 else value


def model(fs_files, sub, name, seed, deny):
    """(expected log, final files, returned path, fault) for FUN_00457f40, written from the disassembly.

    The header lives in the stack frame at [ebp - 0x1c]: magic, next, seed[0..3], then the bytes-read
    word (index 4), the saved EBP (5) and the return address (6). `fault` is the address the return
    jumps to when the seed lands on it.
    """
    log, files = [], {path: bytes(data) for path, data in fs_files.items()}
    root, first = DIR, None
    if sub:
        directory = f"{DIR}{sub}\\"
        log.append(("mkdir", directory))
        first = f"{directory}{name}.map"
        log.append(("open", first))
        if first in deny and mutation != "no-subfolder-fallback":
            first = None
        else:
            root = directory
            files.setdefault(first, b"")
    if first is None:
        first = f"{DIR}{name}.map"
        log.append(("open", first))
        if first in deny:
            return log, files, None, None
        files.setdefault(first, b"")
    data = files[first]
    buffer = data[:24].ljust(24, b"\0")
    magic, nxt, *seeds = struct.unpack("<6I", buffer)
    valid = len(data) >= 24 and (magic == 0xC or mutation == "magic-ignored")
    fault = None
    if not valid:
        magic, nxt, seeds[0] = 0xC, 1, seed
        if mutation != "reset-keeps-files":
            for index in range(4):
                log.append(("delete", f"{root}{name}.ma{index}"))
                files.pop(f"{root}{name}.ma{index}", None)
        slot, rewrite = 0, True
    elif seed in seeds and mutation != "found-slot-ignored":
        slot, rewrite = seeds.index(seed), False
    else:
        raw = signed(nxt)
        if mutation == "next-checked":
            raw &= 3
        mod = (raw + 1) & 0x80000003
        if mod >= 0x80000000:
            mod = ((mod - 1) | 0xFFFFFFFC) + 1
        if mutation == "negative-next-mod":
            mod = (raw + 1) % 4
        nxt = raw & 0xFFFFFFFF if mutation == "next-unwrapped" else mod & 0xFFFFFFFF
        log.append(("delete", f"{root}{name}.ma{raw}"))
        files.pop(f"{root}{name}.ma{raw}", None)
        slot, rewrite = raw, True
        if 0 <= slot < 4:
            seeds[slot] = seed
        elif slot == -1:
            nxt = seed
        elif slot == -2:
            magic = seed
        elif slot == 6:
            fault = seed
    if rewrite:
        if mutation != "no-header-write":
            blob = header(magic, nxt, seeds)
            log.append(("write", first, blob))
            files[first] = blob + files[first][24:]
        log.append(("close", first))
    else:
        log.append(("close", first))
    path = f"{root}{name}.ma{slot}"
    log.append(("open", path))
    files.setdefault(path, b"")
    return log, files, path, fault


def run(exe):
    """Random files and seeds through FUN_00457f40; returns the summary line."""
    machine = emu.Emu(str(exe))
    fs = Fs(machine)
    rng = random.Random(0x457F40)
    seed_cell = machine.alloc(8)
    machine.w32(SEED, seed_cell)
    cases = 0
    for case in range(400):
        sub = rng.choice(("", "", "Hero", "Bob"))
        name = rng.choice(("Barb", "Sorc", "x"))
        seed = rng.getrandbits(32)
        machine.w32(seed_cell, seed)
        machine.mu.mem_write(SUB, sub.encode() + b"\0" * 8)
        machine.mu.mem_write(NAME, name.encode() + b"\0" * 8)
        root = f"{DIR}{sub}\\" if sub else DIR
        kind = case % 8
        magic = 0xC if kind != 1 else rng.choice((0, 0xD, 0xB))
        nxt = rng.choice((0, 1, 2, 3)) if kind < 6 else rng.choice((4, 5, 6, 0xFFFFFFFF, 0xFFFFFFFE))
        seeds = [rng.getrandbits(32) for _ in range(4)]
        if kind == 2:
            seeds[rng.randrange(4)] = seed
        deny = {f"{root}{name}.map"} if sub and kind == 3 else set()
        deny |= {f"{DIR}{name}.map"} if kind == 4 and not sub else set()
        files = {}
        base = f"{root}{name}.map" if f"{root}{name}.map" not in deny else f"{DIR}{name}.map"
        if kind == 5:
            files[base] = b"\1\2\3"
        elif kind != 0 or case % 16 == 0:
            files[base] = header(magic, nxt, seeds)
        for index in range(4):
            files[f"{root}{name}.ma{index}"] = b"old" + bytes((index,))
        fs.files, fs.deny, fs.log, fs.handles, fs.position = {k: bytearray(v) for k, v in files.items()}, deny, [], {}, {}
        want_log, want_files, want_path, fault = model(files, sub, name, seed, deny)
        try:
            got = machine.call(0x457F40, limit=200000)
        except RuntimeError as error:
            if fault is None or f"emu fault at {fault:08x}" not in str(error):
                raise AssertionError(f"case {case}: unexpected fault {error}; model {fault}") from None
            if fs.log != want_log:
                raise AssertionError(f"case {case} (return address overwritten): log {fs.log}\n model {want_log}") from None
            cases += 1
            continue
        if fault is not None:
            raise AssertionError(f"case {case}: model expected the return address to be overwritten by {fault:#x}")
        if want_path is None:
            if got != INVALID or fs.log != want_log:
                raise AssertionError(f"case {case}: failed open returned {got:#x}, log {fs.log}")
            cases += 1
            continue
        if fs.log != want_log:
            raise AssertionError(f"case {case} kind {kind} sub {sub!r}: log\n {fs.log}\n model\n {want_log}")
        final = {path: bytes(data) for path, data in fs.files.items()}
        if final != {path: bytes(data) for path, data in want_files.items()}:
            raise AssertionError(f"case {case}: files differ\n {final}\n model {want_files}")
        if fs.handles.get(got) != want_path:
            raise AssertionError(f"case {case}: returned handle for {fs.handles.get(got)!r}, model {want_path!r}")
        cases += 1
    return f"ok: {cases} automap index scenarios"


def main():
    """Entry point: run the checks, or every mutation and require each to fail."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(run(exe))
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(exe)
            except AssertionError:
                print(f"mutation {name}: failed as required")
                continue
            finally:
                mutation = None
            raise SystemExit(f"mutation {name} was not caught")


if __name__ == "__main__":
    main()
