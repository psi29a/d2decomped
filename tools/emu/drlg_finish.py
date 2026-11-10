# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify ten more small DRLG functions in stock game.exe 1.14d against models and hooked callees.

    uv run python drlg_finish.py [--mutation-check]     (needs D2_MPQ_DIR; no MPQ is read)

The names below the FUN_ addresses (DRLGANIM_..., DRLGOUTPLACE_..., SubTypeWpShrine and the rest)
are Redivivus candidate names, not game.exe's; the FUN_ addresses are what the emulator ran.

* DRLGANIM_PropagateAnimationData (FUN_0066d750, ecx source room, edx destination): with a
  destination, the frame is that of the first animated tile list among the source's near
  rooms (room +0x2c count, +0x8 array; room +0x54 tiles, tiles +4 animations, node +8 frame,
  node +0x10 next), 0 when there is none or no source; every node of every animation list
  of the destination's near rooms gets it. No destination: nothing.
* DRLGOUTPLACE_CalcCornerBorderCoords (FUN_00675f80, eax level rect, ecx output rect, edx
  variant, stack corner 0..7; rects are x, y, w, h): eight corner cases place the output's
  x / y beside the level rect; variant 1 shifts by half the output size and 8. Other corners
  write nothing. Halves truncate toward zero.
* DRLGPLACE_ACT2_ValidatePlacementList2 (FUN_00676fc0, ecx placement list, edx index N): for each
  i < N except the entry's previous-node id (table 0x6f0a28, 16 bytes a row), the coordinates
  rect at list + 8 + 16 * N is tested against rect i by FUN_0066b860; the first 0 gives 0, else 1.
* DRLGOUTDOOR_ApplySubstitutionGroup (FUN_0066f990, ebx LvlSub row, stack outdoor state): no
  groups, nothing; else a previous border preset of -1 becomes 0x3e, a BordType of 0 draws
  FUN_0045c3e0(level seed + 0x1c4, count), and groups (rows of 0x18) from (draw + i) % count are
  tried by FUN_0066f690 until one places, when BordType is 0; otherwise all are tried.
* SubTypeWpShrine (FUN_006707a0, ecx wall layer grids): a lookup id of -1 does nothing; else
  FUN_0061fa90 finds the LvlSub row and, for each set bit of the mask at +0x38 (rows of 0x15c),
  FUN_006704e0 loads its file and FUN_00670170 (CheckAll 0) or FUN_0066ff50 (CheckAll set) runs.
* AllocateRoomEx (FUN_00670d70, eax source room, ebx direction, stack pick flag): a new maze
  room from FUN_0066b3e0, sized from its own level's LvlMaze row; FUN_00670880 replacing the
  source failing frees it and gives 0; else the room links to the source, the presets are
  picked when the flag is set, and the room joins the source's level.
* DRLGMAZE_AllocRoomAndPickPreset (FUN_00670e70, eax direction, edi flag, stack source): allocates
  with the flag unchanged, and on success picks the source's preset when the flag is set, then
  the new room's.
* DRLGMAZE_InitializeLevel (FUN_00673b10, ecx level): stores the LvlMaze row of the level id at
  level +0x14, then tail-calls FUN_00642d10 with the level's DRLG and the level.
* DRLGOUTROOM_SpawnPresetIfGridFlagged (FUN_0067db00, ecx wild data, edi x, esi y, ebx level,
  stack preset; esi is also pushed as the grid call's argument): grid flags (FUN_0067c570: ecx grid at +0x2c, edx x, stack y; the entry's push of esi is that argument) with bit 0x400 spawn the preset
  (FUN_006743c0) with direction +1 for level 0x6e, else -1, and give 1; else 0.

