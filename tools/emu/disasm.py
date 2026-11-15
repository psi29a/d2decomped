# SPDX-License-Identifier: GPL-3.0-or-later
"""Disassemble game.exe by address, without Ghidra.

    uv run python disasm.py 623f50              # from 0x623f50 to the function's end
    uv run python disasm.py 623f50 200          # 200 instructions
    uv run python disasm.py --bytes=126 623f50  # exactly one known function size
    uv run python disasm.py --all out.txt       # every executable section, a line an instruction
    uv run python disasm.py --exe PATH ...      # another game.exe than the MPQ folder's bin/game.exe

Without COUNT it stops at the `ret` that ends the function: one followed by
0xCC padding or the next function's `push ebp; mov ebp, esp`. A `ret` inside
it (an early return, a switch arm) doesn't stop it.
It can run on into the next function when that one has neither: a function
without a frame pointer placed right after a `ret` with no padding
(`disasm.py 40a420` reads on past `0040a43f ret`). Pass COUNT there.

A line is `address  mnemonic operands`, so `--all`'s file greps for an
immediate (a string's address, a table's) or an instruction shape. It's a
linear sweep: data inside .text reads as junk, so check a hit in Ghidra.
"""
import os
import sys

import capstone
import pefile

from emu import data_path


def ends_function(image, base, ins):
    """A `ret` followed by 0xCC padding or the next `push ebp; mov ebp, esp`:
    the function's end, not an early return."""
    after = ins.address + ins.size - base
    return ins.mnemonic == "ret" and (after >= len(image) or image[after] == 0xCC or image[after:after + 3] == b"\x55\x8b\xec")


def main():
    args, exe, byte_count = sys.argv[1:], None, None
    if "--exe" in args:
        at = args.index("--exe")
        if at + 1 == len(args):
            sys.exit(__doc__)
        exe = args.pop(at + 1)
        del args[at]
    for arg in tuple(args):
        if arg.startswith("--bytes="):
            if byte_count is not None:
                sys.exit("disasm: --bytes may be given only once")
            try:
                byte_count = int(arg.removeprefix("--bytes="), 0)
            except ValueError:
                sys.exit("disasm: --bytes requires a positive byte count")
            if byte_count < 1:
                sys.exit("disasm: --bytes requires a positive byte count")
            args.remove(arg)
    every = args[:1] == ["--all"]
    if len(args) not in ((2,) if every else (1, 2)) or any(arg.startswith("--") for arg in args[every:]):
        sys.exit(__doc__)
    if not every:
        try:
            va = int(args[0], 16)
            count = int(args[1]) if len(args) > 1 else None
        except ValueError:
            sys.exit(__doc__)
        if count is not None and count < 1:
            sys.exit("disasm: COUNT must be 1 or more")
    try:
        pe = pefile.PE(exe or os.path.join(data_path(), "bin", "game.exe"), fast_load=True)
    except (OSError, pefile.PEFormatError) as error:
        sys.exit(f"disasm: {error}")
    base = pe.OPTIONAL_HEADER.ImageBase
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    if every:
        md.skipdata = True
        try:
            with open(args[1], "w") as out:
                for section in pe.sections:
                    if section.Characteristics & 0x20000000:           # IMAGE_SCN_MEM_EXECUTE
                        for address, _, mnemonic, operands in md.disasm_lite(section.get_data(), base + section.VirtualAddress):
                            out.write(f"{address:08x}  {mnemonic} {operands}\n")
        except OSError as error:
            sys.exit(f"disasm: {error}")
        return
    image = pe.get_memory_mapped_image()
    if not 0 <= va - base < len(image):
        sys.exit(f"disasm: {va:08x} is outside the image ({base:08x}-{base + len(image):08x})")
    executable = next((section for section in pe.sections
                       if section.Characteristics & 0x20000000
                       and base + section.VirtualAddress <= va
                       < base + section.VirtualAddress + max(section.Misc_VirtualSize, section.SizeOfRawData)), None)
    if executable is None:
        sys.exit(f"disasm: {va:08x} is not in an executable section")
    byte_end = va + byte_count if byte_count is not None else None
    end, done = va, 0
    while True:   # capstone decodes a whole call's buffer first, so a batch at a time
        batch = list(md.disasm(image[end - base:end - base + 16 * 256], end, min(256, count - done) if count else 256))
        for ins in batch:
            if byte_end is not None and ins.address + ins.size > byte_end:
                return
            print(f"{ins.address:08x}  {ins.mnemonic:7} {ins.op_str}")
            end = ins.address + ins.size
            done += 1
            if (count and done >= count) or (byte_end is not None and end >= byte_end) or (not count and byte_end is None and ends_function(image, base, ins)):
                return
        if not batch:   # the image's end or a byte it can't decode
            break
    if end - base < len(image):
        sys.exit(f"disasm: stopped at {end:08x}, a byte capstone can't decode")
    sys.exit(f"disasm: reached the image's end at {end:08x}")


if __name__ == "__main__":
    main()
