# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify SUNITEVENT_RemoveEventsByQueueAndOwner (UNIT/SUnitEvent.cpp FUN_005c0b50)
against stock game.exe 1.14d.

    uv run python unit_event_remove.py [--mutation-check]

ECX pGame, EDX pUnit, stack nQueueNo, nOwnerKey. Walks the unit's server-side
event list (unit +0x90, head to tail via +0x1c "pNext"; D2SUnitEventStrc,
32 bytes: +0x2 nFlags, +0x4 nQueueNo, +0x8 nOwnerKey, +0x18 pPrev, +0x1c
pNext). A node whose nQueueNo and nOwnerKey both match `nQueueNo`,
`nOwnerKey`:
  - not executing (nFlags bit 0 clear): unlinked (patching pPrev/pNext, or
    the unit's head) and freed through FreeServerMemory (hooked here, logged
    and not actually released - the emulator's heap never frees);
  - executing (bit 0 set): left linked, with bit 1 ("free pending") set.
A non-matching node is untouched. `pGame` is read only for its memory pool
pointer, passed straight to FreeServerMemory.

No MPQs are read. `--mutation-check` breaks the model three ways and
requires each to be caught.
"""
import argparse
import hashlib
import random
import struct
import sys
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FUNCTION = 0x5C0B50
FREE_SERVER_MEMORY = 0x40B480
UNIT_EVENTS_OFF = 0x90
NODE_SIZE = 32
FLAGS_OFF, QUEUE_OFF, OWNER_OFF, PREV_OFF, NEXT_OFF = 2, 4, 8, 24, 28
GAME_POOL_OFF = 28
MUTATIONS = ("keep-executing-matches", "ignore-owner", "free-non-matching")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Mismatch(Exception):
    """game.exe and the model disagree."""


def require(condition, message):
    """Raise Mismatch(message) unless condition holds."""
    if not condition:
        raise Mismatch(message)


def model(nodes, queue_no, owner_key):
    """(surviving nodes, freed node indices), nodes a list of {flags, queue, owner} in list order."""
    remaining, freed = [], []
    for i, n in enumerate(nodes):
        matched = (n["queue"] == queue_no and (n["owner"] == owner_key or mut("ignore-owner"))) or mut("free-non-matching")
        if not matched:
            remaining.append(n)
        elif (n["flags"] & 1) != 0 and not mut("keep-executing-matches"):
            remaining.append(dict(n, flags=n["flags"] | 2))   # executing: mark deferred, keep linked
        elif mut("keep-executing-matches"):      # a defect that never frees a matched node
            remaining.append(n)
        else:
            freed.append(i)                      # not executing: unlink and free
    return remaining, freed


def build_list(e, unit, nodes):
    """Write `nodes` (list of {flags, queue, owner}) as a doubly-linked list; returns node addresses."""
    addrs = [e.alloc(NODE_SIZE) for _ in nodes]
    for i, (addr, n) in enumerate(zip(addrs, nodes)):
        e.mu.mem_write(addr + FLAGS_OFF, struct.pack("<H", n["flags"] & 0xFFFF))
        e.w32(addr + QUEUE_OFF, n["queue"] & 0xFFFFFFFF)
        e.w32(addr + OWNER_OFF, n["owner"] & 0xFFFFFFFF)
        e.w32(addr + PREV_OFF, addrs[i - 1] if i > 0 else 0)
    for i, addr in enumerate(addrs):
        e.w32(addr + NEXT_OFF, addrs[i + 1] if i + 1 < len(addrs) else 0)
    e.w32(unit + UNIT_EVENTS_OFF, addrs[0] if addrs else 0)
    return addrs


def read_list(e, unit):
    """The event list as it stands now: [{flags, queue, owner}], head to tail."""
    out, addr, seen = [], e.r32(unit + UNIT_EVENTS_OFF), set()
    while addr:
        require(addr not in seen, "cyclic event list")
        seen.add(addr)
        flags = struct.unpack_from("<H", e.read(addr + FLAGS_OFF, 2))[0]
        out.append({"flags": flags, "queue": e.s32(addr + QUEUE_OFF), "owner": e.s32(addr + OWNER_OFF)})
        addr = e.r32(addr + NEXT_OFF)
    return out


def one_case(e, rng, freed):
    """Build one random event list, run FUN_005c0b50, compare the surviving list and the frees."""
    game, unit = e.alloc(64), e.alloc(0xA0)
    e.w32(game + GAME_POOL_OFF, e.alloc(4))
    queue_no, owner_key = rng.choice((0, 1, 2, 7)), rng.randrange(-1000, 1000)
    nodes = [{"flags": rng.choice((0, 1, 2, 3)),
              "queue": rng.choice((queue_no, queue_no, rng.choice((0, 1, 2, 7)))),
              "owner": rng.choice((owner_key, owner_key, rng.randrange(-1000, 1000)))}
             for _ in range(rng.randrange(0, 6))]
    node_addrs = build_list(e, unit, nodes)
    freed.clear()
    e.call(FUNCTION, queue_no, owner_key, ecx=game, edx=unit)

    want_remaining, want_freed_idx = model(nodes, queue_no, owner_key)
    got_remaining = read_list(e, unit)
    require(got_remaining == want_remaining, f"queue {queue_no} owner {owner_key} nodes {nodes}: list now {got_remaining} want {want_remaining}")
    want_freed_addrs = sorted(node_addrs[i] for i in want_freed_idx)
    require(sorted(freed) == want_freed_addrs, f"queue {queue_no} owner {owner_key} nodes {nodes}: freed {sorted(freed)} want {want_freed_addrs}")


def run_all(count=3000, seed=0x5C0B50):
    """Run count random cases through FUN_005c0b50 and the model, requiring agreement."""
    rng = random.Random(seed)
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256, "requires stock game.exe 1.14d (1.14.3.71)")
    e = emu.Emu(str(exe))
    freed = []
    e.hook(FREE_SERVER_MEMORY, lambda current: (freed.append(current.mu.reg_read(UC_X86_REG_EDX)), 1)[1], 3)
    for _ in range(count):
        one_case(e, rng, freed)
    return count


def main():
    """Run the cases, or --mutation-check each deliberate model defect."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate model defect to fail")
    options = parser.parse_args()
    global mutation
    if options.mutation_check:
        mutation = None
        try:
            run_all(count=500)
        except Mismatch as exc:
            sys.exit(f"baseline failed (not a caught mutation): {exc}")
        for name in MUTATIONS:
            mutation = name
            try:
                run_all(count=500)
            except Mismatch:
                print(f"caught: {name}")
                continue
            sys.exit(f"NOT caught: {name}")
        print(f"ok: {len(MUTATIONS)} deliberate model defects all caught")
        return
    print(f"ok: {run_all()} SUnitEvent remove-by-queue-and-owner cases")


if __name__ == "__main__":
    main()
