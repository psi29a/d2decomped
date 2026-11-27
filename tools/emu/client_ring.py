# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the client's status-2 room bring-up (FUN_0061b920) in stock game.exe 1.14d.

Runs FUN_0061b920 on a made-up act: the status-2 list's own node at act
+0x278 (next +0x1c, prev +0xe8), its rooms (flags +0x28, room1 +0x30,
status +0x44), the cursor +0x460, the countdown +0x45c and the made count
+0x98, client bit +0x8c. FUN_0061b190 is hooked to record the room (ESI),
set its room1 and count +0x98 as it does. Each case is diffed against a
line-for-line copy of d2d's step_client_ring (components/game/gamedata/
gamedata.cpp).

Run: .venv/bin/python client_ring.py [--mutation-check] [cases] [seed]
"""
import argparse
import hashlib
import random
from pathlib import Path

import emu
from unicorn.x86_const import UC_X86_REG_ESI

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
STEP, MAKE_ROOM1 = 0x61B920, 0x61B190
SENTINEL = 0x278
mutation = None


class Mismatch(Exception):
    """Stock execution disagreed with d2d's step_client_ring."""


def require(condition, message):
    """Raise Mismatch with `message` unless `condition` holds."""
    if not condition:
        raise Mismatch(message)


def model(made_up, cursor, countdown, made):
    """d2d's step_client_ring on a list of has-room1 flags; returns (made order, cursor, countdown, made)."""
    order = []
    if made >= 2:
        return order, cursor, countdown, 0
    countdown = (countdown - 1) & 0xFF
    if countdown != 0:
        return order, cursor, countdown, made
    countdown = 4 if mutation == "four_frames" else 5
    if not made_up:
        return order, cursor, countdown, made if mutation == "empty_keeps" else 0
    start = cursor if cursor is not None and mutation != "cursor_head" else 0
    at = start
    while True:
        if not made_up[at]:
            made_up[at] = True
            order.append(at)
            made += 1
        at = (at + 1) % len(made_up)
        if at == start or made >= (2 if mutation == "walk_on" else 1):
            break
    return order, at, countdown, 0


def run_case(machine, rng):
    """One random act state through game.exe's FUN_0061b920 and the model; returns nothing, raises on a difference."""
    count = rng.randrange(0, 7)
    act = machine.alloc(0x500)
    rooms = [machine.alloc(0x100) for _ in range(count)]
    outsider = machine.alloc(0x100)                       # a room no longer status 2
    sentinel = act + SENTINEL
    ring = [sentinel] + rooms
    for index, node in enumerate(ring):
        machine.w32(node + 0x1C, ring[(index + 1) % len(ring)])
        machine.w32(node + 0xE8, ring[index - 1])
    made_up = [rng.random() < 0.4 for _ in rooms]
    for node, has in zip(rooms, made_up):
        machine.w32(node + 0x30, 1 if has else 0)
        machine.mu.mem_write(node + 0x44, b"\x02")
    machine.mu.mem_write(outsider + 0x44, b"\x03")
    machine.w32(act + 0x8C, 1)                            # the client's act (FUN_00642a00)
    countdown = rng.choice((1, 1, 2, 5))
    made = rng.choice((0, 0, 0, 1, 2, 3))
    machine.mu.mem_write(act + 0x45C, bytes((countdown,)))
    machine.mu.mem_write(act + 0x98, bytes((made,)))
    pick = rng.randrange(0, 4)
    if pick == 0 or not rooms:
        cursor_node, cursor = (0, None) if pick == 0 else (outsider, None)
    elif pick == 1:
        cursor_node, cursor = outsider, None
    else:
        index = rng.randrange(len(rooms))
        cursor_node, cursor = rooms[index], index
    machine.w32(act + 0x460, cursor_node)
    seen = []

    def make(current):
        """FUN_0061b190 hooked: record the room (ESI), give it a room1, count act +0x98."""
        node = current.mu.reg_read(UC_X86_REG_ESI)
        seen.append(rooms.index(node))
        current.w32(node + 0x30, 1)
        current.mu.mem_write(act + 0x98, bytes(((current.read(act + 0x98, 1)[0] + 1) & 0xFF,)))
        return 0

    machine.hook(MAKE_ROOM1, make, 0)
    machine.call(STEP, act)
    want_order, want_cursor, want_countdown, want_made = model(list(made_up), cursor, countdown, made)
    game_cursor = machine.r32(act + 0x460)
    if game_cursor == sentinel:
        game_cursor = 0                                   # its own node walks as the head
    elif game_cursor in rooms:
        game_cursor = rooms.index(game_cursor)
    elif game_cursor == outsider:
        game_cursor = None
    state = f"rooms {made_up} cursor {cursor} countdown {countdown} made {made}"
    require(seen == want_order, f"{state}: made {seen}, d2d {want_order}")
    require(machine.read(act + 0x45C, 1)[0] == want_countdown, f"{state}: countdown {machine.read(act + 0x45C, 1)[0]}, d2d {want_countdown}")
    require(machine.read(act + 0x98, 1)[0] == want_made, f"{state}: +0x98 {machine.read(act + 0x98, 1)[0]}, d2d {want_made}")
    if want_order or (countdown == 1 and made < 2 and rooms):
        require(game_cursor == want_cursor, f"{state}: cursor {game_cursor}, d2d {want_cursor}")


def check(exe, cases, seed):
    """Run `cases` random states; returns how many were compared."""
    machine = emu.Emu(exe=exe)
    rng = random.Random(seed)
    for _ in range(cases):
        run_case(machine, rng)
    return cases


def main():
    """Check FUN_0061b920 against the model, then (asked) that each planted defect is caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cases", nargs="?", type=int, default=3000)
    parser.add_argument("seed", nargs="?", type=int, default=1)
    parser.add_argument("--exe")
    parser.add_argument("--mutation-check", action="store_true")
    options = parser.parse_args()
    exe = options.exe or str(Path(emu.data_path()) / "bin" / "game.exe")
    require(hashlib.sha256(Path(exe).read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    print(f"ok: {check(exe, options.cases, options.seed)} FUN_0061b920 client ring cases")
    if options.mutation_check:
        global mutation
        for defect in ("four_frames", "empty_keeps", "cursor_head", "walk_on"):
            mutation = defect
            try:
                check(exe, options.cases, options.seed)
            except Mismatch:
                print(f"mutation {defect}: failed as required")
            else:
                raise RuntimeError(f"mutation {defect} was not detected")
        mutation = None


if __name__ == "__main__":
    main()
