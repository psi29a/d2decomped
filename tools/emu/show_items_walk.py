# SPDX-License-Identifier: GPL-3.0-or-later
"""Which ground items Show Items labels, in what order, where and lit: FUN_004c0810.

    uv run python show_items_walk.py gen 3000 1 > cases.tsv
    uv run python show_items_walk.py [--exe PATH] game cases.tsv > g.txt      # game.exe's draws
    ../../build/tests/test_show_items_walk cases.tsv | diff g.txt -
    uv run python show_items_walk.py inc cases.tsv g.txt > ../../tests/show_items_walk_cases.inc   # (gen 300 1)

FUN_004c0810 (UI\\showitems.cpp) runs for real: the walk of the player's
room's near list (FUN_00619790, real), each room's units from its list's
head (room +0x74, the next at unit +0xe8), the drawn items (type 4, +0xc8
bit 7) while under 0x20 are placed; the spot (FUN_00620650 / FUN_006206b0,
real: a static path's +4 / +8, less FUN_0045afc0 / FUN_0045afd0, real, the
view at 0) 16 up; the rect, FUN_004c0640's tries (real), and the hover
test. Hooked: the player and its room (FUN_00463dd0, FUN_00620bb0), the
cursor's unit and the mouse (FUN_00467a10, FUN_00468730 / FUN_00468740),
the open panel (FUN_0045ae90), the name and its size (FUN_004527d0,
FUN_00502520), the palette's blue (FUN_004fb180), the colour lookups
(FUN_00627e70 quality 2, FUN_006280a0 0, FUN_006335f0 a zeroed row), the
hover text (FUN_00467a70, FUN_00466fe0, FUN_00502280), and the draw
(FUN_005023b0) recorded: each label in draw order, from its slot.

A case is one screen: `scene width height side mouse_x mouse_y cursor rooms
{units {type drawn x y w h}...}...` (side 0 none, 1 right panel, 2 left;
cursor the index of the unit under the cursor, -1 none; units numbered
across the rooms in order, each room's from its head; x y the client
position). `game` prints a line a label: `scene unit left top right bottom
lit`, then `scene end count`.
"""
import random
import sys

import emu

WIDTH, HEIGHT = 0x71146C, 0x711470
VIEW_X, VIEW_SHIFT, VIEW_Y = 0x7A520C, 0x7A5214, 0x7A5208
SLOT_UNIT, SLOT_COLOUR, SLOT_MODE = 0x10, 0x114, 0x118


def gen(count, seed):
    rng = random.Random(seed)
    for scene in range(count):
        width, height = rng.choice(((800, 600), (640, 480)))
        side = rng.choice((0, 0, 1, 2))
        crowd = rng.random() < 0.5
        busy = rng.random() < 0.2                                 # past 0x20 labels: a floor full of loot
        centre = (rng.randrange(width), rng.randrange(height))
        rooms, total = [], 0
        for _ in range(rng.randint(1, 9)):
            units = []
            for _ in range(rng.randint(8, 20) if busy else rng.choice((0, 1, 2, 4, rng.randint(0, 12)))):
                kind = 4 if rng.random() < 0.8 else rng.choice((0, 1, 2, 3, 5))
                drawn = int(rng.random() < 0.85)
                if crowd and not busy:
                    x, y = centre[0] + rng.randint(-60, 60), centre[1] + rng.randint(-40, 40)
                else:
                    x, y = rng.randint(-30, width + 30), rng.randint(-20, height + 30)
                units.append((kind, drawn, x, y, rng.randint(6, 60 if busy else 200), rng.choice((12, 16, 18, 22))))
            rooms.append(units)
            total += len(units)
        mouse = (rng.randrange(width), rng.randrange(height)) if rng.random() < 0.6 else (-1000, -1000)
        if crowd and rng.random() < 0.5:
            mouse = (centre[0] + rng.randint(-40, 40), centre[1] + rng.randint(-50, 20))
        cursor = rng.randrange(total) if total and rng.random() < 0.3 else -1
        flat = " ".join(f"{len(units)} " + " ".join(" ".join(map(str, unit)) for unit in units) for units in rooms)
        print(scene, width, height, side, mouse[0], mouse[1], cursor, len(rooms), " ".join(flat.split()))


def parse(line):
    field = list(map(int, line.split()))
    scene, width, height, side, mouse_x, mouse_y, cursor, room_count = field[:8]
    at, rooms = 8, []
    for _ in range(room_count):
        count = field[at]
        at += 1
        rooms.append([tuple(field[at + 6 * k:at + 6 * k + 6]) for k in range(count)])
        at += 6 * count
    return scene, width, height, side, mouse_x, mouse_y, cursor, rooms


