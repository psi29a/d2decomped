# SPDX-License-Identifier: GPL-3.0-or-later
"""BaalTaunt's think (MonAI 136, FUN_005ef710) run natively on random scenes.

    uv run python baal_taunt.py [cases] [seed] [--dump ../../tests/baal_taunt_cases.inc]

A scene: the target's unit type and mode, the AI control's +0x14 count, the AI
distance, aip1..3, the difficulty. The acts are hooked (FUN_005dead0 skill,
FUN_00554ea0 warp, always made; FUN_005deca0 straight at it, FUN_005de080
stand). A line: type mode count dist aip1 aip2 aip3 | act n count-after.
tests/test_monsters.cpp runs rules::mon_think on each.
"""
import random
import struct
import sys

import emu

THINK = 0x5ef710


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dump = sys.argv[sys.argv.index("--dump") + 1] if "--dump" in sys.argv else None
    if dump: args.remove(dump)
    cases, seed = int(args[0]) if args else 400, int(args[1]) if len(args) > 1 else 1
    rng = random.Random(seed)
    e = emu.Emu()
    out = []
    e.hook(0x5dead0, lambda e: out.append(("skill", e.arg(1))) or 1, 5)
    e.hook(0x554ea0, lambda e: out.append(("warp", 0)) or 1, 5)
    e.hook(0x5deca0, lambda e: out.append(("direct", 0)) or 0, 1)
    e.hook(0x5de080, lambda e: out.append(("idle", e.arg(0))) or 0, 1)
    e.hook(0x620bb0, lambda e: 1, 1)
    e.hook(0x45adf0, lambda e: 5000, 0)
    e.hook(0x45ae20, lambda e: 5000, 0)

    def alloc(n): a = e.alloc(n); e.mu.mem_write(a, bytes(n)); return a
    game, mon, foe, ctrl, row, params = alloc(0x100), alloc(0x100), alloc(0x100), alloc(0x40), alloc(0x200), alloc(0x40)
    lines = []
    for _ in range(cases):
        kind, mode = rng.choice((0, 0, 1, 1, 2, 3)), rng.choice((1, 1, 1, 2, 3, 0, rng.randint(0, 15)))
        count, dist, diff = rng.choice((0, rng.randint(0, 12), rng.randint(0, 40))), rng.choice((rng.randint(0, 6), rng.randint(0, 40))), rng.randint(0, 2)
        aip = [rng.choice((3, 10, 20, rng.randint(0, 30))) for _ in range(3)]
        e.mu.mem_write(game + 0x6d, bytes([diff]))
        for k, v in enumerate(aip): e.mu.mem_write(row + 0x56 + 6 * k + 2 * diff, struct.pack("<h", v))
        e.w32(foe, kind); e.w32(foe + 0x10, mode)
        e.w32(ctrl + 0x14, count)
        e.w32(params, ctrl); e.w32(params + 8, foe); e.w32(params + 20, dist); e.w32(params + 28, row)
        out.clear()
        e.call(THINK, params, ecx=game, edx=mon)
        assert len(out) == 1 or out == [("warp", 0), ("idle", 25)], out   # a warp made, it stands 25
        act, n = out[0]
        lines.append(f'"{kind} {mode} {count} {dist} {aip[0]} {aip[1]} {aip[2]} | {act} {n} {e.r32(ctrl + 0x14)}",')
    if dump:
        with open(dump, "w") as f:
            f.write("// tools/emu/baal_taunt.py --dump: game.exe's BaalTaunt think (FUN_005ef710), a line each.\n")
            f.write("\n".join(lines) + "\n")
    print("ok", len(lines))


if __name__ == "__main__":
    main()
