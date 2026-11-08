# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the objects region data helpers (OBJECTS\\objrgn.cpp, OBJECTS\\Objects.cpp) in stock game.exe 1.14d.

    uv run python objects_region.py [--mutation-check]

Runs game.exe's own code against a hand-written model on random game / region /
unit records laid out in a random-filled arena (_objects_world.py). Compared
after every call: the return value, the ordered log of the hooked callees
(registers and stack arguments), the fatal-error line when there is one, the
`ret n` pop count, EBX / ESI / EDI / EBP, and every byte of the arena.

Claimed (ECX = game unless a register is named):
  0x546e90  free the region data: the pending-waypoint list, the table's pointer
            array, eight more pointers, then the region itself; game NULL halts (line 0xb6)
  0x546fb0  game +0x10f0 region + 8 (the object seed)
  0x546ff0  one stack arg, a string: broadcasts packet 0x26 with it
  0x547080  logs the placement counters through 0x546ff0
  0x547170  EDX unit, one stack arg: pushes {arg, unit x, unit y, old head}
            on region +0x1110
  0x547210  ECX = {game, object, room}: InitFn 17 (waypoint) pending-list match
  0x54f520  allocates an 8-byte header and one 8-byte sentinel node
  0x54f580  EDX header: frees its chain and then the header

Callees are hooked and logged, not claimed: the allocator 0x40_B430 / 0x40_B480,
the position getters 0x64_88C0 / 0x64_8900, the broadcast 0x52_DED0, the town
test 0x61_AB00, the mode setter 0x62_4690, the objects record lookup
0x64_0E90 and the event queue 0x54_17D0. `--mutation-check` breaks the model
on purpose, one defect at a time, and requires each to fail. No MPQs are read.
"""
import argparse
import random
import sys

import _objects_world as ow
from _objects_world import M32, Mismatch, mix

FREE_REGION, SEED_PTR, BROADCAST, LOG_COUNTERS = 0x546E90, 0x546FB0, 0x546FF0, 0x547080
PUSH_PENDING, INIT_WAYPOINT, NEW_LIST, FREE_LIST = 0x547170, 0x547210, 0x54F520, 0x54F580
TABLE_PTR = 0x744304
FILE_RGN, FILE_OBJ, CALLBACK = 0x6E0C40, 0x6E10C8, 0x54_6FE0
MUTATIONS = ("line-swap", "rect-inclusive", "type3-static", "sentinel-swap", "counter-swap", "list-keep-first")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Rig:
    """The world, the hooks and the case builders for this oracle."""

    def __init__(self, real=()):
        """Hook the callees that are not claimed; `real` functions stay game.exe's own."""
        self.w = w = ow.World(real)
        self.tab = None
        w.rec(0x40_B480, "free", 3, ("ecx", "edx"), ret=1)
        w.rec(0x40_B430, "alloc", 3, ("ecx", "edx"), ret=lambda entry: w.queue.pop(0))
        w.rec(0x64_88C0, "px", 1)
        w.rec(0x64_8900, "py", 1)
        w.rec(0x61_AB00, "town", 1, ret=lambda entry: self.town)
        w.rec(0x62_4690, "mode", 2, ret=0)
        w.rec(0x64_0E90, "objrec", 1, ret=lambda entry: self.objrec)
        w.rec(0x54_17D0, "event", 4, ("ecx", "edx"), ret=0)
        w.m.hook(BROADCAST, self.chat, 1)
        w.m.hook(0x52_DED0, self.bcast, 1)
        self.town = 0
        self.objrec = 0
        self.fmts = [w.m.cstr(a) for a in (0x6E0CD0, 0x6E0C94, 0x6E0C58)]

    def chat(self, current):
        """0x546ff0 stand-in (ECX game, one stack arg: the string): log the game and the text."""
        from unicorn.x86_const import UC_X86_REG_ECX
        self.w.log.append(("chat", current.mu.reg_read(UC_X86_REG_ECX), current.cstr(current.arg(0)).encode("latin-1")))
        return 0

    def bcast(self, current):
        """Broadcast stand-in (EAX message, EDX callback, one stack arg: pointer to the message pointer)."""
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX
        msg = current.mu.reg_read(UC_X86_REG_EAX)
        body = current.read(msg, 0xB)
        defined = bytes([body[0], body[1]]) + body[3:0xB]
        self.w.log.append(("bcast", current.mu.reg_read(UC_X86_REG_EDX), current.r32(current.arg(0)) == msg,
                           defined, current.cstr(msg + 0x1A).encode("latin-1")))
        return 0

    def game(self, rng, with_region=True):
        """A game record (pool +0x1c, region +0x10f0, frame +0xa8) and its region; returns (game, region)."""
        a = self.w.arena
        g = a.new(0x1200)
        r = a.new(0x1200) if with_region else 0
        a.w32(g + 0x1C, rng.randrange(1 << 32))
        a.w32(g + 0x10F0, r)
        return g, r

    def go(self, label, address, *args, ecx=0, edx=0, regs=None, pops=None, build=None):
        """Sync, run the stock function, then compare with the model closure `build(clone)` -> (ret, log, halt)."""
        a = self.w.arena
        model = a.clone()
        want_ret, want_log, want_halt = build(model)
        self.w.sync()
        eax, halted = self.w.call(address, *args, ecx=ecx, edx=edx, regs=regs, pops=pops)
        self.w.check(label, eax, halted, model, want_log, want_ret, want_halt)


