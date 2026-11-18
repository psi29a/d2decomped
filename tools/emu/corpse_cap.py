# SPDX-License-Identifier: GPL-3.0-or-later
"""Which way a death goes by the player's corpse count, as the oracle: FUN_0057f700.

    uv run python corpse_cap.py > ../../tests/corpse_cap_cases.inc

The death path (FUN_0057fca0 -> FUN_0057f700) first prunes the corpse list
(FUN_0057f690), then asks the inventory for its count (FUN_0063d5f0,
inventory +0x3c) and compares it with 0xf (CMP / JBE at 0x57f72d): above,
FUN_0057f540 drops the gear on the ground and the death makes no corpse
(returns 0); else it goes on to make one. Run for real: FUN_0057f700 up to
that branch and FUN_0063d5f0. Hooked: FUN_0057f690 (its count is unused
there), FUN_0057f540 (records "drop"), and the corpse path's first call,
the assert lookup FUN_00408090 reached with no corpse spot (records
"corpse" and stops). Prints one `{count, made}` line per count 0..20 (made
1: the corpse path), the file d2d's test reads.
"""
import emu

INVENTORY_MAGIC = 0x1020304


def main():
    e = emu.Emu()
    seen = []
    e.hook(0x57f690, lambda _e: 0, 1)
    e.hook(0x57f540, lambda _e: seen.append(0), 1)

    def corpse_path(em):
        seen.append(1)
        em.mu.emu_stop()
        return 0
    e.hook(0x408090, corpse_path, 1)
    inventory = e.alloc(0x40)
    unit = e.alloc(0x100)
    game = e.alloc(0x100)
    e.w32(inventory, INVENTORY_MAGIC)
    e.w32(unit + 0x60, inventory)
    print("// tools/emu/corpse_cap.py: {corpse count, 1 when FUN_0057f700 goes on to make a corpse}")
    for count in range(21):
        e.w32(inventory + 0x3c, count)
        seen.clear()
        e.call(0x57f700, 0, 0, 0, ecx=game, edx=unit)    # stack: room, x / y, no spot (0)
        if len(seen) != 1: raise SystemExit(f"corpse_cap: count {count}: {seen}")
        print(f"{{{count}, {seen[0]}}},")


if __name__ == "__main__":
    main()
