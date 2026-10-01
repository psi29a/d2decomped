"""A monster's line of sight (FUN_00622920 -> FUN_0064e260) against
rules::sight_blocked: random walls in one fake room, random unit pairs.

    uv run python sight.py [cases] [seed]

FUN_00622920 (fastcall ECX x1, EDX size2, EAX x2; stack y1, size1, y2, room,
mask): the ends step their sizes toward each other, then FUN_0064e260 walks
the line over the room's collision words (room +0x4c/+0x50 origin, +0x54/
+0x58 size, +0x20 -> {+8 width, +0x20 words}). Prints `ok` or each
mismatch; `--dump` prints the cases as C++ test lines.
"""
import random
import sys

import emu


def port(x1, y1, s1, x2, y2, s2, wall):
    """components/rules/monsters.hpp sight_blocked, line for line."""
    s1, s2 = min(s1, 2), min(s2, 2)
    across, down = abs(x2 - x1), abs(y2 - y1)
    if across + down < s1 + s2: return False
    if down <= across:
        if x1 < x2: x1, x2 = x1 + s1, x2 - s2
        else: x1, x2 = x1 - s1, x2 + s2
    if across <= down:
        if y1 < y2: y1, y2 = y1 + s1, y2 - s2
        else: y1, y2 = y1 - s1, y2 + s2
    dx, dy = abs(x2 - x1), abs(y2 - y1)
    sx, sy = (1 if x2 >= x1 else -1), (1 if y2 >= y1 else -1)
    for k in range(max(dx, dy) + 1):
        ox, oy = (k, k * dy // dx if dx else 0) if dx >= dy else (k * dx // dy, k)
        if wall(x1 + sx * ox, y1 + sy * oy): return True
    return False


def main():
    cases = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 3000
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    dump = "--dump" in sys.argv
    e = emu.Emu()
    W = H = 40
    words = e.alloc(W * H * 2)
    coll = e.alloc(0x40); e.w32(coll + 8, W); e.w32(coll + 0x20, words)
    room = e.alloc(0x80); e.w32(room + 0x20, coll)
    for off, v in ((0x4c, 0), (0x50, 0), (0x54, W), (0x58, H)): e.w32(room + off, v)
    bad = seen = 0
    for case in range(cases):
        grid = [[0] * W for _ in range(H)]
        for _ in range(rng.choice((0, 3, 12, 40))):
            grid[rng.randrange(H)][rng.randrange(W)] = rng.choice((4, 4, 1, 0x405))
        e.mu.mem_write(words, bytes(b for row in grid for v in row for b in v.to_bytes(2, "little")))
        x1, y1, x2, y2 = (rng.randrange(3, W - 3) for _ in range(4))
        s1, s2 = rng.randrange(0, 4), rng.randrange(0, 4)
        got = e.call(0x622920, y1, s1, y2, room, 4, ecx=x1, edx=s2, regs={"eax": x2}) & 0xff
        seen += bool(got)
        want = port(x1, y1, s1, x2, y2, s2, lambda x, y: grid[y][x] & 4 != 0)
        if bool(got) != want:
            bad += 1
            print(f"case {case}: ({x1},{y1}) size {s1} -> ({x2},{y2}) size {s2}: game {got}, port {int(want)}")
        if dump and case < 12:
            walls = [(x, y) for y in range(H) for x in range(W) if grid[y][x] & 4]
            print(f"{{ {x1}, {y1}, {s1}, {x2}, {y2}, {s2}, {int(bool(got))}, {{ {', '.join(f'{{{x}, {y}}}' for x, y in walls)} }} }},")
    print(f"ok: {cases} cases, {seen} blocked" if not bad else f"{bad} of {cases} differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