* DRLGPATH_SmoothPathPoints (FUN_00679b30, ecx path-type argument; the packed source at +0, the packed
  destination at +4, the step limit at +0x18, the dynamic path at +0x30; path +0x28 point count,
  +0x98 direction offset, +0x9c points): up to the limit it asks FUN_00678d70 for the direction
  triplet source to destination (each plus the offset, & 7), FUN_006793e0 for a direction not
  blocked, and steps the packed position by the 0x6f1798 offset table (16 bits each half). A
  direction other than the previous one stores the position it left; no direction, or a reversal
  of the previous one, stores the position and stops; a walk that ends having stored nothing
  last stores its final position. The count is stored and returned.

The callees are hooked and compared in order with their registers and arguments. FUN_00675f80
and FUN_00670e70 have no caller in game.exe; they are run directly with scripted callees.
`--mutation-check` breaks the models on purpose, one defect at a time, and requires each to fail.
"""
import random
import sys

import _monster_world as mw
from _monster_world import require

ANIM, CORNER, VALIDATE, SUBST, WPSHRINE = 0x66_D750, 0x67_5F80, 0x67_6FC0, 0x66_F990, 0x67_07A0
ALLOC, ALLOC_PICK, MAZE_INIT, SPAWN, SMOOTH = 0x67_0D70, 0x67_0E70, 0x67_3B10, 0x67_DB00, 0x67_9B30
DIRECTIONS = 0x6F_1798
PREV_TABLE = 0x6F_0A28
MASK = 0xFFFFFFFF
MUTATIONS = ("anim-default-frame", "anim-first-node-only", "anim-last-room", "anim-null-dst-written", "anim-chain-end-frame",
             "corner-variant-half", "corner-round-down", "corner-case-3-sign", "corner-extra-corner",
             "validate-skip-last", "validate-no-prev-skip", "validate-reverse-args", "validate-returns-count",
             "subst-no-patch", "subst-draw-always", "subst-stop-always", "subst-modulo-off", "subst-stride",
             "subst-patch-zero-count", "wps-zero-lookup-skipped", "wps-mask-low-byte", "wps-checkall-swapped",
             "wps-row-stride", "alloc-size-swap", "alloc-pick-always", "alloc-no-free", "alloc-level-new",
             "pick-flag-bool", "pick-source-always", "init-store-skipped", "init-level-id-off", "spawn-dir-inverted",
             "spawn-mask-wrong", "spawn-swap-xy", "spawn-level-id-low", "smooth-offset-not-masked", "smooth-no-reversal-stop",
             "smooth-store-new-position", "smooth-always-store-end", "smooth-skip-offset", "smooth-table-y-x",
             "smooth-count-not-stored", "smooth-limit-off-by-one", "smooth-store-every-step")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def new_rig():
    """A fresh emulated game: hooks are permanent per machine, so each function gets its own."""
    mw.INSTRUCTION_LIMIT = 100000
    return mw.World()


def reg(current, name):
    """A register of the stopped call."""
    return current.mu.reg_read(mw.REGISTERS[name])


def trunc_half(value):
    """C `value / 2` (truncation toward zero), the cdq / sub / sar sequence."""
    return -((-value) // 2) if value < 0 else value // 2


def make_room(rig, tiles_lists):
    """A room whose near-room array holds rooms with the given animation frame lists.

    tiles_lists[i] is None (room without tiles), [] (tiles without animation) or a list of
    frames, one chain node each. Returns (room, [node addresses per near room])."""
    near, nodes = [], []
    for frames in tiles_lists:
        room = rig.m.alloc(0x80)
        chain = []
        if frames is not None:
            tiles = rig.m.alloc(0x20)
            rig.m.w32(room + 0x54, tiles)
            previous = 0
            for frame in reversed(frames):
                node = rig.m.alloc(0x20)
                rig.m.w32(node + 8, frame)
                rig.m.w32(node + 0x10, previous)
                previous = node
                chain.insert(0, node)
            rig.m.w32(tiles + 4, previous)
        near.append(room)
        nodes.append(chain)
    owner = rig.m.alloc(0x80)
    array = rig.m.alloc(max(len(near), 1) * 4 + 4)
    rig.put_words(array, near or [0])
    rig.m.w32(owner + 8, array)
    rig.m.w32(owner + 0x2C, len(near))
    return owner, nodes


def random_lists(rng):
    """Random near-room tile layouts."""
    lists = []
    for _ in range(rng.choice((0, 1, 2, 3, 4))):
        kind = rng.random()
        if kind < 0.25:
            lists.append(None)
        elif kind < 0.4:
            lists.append([])
        else:
            lists.append([rng.randrange(1, 1 << 16) for _ in range(rng.choice((1, 2, 3)))])
    return lists


def check_anim(rig, rng):
    """FUN_0066d750: the first animated source neighbour's frame goes to every destination animation node."""
    src_lists, dst_lists = random_lists(rng), random_lists(rng)
    have_src, have_dst = rng.random() < 0.85, rng.random() < 0.9
    src, src_nodes = make_room(rig, src_lists)
    dst, dst_nodes = make_room(rig, dst_lists)
    garbage = rng.randrange(1 << 20, 1 << 24)
    for chain in dst_nodes:
        for node in chain:
            rig.m.w32(node + 8, garbage)
    frame = 1 if mut("anim-default-frame") else 0
    candidates = [chain for chain in src_nodes if chain]
    if have_src and candidates:
        chain = candidates[-1 if mut("anim-last-room") else 0]
        frame = rig.m.r32(chain[-1 if mut("anim-chain-end-frame") else 0] + 8)
    before_src = [rig.m.r32(n + 8) for chain in src_nodes for n in chain]
    rig.run(ANIM, ecx=src if have_src else 0, edx=dst if have_dst else 0, nstack=0)
    require(rig.log == [], f"anim made calls {rig.log}")
    for chain in dst_nodes:
        for position, node in enumerate(chain):
            written = (have_dst or mut("anim-null-dst-written")) and not (mut("anim-first-node-only") and position > 0)
            want = frame if written else garbage
            require(rig.m.r32(node + 8) == want, f"anim node frame {rig.m.r32(node + 8)} != {want}")
    require([rig.m.r32(n + 8) for chain in src_nodes for n in chain] == before_src, "anim changed the source's frames")


