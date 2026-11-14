# SPDX-License-Identifier: GPL-3.0-or-later
"""Check both branches of DataTbls.cpp FUN_00612750 on stock game.exe 1.14d.

Usage: D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_code_tables.py
       D2_GAME_EXE=/path/to/Game.exe .venv/bin/python datatbls_code_tables.py --schema-digests

CompileTxt and link allocation are hooked. The function itself chooses the
table order and destinations. This does not read MPQs or write game data.
"""

import sys

from _monster_world import require
from datatbls_probe import schema_digest
from monster_loaders import Rig


# Name, record size, link source line, table pointer, link pointer, row count.
# The addresses and order are from FUN_00612750 in stock game.exe 1.14d.
TABLES = (
    ("compcode", 4, 0x73, 0x96BCAC, 0x96BCB0, 0x96BCB4),
    ("playerclass", 4, 0x84, 0x96BC30, 0x96BC34, None),
    ("bodylocs", 4, 0x90, 0x96BC38, 0x96BC3C, None),
    ("storepage", 4, 0x9C, 0x96BC40, 0x96BC44, None),
    ("elemtypes", 4, 0xA8, 0x96BC48, 0x96BC4C, None),
    ("hitclass", 4, 0xB8, 0x96BC50, 0x96BC54, None),
    ("colors", 4, 0xC4, 0x96C5E0, 0x96C5E4, None),
    ("hiredesc", 4, 0xD0, 0x96BCE4, 0x96BCE8, None),
    ("monmode", 4, 0xDC, 0x96BC58, 0x96BC5C, None),
    ("plrmode", 4, 0xE8, 0x96BC60, 0x96BC64, None),
    ("monai", 2, 0xF4, 0x96BCB8, 0x96BCBC, 0x96BCC0),
    ("monplace", 2, 0x100, 0x96C6CC, 0x96C6D0, 0x96C6D4),
    ("skillcalc", 4, 0x10C, 0x96BC68, 0x96BC6C, None),
    ("misscalc", 4, 0x118, 0x96BC88, 0x96BC8C, None),
    ("skills", 2, 0x123, 0x96BC9C, 0x96BCA0, None),
    ("events", 2, 0x12F, 0x96BCA4, 0x96BCA8, None),
)
SCHEMA_DIGESTS = (
    "ed8902f1cb3a05d14e817cdaef352b02bfc97238fdf87316ccf684543d5531aa",
    "0c7f16b64d105e9894b438ccc702a59026404ff6bf071f05824d7f0d00ee48b4",
    "7703e888d728b7ada37d91a279dc53561f1722f0845c43974b14790fc9e209a6",
    "d583bf0923bbf88260c78ccef04374616f42a827fd370c08acf75e2e0d321649",
    "53d52757959ca9fd690939db1850e589f0c574553f840e81ae6f251afead9191",
    "fd8332203294b86943e37aa28c7fb37a124e158e091b9685dadbbcc6911bce52",
    "d901c7eb79b73da18c28d0818d512a61305e811922d610236c37c7af513569c4",
    "883f7589812f68e00c5324fa1a6beb8f13abce0d89e0b54b173ad5759e8f9fce",
    "7b6587bebbc88d8a23511e31ad7708db33a4c79873c765f6bfea4e4f4394b7a1",
    "c68aaabdd466284732fd26ada33a9633b08efddf2b8bfbad278694b09ceda764",
    "30d7ec05f042de49c82336c16575abf575c913f9def3b7eb840f9629df98f8f7",
    "c1d5c66f36964443f952aa75ac71fcdaa06353b0a4c382962ad4e2dc7b168ee9",
    "70cb3a1ecd2608f8ab05ac908244871f107d2c07291c713e5d26ffcd9783e890",
    "70ffe63cafa4dbd94b0aabbe4640d25d2cf0e1dc1693e2794583dfb519a1b85e",
    "6ba34d3ae829e8266437fd2adb2b934a97d28f04e04b6e64c0a164a0e8a8fc05",
    "10dc4742d7126d88a87e98123f827b3627c24a079f5bf43385eb1b69e18f0fb2",
)


def check(enabled, rows):
    """Check the call sequence and every persistent table, link and count slot."""
    rig = Rig()
    rig.table = rig.m.alloc(0x100)
    rig.rows = rows
    rig.links = [rig.m.alloc(4) for _ in TABLES]
    links = tuple(rig.links)
    rig.m.w32(0x96C8B4, enabled)
    for _, _, _, table_slot, link_slot, count_slot in TABLES:
        rig.m.w32(table_slot, 0)
        rig.m.w32(link_slot, 0)
        if count_slot is not None:
            rig.m.w32(count_slot, 0)
    result = rig.go(0x612750)
    require(result[0] == "ret", f"code table loader did not return: {result}; {rig.log}")
    expected = TABLES if enabled else TABLES[:1]
    observed = tuple((event[2], event[4]) for event in rig.log if event[0] == "compile")
    created = tuple(event[2] for event in rig.log if event[0] == "link")
    require(observed == tuple((name, size) for name, size, *_ in expected),
            f"code table compile order: {observed}")
    require(created == tuple(line for _, _, line, *_ in expected),
            f"code table link lines: {created}")
    for index, (_, _, _, table_slot, link_slot, count_slot) in enumerate(TABLES):
        active = index < len(expected)
        require(rig.m.r32(table_slot) == (rig.table if active else 0),
                f"table slot {table_slot:#x}")
        require(rig.m.r32(link_slot) == (links[index] if active else 0),
                f"link slot {link_slot:#x}")
        if count_slot is not None:
            require(rig.m.r32(count_slot) == (rows if active else 0),
                    f"count slot {count_slot:#x}")
    require(len(rig.field_history) == len(expected), "missing field schemas")
    digests = tuple(schema_digest(fields) for fields in rig.field_history)
    require(digests == SCHEMA_DIGESTS[:len(expected)],
            f"code table field schemas differ: {digests}")
    return digests


def main():
    """Exercise compile flag off/on with empty and populated synthetic tables."""
    if "--schema-digests" in sys.argv[1:]:
        for row, digest in zip(TABLES, check(1, 0)):
            print(f"{row[0]} {digest}")
        return
    for enabled in (0, 1):
        for rows in (0, 1, 3):
            check(enabled, rows)
    print("ok: FUN_00612750 six flag and row-count cases, 16 table destinations")


if __name__ == "__main__":
    main()