def chain(a, rng, n, next_offset, size=0x10):
    """n nodes linked through `next_offset`; returns (head, [nodes])."""
    nodes = [a.new(size) for _ in range(n)]
    for i, node in enumerate(nodes):
        a.w32(node + next_offset, nodes[i + 1] if i + 1 < n else 0)
    return (nodes[0] if nodes else 0), nodes


def check_seed_pointer(rig, rng):
    """0x546fb0: game +0x10f0 plus 8, with any value in the field."""
    for _ in range(20):
        rig.w.arena.reset(rng)
        g, _ = rig.game(rng)
        value = rng.choice((0, rng.randrange(1 << 32)))
        rig.w.arena.w32(g + 0x10F0, value)
        rig.go("seed-ptr", SEED_PTR, ecx=g, pops=0, build=lambda m, v=value: ((v + 8) & M32, [], None))


def check_free_region(rig, rng):
    """0x546e90: the pending list, counts-1 table pointers, eight pointers, then the region."""
    a = rig.w.arena
    for _ in range(60):
        a.reset(rng)
        g, r = rig.game(rng, with_region=rng.random() < 0.9)
        tab = a.new(0xD00)
        rig.w.m.w32(TABLE_PTR, tab)
        count = rng.choice((0, 1, 2, 3, 6, 9))
        a.w32(tab + 0xC5C, count)
        head, nodes = chain(a, rng, rng.choice((0, 1, 2, 4)), 0xC)
        if r:
            a.w32(r + 0x1110, head)
            for i in range(8):
                a.w32(r + 0x28 + 4 * i, rng.choice((0, rng.randrange(1, 1 << 32))))

        def build(m, g=g, r=r, nodes=nodes, count=count):
            """Model: frees in order; the pointer array is freed even where it holds 0."""
            pool, log = m.r32(g + 0x1C), []
            if not r:
                return None, log, None
            for node in nodes:
                log.append(("free", pool, node, FILE_RGN, 0xC1, 0))
            m.w32(r + 0x1110, 0)
            for i in range(1, count):
                log.append(("free", pool, m.r32(r + 0x4C + 4 * (i - 1)), FILE_RGN, 0xE8 if mut("line-swap") else 0xE2, 0))
            for i in range(8):
                p = m.r32(r + 0x28 + 4 * i)
                if p:
                    log.append(("free", pool, p, FILE_RGN, 0xE2 if mut("line-swap") else 0xE8, 0))
            log.append(("free", pool, r, FILE_RGN, 0xEC, 0))
            return None, log, None
        rig.go("free-region", FREE_REGION, ecx=g, pops=0, build=build)
    rig.go("free-region-null", FREE_REGION, ecx=0, pops=0, build=lambda m: (None, [], 0xB6))


def check_broadcast(rig, rng):
    """0x546ff0: packet 0x26 with the string copied behind a 0x1a byte header."""
    a = rig.w.arena
    for _ in range(60):
        a.reset(rng)
        g, _ = rig.game(rng)
        text = bytes(rng.randrange(1, 256) for _ in range(rng.choice((0, 1, 5, 40, 0x80, 0xF0))))
        s = a.new(0x120)
        a.put(s, text + b"\0")
        defined = bytes([0x26, 1]) + bytes([2]) + bytes(7)
        rig.go("broadcast", BROADCAST, s, ecx=g, pops=1,
               build=lambda m, t=text, d=defined: (None, [("bcast", CALLBACK, True, d, t)], None))


