# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's struct fields by name, from docs/research/re/structs.tsv.

    unit = structs.view(e, "unit", e.alloc(0x100))
    unit.type, unit["class"] = 1, 60          # e.w32(addr, 1); e.w32(addr + 4, 60)
    path = unit.path                          # a ptr field reads as an int; view it in turn

Works on anything with read(addr, n) and mu.mem_write (an emu.Emu). A field
not in the table is an error: add its row, with its evidence, there.
"""
import csv, pathlib, struct

TABLE = pathlib.Path(__file__).resolve().parents[2] / "docs/research/re/structs.tsv"
FORMATS = {"u8": "<B", "u16": "<H", "u32": "<I", "i32": "<i", "ptr": "<I"}


def load(path=TABLE):
    fields = {}
    with open(path, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            fields.setdefault(row["struct"], {})[row["name"]] = (int(row["offset"], 16), FORMATS[row["type"]])
    return fields


FIELDS = load()


class View:
    def __init__(self, e, kind, addr):
        object.__setattr__(self, "_at", (e, FIELDS[kind], addr))

    def __getattr__(self, name):
        e, fields, addr = self._at
        offset, form = fields[name]
        return struct.unpack(form, e.read(addr + offset, struct.calcsize(form)))[0]

    def __setattr__(self, name, value):
        e, fields, addr = self._at
        offset, form = fields[name]
        e.mu.mem_write(addr + offset, struct.pack(form, value & (1 << 8 * struct.calcsize(form)) - 1 if form[1].isupper() else value))

    __getitem__, __setitem__ = __getattr__, __setattr__   # for names Python reserves: unit["class"]


def view(e, kind, addr):
    return View(e, kind, addr)
