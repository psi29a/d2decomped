# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a2q2's quest-log send from an item placed on a player corpse.

    .venv/bin/python staff_corpse_news.py [--mutation-check]
    .venv/bin/python staff_corpse_news.py --dump ../../tests/staff_corpse_news_cases.inc

The stock death path FUN_0057f700 and saved-corpse loader FUN_00533850 copy
the owner's client from FUN_005531c0 onto the corpse with FUN_00553180.  This
oracle performs that copy with the real functions, then runs FUN_00599a30,
FUN_00599510 / FUN_00599570 and FUN_00544190 for real.  Item-row, quest-bit
and act lookups are synthetic; the final FUN_0053d710 send is captured.
"""
import argparse
import hashlib
import sys
import tempfile
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
CODES = ("tr1 ", "vip ", "box ", "msf ", "hst ", "qey ")
OWNER_MASKS = (0, 1 << 0, 1 << 3, 1 << 15, (1 << 0) | (1 << 13))
MUTATIONS = ("suppress-corpse", "owner-bits", "wrong-client", "staff-sends")


class Game:
    """A stock-code runner with one owner, attached corpse and quest record."""

    def __init__(self, exe):
        """Lay out synthetic units and hook boundaries outside the claim."""
        self.machine = machine = emu.Emu(str(exe))
        self.game = machine.alloc(0x1200)
        self.quest_data = machine.alloc(0x20)
        self.record = machine.alloc(0x100)
        self.block = machine.alloc(0x40)
        self.item = machine.alloc(0x40)
        self.item_row = machine.alloc(0x100)
        self.owner = machine.alloc(0x100)
        self.owner_data = machine.alloc(0x100)
        self.owner_bits = machine.alloc(0x60)
        self.corpse = machine.alloc(0x100)
        self.corpse_data = machine.alloc(0x100)
        self.corpse_bits = machine.alloc(0x60)
        self.client = 0x12345678
        self.owner_mask = 0
        self.sent = []

        machine.w32(self.game + 0x10F4, self.quest_data)
        machine.w32(self.quest_data, self.record)
        machine.w32(self.record, 9)
        machine.mu.mem_write(self.record + 8, b"\x01")
        machine.w32(self.record + 4, self.game)
        machine.w32(self.block, self.game)
        machine.w32(self.block + 8, self.item)
        machine.w32(self.block + 0xC, self.corpse)
        machine.w32(self.item + 4, 77)
        machine.w32(self.owner + 0x14, self.owner_data)
        machine.w32(self.owner_data + 0x10, self.owner_bits)
        machine.w32(self.owner_data + 0x9C, self.client)
        machine.w32(self.corpse + 0x14, self.corpse_data)
        machine.w32(self.corpse_data + 0x10, self.corpse_bits)

        machine.hook(0x6335F0, lambda current: self.item_row if current.arg(0) == 77 else 0, 1)
        machine.hook(0x620BB0, lambda current: 0x2000, 1)
        machine.hook(0x61A1B0, lambda current: 0x3000, 1)
        machine.hook(0x6427F0, lambda current: 1, 1)

        def bit(current):
            """FUN_0065c310 over the owner's variable and corpse's clear bits."""
            bits = current.arg(0)
            quest = current.arg(1)
            index = current.arg(2)
            if quest != 10:
                return 0
            if bits == self.owner_bits:
                return int(bool(self.owner_mask & (1 << index)))
            if bits == self.corpse_bits:
                return 0
            raise AssertionError(f"unexpected quest-bits pointer {bits:#x}")

        def default_log(current):
            """FUN_00543f90 boundary: put a recognizable log state in 0x5d."""
            machine.mu.mem_write(current.mu.reg_read(UC_X86_REG_EDX), b"\x07")
            return 0

        def send(current):
            """FUN_0053d710 boundary: capture the resolved client and packet."""
            target = current.mu.reg_read(UC_X86_REG_ECX)
            packet = machine.read(current.mu.reg_read(UC_X86_REG_EDX), 6)
            self.sent.append((target, packet))
            return 0

        machine.hook(0x65C310, bit, 3)
        machine.hook(0x543F90, default_log, 1)
        machine.hook(0x53D710, send, 0)

        owner_client = machine.call(0x5531C0, 0x306, ecx=self.owner, edx=0x6E17E0)
        if owner_client != self.client:
            raise AssertionError("FUN_005531c0 did not resolve the owner's client")
        machine.call(0x553180, ecx=self.corpse, edx=owner_client)
        if machine.call(0x5531C0, 0x57E, ecx=self.corpse, edx=0x6E0BE4) != self.client:
            raise AssertionError("FUN_00553180 did not attach the corpse to the owner's client")

    def pickup(self, code, owner_mask):
        """Run one corpse item event and return its captured sends."""
        self.owner_mask = owner_mask
        self.sent.clear()
        self.machine.mu.mem_write(self.item_row + 0x80, code.encode())
        self.machine.mu.mem_write(self.record + 0xB, b"\xee")
        self.machine.call(0x599A30, ecx=self.record, edx=self.block, limit=20000)
        return tuple(self.sent)


def model(code, owner_mask, client, mutation=None):
    """Expected corpse send, with optional deliberately wrong variants."""
    code = code.strip()
    sends = code in ("tr1", "vip", "box", "msf")
    if mutation == "suppress-corpse":
        sends = False
    elif mutation == "owner-bits":
        sends = sends and ((not owner_mask & 1 and not owner_mask & (1 << 15)) or owner_mask & ((1 << 13) | (1 << 14)))
    elif mutation == "staff-sends" and code == "hst":
        sends = True
    target = 0 if mutation == "wrong-client" else client
    return ((target, bytes((0x5D, 9, 0, 7, 0, 0))),) if sends else ()


def run(game, mutation=None):
    """Compare every stock result with the model and return fixture rows."""
    rows = []
    differences = 0
    for code in CODES:
        for owner_mask in OWNER_MASKS:
            actual = game.pickup(code, owner_mask)
            expected = model(code, owner_mask, game.client, mutation)
            if actual != expected:
                differences += 1
                if mutation is None:
                    print(f"{code!r} owner mask {owner_mask:#x}: game {actual!r}, model {expected!r}", file=sys.stderr)
            rows.append((code.strip(), owner_mask, bool(actual)))
    return rows, differences


def write_fixture(path, rows):
    """Replace the fixture only after a passing complete oracle run."""
    lines = [
        "// Stock game.exe 1.14d; generated by tools/emu/staff_corpse_news.py --dump.",
        "// item code, owning player's quest-10 bit mask, quest-news events",
    ]
    lines.extend(f'    {{ "{code}", 0x{owner_mask:x}u, {int(sent)} }},' for code, owner_mask, sent in rows)
    text = "\n".join(lines) + "\n"
    destination = Path(path)
    handle = tempfile.NamedTemporaryFile("w", dir=destination.parent, prefix=destination.name + ".", suffix=".tmp", delete=False)
    try:
        with handle:
            handle.write(text)
        Path(handle.name).replace(destination)
    except BaseException:
        Path(handle.name).unlink(missing_ok=True)
        raise


def main():
    """Run the oracle, optionally write its fixture and check mutations."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dump", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    game = Game(exe)
    rows, differences = run(game)
    if differences:
        raise SystemExit(f"FAIL: {differences} corpse pickup cases differ")
    print(f"ok: {len(rows)} a2q2 corpse pickup cases, routed to the owner's client")
    if args.dump:
        write_fixture(args.dump, rows)
        print(f"wrote {len(rows)} rows")
    if args.mutation_check:
        for mutation in MUTATIONS:
            _, caught = run(game, mutation)
            if not caught:
                print(f"NOT CAUGHT: {mutation}")
                return 1
            print(f"caught: {mutation} ({caught} cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