def check_counters(rig, rng):
    """0x547080: one line per non-zero group counter, then the two summary lines."""
    a = rig.w.arena
    for _ in range(40):
        a.reset(rng)
        g, r = rig.game(rng)
        for i in range(0x60):
            a.w16(r + 0x104A + 2 * i, rng.choice((0, 0, 1, 7, 300, 65535)))
        for offset in (0x110C, 0x110A, 0x1048):
            a.w16(r + offset, rng.choice((0, 3, 4000, 65535)))

        def build(m, g=g, r=r):
            """Model: Python formatting of the game's own format strings."""
            log = []
            for i in range(0x60):
                w = m.r16(r + 0x104A + 2 * i)
                if w:
                    log.append(("chat", g, (rig.fmts[0] % (i + 1, w)).encode()))
            log.append(("chat", g, (rig.fmts[1] % m.r16(r + 0x110C)).encode()))
            first, second = m.r16(r + 0x110A), m.r16(r + 0x1048)
            if mut("counter-swap"):
                first, second = second, first
            log.append(("chat", g, (rig.fmts[2] % (first, second)).encode()))
            return None, log, None
        rig.go("counters", LOG_COUNTERS, ecx=g, pops=0, build=build)


def check_push_pending(rig, rng):
    """0x547170: a {arg, x, y, next} record from the unit's position, pushed on region +0x1110."""
    a = rig.w.arena
    for _ in range(120):
        a.reset(rng)
        g, r = rig.game(rng)
        kind = rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 0xFFFFFFFF, 0x80000000))
        u, path = a.new(0x80), a.new(0x40)
        unit_type = kind
        a.w32(u, unit_type)
        static = unit_type in (2, 4, 5) and not mut("type3-static") or (mut("type3-static") and unit_type in (2, 3, 4, 5))
        has_path = static or rng.random() < 0.8
        a.w32(u + 0x2C, path if has_path else 0)
        node = a.new(0x10)
        rig.w.queue[:] = [node]
        arg = rng.randrange(1 << 32)
        pool = a.r32(g + 0x1C)

        def build(m, g=g, r=r, u=u, path=path, node=node, arg=arg, pool=pool, has_path=has_path, static=static):
            """Model: static paths hold x / y at +0xc / +0x10; the others answer through the getters."""
            log = [("alloc", pool, 0x10, FILE_RGN, 0x177, 0)]
            if static:
                x, y = m.r32(path + 0xC), m.r32(path + 0x10)
            elif has_path:
                x, y = mix("px", path), mix("py", path)
                log += [("px", path), ("py", path)]
            else:
                x = y = 0
            m.w32(node, arg)
            m.w32(node + 4, x)
            m.w32(node + 8, y)
            m.w32(node + 0xC, m.r32(r + 0x1110))
            m.w32(r + 0x1110, node)
            return None, log, None
        rig.go("push-pending", PUSH_PENDING, arg, ecx=g, edx=u, pops=1, build=build)


