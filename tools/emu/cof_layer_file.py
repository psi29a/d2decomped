# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the COF layer-file path builder FUN_005fe610 and loader FUN_005fe990 in stock game.exe 1.14d.

    uv run python cof_layer_file.py --exe /path/to/game.exe [--mutation-check]

FUN_005fe610 (ESI = the unit-type record, EBX = output buffer, one stack word =
the layer stem) builds the file path and sets the record's +0x44 bit 0 for a
`.dcc`; the model here is written from the decompile and compared on random
records, every monster class 0..0x2ff and the compressed flag 0x741efc both ways.

FUN_005fe990 (EAX = record, EDI = stem buffer, one stack word = the path buffer)
returns a cached header, or builds one: the file's open, read and close are
hooked (a scripted file), and everything else (FUN_005fe2b0, FUN_005fe610,
FUN_0060af90, FUN_0060b010, FUN_00413ce0, FUN_0060bf20) runs for real. It shows
that a file which isn't there is NOT fatal: the open returns handle 0 and the
header's bit 0 is set. Error 0x413 is FUN_0060b010 returning null, which its
own code cannot do (it writes through the pool block without a test); 0x444,
0x449 and 0x45c are the other three fatal lines.

`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ESP

import unicorn.x86_const as unicorn_const

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
COMPRESSED = 0x741EFC
MUTATIONS = ("missiles-always-dcc", "overlay-7a-dcc", "object-156-dcc", "monster-209-conditional", "token-space-kept",
             "special-name-keeps-flag", "item-mode-5", "missing-file-fatal", "header-size-limit", "frames-limit",
             "stem-not-copied", "hit-skips-touch")
mutation = None
SAME_DC6 = {0xF2, 0xFB, 0x16F, 0x209, 0x2C0}
MODE_DC6 = {0xF3, 0x11C, 0x14D, 0x220, 0x22F, 0x23A, 0x2C1, 0x2C5}


class Fatal(Exception):
    """game.exe's fatal-error exit, carrying the error number FUN_00408090 was given."""


def tokens(raw):
    """Three layer-token bytes with each space (0x20) turned into NUL, as a C string."""
    out = bytes(0 if (byte == 0x20 and mutation != "token-space-kept") else byte for byte in raw)
    return out.split(b"\0")[0].decode("latin-1") if out[:1] != b"\0" else ""


def model_path(rec, stem, stem_pointer_name, compressed):
    """FUN_005fe610: (return, path or None, new flag word) for a record dict and the stem."""
    flags = rec["flags"]
    if stem == "":
        return 0, "", flags
    kind = rec["type"]
    ext_dcc = compressed

    def pick(root, name, dcc):
        """`root\\name.dcc` and flag bit 0, or `.dc6`."""
        if dcc and (kind != 6 or rec["w14"] != 0x7A or mutation == "overlay-7a-dcc"):
            return 1, f"{root}\\{name}.dcc", flags | 1
        return 1, f"{root}\\{name}.dc6", flags

    if kind == 3:
        if stem_pointer_name is None:
            return 0, "", flags
        return pick("DATA\\GLOBAL\\MISSILES", stem_pointer_name, compressed or mutation == "missiles-always-dcc")
    if kind == 4:
        if stem_pointer_name is None or rec["w10"] not in ((3, 4, 5) if mutation == "item-mode-5" else (3, 4)):
            return 0, "", flags
        return 1, f"DATA\\GLOBAL\\items\\{stem}.dc6", flags
    if kind == 6:
        if not rec["f4"] & 2:
            return 0, "", flags
        return pick("DATA\\GLOBAL\\OVERLAYS", stem, compressed)
    if compressed and stem.lower() == "oytrlittnhth" and mutation != "special-name-keeps-flag":
        ext_dcc = False
        flags &= ~1
    elif compressed and stem == "OYTRlitTNhth":     # the comparison is case-insensitive in game.exe (FUN_00413590)
        ext_dcc = False
        flags &= ~1
    if kind == 0:
        root = "DATA\\GLOBAL\\CHARS"
    elif kind == 1:
        root = "DATA\\GLOBAL\\MONSTERS"
        cls = rec["w0c"]
        if cls in SAME_DC6 and not (cls == 0x209 and mutation == "monster-209-conditional" and rec["w10"] != 0):
            ext_dcc = False
        elif cls in MODE_DC6 and rec["w10"] == 0:
            ext_dcc = False
    elif kind == 2:
        root = "DATA\\GLOBAL\\OBJECTS"
        if rec["w0c"] in (0x156, 0x233) and mutation != "object-156-dcc":
            ext_dcc = False
    else:
        return 0, "", flags
    first = tokens(rec["tok1"])
    second = tokens(rec["tok2"])
    if ext_dcc:
        return 1, f"{root}\\{first}\\{second}\\{stem}.dcc", flags | 1
    return 1, f"{root}\\{first}\\{second}\\{stem}.dc6", flags


def make_record(machine, rec, name_pointer):
    """Write a record dict into emulator memory; returns its address."""
    address = machine.alloc(0x80)
    machine.w32(address + 4, rec["f4"])
    machine.w32(address + 8, rec["type"])
    machine.w32(address + 0xC, rec["w0c"])
    machine.w32(address + 0x10, rec["w10"])
    machine.w32(address + 0x14, rec["w14"])
    machine.mu.mem_write(address + 0x18, rec["tok1"] + b"\0" + rec["tok2"] + b"\0")
    machine.w32(address + 0x2C, name_pointer)
    machine.w32(address + 0x44, rec["flags"])
    return address


def random_record(rng):
    """A random unit-type record, biased toward the cases the function branches on."""
    tok = lambda: bytes(rng.choice(b"ABCDEFGHLITRSPHDTN ") for _ in range(3))
    return {"f4": rng.choice((0, 2, 3, 1)), "type": rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 1, 1)),
            "w0c": rng.choice((rng.randrange(0x300), 0xF2, 0xF3, 0xFB, 0x11C, 0x14D, 0x156, 0x16F, 0x209, 0x220, 0x22F,
                               0x233, 0x23A, 0x2C0, 0x2C1, 0x2C5)),
            "w10": rng.choice((0, 1, 3, 4, 5, 7)), "w14": rng.choice((0, 0x7A, 5, 0x7B)),
            "tok1": tok(), "tok2": tok(), "flags": rng.choice((0, 1, 2, 3, 0x10)) }


def check_path(machine, rng):
    """Compare FUN_005fe610 with the model over random records for both compression settings."""
    stems = ("A1", "", "OYTRlitTNhth", "oytrlittnhth", "HD", "stem9")
    count = 0
    for compressed in (0, 1):
        machine.w32(COMPRESSED, compressed)
        for case in range(1500):
            rec = random_record(rng)
            rec["type"] = case % 8 if case < 400 else rec["type"]
            if rec["type"] == 1 and case >= 400:
                rec["w0c"] = case % 0x300
            stem = rng.choice(stems)
            name = rng.choice((None, "bolt", "fx"))
            record = make_record(machine, rec, 0 if name is None else machine.put(name.encode() + b"\0"))
            stem_pointer = machine.put(stem.encode() + b"\0")
            buffer = machine.alloc(0x100)
            machine.mu.mem_write(buffer, b"\xee" * 0x100)
            sp_top = emu.STACK + emu.STACK_SIZE - 0x1000
            got = machine.call(0x5FE610, stem_pointer, regs={"esi": record, "ebx": buffer}, limit=5000)
            popped = machine.mu.reg_read(UC_X86_REG_ESP) - (sp_top - 8)
            want, path, flags = model_path(rec, stem, name, bool(compressed))
            text = machine.cstr(buffer)
            if got != want or machine.r32(record + 0x44) != flags or popped != 8:
                raise AssertionError(f"FUN_005fe610 {rec} stem {stem!r} comp {compressed}: ret {got} flags "
                                     f"{machine.r32(record + 0x44):#x} popped {popped}; model {want} {flags:#x}")
            if want and text != path or (not want and text != ""):
                raise AssertionError(f"FUN_005fe610 {rec} stem {stem!r}: path {text!r}, model {path!r}")
            count += 1
    return count


class Loader:
    """FUN_005fe990 with a scripted pool, LRU and file system; the lookups and the path build run for real."""

    def __init__(self, machine):
        """Hook the pool, the LRU and the file calls (FUN_0040b430, 00609420, 00609380, 004068e0, 00406900/30, 004068f0)."""
        self.m = machine
        self.files = {}
        self.log = []
        self.lru_null = False
        self.error = None
        self.data, self.cursor, self.position = None, 0, 0x1234
        machine.hook(0x40B430, lambda c: machine.alloc(0x200), 3)
        machine.hook(0x408090, self._error, 0)
        machine.hook(0x408A60, lambda c: 0, 0)
        machine.hook(0x681E09, self._exit, 0)
        machine.hook(0x609420, self._lru_alloc, 1)
        machine.hook(0x609380, self._touch, 0)
        machine.hook(0x4068E0, self._open, 0)
        machine.hook(0x406900, self._read, 5)
        machine.hook(0x406930, self._tell, 0)
        machine.hook(0x4068F0, self._close, 0)

    def reg(self, name):
        """A register by name at the hooked call."""
        return self.m.mu.reg_read(getattr(unicorn_const, "UC_X86_REG_" + name.upper()))

    def _error(self, current):
        """FUN_00408090 (cdecl, one word): remember the error number."""
        self.error = current.arg(0)
        return 0

    def _exit(self, current):
        """FUN_00681e09: the fatal exit."""
        raise Fatal(self.error)

    def _lru_alloc(self, current):
        """FUN_00609420 (ECX pool, EDX size, one word = the cache entry): a slot whose +0 is a 0xaa-filled block."""
        self.log.append(("lru", self.reg("edx"), self.reg("ecx")))
        if self.lru_null:
            return 0
        slot = self.m.alloc(0x20)
        block = self.m.alloc(0x400, zero=False)
        self.m.mu.mem_write(block, b"\xaa" * 0x400)
        self.m.w32(slot, block)
        return slot

    def _touch(self, current):
        """FUN_00609380 (ECX pool, EDX slot): the LRU touch on a cache hit."""
        self.log.append(("touch", self.reg("ecx"), self.reg("edx")))
        return 0

    def _open(self, current):
        """FUN_004068e0 (ECX path, EDX &handle): handle 1 if the scripted file exists, else 0."""
        path = self.m.cstr(self.reg("ecx"))
        self.data, self.cursor = self.files.get(path), 0
        self.m.w32(self.reg("edx"), 0 if self.data is None else 1)
        self.log.append(("open", path))
        return 0

    def _read(self, current):
        """FUN_00406900 (ECX handle, EDX buffer, stack: count, &bytes read): copy scripted bytes."""
        count, done = current.arg(0), current.arg(1)
        chunk = self.data[self.cursor:self.cursor + count]
        self.cursor += count
        current.mu.mem_write(self.reg("edx"), chunk.ljust(count, b"\0"))
        if done:
            self.m.w32(done, len(chunk))
        self.log.append(("read", count, self.reg("edx")))
        return 1

    def _tell(self, current):
        """FUN_00406930 (ECX handle, EDX 0): the scripted file position."""
        self.log.append(("tell",))
        return self.position

    def _close(self, current):
        """FUN_004068f0: close."""
        self.log.append(("close",))
        return 0


def stem_of(rec):
    """FUN_005fe2b0 for types 0..2: the five 3-letter token words joined, each stopping at its first space."""
    out = ""
    for word in rec["words"]:
        for byte in word:
            if byte == 0x20:
                break
            out += chr(byte)
    return out


def new_world(machine, loader, rec, comp):
    """Fresh hash table, a record, a path buffer and a stem buffer for one FUN_005fe990 call."""
    machine.mu.mem_write(0x88CB20 + 0x800, b"\0" * 0x800)
    machine.w32(COMPRESSED, comp)
    record = make_record(machine, rec, 0)
    for index, word in enumerate(rec["words"]):
        machine.mu.mem_write(record + 0x18 + 4 * index, word + b"\0")
    return record, machine.alloc(0x100), machine.alloc(0x100)


def call_loader(machine, loader, record, path, stem):
    """Run FUN_005fe990; returns (block, fatal code, popped words)."""
    loader.log.clear()
    loader.error = None
    machine.mu.mem_write(path, b"\xee" * 0x100)
    top = emu.STACK + emu.STACK_SIZE - 0x1000
    try:
        block = machine.call(0x5FE990, path, regs={"eax": record, "edi": stem}, limit=500000)
        fatal = None
    except Fatal as error:
        return None, error.args[0], None
    return block, fatal, (machine.mu.reg_read(UC_X86_REG_ESP) - (top - 8)) // 4


def dcb_header(words, table):
    """The 0x18-byte header the uncompressed branch reads, then the `table` dwords."""
    return struct.pack("<6I", *words) + b"".join(struct.pack("<I", value) for value in table)


def check_loader(machine, rng):
    """FUN_005fe990: no stem, no path, a missing file (not fatal), a .dc6-style header, a .dcc header, the fatal lines."""
    loader = Loader(machine)
    base = {"f4": 0, "type": 0, "w0c": 0, "w10": 0, "w14": 0, "tok1": b"HD ", "tok2": b"LIT", "flags": 0,
            "words": [b"HD ", b"LIT", b"A1 ", b"   ", b"   "]}
    stem = "HDLITA1"
    dc6 = "DATA\\GLOBAL\\CHARS\\HD\\LIT\\HDLITA1.dc6"
    dcc = dc6[:-3] + "dcc"
    rounds = 0
    # No stem: FUN_005fe2b0 fails (type 3 without a name); *path = 0 and nothing is called.
    rec = dict(base, type=3, words=[b"   "] * 5)
    record, path, buffer = new_world(machine, loader, rec, 0)
    block, fatal, popped = call_loader(machine, loader, record, path, buffer)
    if (block, fatal, popped) != (0, None, 2) or loader.log or machine.read(path, 1) != b"\0":
        raise AssertionError(f"no stem: {block} {fatal} {popped} {loader.log}")
    # A stem but no path (type 4, mode not 3 / 4): FUN_005fe610 returns 0, nothing is opened.
    rec = dict(base, type=4, w10=0)
    record, path, buffer = new_world(machine, loader, rec, 0)
    machine.w32(record + 0x2C, machine.put(b"item\0"))
    block, fatal, popped = call_loader(machine, loader, record, path, buffer)
    if (block, fatal, popped) != (0, None, 2) or loader.log:
        raise AssertionError(f"no path: {block} {fatal} {popped} {loader.log}")
    # A file that isn't there is not fatal: the header comes back with bit 0 set, and a second call hits the cache.
    for comp in (0, 1):
        rec = dict(base)
        record, path, buffer = new_world(machine, loader, rec, comp)
        loader.files = {}
        block, fatal, popped = call_loader(machine, loader, record, path, buffer)
        if mutation == "missing-file-fatal":
            fatal = 0x413
        if fatal is not None or not block or popped != 2:
            raise AssertionError(f"missing file (comp {comp}): fatal {fatal}, block {block}")
        expect_path = dcc if comp else dc6
        if machine.read(block, 0xC0)[0] != 1 or machine.cstr(block + 0xA0) != ("" if mutation == "stem-not-copied" else stem) or loader.log != [
                ("lru", 0xC0, 0x88DB20), ("open", expect_path)]:
            raise AssertionError(f"missing file (comp {comp}): header {machine.read(block, 8).hex()} log {loader.log}")
        if any(machine.read(block + 4, 0x9C)):
            raise AssertionError("missing file: the header block isn't zeroed past byte 0")
        again, fatal, popped = call_loader(machine, loader, record, path, buffer)
        if again != block or fatal is not None or len(loader.log) != (0 if mutation == "hit-skips-touch" else 1) or (loader.log and loader.log[0][:2] != ("touch", 0x88DB20)):
            raise AssertionError(f"cache hit (comp {comp}): {again} vs {block}, log {loader.log}")
        rounds += 2
    # An uncompressed file: the 0x18-byte header, the offset table, close, bit 0 clear.
    for _ in range(40):
        width = rng.randrange(1, 9)
        frames = rng.randrange(1, 0x21)
        words = [rng.getrandbits(32) for _ in range(4)] + [frames, width]
        table = [rng.getrandbits(32) for _ in range(width * frames)]
        rec = dict(base)
        record, path, buffer = new_world(machine, loader, rec, 0)
        loader.files = {dc6: dcb_header([words[0], words[1], words[2], words[3], frames, width], table)}
        loader.position = rng.getrandbits(32)
        block, fatal, popped = call_loader(machine, loader, record, path, buffer)
        if fatal is not None or popped != 2:
            raise AssertionError(f"header: fatal {fatal}")
        data = machine.read(block, 0xC0)
        want = bytearray(b"\0" * 0xC0)
        want[4:8], want[8:0xC], want[0xC:0x10] = struct.pack("<I", words[0]), struct.pack("<I", words[1]), struct.pack("<I", words[2])
        want[0x14:0x18], want[0x18:0x1C] = struct.pack("<I", frames), struct.pack("<I", width)
        for index in range(frames):
            want[0x1C + 4 * index:0x20 + 4 * index] = struct.pack("<I", table[width * index])
        want[0x1C + 4 * frames:0x20 + 4 * frames] = struct.pack("<I", loader.position)
        want[0xA0:0xA0 + len(stem) + 1] = stem.encode() + b"\0"
        if data != bytes(want) or [item[0] for item in loader.log] != ["lru", "open", "read", "tell", "read", "close"]:
            raise AssertionError(f"header: {data.hex()}\n want {bytes(want).hex()}\n log {loader.log}")
        rounds += 1
    # The fatal lines: 0x444 for width x frames of 0x4000 or more, 0x449 for more than 0x20 frames, 0x418 for no LRU slot.
    size_limit = 0x8000 if mutation == "header-size-limit" else 0x4000
    frame_limit = 0x21 if mutation == "frames-limit" else 0x20
    for frames, width in ((0x21, 4), (4, 0x1000), (0x20, 0x200), (0x40, 2), (0x20, 0x1FF), (0x21, 0x1FF), (0x20, 0x300)):
        code = 0x444 if width * frames >= size_limit else 0x449 if frames > frame_limit else None
        record, path, buffer = new_world(machine, loader, dict(base), 0)
        loader.files = {dc6: dcb_header([1, 2, 3, 4, frames, width], [0] * min(width * frames, 0x200))}
        _, fatal, _ = call_loader(machine, loader, record, path, buffer)
        if fatal != code:
            raise AssertionError(f"frames {frames} width {width}: fatal {fatal}, model {code}")
        rounds += 1
    record, path, buffer = new_world(machine, loader, dict(base), 0)
    loader.lru_null = True
    _, fatal, _ = call_loader(machine, loader, record, path, buffer)
    loader.lru_null = False
    if fatal != 0x418:
        raise AssertionError(f"no LRU slot: fatal {fatal}")
    # A .dcc file (bit 0 set by FUN_005fe2b0): FUN_0060bf20's 0xf-byte header and offsets, flag bit 1; a bad one sets bit 0.
    for case in range(40):
        frames = rng.randrange(1, 0x30)
        capped = min(frames, 0x20)
        body = bytes([ord("t"), rng.getrandbits(8), frames]) + struct.pack("<I", rng.getrandbits(32)) + struct.pack("<I", rng.getrandbits(32)) + b"\0" * 4
        offsets = b"".join(struct.pack("<I", rng.getrandbits(32)) for _ in range(capped))
        bad = case % 4 == 3
        if bad:
            body = bytes([ord("x")]) + body[1:]
        record, path, buffer = new_world(machine, loader, dict(base), 1)
        loader.files = {dcc: body + offsets}
        loader.position = rng.getrandbits(32)
        block, fatal, popped = call_loader(machine, loader, record, path, buffer)
        if fatal is not None or popped != 2:
            raise AssertionError(f"dcc: fatal {fatal}")
        data = machine.read(block, 0xC0)
        if bad:
            ok = data[0] == 1 and not any(data[4:0xA0])
        else:
            want = bytearray(b"\0" * 0xC0)
            want[0] = 2
            want[4:8], want[8:0xC] = struct.pack("<I", body[1]), body[7:11]
            want[0x14:0x18], want[0x18:0x1C] = struct.pack("<I", frames), body[3:7]
            want[0x1C:0x1C + 4 * capped] = offsets
            want[0x1C + 4 * capped:0x20 + 4 * capped] = struct.pack("<I", loader.position)
            ok = data[:0xA0] == bytes(want[:0xA0])
        if not ok or machine.cstr(block + 0xA0) != stem:
            raise AssertionError(f"dcc case {case} (bad {bad}): {data.hex()}\n log {loader.log}")
        rounds += 1
    return rounds


def run(exe):
    """Run every check once with the active mutation; returns the summary line."""
    machine = emu.Emu(str(exe))
    rng = random.Random(0x5FE610)
    return f"ok: {check_path(machine, rng)} path cases, {check_loader(machine, rng)} loader scenarios"


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