def corner_model(level, out, variant, corner):
    """DRLGOUTPLACE_CalcCornerBorderCoords as the code reads: the new x, y of the output rect."""
    lx, ly, lw, lh = level
    ox, oy, ow, oh = out
    half_w, half_h = trunc_half(ow), trunc_half(oh)
    if mut("corner-round-down"):
        half_w, half_h = ow // 2, oh // 2
    if mut("corner-variant-half"):
        half_w, half_h = ow, oh
    one = variant == 1
    if corner in (0, 4):
        ox, oy = lx, (ly + lh if corner == 0 else ly - oh)
        if one:
            ox += -8 - half_w
    elif corner in (1, 5):
        ox, oy = lx, (ly + lh if corner == 1 else ly - oh)
        if one:
            ox += half_w + 8
    elif corner == 2:
        ox, oy = lx - ow, ly
        if one:
            oy = ly - half_h - 8
    elif corner == 3:
        ox, oy = lx - ow, ly
        if one:
            oy = ly + half_h + (-8 if mut("corner-case-3-sign") else 8)
    elif corner == 6:
        ox, oy = lx + lw, ly
        if one:
            oy = ly - half_h - 8
    elif corner == 7 or (corner == 8 and mut("corner-extra-corner")):
        ox, oy = lx + lw, ly
        if one:
            oy = ly + half_h + 8
    return [ox, oy, ow, oh]


