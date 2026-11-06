# SPDX-License-Identifier: GPL-3.0-or-later
"""Where Show Items puts each floor item's label, as the oracle: FUN_004c0640.

    uv run python show_items_layout.py gen 5000 1 > cases.tsv
    uv run python show_items_layout.py game cases.tsv > g.txt

FUN_004c0810 (UI\\showitems.cpp) builds a label's rect from the item's spot
(x, y) and its text's size (w, h): left = x - (w + 8) / 2 (sar), right =
left + w + 8, top = y - h + 4, bottom = y. Then it calls FUN_004c0640(&left,
&top, &right, &bottom, try, max_x, min_x) for try 0..11 (fastcall: ECX &left,
EDX &top, the rest on the stack, ret 0x14) and stops at the first that
returns 1 (clear of every label placed so far). The label is placed (its
rect stored at 0x7c54a8 + 0x120 i, the count at 0x7c54a0 bumped) only if a
try returned 1 and then right <= max_x and left >= min_x. max_x is the
screen width (0x71146c), or width / 2 - 0x20 with a right panel open; min_x
0, or width / 2 with a left panel. A spot outside min_x..max_x or y outside
-8..height - 0x10 gets no label and no try.

A case is one screen: `scene width height min_x max_x n {x y w h}...`, the
items in draw order. `game` writes the screen size (0x71146c / 0x711470),
zeroes the list, and for each item runs the caller's loop above (read from
FUN_004c0810's asm, 0x4c09df..0x4c0ab8) around FUN_004c0640 run for real.
It prints a line an item: `scene item tries placed left top right bottom`
(tries: how many calls, 0 for a skipped spot; the rect as the last try left
it, or as built for a skipped spot).

What the run shows (a Python model of cursor-units.md's steps matched 81942
of 81942 items, gen 5000 1, every try count 0..12 hit): a left edge under
min_x goes to 6, not min_x; tries 0..6 move the rect up above the label it
hits (bottom = its top - 3), or down below it (top = its bottom + 5) when
it's under 2 high or its top is past height - 0x30; tries 7..11 move it right
of the label (left = its right + 1), or left of it only when left >= max_x.
"""
import random
import sys

import emu

WIDTH, HEIGHT = 0x71146C, 0x711470
COUNT, LIST, STRIDE = 0x7C54A0, 0x7C54A8, 0x120


def gen(count, seed):
    rng = random.Random(seed)
    for scene in range(count):
        width, height = rng.choice(((800, 600), (640, 480)))
        min_x, max_x = rng.choice(((0, width), (0, width), (0, width // 2 - 0x20), (width // 2, width)))
        crowd = rng.random() < 0.5                  # a pile on a few spots, or spread out
        centre = (rng.randrange(width), rng.randrange(height))
        items = []
        for _ in range(rng.randint(1, 32)):
            if crowd:
                x, y = centre[0] + rng.randint(-60, 60), centre[1] + rng.randint(-40, 40)
            else:
                x, y = rng.randint(-20, width + 20), rng.randint(-20, height + 10)
            items.append((x, y, rng.randint(6, 260), rng.choice((10, 12, 13, 16, 18))))
        flat = " ".join(f"{x} {y} {w} {h}" for x, y, w, h in items)
        print(scene, width, height, min_x, max_x, len(items), flat)


def game(path):
    e = emu.Emu()
    e.hook(0x408A60, emu.Emu.fatal, 0)
    rect = e.alloc(16)
    left, top, right, bottom = rect, rect + 4, rect + 8, rect + 12
    for line in open(path):
        field = list(map(int, line.split()))
        scene, width, height, min_x, max_x, count = field[:6]
        e.w32(WIDTH, width)
        e.w32(HEIGHT, height)
        e.w32(COUNT, 0)
        placed = 0
        for item in range(count):
            x, y, text_w, text_h = field[6 + 4 * item:10 + 4 * item]
            box_w = text_w + 8
            e.w32(left, x - (box_w >> 1))
            e.w32(right, x - (box_w >> 1) + box_w)
            e.w32(top, y - text_h + 4)
            e.w32(bottom, y)
            tries, clear = 0, 0
            if min_x <= x <= max_x and -8 <= y <= height - 0x10:
                while tries < 12 and not clear:
                    clear = e.call(0x4C0640, right, bottom, tries, max_x, min_x, ecx=left, edx=top)
                    tries += 1
            ok = clear and e.s32(right) <= max_x and e.s32(left) >= min_x
            if ok:
                slot = LIST + STRIDE * placed
                for offset in range(0, 16, 4):
                    e.w32(slot + offset, e.r32(rect + offset))
                placed += 1
                e.w32(COUNT, placed)
            print(scene, item, tries, int(bool(ok)), e.s32(left), e.s32(top), e.s32(right), e.s32(bottom))


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif len(sys.argv) == 3 and sys.argv[1] == "game":
        game(sys.argv[2])
    else:
        sys.exit(__doc__)
