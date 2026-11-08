# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the NPC talker list (MonsterAI.cpp) in stock game.exe 1.14d.

    uv run python monster_talk.py [--mutation-check]

A talking NPC keeps a chain of 12-byte nodes {+0 talker, +4 state, +8 next}
behind a head cell. The cell's address lives at monster data +0x30 (the data
is the unit's +0x14, used only when the unit type is 1). Runs game.exe's own
code against a hand-written model and compares the return value, the ordered
callee log (registers and stack arguments), the chain and cell bytes and the
bytes the `ret n` popped:

  FUN_00572da0 ECX cell: count the nodes whose talker is non-zero.
  FUN_00572de0 ECX cell, EDX talker: 1 if a node holds it, else 0.
  FUN_00572e00 ECX game, EDX talker, stack cell: unlink the first node with
      that talker and release it (MonsterAI.cpp line 0x229); null cell or no
      match does nothing.
  FUN_00572e60 EDX talker, stack (arg, cell): on a node whose state is 0 set
      state 1 and call 0x57_8e70(arg); other states and no match do nothing.
  FUN_00572ea0 / FUN_00572ee0 ECX unit, EDX talker: find the node through the
      unit's cell address; ea0 moves state 1 to 2 and returns the node (or 0),
      ee0 returns 1 when the state is 2. A unit that is not type 1, has no
      data, or whose cell address is 0 reads address 0 and faults.
  FUN_00573020 ECX game, EDX talker, stack (npc, cell, arg): moves them into
      the registers of FUN_00572c10 (EAX npc, EBX talker, stack game, cell,
      arg) and calls it.
  FUN_00572c10 EAX npc, EBX talker, stack (game, cell, arg), `ret 0xc`:
      returns 0 without adding when 0x55_4d00 is non-zero, npc is null, npc
      +0x10 is 0 or 0xc, 0x53_5060 returns exactly 1, or npc class 0x92 and
      0x59_4610 is non-zero; returns 1 when 0x45_7490(class, 9) is 0 or the
      talker is already in the chain; otherwise pushes a node (talker, state
      0) on the head and makes the registration calls (see the log below).
  FUN_00573180 ECX game, EDX unit, stack (callback, flag byte): for each node
      of the unit's chain makes the notification calls, optionally calls the
      callback with the talker in ECX, releases the node (line 0x36b), then
      zeroes the cell. Not type 1, no data or no cell address: returns at once.