def check_corner(rig, rng):
    """FUN_00675f80: the eight corner placements, with and without the variant shift."""
    def value():
        """A coordinate or size, often odd and sometimes negative."""
        return rng.choice((rng.randrange(-300, 300), rng.randrange(-5, 5), rng.randrange(1, 90) * 2 + 1))
    level = [value() for _ in range(4)]
    out = [value() for _ in range(4)]
    variant = rng.choice((0, 1, 2, -1, 1))
    corner = rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 8, 9))
    lrect, orect = rig.m.alloc(0x20), rig.m.alloc(0x20)
    rig.put_words(lrect, level)
    untouched = [0x11111111, 0x22222222, out[2] & MASK, out[3] & MASK]
    rig.put_words(orect, untouched)
    rig.run(CORNER, corner, ecx=orect, edx=variant & MASK, regs={"eax": lrect})
    want = [w & MASK for w in corner_model(level, out, variant, corner)]
    if corner > 7 and not mut("corner-extra-corner"):
        want = untouched
    got = rig.read_words(orect, 4)
    require(got == want, f"corner {corner} variant {variant}: {got} != {want}")
    require(rig.read_words(lrect, 4) == [v & MASK for v in level], "corner changed the level rect")


def check_validate(rig, rng):
    """FUN_00676fc0: every earlier rect except the previous-node id must clear the distance test, in order."""
    count = rng.choice((0, 1, 2, 3, 5, 7, -1))
    prev = rng.choice((-1, 0, 1, 2, 3, 4, 6, 9))
    rig.m.w32(PREV_TABLE + 16 * count, prev & MASK)
    results = [rng.random() < 0.8 for _ in range(8)]
    base = rig.m.alloc(8 + 16 * 10)
    calls = []

    def verdict(current):
        """Scripted result of the coordinates test: record the rects it was given."""
        calls.append((reg(current, "ecx"), reg(current, "edx"), current.arg(0)))
        return int(results[len(calls) - 1])
    rig.m.hook(0x66_B860, verdict, 1)
    got = rig.run(VALIDATE, ecx=base, edx=count & MASK, nstack=0)
    want_calls, want = [], 1
    rows = range(count - 1) if mut("validate-skip-last") and count > 0 else range(max(count, 0))
    for i in rows:
        if i == prev and not mut("validate-no-prev-skip"):
            continue
        a, b = base + 8 + 16 * count, base + 8 + 16 * i
        want_calls.append((b, a, 0) if mut("validate-reverse-args") else (a, b, 0))
        if not results[len(want_calls) - 1]:
            want = 0
            break
    if want and mut("validate-returns-count"):
        want = max(count, 0) + 1
    require(calls == want_calls, f"validate calls {calls} != {want_calls}")
    require(got == want, f"validate returned {got} != {want}")


def check_subst(rig, rng):
    """FUN_0066f990: patch the previous border, draw the start group, try groups until one places."""
    groups = rng.choice((0, 1, 2, 3, 5, -3))
    bord = rng.choice((0, 0, 1, 2))
    prev = rng.choice((-1, 0, 7, 0x3E, -2))
    pfile, table = rig.m.alloc(0x80), rig.m.alloc(0x18 * 8 + 0x20)
    rig.m.w32(pfile + 0x4C, groups & MASK)
    rig.m.w32(pfile + 0x50, table)
    sub = rig.m.alloc(0x80)
    rig.m.w32(sub + 0x44, bord)
    rig.m.w32(sub + 0x50, pfile)
    level, outdoor = rig.m.alloc(0x200), rig.m.alloc(0x40)
    rig.m.w32(outdoor, level)
    rig.m.w32(outdoor + 0x14, prev & MASK)
    draw = rng.randrange(0, groups) if groups > 0 else rng.randrange(0, 5)
    draws, placed = [], []
    results = [rng.random() < 0.35 for _ in range(10)]

    def draw_hook(current):
        """The seeded draw: scripted."""
        draws.append((reg(current, "ecx"), reg(current, "edx")))
        return draw

    def place_hook(current):
        """The group placer: scripted per call."""
        placed.append((current.arg(0), current.arg(1), current.arg(2)))
        return int(results[len(placed) - 1])
    rig.m.hook(0x45_C3E0, draw_hook, 0)
    rig.m.hook(0x66_F690, place_hook, 3)
    rig.run(SUBST, outdoor, regs={"ebx": sub}, nstack=1)
    want_draws, want_placed = [], []
    want_prev = prev & MASK
    if groups == 0 and prev == -1 and mut("subst-patch-zero-count"):
        want_prev = 0x3E
    if groups != 0:
        if prev == -1 and not mut("subst-no-patch"):
            want_prev = 0x3E
        drawn = bord == 0 or mut("subst-draw-always")
        if drawn:
            want_draws.append((level + 0x1C4, groups & MASK))
        start = draw if drawn else 0
        stride = 0x18 if not mut("subst-stride") else 0x10
        modulo = groups if not mut("subst-modulo-off") else groups + 1
        for i in range(max(groups, 0)):
            want_placed.append((outdoor, sub, table + stride * ((start + i) % modulo)))
            if results[len(want_placed) - 1] and (bord == 0 or mut("subst-stop-always")):
                break
    require(draws == want_draws, f"subst draws {draws} != {want_draws}")
    require(placed == want_placed, f"subst placements {placed} != {want_placed}")
    require(rig.m.r32(outdoor + 0x14) == want_prev, f"subst previous border {rig.m.r32(outdoor + 0x14):#x} != {want_prev:#x}")


