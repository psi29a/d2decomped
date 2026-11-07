# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify which COF layers body armour colours, and which armour tier each takes, in stock game.exe 1.14d.

    uv run python composit_layers.py

FUN_0064f420(layer) is 1 for layers 1 TR, 2 LG, 3 RA, 4 LA, 8 S1, 9 S2 and 0
for the other 10 of 0..15. FUN_0063d690 (item in EAX, out in ESI) copies
the item's ItemsTxt bytes +0x116..+0x11b, which FUN_006315d0's column list
names rArm, lArm, torso, legs, rspad, lspad, into six bytes in that order (all
0 for no item). FUN_0064f500(layer, bytes) picks one of them per lit layer:
TR torso, LG legs, RA rArm, LA lArm, S1 rspad, S2 lspad; any other layer below
16 is fatal (line 0x274), 16 and up too (0x265). d2d's compcode.hpp `kTier`
and its two lit-layer lists (`lit_layer`, `each_layer`'s `kLayer`) are compared against these tables. The ItemsTxt
lookup is hooked to return a synthetic record; no MPQ is read.
"""
import argparse
import hashlib
import re
from pathlib import Path

import pefile
from unicorn.x86_const import UC_X86_REG_EIP

import emu
import txt_fields

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
HAS_COLOURS = 0x64F420
LAYER_TIER = 0x64F500
BODY_COMPONENTS = 0x63D690
ITEMS_LINE = 0x6335F0
ERROR_LINE = 0x408A60
FATAL_EXIT = 0x681E09
ARMOR_COLUMNS = 0x6315D0
INSTRUCTION_LIMIT = 5000
LAYER_NAMES = ("HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8")
LIT_LAYERS = {1: "torso", 2: "legs", 3: "rArm", 4: "lArm", 8: "rspad", 9: "lspad"}
COLUMNS = ("rArm", "lArm", "torso", "legs", "rspad", "lspad")
D2D_COMPCODE = Path(__file__).resolve().parents[2] / "components" / "compcode" / "compcode.hpp"
D2D_TIER_COLUMNS = ("Torso", "Legs", "rArm", "lArm", "rSPad", "lSPad")


class Fatal(Exception):
    """game.exe reached its fatal exit; carries the line it reported."""


def require(condition, message):
    """Stop the probe when a required stock-code observation is absent."""
    if not condition:
        raise RuntimeError(message)


def armor_columns(exe):
    """FUN_006315d0's column list: {name: ItemsTxt offset} for the six tier columns."""
    rows = txt_fields.column_list(pefile.PE(str(exe), fast_load=True), ARMOR_COLUMNS)
    return {name: offset for name, _type, _size, offset, *_ in rows if name in COLUMNS}


def d2d_tables():
    """d2d's lit layers and per-layer tier column, read from compcode.hpp."""
    source = D2D_COMPCODE.read_text()
    lit = re.search(r"for \(int lit_layer : \{([^}]*)\}\)", source)
    placed = re.search(r"kLayer\[6\] = \{([^}]*)\}", source)
    tier = re.search(r"kTier\[10\] = \{([^}]*)\}", source)
    require(lit and placed and tier, "compcode.hpp's lit-layer lists or kTier moved")
    tiers = [int(value) for value in tier.group(1).split(",")]
    layers = [int(value) for value in lit.group(1).split(",")]
    placed_layers = [int(value) for value in placed.group(1).split(",")]
    require(placed_layers == layers, f"compcode.hpp's kLayer {placed_layers} != lit_layer {layers}")
    return layers, {layer: D2D_TIER_COLUMNS[tiers[layer]].lower() for layer in layers}


def main():
    """Run every layer through both functions and the component copy, then compare d2d."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    lines = []

    def call(name, *args, **kwargs):
        """Call a stock function and require that it returned."""
        value = machine.call(*args, limit=INSTRUCTION_LIMIT, **kwargs)
        require(machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{name} did not return")
        return value

    def error_line(current):
        """Record the reported line (cdecl: the caller pops)."""
        lines.append(current.arg(2) if current.arg(2) < 0x10000 else None)
        return 0

    def fatal(current):
        """Stop at the fatal exit instead of running it."""
        raise Fatal(lines[-1] if lines else None)

    machine.hook(ERROR_LINE, error_line, 0)
    machine.hook(FATAL_EXIT, fatal, 0)
    record = machine.alloc(0x200)
    machine.hook(ITEMS_LINE, lambda e: record, 1)

    # Which layers have colours.
    lit = [layer for layer in range(16) if call("FUN_0064f420", HAS_COLOURS, layer) == 1]
    require(lit == sorted(LIT_LAYERS), f"lit layers {lit}")

    # The six bytes FUN_0063d690 copies, by column name.
    offsets = armor_columns(exe)
    require(sorted(offsets.values()) == list(range(0x116, 0x11C)) and len(offsets) == 6
            and sorted(offsets, key=offsets.get) == list(COLUMNS),
            f"FUN_006315d0 tier columns {offsets}")
    for name, offset in offsets.items():
        machine.mu.mem_write(record + offset, bytes([0x40 + COLUMNS.index(name)]))
    unit = machine.alloc(0x100)
    out = machine.alloc(8)
    call("FUN_0063d690", BODY_COMPONENTS, regs={"eax": unit, "esi": out})
    copied = machine.read(out, 6)
    require(copied == bytes(0x40 + COLUMNS.index(name) for name in sorted(offsets, key=offsets.get)),
            f"FUN_0063d690 copied {copied.hex()}")
    order = [COLUMNS[byte - 0x40] for byte in copied]
    machine.w32(out, 0x77777777)
    call("FUN_0063d690", BODY_COMPONENTS, regs={"eax": 0, "esi": out})
    require(machine.read(out, 6) == bytes(6), "FUN_0063d690 with no item did not clear six bytes")

    # Which of them each layer takes; the rest are fatal.
    machine.mu.mem_write(out, bytes(range(0x40, 0x46)))
    taken, fatal_lines = {}, {}
    for layer in range(18):
        lines.clear()
        try:
            value = call("FUN_0064f500", LAYER_TIER, layer, out)
            require(0x40 <= value < 0x46, f"FUN_0064f500 layer {layer} returned {value:#x}")
            taken[layer] = order[value - 0x40]
        except Fatal as stop:
            fatal_lines[layer] = stop.args[0]
    require(taken == LIT_LAYERS, f"FUN_0064f500 layer -> column {taken}")
    require(all(fatal_lines[layer] == 0x274 for layer in range(16) if layer not in LIT_LAYERS)
            and fatal_lines[16] == fatal_lines[17] == 0x265, f"fatal lines {fatal_lines}")

    # d2d: the same lit layers, each from the same column.
    d2d_lit, d2d_columns = d2d_tables()
    require(d2d_lit == lit, f"d2d's lit layers {d2d_lit} != game.exe's {lit}")
    require(d2d_columns == {layer: name.lower() for layer, name in taken.items()},
            f"d2d's kTier {d2d_columns} != game.exe's {taken}")
    print("ok: lit layers " + " ".join(f"{LAYER_NAMES[layer]}={taken[layer]}" for layer in lit)
          + f"; {16 - len(lit)} other layers fatal; d2d's compcode tables match")


if __name__ == "__main__":
    main()