Callees are hooked and logged, not claimed, and written with an underscore.
`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail. No MPQs are read.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import Fault, require

COUNT, CONTAINS, REMOVE, MARK, MARK_UNIT, IS_MARKED, ADD_WRAPPER, ADD, CLEAR = (
    0x572DA0, 0x572DE0, 0x572E00, 0x572E60, 0x572EA0, 0x572EE0, 0x573020, 0x572C10, 0x573180)
FILE_AI = 0x6E1560
MUTATIONS = ("count-counts-zero", "contains-always-1", "remove-last-match", "mark-state-any", "mark-unit-2-to-3",
             "marked-state-1", "add-allows-mode-12", "add-535060-nonzero", "add-dup-returns-0", "add-flag-inverted",
             "add-msg-type", "clear-skips-flag-test", "clear-keeps-cell")
mutation = None
S_TOKEN, A_TOKEN, T_TOKEN, W_TOKEN = 0x1111, 0x2222, 0x3333, 0x4444


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Chain:
    """A head cell and its nodes in emulator memory."""

    def __init__(self, world, rng, ids, states=(0, 1, 2, 3)):
        """Build a chain whose node talkers are `ids` and whose states are random."""
        machine = world.m
        self.world = world
        self.nodes = [machine.alloc(12) for _ in ids]
        for index, (node, talker) in enumerate(zip(self.nodes, ids)):
            nxt = self.nodes[index + 1] if index + 1 < len(ids) else 0
            world.put_words(node, [talker, rng.choice(states), nxt])
        self.cell = machine.alloc(4)
        machine.w32(self.cell, self.nodes[0] if self.nodes else 0)

    def snapshot(self):
        """The cell and every node as words."""
        return [self.world.read_words(self.cell, 1)] + [self.world.read_words(n, 3) for n in self.nodes]


def make_unit(world, cell_address, utype=1, with_data=True):
    """A unit record whose monster data holds the cell address at +0x30."""
    machine = world.m
    unit = machine.alloc(0x40)
    data = machine.alloc(0x80)
    machine.w32(data + 0x30, cell_address)
    machine.w32(unit, utype)
    machine.w32(unit + 0x14, data if with_data else 0)
    machine.w32(unit + 0xC, 0x777)
    return unit


def check_count_contains(world, rng):
    """FUN_00572da0 and FUN_00572de0."""
    ids = [rng.choice((0, 0, 5, 6, 7, 9)) for _ in range(rng.randrange(0, 6))]
    chain = Chain(world, rng, ids)
    before = chain.snapshot()
    want = sum(1 for talker in ids if talker or mut("count-counts-zero"))
    require(world.run(COUNT, ecx=chain.cell, nstack=0) == want, f"count {ids}")
    for talker in (0, 5, 6, 8, 9, 0x10000):
        got = world.run(CONTAINS, ecx=chain.cell, edx=talker, nstack=0)
        want = int(talker in ids or mut("contains-always-1"))
        require(got == want and world.log == [], f"contains {ids} {talker}: {got}")
    require(chain.snapshot() == before, "count/contains changed the chain")


def check_remove(world, rng):
    """FUN_00572e00."""
    machine = world.m
    ids = [rng.choice((5, 6, 7, 9)) for _ in range(rng.randrange(0, 5))]
    chain = Chain(world, rng, ids)
    game = machine.alloc(0x40)
    pool = rng.randrange(1, 1 << 30)
    machine.w32(game + 0x1C, pool)
    talker = rng.choice((5, 6, 7, 8, 9))
    null_cell = rng.random() < 0.1
    world.run(REMOVE, 0 if null_cell else chain.cell, ecx=game, edx=talker, nstack=1)
    want_log, want_nodes = [], list(ids)
    if not null_cell and talker in ids:
        index = len(ids) - 1 - ids[::-1].index(talker) if mut("remove-last-match") else ids.index(talker)
        want_log = [("free", pool, chain.nodes[index], world.file_of(FILE_AI), 0x229, 0)]
        want_nodes = ids[:index] + ids[index + 1:]
    require(world.log == want_log, f"remove {ids} {talker}: log {world.log} != {want_log}")
    head = machine.r32(chain.cell)
    seen = []
    while head and len(seen) < 8:
        seen.append(machine.r32(head))
        head = machine.r32(head + 8)
    require(seen == want_nodes, f"remove {ids} {talker}: chain {seen} != {want_nodes}")


def check_mark(world, rng):
    """FUN_00572e60: state 0 becomes 1 and calls the hooked 0x57_8e70."""
    ids = [rng.choice((5, 6, 7)) for _ in range(rng.randrange(0, 4))]
    chain = Chain(world, rng, ids)
    talker = rng.choice((5, 6, 7, 8))
    arg = rng.randrange(1 << 32)
    before = chain.snapshot()
    world.run(MARK, arg, chain.cell, edx=talker, nstack=2)
    want_log, want = [], [list(w) for w in before]
    if talker in ids:
        index = ids.index(talker)
        if before[1 + index][1] == 0 or mut("mark-state-any"):
            want[1 + index][1] = 1
            want_log = [("marked", arg)]
    require(world.log == want_log, f"mark {ids}: log {world.log} != {want_log}")
    require(chain.snapshot() == [list(w) for w in want], f"mark {ids} {talker}: chain differs")


def check_unit_marks(world, rng):
    """FUN_00572ea0 and FUN_00572ee0 through a unit; a missing cell address faults."""
    ids = [rng.choice((5, 6, 7)) for _ in range(rng.randrange(0, 4))]
    chain = Chain(world, rng, ids)
    talker = rng.choice((5, 6, 7, 8))
    bad = rng.choice((None, None, None, "type", "data", "cell"))
    unit = make_unit(world, 0 if bad == "cell" else chain.cell, utype=rng.choice((0, 2, 3)) if bad == "type" else 1,
                     with_data=bad != "data")
    before = chain.snapshot()
    for address in (MARK_UNIT, IS_MARKED):
        try:
            got = world.run(address, ecx=unit, edx=talker, nstack=0)
        except Fault:
            require(bad is not None, f"{address:#x} faulted on a good unit")
            continue
        require(bad is None, f"{address:#x} {bad}: did not fault")
        index = ids.index(talker) if talker in ids else None
        state = before[1 + index][1] if index is not None else None
        if address == MARK_UNIT:
            require(got == (chain.nodes[index] if index is not None else 0), f"mark-unit eax {got:#x}")
            if state == 1:
                before[1 + index][1] = 3 if mut("mark-unit-2-to-3") else 2
        else:
            want = int(state == (1 if mut("marked-state-1") else 2))
            require(got == want, f"is-marked {ids} {talker} state {state}: {got}")
        require(chain.snapshot() == [list(w) for w in before], f"{address:#x}: chain differs")
    if bad is None:
        # the state moved by the first call; refresh the model's view
        before[:] = chain.snapshot()


def add_hooks(world):
    """Hook every FUN_00572c10 callee; world.script holds the scripted results."""
    log = world.logger
    script = world.script
    log(0x55_4D00, 0, "ptr", ret=lambda c: script["r554d00"])
    log(0x53_5060, 0, "busy", ret=lambda c: script["r535060"], regs=("ecx",))
    log(0x45_7490, 0, "kind", ret=lambda c: script["r457490"])
    log(0x59_4610, 0, "sixteen", ret=lambda c: script["r594610"], regs=("ecx",))
    log(0x55_31C0, 1, "unit-of", ret=S_TOKEN)
    log(0x53_87C0, 0, "begin", ret=A_TOKEN, regs=("ecx",))
    log(0x57_6770, 2, "attach", ret=0)
    log(0x55_4120, 3, "notice", ret=0, regs=())
    log(0x66_1240, 1, "scratch", ret=T_TOKEN, regs=())
    log(0x54_3D10, 2, "pair", ret=0)
    log(0x66_1480, 2, "encode", ret=0, regs=())
    log(0x66_12A0, 1, "scratch-end", ret=0, regs=())
    log(0x54_4520, 0, "register", ret=0)
    log(0x54_3520, 0, "owner", ret=W_TOKEN)

    def msg(current):
        """0x53_c8d0: log ECX and the first six message bytes."""
        world.log.append(("msg", current.mu.reg_read(mw.REGISTERS["ecx"]),
                          bytes(current.read(current.mu.reg_read(mw.REGISTERS["edx"]), 6))))
        return 0

    def final(current):
        """0x53_d670: log ECX, DL and four stack arguments."""
        world.log.append(("announce", current.mu.reg_read(mw.REGISTERS["ecx"]),
                          current.mu.reg_read(mw.REGISTERS["edx"]) & 0xFF) + tuple(current.arg(i) for i in range(4)))
        return 0
    world.m.hook(0x53_C8D0, msg, 0)
    world.m.hook(0x53_D670, final, 4)
    world.m.hook(0x57_8E70, lambda current: world.log.append(("marked", current.arg(0))) or 0, 1)


def check_add(world, rng):
    """FUN_00572c10 on a random talker, NPC and chain."""
    machine = world.m
    game = machine.alloc(0x40)
    pool = rng.randrange(1, 1 << 30)
    machine.w32(game + 0x1C, pool)
    talker = rng.randrange(1, 1 << 30)
    present = rng.random() < 0.25
    others = [rng.randrange(1, 1 << 30) for _ in range(rng.randrange(0, 3))]
    ids = others + ([talker] if present else [])
    rng.shuffle(ids)
    chain = Chain(world, rng, ids)
    npc = machine.alloc(0x40)
    mode = rng.choice((0, 0xC, 0xD, rng.randrange(1, 20)))
    cls = rng.choice((0x92, 0x92, rng.randrange(0, 0x200)))
    npc_id = rng.randrange(1 << 32)
    world.put_words(npc, [rng.randrange(1 << 32), cls, 0, npc_id, mode])
    world.script.update(r554d00=rng.choice((0, 0, 0, 0, 1, 5)), r535060=rng.choice((0, 0, 1, 2, 3)),
                        r457490=rng.choice((0, 1, 1, 1, 7)), r594610=rng.choice((0, 0, 1)))
    use_npc = 0 if rng.random() < 0.06 else npc
    arg = rng.randrange(1 << 32)
    before = chain.snapshot()
    got = world.run(ADD, game, chain.cell, arg, regs={"eax": use_npc, "ebx": talker}, nstack=3)
    s = world.script
    log = [("ptr", game, talker)]
    expect_ret, add = 0, False
    blocked = mode == 0 or (mode == 0xC and not mut("add-allows-mode-12"))
    if s["r554d00"] or not use_npc or blocked:
        pass
    elif (s["r535060"] != 0 if mut("add-535060-nonzero") else s["r535060"] == 1):
        log.append(("busy", talker))
    else:
        log += [("busy", talker), ("kind", cls, 9)]
        if not s["r457490"]:
            expect_ret = 1
        else:
            skip = False
            if cls == 0x92:
                log.append(("sixteen", game))
                skip = bool(s["r594610"])
            if not skip:
                if talker in ids:
                    expect_ret = 0 if mut("add-dup-returns-0") else 1
                else:
                    add = True
    if add:
        node = None
        head = before[0][0]
        flag = int(head != 0) if mut("add-flag-inverted") else int(head == 0)
        log += [("unit-of", talker, FILE_AI, 0x1C4),
                ("alloc", pool, 12, world.file_of(FILE_AI), 0x1C7, 0),
                ("begin", S_TOKEN),
                ("attach", A_TOKEN, S_TOKEN, npc, flag),
                ("notice", talker, 1, npc_id),
                ("scratch", pool),
                ("pair", S_TOKEN, talker, npc, T_TOKEN),
                ("encode", T_TOKEN, None),
                ("msg", S_TOKEN, bytes([0x26 if mut("add-msg-type") else 0x27, struct.unpack("<I", machine.read(npc, 4))[0] & 0xFF])
                 + struct.pack("<I", npc_id)),
                ("scratch-end", T_TOKEN),
                ("register", game, S_TOKEN),
                ("owner", game, talker),
                ("announce", S_TOKEN, 0x28, 1, npc_id, W_TOKEN, arg)]
    # the encode call's second argument is a stack address; compare it loosely
    seen = [(entry[0], entry[1]) if entry[0] == "encode" else entry for entry in world.log]
    want = [(entry[0], entry[1]) if entry[0] == "encode" else entry for entry in log]
    # the alloc entry is the second one of the add path; the unit-of entry carries (ecx, edx, line)
    require(seen == want, f"add: log\n{world.log}\n!=\n{log}")
    require(got == expect_ret, f"add: eax {got} != {expect_ret}")
    if add:
        node = machine.r32(chain.cell)
        require(node not in chain.nodes and world.read_words(node, 3) == [talker, 0, before[0][0]],
                f"add: new node {world.read_words(node, 3)}")
        for index, snap in enumerate(before[1:]):
            require(world.read_words(chain.nodes[index], 3) == snap, "add: an old node changed")
    else:
        require(chain.snapshot() == before, "add: the chain changed")


def check_wrapper(world, rng):
    """FUN_00573020 hands its registers and stack to FUN_00572c10."""
    world.script["ret"] = rng.randrange(1 << 32)
    game, talker, npc, cell, arg = (rng.randrange(1, 1 << 30) for _ in range(5))
    got = world.run(ADD_WRAPPER, npc, cell, arg, ecx=game, edx=talker, nstack=3)
    require(world.log == [("add", npc, talker, game, cell, arg)], f"wrapper: log {world.log}")
    require(got == world.script["ret"], "wrapper: the result of FUN_00572c10 is not passed back")


def check_clear(world, rng):
    """FUN_00573180 on a unit chain, with and without a callback and the owner test."""
    machine = world.m
    game = machine.alloc(0x40)
    pool = rng.randrange(1, 1 << 30)
    machine.w32(game + 0x1C, pool)
    ids = [rng.randrange(1, 1 << 30) for _ in range(rng.randrange(0, 4))]
    chain = Chain(world, rng, ids)
    bad = rng.choice((None, None, None, "null", "type", "data", "cell"))
    unit = 0 if bad == "null" else make_unit(world, 0 if bad == "cell" else chain.cell,
                                            utype=2 if bad == "type" else 1, with_data=bad != "data")
    use_callback = rng.random() < 0.5
    callback = world.slot(lambda current: world.log.append(("callback", current.mu.reg_read(mw.REGISTERS["ecx"]))) or 0, 0) \
        if use_callback else 0
    flag = rng.choice((0, 1, 2, 0x100, 0x1FF))
    world.script.update(r4100=rng.choice((0, 1)), out4=rng.choice((0, 1, 1, 2)), out8=rng.choice((0x777, 0x778)))
    before = chain.snapshot()
    world.run(CLEAR, callback, flag, ecx=game, edx=unit, nstack=2)
    want = []
    if bad is None:
        flag_byte = flag & 0xFF
        for talker, node in zip(ids, chain.nodes):
            want += [("id", unit), ("unit-of", talker, world.file_of(FILE_AI), 0x35C), ("tell", S_TOKEN, 0x62, 1, 0x777, 0)]
            drop = True
            if flag_byte != 0 and not mut("clear-skips-flag-test"):
                want.append(("query", talker))
                drop = bool(world.script["r4100"]) and world.script["out4"] == 1
                if drop:
                    want.append(("id", unit))
                    drop = world.script["out8"] == 0x777
            if drop:
                want.append(("reset", talker))
            if use_callback:
                want.append(("callback", talker))
            want.append(("free", pool, node, world.file_of(FILE_AI), 0x36B, 0))
    seen = world.log
    require(seen == want, f"clear {bad} flag {flag}: log\n{seen}\n!=\n{want}")
    cell_now = machine.r32(chain.cell)
    if bad is None:
        require(cell_now == (before[0][0] if mut("clear-keeps-cell") else 0), "clear: the cell is not zeroed")
    else:
        require(chain.snapshot() == before, f"clear {bad}: memory changed")


def run_all():
    """Run every check on seeded random inputs; returns the case count."""
    rng = random.Random(0x572C10)
    plain = mw.World()
    plain.m.hook(0x57_8E70, lambda current: plain.log.append(("marked", current.arg(0))) or 0, 1)
    cases = 0
    for check, repeat in ((check_count_contains, 60), (check_remove, 80), (check_mark, 80), (check_unit_marks, 100)):
        for _ in range(repeat):
            check(plain, rng)
            cases += 1
    add_world = mw.World()
    add_hooks(add_world)
    for _ in range(600):
        check_add(add_world, rng)
        cases += 1
    wrapper = mw.World()
    wrapper.m.hook(ADD, lambda c: wrapper.log.append(("add", c.mu.reg_read(mw.REGISTERS["eax"]),
                                                       c.mu.reg_read(mw.REGISTERS["ebx"]), c.arg(0), c.arg(1), c.arg(2)))
                   or wrapper.script["ret"], 3)
    for _ in range(40):
        check_wrapper(wrapper, rng)
        cases += 1
    clear = mw.World()
    clear.m.hook(0x45_1F50, lambda c: clear.log.append(("id", c.mu.reg_read(mw.REGISTERS["ecx"]))) or 0x777, 0)
    clear.m.hook(0x55_31C0, lambda c: clear.log.append(("unit-of", c.mu.reg_read(mw.REGISTERS["ecx"]),
                                                         clear.file_of(c.mu.reg_read(mw.REGISTERS["edx"])), c.arg(0)))
                 or S_TOKEN, 1)
    clear.m.hook(0x53_D6D0, lambda c: clear.log.append(("tell", c.mu.reg_read(mw.REGISTERS["ecx"]),
                                                         c.mu.reg_read(mw.REGISTERS["edx"]) & 0xFF, c.arg(0), c.arg(1), c.arg(2)))
                 or 0, 3)

    def query(current):
        """0x55_4100: log the talker, write the two out cells, return the scripted result."""
        clear.log.append(("query", current.arg(0)))
        current.w32(current.arg(1), clear.script["out4"])
        current.w32(current.arg(2), clear.script["out8"])
        return clear.script["r4100"]
    clear.m.hook(0x55_4100, query, 3)
    clear.m.hook(0x55_4190, lambda c: clear.log.append(("reset", c.arg(0))) or 0, 1)
    for _ in range(300):
        check_clear(clear, rng)
        cases += 1
    return cases


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "NPC talker list cases")


if __name__ == "__main__":
    main()
