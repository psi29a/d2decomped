# SPDX-License-Identifier: GPL-3.0-or-later
"""Check stock item-panel mode, cell conversion and pointer hit-test helpers.

Run with ``python d2common_item_ui.py`` from ``tools/emu``. The cell oracle
stubs only item dimensions and viewport adjustment; these checks do not run
the full inventory mouse event or UI rendering paths.
"""
import hashlib
import sys
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP

import emu


GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
RET_MAGIC = emu.RET_MAGIC
CALL_LIMIT = 1_000_000
STARTUP_LIMIT = 50_000_000
MODE_PREDICATE = 0x483E10
GRID_COORDINATES = 0x486BD0
PANEL_HIT_TEST = 0x486E10
ITEM_MODE = 0x7BCBF0
SCREEN_WIDTH = 0x71146C
SCREEN_TOP = 0x711470
PANEL_WIDTH = 0x7A2858
PANEL_TOP = 0x7A285C


class BoundedEmu(emu.Emu):
    """Cap each stock function call and reject an instruction-limit stop."""

    def call(self, address, *args, ecx=0, edx=0, cdecl=False, limit=0, regs=None):
        """Run a call with a finite limit and require the emulator return sentinel."""
        cap = limit or STARTUP_LIMIT
        result = super().call(address, *args, ecx=ecx, edx=edx, cdecl=cdecl, limit=cap, regs=regs)
        eip = self.mu.reg_read(UC_X86_REG_EIP)
        if eip != RET_MAGIC:
            raise AssertionError(f"game.exe call {address:08x} stopped at {eip:08x}, not its return sentinel")
        return result


def require(condition, message):
    """Fail the oracle with a focused mismatch message."""
    if not condition:
        raise AssertionError(message)


def call_returned(machine, address, *args, ecx=0, edx=0, regs=None):
    """Run one stock function with a cap and require that it returned."""
    return machine.call(address, *args, ecx=ecx, edx=edx, limit=CALL_LIMIT, regs=regs)


def check_mode_predicate(machine):
    """Check FUN_00483e10's allowed inventory interaction modes."""
    for mode in range(20):
        machine.w32(ITEM_MODE, mode)
        got = call_returned(machine, MODE_PREDICATE)
        expected = int(mode in {0, 1, 2, 3, 4})
        require(got == expected, f"FUN_00483e10 mode {mode}: {got} != {expected}")
    return 20


def check_panel_hit_test(machine):
    """Check FUN_00486e10's inclusive panel rectangle and adjacent pixels."""
    machine.w32(SCREEN_WIDTH, 800)
    machine.w32(SCREEN_TOP, 20)
    machine.w32(PANEL_WIDTH, 100)
    machine.w32(PANEL_TOP, 400)
    x0, x1 = 800 - 100 - 0x12E, 800 - 100 - 0x10E
    y0, y1 = 400 - 0x60 + 20, 400 - 0x40 + 20
    cases = (
        (x0, y0, 1), (x0, y1, 1), (x1, y0, 1), (x1, y1, 1),
        (x0 - 1, y0, 0), (x1 + 1, y0, 0), (x0, y0 - 1, 0),
        (x0, y1 + 1, 0), ((x0 + x1) // 2, (y0 + y1) // 2, 1),
    )
    for x, y, expected in cases:
        got = call_returned(machine, PANEL_HIT_TEST, ecx=x, edx=y)
        require(got == expected, f"FUN_00486e10 ({x}, {y}): {got} != {expected}")
    return len(cases)


def check_grid_coordinates(machine):
    """Check FUN_00486bd0's item-footprint centering and lower-edge clamp."""
    grid = machine.alloc(0x20)
    item = machine.alloc(0x40)
    out_x, out_y = machine.alloc(4), machine.alloc(4)
    machine.w32(grid + 4, 100)
    machine.w32(grid + 0xC, 200)
    machine.mu.mem_write(grid, bytes((10, 8)))
    machine.mu.mem_write(grid + 0x14, bytes((20, 20)))
    dimensions = {"width": 1, "height": 1}

    def item_size(current):
        """Supply controlled item footprint bytes to the stock coordinate helper."""
        width_out = current.arg(1)
        height_out = current.arg(2)
        current.mu.mem_write(width_out, bytes((dimensions["width"],)))
        current.mu.mem_write(height_out, bytes((dimensions["height"],)))
        return 0

    def viewport(current):
        """Report zero viewport correction for this isolated grid calculation."""
        current.w32(current.arg(1), 0)
        current.w32(current.arg(2), 0)
        return 0

    machine.hook(0x6286C0, item_size, 5)
    machine.hook(0x4DBEA0, viewport, 3)
    cases = (
        # Mouse cell (column,row), item footprint, expected top-left cell.
        (141, 241, 1, 1, 2, 2),
        (141, 241, 2, 2, 1, 1),
        (159, 259, 2, 2, 1, 1),
        (141, 241, 3, 1, 1, 2),
        (101, 201, 3, 3, 0, 0),
        (181, 261, 5, 3, 2, 2),
        (141, 241, 3, 5, 1, 0),
        (101, 201, 10, 1, 0, 0),
    )
    for mouse_x, mouse_y, width, height, expected_x, expected_y in cases:
        dimensions.update(width=width, height=height)
        machine.w32(out_x, 0xA5A5A5A5)
        machine.w32(out_y, 0xA5A5A5A5)
        call_returned(machine, GRID_COORDINATES, mouse_x, mouse_y, out_x, out_y,
                      regs={"eax": grid}, ecx=item)
        got_x, got_y = machine.r32(out_x), machine.r32(out_y)
        require((got_x, got_y) == (expected_x, expected_y),
                f"FUN_00486bd0 footprint {width}x{height} at ({mouse_x}, {mouse_y}): "
                f"({got_x}, {got_y}) != ({expected_x}, {expected_y})")
    return len(cases)


def main():
    """Verify hash-pinned stock item UI helper behavior on bounded fixtures."""
    if len(sys.argv) != 1:
        raise SystemExit("usage: d2common_item_ui.py")
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    require(digest == GAME_SHA256, f"requires stock game.exe 1.14d; got {digest}")
    machine = BoundedEmu(str(exe))
    machine.crt_init()
    cases = check_mode_predicate(machine)
    cases += check_panel_hit_test(machine)
    cases += check_grid_coordinates(machine)
    print(f"ok: {cases} stock D2COMMON item UI helper cases")


if __name__ == "__main__":
    main()