def game(path, exe=None):
    e = emu.Emu(exe=exe)
    e.hook(0x408A60, emu.Emu.fatal, 0)
    e.w32(VIEW_X, 0)
    e.w32(VIEW_SHIFT, 0)
    e.w32(VIEW_Y, 8)                                              # FUN_0045afd0: +0x7a5208 - 8
    player, row = e.alloc(0x100), e.alloc(0x200)
    now = {}
    sizes = {}
    draws = []

    def size(e):                                                  # FUN_00502520(ECX text, EDX &w, [&h]): the slot's unit
        width, height = sizes[e.r32(e.mu.reg_read(emu.UC_X86_REG_ECX) - 0x14 + SLOT_UNIT)]
        e.w32(e.mu.reg_read(emu.UC_X86_REG_EDX), width)
        e.w32(e.arg(0), height)
        return 0

    def draw(e):                                                  # FUN_005023b0(ECX = slot + 0x14, ...)
        slot = e.mu.reg_read(emu.UC_X86_REG_ECX) - 0x14
        draws.append((e.r32(slot + SLOT_UNIT), [e.s32(slot + offset) for offset in (0, 4, 8, 12)], e.r32(slot + SLOT_MODE)))
        return 0

    e.hook(0x4F51D0, lambda e: 0, 0)
    e.hook(0x463DD0, lambda e: player, 0)
    e.hook(0x620BB0, lambda e: now["room"], 1)
    e.hook(0x467A10, lambda e: now["cursor"], 0)
    e.hook(0x468730, lambda e: now["mouse_x"], 0)
    e.hook(0x468740, lambda e: now["mouse_y"], 0)
    e.hook(0x4FB180, lambda e: 7, 3)
    e.hook(0x45AE90, lambda e: now["side"], 0)
    e.hook(0x4527D0, lambda e: 0, 1)
    e.hook(0x502520, size, 1)
    e.hook(0x467A70, lambda e: 0, 0)
    e.hook(0x466FE0, lambda e: 0, 0)
    e.hook(0x502280, lambda e: 0, 3)
    e.hook(0x627E70, lambda e: 2, 1)
    e.hook(0x6280A0, lambda e: 0, 4)
    e.hook(0x6335F0, lambda e: row, 1)
    e.hook(0x5023B0, draw, 4)
    base = e.brk
    for line in open(path):
        scene, width, height, side, mouse_x, mouse_y, cursor, rooms = parse(line)
        e.brk = base                                              # each screen's rooms and units afresh
        e.w32(WIDTH, width)
        e.w32(HEIGHT, height)
        room_ptrs = [e.alloc(0x100) for _ in rooms]
        near = e.alloc(4 * len(rooms))
        unit_index, cursor_ptr = {}, 0
        number = 0
        for room_ptr, units in zip(room_ptrs, rooms):
            nxt = 0
            ptrs = []
            for kind, drawn, x, y, text_w, text_h in units:
                unit, path_ptr = e.alloc(0x100), e.alloc(0x20)
                e.w32(unit, kind)
                e.w32(unit + 0x2C, path_ptr)
                e.w32(path_ptr + 4, x)
                e.w32(path_ptr + 8, y)
                e.w32(unit + 0xC8, 0x80 if drawn else 0)
                sizes[unit] = (text_w, text_h)
                unit_index[unit] = number
                if number == cursor: cursor_ptr = unit
                number += 1
                ptrs.append(unit)
            for unit in reversed(ptrs):                           # linked from the head: the case's first
                e.w32(unit + 0xE8, nxt)
                nxt = unit
            e.w32(room_ptr + 0x74, nxt)
        for k, room_ptr in enumerate(room_ptrs):
            e.w32(near + 4 * k, room_ptr)
        e.w32(room_ptrs[0], near)                                 # the player's room: +0 its near list, +0x24 its count
        e.w32(room_ptrs[0] + 0x24, len(rooms))
        now.update(room=room_ptrs[0], cursor=cursor_ptr, mouse_x=mouse_x & 0xFFFFFFFF, mouse_y=mouse_y & 0xFFFFFFFF, side=side)
        draws.clear()
        e.call(0x4C0810)
        for unit, rect, mode in draws:
            print(scene, unit_index[unit], *rect, int(mode == 5))
        print(scene, "end", len(draws))


def inc(cases_path, game_path):
    print("// Stock game.exe 1.14d, FUN_004c0810; generated by tools/emu/show_items_walk.py (gen 300 1, game, inc).")
    print("// { the case line, game.exe's labels: `unit left top right bottom lit;` each in draw order }")
    labels = {}
    for line in open(game_path):
        field = line.split()
        if field[1] != "end":
            labels.setdefault(field[0], []).append(" ".join(field[1:]) + ";")
    for line in open(cases_path):
        case = line.strip()
        print(f'    {{ "{case}", "{"".join(labels.get(case.split()[0], []))}" }},')


def main():
    args = sys.argv[1:]
    exe = None
    if "--exe" in args:
        at = args.index("--exe")
        exe = args[at + 1]
        del args[at:at + 2]
    if len(args) == 3 and args[0] == "gen":
        gen(int(args[1]), int(args[2]))
    elif len(args) == 2 and args[0] == "game":
        game(args[1], exe)
    elif len(args) == 3 and args[0] == "inc":
        inc(args[1], args[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