def check_wpshrine(rig, rng):
    """FUN_006707a0: each set mask bit loads its LvlSub row and runs the CheckAll or the plain placer."""
    lookup = rng.choice((-1, 0, 5, 0x7FFFFFFF, -2))
    mask = rng.choice((0, 1, 2, 0x80, 0xA5, 0x101, 0x80000000, 0x8000000F, MASK, rng.randrange(1 << 32)))
    grids, room, plevel, drlg = (rig.m.alloc(0x80), rig.m.alloc(0x80), rig.m.alloc(0x600), rig.m.alloc(0x600))
    pool = rng.randrange(1 << 20, 1 << 28)
    rows = rig.m.alloc(0x15C * 33)
    flags = [rng.choice((0, 0, 1, 0x20000)) for _ in range(33)]
    for i, flag in enumerate(flags):
        rig.m.w32(rows + 0x15C * i + 0x40, flag)
    rig.m.w32(grids, room)
    rig.m.w32(room + 0x58, plevel)
    rig.m.w32(plevel + 0x1B4, drlg)
    rig.m.w32(drlg + 0x464, pool)
    rig.m.w32(grids + 0x30, lookup & MASK)
    rig.m.w32(grids + 0x38, mask)
    log = []
    rig.m.hook(0x61_FA90, lambda c: (log.append(("find", reg(c, "ecx"))), rows)[1], 0)
    rig.m.hook(0x67_04E0, lambda c: (log.append(("load", reg(c, "ecx"), reg(c, "edx"))), 0)[1], 0)
    rig.m.hook(0x67_0170, lambda c: (log.append(("plain", c.arg(0), c.arg(1))), 0)[1], 2)
    rig.m.hook(0x66_FF50, lambda c: (log.append(("checkall", c.arg(0), c.arg(1))), 0)[1], 2)
    rig.run(WPSHRINE, ecx=grids, nstack=0)
    want = []
    skipped = lookup == -1 or (mut("wps-zero-lookup-skipped") and lookup == 0)
    if not skipped:
        want.append(("find", lookup & MASK))
        bits = mask & 0xFF if mut("wps-mask-low-byte") else mask
        stride = 0x15C if not mut("wps-row-stride") else 0x158
        for i in range(32):
            if bits >> i & 1:
                row = rows + stride * i
                want.append(("load", pool, row))
                use_all = (flags[i] != 0) != mut("wps-checkall-swapped")
                want.append(("checkall" if use_all else "plain", grids, row))
    require(log == want, f"wpshrine log {log} != {want}")