def check_init_waypoint(rig, rng):
    """0x547210: match a pending entry by room or by rect, light the object, else the town rule."""
    a = rig.w.arena
    for _ in range(400):
        a.reset(rng)
        g, r = rig.game(rng)
        room, other = a.new(0x80), a.new(0x80)
        for offset in (0x4C, 0x50):
            a.w32(room + offset, rng.randrange(-5, 5) & M32)
        for offset in (0x54, 0x58):
            a.w32(room + offset, rng.randrange(0, 5))
        entries = []
        for _ in range(rng.choice((0, 1, 2, 3))):
            e = a.new(0x10)
            a.w32(e, rng.choice((other, room, 0)))
            a.w32(e + 4, rng.randrange(-6, 12) & M32)
            a.w32(e + 8, rng.randrange(-6, 12) & M32)
            entries.append(e)
        for i, e in enumerate(entries):
            a.w32(e + 0xC, entries[i + 1] if i + 1 < len(entries) else 0)
        a.w32(r + 0x1110, entries[0] if entries else 0)
        room_arg = rng.choice((room, room, 0))
        obj = rng.choice((0, a.new(0x40)))
        if obj:
            a.w32(obj + 0x10, rng.choice((0, 0, 1, 2, 3)))
        rec = a.new(0x100)
        rig.objrec = rec
        rig.town = rng.choice((0, 1, 5))
        ctx = a.new(0x10)
        a.w32(ctx, g)
        a.w32(ctx + 4, obj)
        a.w32(ctx + 8, room_arg)
        a.w32(ctx + 0xC, rng.randrange(1 << 32))
        pool = a.r32(g + 0x1C)

        def build(m, entries=entries, room_arg=room_arg, obj=obj, ctx=ctx, g=g, r=r, rec=rec, pool=pool):
            """Model: first entry that names the room or lies inside its rect wins."""
            log = []
            for e in entries:
                hit = m.r32(e) == room_arg
                if not hit and room_arg:
                    x, y = m.s32(e + 4), m.s32(e + 8)
                    x0, y0 = m.s32(room_arg + 0x4C), m.s32(room_arg + 0x50)
                    xe = m.s32(room_arg + 0x54) + x0 + (1 if mut("rect-inclusive") else 0)
                    ye = m.s32(room_arg + 0x58) + y0
                    hit = x0 <= x < xe and y0 <= y < ye
                if hit:
                    if obj == 0 or m.r32(obj + 0x10) == 0:
                        log.append(("mode", obj, 1))
                        cls = 0xFFFFFFFF if obj == 0 else m.r32(obj + 4)
                        log.append(("objrec", cls))
                        when = (m.r32(rec + 0xDC) >> 8) + m.r32(g + 0xA8)
                        log.append(("event", g, obj, 1, when & M32, 0, 0))
                    log.append(("free", pool, e, FILE_RGN, 0x19B, 0))
                    m.w32(r + 0x1110, 0)
                    return None, log, None
            log.append(("town", room_arg))
            if rig.town:
                log.append(("mode", obj, 2))
            return None, log, None
        rig.go("init-waypoint", INIT_WAYPOINT, ecx=ctx, pops=0, build=build)


def check_list(rig, rng):
    """0x54f520 builds a header and a sentinel node; 0x54f580 frees a chain and its header."""
    a = rig.w.arena
    for _ in range(40):
        a.reset(rng)
        g, _ = rig.game(rng)
        header, node = a.new(8), a.new(8)
        rig.w.queue[:] = [header, node]
        pool = a.r32(g + 0x1C)

        def build(m, header=header, node=node, pool=pool):
            """Model: header {node, -1}, node {-1, 0}."""
            m.w32(header, node)
            m.w32(header + 4, M32 if not mut("sentinel-swap") else 0)
            m.w32(node, M32)
            m.w32(node + 4, 0 if not mut("sentinel-swap") else M32)
            return header, [("alloc", pool, 8, FILE_OBJ, 0x1B8, 0), ("alloc", pool, 8, FILE_OBJ, 0x1BC, 0)], None
        rig.go("new-list", NEW_LIST, ecx=g, pops=0, build=build)
        a.reset(rng)
        g, _ = rig.game(rng)
        header = a.new(8)
        head, nodes = chain(a, rng, rng.choice((0, 1, 3, 5)), 4, 8)
        a.w32(header, head)
        pool = a.r32(g + 0x1C)

        def build_free(m, header=header, nodes=nodes, pool=pool):
            """Model: nodes in order, then the header."""
            log = [("free", pool, n, FILE_OBJ, 0x1CE, 0) for n in (nodes[1:] if mut("list-keep-first") else nodes)]
            log.append(("free", pool, header, FILE_OBJ, 0x1D1, 0))
            return None, log, None
        rig.go("free-list", FREE_LIST, ecx=g, edx=header, pops=0, build=build_free)


def run_all(seed, rounds):
    """Build the rig and run every group; returns the case count."""
    rng = random.Random(seed)
    rig, plain = Rig(), Rig(real=(BROADCAST,))
    for _ in range(rounds):
        check_broadcast(plain, rng)
        check_seed_pointer(rig, rng)
        check_free_region(rig, rng)
        check_counters(rig, rng)
        check_push_pending(rig, rng)
        check_init_waypoint(rig, rng)
        check_list(rig, rng)
    return rig.w.cases + plain.w.cases


def main():
    """Run the oracle, or with --mutation-check require every model defect to be caught."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mutation-check", action="store_true", help="break the model on purpose and require failures")
    options = parser.parse_args()
    if options.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run_all(1, 1)
            except Mismatch:
                print(f"caught: {name}")
                continue
            sys.exit(f"mutation {name} was not caught")
        print(f"ok: {len(MUTATIONS)} model mutations caught")
        return
    print(f"ok: {run_all(1, 2)} object region data helper cases")


if __name__ == "__main__":
    main()
