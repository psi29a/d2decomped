"""Object-group placements per room for a map seed, from game.exe's tables.

    uv run python objgroups.py <map seed> [level]     # dead rogues, forest debris etc.

Reads Levels.txt ObjGrp0..7 / ObjPrb0..7 (bytes +0xe5..+0xf4 of the level
record) and objgroup.bin (0x34 records after a 4-byte header) from the
booted emu, then runs the FUN_00552610 algorithm in Python against each
room's room1 seed. Not a bit-exact emu run of 552610 itself (needs a full
game struct + objrgn.cpp init to call the real function); this is the
Python reference that the C++ port (rules::place_object_groups) matches.

Room seeds come from drlg.seeds_dump's walk of the room list once
tiles_dump has brought the rooms up.
"""
import struct
import sys

import drlg


LCG_MUL = 0x6ac690c5


def objgroup_rows(e):
    """objgroup.bin: u32 header (row count) then 0x34-byte rows."""
    data = e.mpq_file(r"data\global\excel\objgroup.bin")
    count = struct.unpack_from("<I", data, 0)[0]
    rows = []
    for i in range(count):
        offset = 4 + i * 0x34
        row = data[offset:offset + 0x34]
        ids     = list(struct.unpack_from("<8I", row, 0))
        density = list(row[0x20:0x28])
        weight  = list(row[0x28:0x30])
        rows.append({"id": ids, "density": density, "weight": weight})
    return rows


def level_columns(e, lid):
    """Levels.txt ObjGrp[i]/ObjPrb[i]: bytes +0xe5+i / +0xed+i of the compiled record."""
    base = e.r32(e.r32(0x744304) + 0xc58)
    record = base + lid * 0x220
    return (list(e.read(record + 0xe5, 8)), list(e.read(record + 0xed, 8)))


def picks_per_room(level_mon_group, level_mon_prob, groups, seed, room_index, rooms_total):
    """FUN_00552610 in Python: per slot i = 0..7, one seed step for the roll,
    plus one more when the roll passes ObjPrb and picks an entry. The throttle
    (FUN_00552560 / FUN_00552400) forces the roll to 100 past 75 % of the
    rooms; the seed still steps once per slot regardless."""
    throttle = rooms_total > 0 and room_index * 128 // rooms_total > 96
    state_lo, state_hi = seed & 0xffffffff, 666
    picks = []
    for i in range(8):
        product = state_lo * LCG_MUL + state_hi
        state_lo, state_hi = product & 0xffffffff, (product >> 32) & 0xffffffff
        roll = 100 if throttle else state_lo % 100
        group_id = level_mon_group[i]
        prob = level_mon_prob[i]
        if group_id == 0 or roll > prob or group_id >= len(groups):
            continue
        product = state_lo * LCG_MUL + state_hi
        state_lo, state_hi = product & 0xffffffff, (product >> 32) & 0xffffffff
        weight_roll = state_lo % 100
        group = groups[group_id]
        accumulated = 0
        for j in range(8):
            if group["id"][j] == 0:
                break
            accumulated += group["weight"][j]
            if weight_roll < accumulated:
                picks.append((i, group["id"][j], group["density"][j]))
                break
    return picks, state_lo


def dump(e, seed, lid):
    act = drlg.alloc_act(e, 0, seed, lid)
    lvl = drlg.find_level(e, act, lid)
    if not lvl:
        raise SystemExit(f"objgroups: level {lid} not built")
    room_ptrs = []
    r = e.r32(lvl + 0x10)
    while r:
        e.call(0x61b730, ecx=r)         # bring the room up (drlg.tiles_dump does this too)
        room_ptrs.append(r)
        r = e.r32(r + 0x24)
    x0, y0 = e.s32(lvl + 0x1c), e.s32(lvl + 0x20)
    rooms = sorted(room_ptrs, key=lambda p: (e.s32(p + 0x38), e.s32(p + 0x34)))
    groups = objgroup_rows(e)
    grp, prb = level_columns(e, lid)
    total = len(rooms)
    out = [f"level {lid} seed {seed} objgrp {grp} objprb {prb} rooms {total}"]
    for i, room_ptr in enumerate(rooms):
        room_seed = e.r32(e.r32(room_ptr + 0x30) + 0x6c)
        rx, ry = e.s32(room_ptr + 0x34) - x0, e.s32(room_ptr + 0x38) - y0
        picks, post = picks_per_room(grp, prb, groups, room_seed, i, total)
        picks_txt = " ".join(f"s{slot}=obj{obj_id}d{d}" for slot, obj_id, d in picks) or "-"
        out.append(f"room {rx},{ry} seed {room_seed:08x} post {post:08x} picks {picks_txt}")
    return "\n".join(out)


if __name__ == "__main__":
    lid = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    e = drlg.boot()
    print(dump(e, int(sys.argv[1], 0), lid))