def check_alloc(rig, rng):
    """FUN_00670d70: size the new room from its level's LvlMaze row, replace, link, pick, add to the level."""
    flag = rng.choice((0, 1, 7))
    direction = rng.choice((0, 1, 2, 3, 5))
    replaced = rng.random() < 0.6
    source, source_level = rig.m.alloc(0x80), rig.m.alloc(0x80)
    rig.m.w32(source + 0x58, source_level)
    new, new_level, maze = rig.m.alloc(0x80), rig.m.alloc(0x80), rig.m.alloc(0x40)
    size_x, size_y = rng.randrange(1, 90), rng.randrange(91, 200)
    rig.m.w32(new + 0x58, new_level)
    rig.m.w32(new_level + 0x14, maze)
    rig.m.w32(maze + 0x10, size_x)
    rig.m.w32(maze + 0x14, size_y)
    log = []
    rig.m.hook(0x66_B3E0, lambda c: (log.append(("newroom", reg(c, "ecx"), reg(c, "edx"))), new)[1], 0)
    rig.m.hook(0x67_0880, lambda c: (log.append(("replace", reg(c, "eax"), c.arg(0))), int(replaced))[1], 1)
    rig.m.hook(0x66_C100, lambda c: (log.append(("free", reg(c, "ecx"))), 0)[1], 0)
    rig.m.hook(0x66_B5E0, lambda c: (log.append(("link", reg(c, "ecx"), reg(c, "edx"), c.arg(0))), 0)[1], 1)
    rig.m.hook(0x67_0C70, lambda c: (log.append(("pick", c.arg(0))), 0)[1], 1)
    rig.m.hook(0x66_B970, lambda c: (log.append(("add", reg(c, "ecx"), reg(c, "edx"))), 0)[1], 0)
    got = rig.run(ALLOC, flag, regs={"eax": source, "ebx": direction}, nstack=1)
    want = [("newroom", source_level, 2), ("replace", direction, new)]
    want_ret = 0
    if replaced:
        want.append(("link", source, new, direction))
        if flag or mut("alloc-pick-always"):
            want.append(("pick", new))
        want.append(("add", new_level if mut("alloc-level-new") else source_level, new))
        want_ret = new
    elif not mut("alloc-no-free"):
        want.append(("free", new))
    require(log == want, f"alloc log {log} != {want}")
    require(got == want_ret, f"alloc returned {got:#x} != {want_ret:#x}")
    want_size = (size_y, size_x) if mut("alloc-size-swap") else (size_x, size_y)
    require((rig.m.r32(new + 0x3C), rig.m.r32(new + 0x40)) == want_size, "alloc room size")


def check_alloc_pick(rig, rng):
    """FUN_00670e70: allocate with the flag as given; on success pick the source's (flag set) then the new room's preset."""
    flag = rng.choice((0, 1, 9))
    direction = rng.choice((0, 2, 6))
    source = rig.m.alloc(0x40)
    new = rng.choice((0, rig.m.alloc(0x40)))
    log = []
    rig.m.hook(0x67_0D70, lambda c: (log.append(("alloc", reg(c, "eax"), reg(c, "ebx"), c.arg(0))), new)[1], 1)
    rig.m.hook(0x67_09B0, lambda c: (log.append(("pick", reg(c, "ebx"), c.arg(0))), 0)[1], 1)
    got = rig.run(ALLOC_PICK, source, regs={"eax": direction, "edi": flag}, nstack=1)
    want = [("alloc", source, direction, int(bool(flag)) if mut("pick-flag-bool") else flag)]
    if new:
        if flag or mut("pick-source-always"):
            want.append(("pick", source, 1))
        want.append(("pick", new, 1))
    require(log == want, f"alloc-pick log {log} != {want}")
    require(got == new, f"alloc-pick returned {got:#x} != {new:#x}")


def check_init(rig, rng):
    """FUN_00673b10: store the level's LvlMaze row, then tail-call the size / alignment routine."""
    level, drlg = rig.m.alloc(0x200), rig.m.alloc(0x40)
    level_id = rng.randrange(1, 140)
    maze = rng.randrange(1 << 20, 1 << 28)
    rig.m.w32(level + 0x1D0, level_id)
    rig.m.w32(level + 0x1B4, drlg)
    rig.m.w32(level + 0x14, 0x5555AAAA)
    log = []
    rig.m.hook(0x61_F490, lambda c: (log.append(("find", reg(c, "ecx"))), maze)[1], 0)
    rig.m.hook(0x64_2D10, lambda c: (log.append(("size", reg(c, "ecx"), reg(c, "edx"))), 0)[1], 0)
    rig.run(MAZE_INIT, ecx=level, nstack=0)
    want = [("find", level_id + 1 if mut("init-level-id-off") else level_id), ("size", drlg, level)]
    require(log == want, f"init log {log} != {want}")
    stored = rig.m.r32(level + 0x14)
    require(stored == (0x5555AAAA if mut("init-store-skipped") else maze), f"init stored {stored:#x}")


def check_spawn(rig, rng):
    """FUN_0067db00: flag 0x400 spawns the preset with direction +1 for level 0x6e else -1."""
    flags = rng.choice((0x400, 0x3FF, 0xFFFFFBFF, 0x80000400, 0, 0x401, 0x800))
    level_id = rng.choice((0x6E, 0x6D, 0x16E, 0x6F, 0))
    wild, level = rig.m.alloc(0x80), rig.m.alloc(0x200)
    rig.m.w32(level + 0x1D0, level_id)
    x, y, preset = rng.randrange(1, 200), rng.randrange(201, 400), rng.randrange(1, 900)
    log = []
    rig.m.hook(0x67_C570, lambda c: (log.append(("flags", reg(c, "ecx"), reg(c, "edx"), c.arg(0))), flags)[1], 1)
    rig.m.hook(0x67_43C0, lambda c: (log.append(("spawn", reg(c, "ecx"), reg(c, "edx"), c.arg(0), c.arg(1), c.arg(2),
                                                 c.arg(3))), 0)[1], 4)
    got = rig.run(SPAWN, preset, ecx=wild, regs={"edi": x, "esi": y, "ebx": level}, nstack=1)
    want = [("flags", wild + 0x2C, x, y)]
    hit = bool(flags & (0x400 if not mut("spawn-mask-wrong") else 0x800))
    if hit:
        center = level_id == 0x6E if not mut("spawn-level-id-low") else (level_id & 0xFF) == 0x6E
        direction = (1 if center else -1) * (-1 if mut("spawn-dir-inverted") else 1)
        px, py = (y, x) if mut("spawn-swap-xy") else (x, y)
        want.append(("spawn", level, px, py, preset, direction & MASK, 0))
    require(log == want, f"spawn log {log} != {want}")
    require(got == int(hit), f"spawn returned {got} != {int(hit)}")


def pack(x, y):
    """A packed position: x in the low half, y in the high half."""
    return (x & 0xFFFF) | (y & 0xFFFF) << 16


def check_smooth(rig, rng):
    """FUN_00679b30: walk source to destination by non-blocked directions, storing a point at each turn."""
    start = pack(rng.randrange(0x7FF0, 0x8010), rng.randrange(0xFFF0, 0x10010))
    if rng.random() < 0.3:
        destination = start
    else:
        destination = pack(start & 0xFFFF, (start >> 16) + rng.randrange(-4, 5)) if rng.random() < 0.3 else \
            pack((start & 0xFFFF) + rng.randrange(-6, 7), (start >> 16) + rng.randrange(-6, 7))
    limit = rng.choice((-1, 0, 1, 2, 3, 5, 9, 12))
    offset = rng.choice((0, 1, 3, 5, 7, 8, 13, -1, -3))
    count0 = rng.choice((0, 0, 1, 4))
    arg, path = rig.m.alloc(0x40), rig.m.alloc(0x100 + 4 * 64)
    rig.put_words(arg, [start, destination])
    rig.m.w32(arg + 0x18, limit & MASK)
    rig.m.w32(arg + 0x30, path)
    rig.m.w32(path + 0x28, count0)
    rig.m.w32(path + 0x98, offset & MASK)
    for i in range(64):
        rig.m.w32(path + 0x9C + 4 * i, 0xDEAD0000 + i)
    triplets = [[rng.randrange(8) for _ in range(3)] for _ in range(16)]
    verdicts = [(rng.random() < 0.85, rng.randrange(8)) for _ in range(16)]
    log = []

    def triplet_hook(current):
        """The direction triplet: scripted values into the out slot."""
        log.append(("triplet", reg(current, "ecx") != 0, current.arg(0), current.arg(1)))
        rig.put_words(reg(current, "ecx"), triplets[sum(1 for e in log if e[0] == "triplet") - 1] + [0])
        return 0

    def find_hook(current):
        """The free-direction search: log the adjusted triplet it gets, answer from the script."""
        triple = tuple(rig.read_words(current.arg(1), 3))
        index = sum(1 for e in log if e[0] == "find")
        log.append(("find", reg(current, "eax"), current.arg(0), triple))
        found, direction = verdicts[index]
        rig.m.w32(current.arg(2), direction)
        return int(found)
    rig.m.hook(0x67_8D70, triplet_hook, 2)
    rig.m.hook(0x67_93E0, find_hook, 3)
    got = rig.run(SMOOTH, ecx=arg, nstack=0)
    table = rig.read_words(DIRECTIONS, 16)
    x, y = start & 0xFFFF, start >> 16
    count, previous, added, steps = count0, 0xFF, False, 0
    want_log, points = [], list(range(64))
    dst_x, dst_y = destination & 0xFFFF, destination >> 16
    done = False
    if limit > 0:
        while steps < limit + (1 if mut("smooth-limit-off-by-one") else 0):
            if (x, y) == (dst_x, dst_y):
                break
            added = False
            want_log.append(("triplet", True, pack(x, y), destination))
            triple = [t if mut("smooth-skip-offset") else (t + offset) for t in triplets[len(want_log) // 2]][:3]
            triple = [t & 7 if not mut("smooth-offset-not-masked") else t & 0xF for t in triple]
            index = sum(1 for e in want_log if e[0] == "find")
            want_log.append(("find", arg, pack(x, y), tuple(triple)))
            found, direction = verdicts[index]
            if not found or (not mut("smooth-no-reversal-stop") and ((direction - 4) & 7) == previous):
                done = True
                break
            dx = table[direction * 2] & 0xFFFF
            dy = table[direction * 2 + 1] & 0xFFFF
            if mut("smooth-table-y-x"):
                dx, dy = dy, dx
            before = pack(x, y)
            x, y = (x + dx) & 0xFFFF, (y + dy) & 0xFFFF
            if direction != previous or mut("smooth-store-every-step"):
                points[count] = before
                count += 1
                added = True
            steps += 1
            previous = direction
        if done:
            if steps != 0:
                points[count] = pack(x, y)
                count += 1
        elif (not added or mut("smooth-always-store-end")) and steps != 0:
            points[count] = pack(x, y)
            count += 1
    if mut("smooth-store-new-position") and count != count0:
        points[count0] = pack(x, y)
    require(got == count, f"smooth returned {got} != {count}")
    require(rig.m.r32(path + 0x28) == (count0 if mut("smooth-count-not-stored") else count), "smooth stored count")
    stored_points = [rig.m.r32(path + 0x9C + 4 * i) for i in range(64)]
    want_points = [0xDEAD0000 + i if points[i] == i else points[i] for i in range(64)]
    require(stored_points == want_points, f"smooth points {stored_points[:8]} != {want_points[:8]}")
    require(log == want_log, f"smooth calls {log} != {want_log}")


def run_all():
    """Run every check on random inputs; returns the case count."""
    rng = random.Random(0x66D750)
    plan = [(check_anim, 150), (check_corner, 300), (check_validate, 200), (check_subst, 300), (check_wpshrine, 200),
            (check_alloc, 150), (check_alloc_pick, 100), (check_init, 60), (check_spawn, 150), (check_smooth, 400)]
    cases = 0
    for check, rounds in plan:
        rig = new_rig()
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    return cases


def main():
    """Command line: run the oracle, or with --mutation-check require every defect to fail."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "more small DRLG function cases")


if __name__ == "__main__":
    main()
