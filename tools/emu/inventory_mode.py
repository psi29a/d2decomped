# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's InvMode.cpp item-event handlers and the socket move (FUN_0063b210) on fake units.

InvMode.cpp walks an inventory's changed-unit list and dispatches each unit to a per-kind handler. The handlers and
the unit lookups are hooked: each hook logs (address, stack arguments, the register arguments the disassembly shows
being set) and answers from a memo of random answers, so a function passes only when it makes the same decisions,
calls the same helpers with the same arguments in the same order, and leaves the arena the same. Runs for real: the
function under test, FUN_0063cbb0 / 0063cd40 / 0063cd20 / 0063cbd0 (the changed-unit list), FUN_0063a7e0.

Covers FUN_005973f0 (the per-unit dispatch: ESI unit, EDI player, EBX game), FUN_00597890 (dispatch the list, and the
sockets of each), FUN_005979b0 (clear a unit's flags and run two global lists), FUN_00597a20 (is anything still
pending), FUN_00597b00 (finish the list) and FUN_0063b210 (move an item into a socketed item's inventory).
The register arguments of the handlers (ECX = EBX, EDX = EDI) are the disassembly's; their bodies are not covered.

Usage: D2_MPQ_DIR=<MPQ folder> uv run python inventory_mode.py
"""
import random

from unicorn.x86_const import UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_ESI

from _inventory_world import cases, check
from inventory_queries import World, arena_equal, run

FILE = 0x6EA918
REGS = {"ecx": UC_X86_REG_ECX, "edx": UC_X86_REG_EDX, "esi": UC_X86_REG_ESI, "edi": UC_X86_REG_EDI, "ebx": UC_X86_REG_EBX}
HANDLERS = {0x53EC20: 2, 0x53D4B0: 2, 0x53ED50: 2, 0x53D010: 2, 0x53D090: 2, 0x53D0B0: 2, 0x53D0D0: 2, 0x53D0F0: 2,
            0x53EDB0: 2, 0x53EE10: 2, 0x53EE70: 2, 0x53EED0: 2, 0x53EEF0: 2, 0x53EF10: 1, 0x53D110: 2, 0x53D350: 2,
            0x53D2C0: 2, 0x53D420: 2, 0x53D440: 3, 0x53EF30: 2}
# address -> (stack arg count, register arguments logged, generator of the answer)
HOOKS = {
    0x537860: (2, (), lambda: random.choice((1, 2, 3))),
    0x5387C0: (0, ("ecx",), lambda: random.choice((10, 11))),
    0x554D00: (0, ("ecx", "edx"), lambda: random.choice((1, 2, 3))),
    0x579360: (3, ("ecx", "edx"), lambda: int(random.random() < 0.6)),
    0x628140: (2, (), lambda: int(random.random() < 0.25)),
    0x6280A0: (4, (), lambda: int(random.random() < 0.4)),
    0x63EE90: (1, (), lambda: int(random.random() < 0.5)),
    0x55DF00: (2, ("ecx", "edx"), lambda: 0),
    0x55F4F0: (1, ("edx",), lambda: 0),
    0x621000: (2, (), lambda: 0),
    0x627D70: (2, (), lambda: 0),
    0x628170: (3, (), lambda: 0),
    0x6280D0: (3, (), lambda: 0),
    0x557FD0: (0, ("ecx", "edx"), lambda: 0),
    0x5979B0: (0, ("edx",), lambda: 0),
    0x5973F0: (0, ("ebx", "esi", "edi"), lambda: 0),
    0x620470: (2, (), lambda: 0),
    0x6204C0: (2, (), lambda: 0),
    0x629ED0: (2, (), lambda: 0),
    0x63AAF0: (0, ("ecx",), lambda: 0),
    0x63AF20: (0, ("ecx", "esi"), lambda: 0),
}


class ModeWorld(World):
    """The shared fixture plus logged hooks for the InvMode callees; `real` stay game.exe's."""

    def __init__(self, real=()):
        super().__init__(real=real)
        self.cand = ()
        for address, (stack, regs, _gen) in HOOKS.items():
            self.e.hook(address, lambda x, a=address, s=stack, r=regs: self._call(x, a, s, r), stack)
        for address, stack in HANDLERS.items():
            self.e.hook(address, lambda x, a=address, s=stack: self._call(x, a, s, ("ecx", "edx"), 0), stack)
        self.e.hook(0x552F60, self._unit_of, 1)

    def _call(self, e, address, stack, regs, forced=None):
        key = (address,) + tuple(e.arg(i) for i in range(stack)) + tuple(e.mu.reg_read(REGS[r]) for r in regs)
        return self.record(key, forced)

    def record(self, key, forced=None):
        """Log a hooked call and give its memoised answer; the model calls this too."""
        self.log.append(key)
        if key not in self.memo:
            self.memo[key] = forced if forced is not None else HOOKS[key[0]][2]()
        return self.memo[key]

    def _unit_of(self, e):
        """FUN_00552f60 (id, with ECX the game and EDX 4): the unit for an id, one of the case's candidates or none."""
        key = (0x552F60, e.arg(0), e.mu.reg_read(UC_X86_REG_ECX), e.mu.reg_read(UC_X86_REG_EDX))
        self.log.append(key)
        if key not in self.memo:
            self.memo[key] = random.choice((0,) + tuple(self.cand))
        return self.memo[key]

    def unit_of(self, unit_id, game):
        """The model's FUN_00552f60."""
        return self.record_unit((0x552F60, unit_id, game, 4))

    def record_unit(self, key):
        """Log and answer like _unit_of."""
        self.log.append(key)
        if key not in self.memo:
            self.memo[key] = random.choice((0,) + tuple(self.cand))
        return self.memo[key]


def call(w, address, *stack, **regs):
    """The model's version of a hooked helper call: stack arguments, then the register arguments in HOOKS order."""
    names = HOOKS[address][1] if address in HOOKS else ("ecx", "edx")
    forced = 0 if address in HANDLERS else None
    return w.record((address,) + stack + tuple(regs[name] for name in names), forced)


FLAGS = 0xC8


def m_5973f0(w, m, ebx, esi, edi):
    """FUN_005973f0: choose the handler for the unit in ESI by its kind flags (the decompile, in its order)."""
    first = call(w, 0x537860, ebx, 0)
    second = call(w, 0x5387C0, ecx=ebx)
    third = call(w, 0x554D00, ecx=second, edx=first)
    if esi:
        flags = m.r32(esi + FLAGS)
        for bit, flag in ((2, 0), (4, 1)):
            if (flags >> bit) & 1:
                if third != edi:
                    return 1
                if call(w, 0x579360, third, esi, flag, ecx=second, edx=first) == 0:
                    return 1
                call(w, 0x53EF30, esi, 0x38 + flag, ecx=ebx, edx=edi)
                return 1
    f = lambda mask: call(w, 0x628140, esi, mask)
    h = lambda address, *rest: call(w, address, esi, *rest, ecx=ebx, edx=edi)
    if f(0x40) and edi == first:
        h(0x53EC20, 0)
        return 1
    if f(0x80000) and edi == first:
        h(0x53D4B0, 0)
        return 1
    if (f(2) or f(0x80)) and edi == first:
        h(0x53ED50, 0)
        return 1
    if f(4) and edi == first:
        h(0x53D010, 0)
        return 1
    if f(8) or f(0x200):
        h(0x53D090, 0)
        return 1
    if f(0x10000):
        h(0x53D0B0, 0)
        return 1
    if f(0x10):
        h(0x53D0D0, 0)
        return 1
    if f(0x20):
        h(0x53D0F0, 0)
        return 1
    if f(0x100) and edi == first:
        h(0x53EDB0, 0)
        return 1
    if f(0x40000) and edi == first:
        h(0x53EE10, 0)
        return 1
    if (f(0x400) or f(0x2000)) and edi == first:
        h(0x53EE70, 0)
        return 1
    if f(0x800) and edi == first:
        h(0x53EED0, 0)
        return 1
    if f(0x1000) and edi == first:
        h(0x53EEF0, 0)
        return 1
    if f(0x100000) and edi == first:
        h(0x53EF10)
        return 1
    if f(0x200000):
        h(0x53D110, 0)
        return 1
    if f(0x4000):
        h(0x53D2C0, 0)
        return 1
    if f(0x8000):
        h(0x53D350, 0)
        return 1
    g = lambda mask, line: call(w, 0x6280A0, esi, mask, line, FILE_MODE)
    mode = m.r32(esi + 0x10) if esi else 0
    if g(0x100, 0xB7) == 0:
        if g(0x200, 0xC5) == 0:
            if g(1, 0xD3) != 0:
                if (edi == first or (mode != 0 and mode != 2)) and mode in (0, 1, 2):
                    if edi == first and g(0x40000, 0xE3) != 0:
                        return 0
                    h(0x53D420, 0)
                    return 1
        elif edi == first or (esi and mode == 1):
            value = g(0x200, 0xCF)
            h(0x53D440, 0x200, value)
            return 1
    elif edi == first or (esi and mode == 1):
        value = g(0x100, 0xC1)
        h(0x53D440, 0x100, value)
        return 1
    return 0


FILE_MODE = 0


def setup_5973f0(w):
    """A random unit with random flag bits in ESI, plus EBX / EDI."""
    a, e = w.arena, w.e
    unit = a.new(0x100) if random.random() < 0.95 else 0
    if unit:
        flags = 0
        for bit in random.sample(range(0, 8), random.randint(0, 2)):
            flags |= 1 << bit
        e.w32(unit + FLAGS, flags)
        e.w32(unit + 0x10, random.choice((0, 1, 2, 3)))
    return unit, random.choice((1, 2, 3)), random.getrandbits(32) | 1


def test_dispatch(w):
    """FUN_005973f0 over random flag words and answers."""
    global FILE_MODE
    e, a = w.e, w.arena
    reached = set()
    for case in range(6000):
        a.reset()
        w.log.clear()
        w.memo.clear()
        unit, edi, ebx = setup_5973f0(w)
        edi = random.choice((1, 2, 3))
        result, before, after, game_log = run(w, 0x5973F0, regs={"ebx": ebx, "esi": unit, "edi": edi})
        FILE_MODE = game_log_file(game_log) or FILE_MODE
        w.log.clear()
        expect = m_5973f0(w, before, ebx, unit, edi)
        check(result & 0xFFFFFFFF, expect, f"5973f0 result {case}")
        arena_equal(before, after, f"5973f0 arena {case}")
        check(game_log, list(w.log), f"5973f0 calls {case}")
        reached.add(tuple(entry[0] for entry in game_log[3:]))
    if len(reached) < 25:
        raise AssertionError(f"the dispatch fixture reached only {len(reached)} distinct call chains")


def game_log_file(log):
    """The file-name pointer the game passes to FUN_006280a0 (the last argument), 0 when it never calls it."""
    for entry in log:
        if entry[0] == 0x6280A0:
            return entry[4]
    return 0


def mk_unit(w, flags, ids):
    """A fake unit with an inventory whose changed-unit list holds `ids`; returns (unit, inventory)."""
    a, e = w.arena, w.e
    unit, inv = a.new(0x100), a.inventory()
    e.w32(unit + 0x60, inv)
    e.w32(unit + FLAGS, flags)
    head = tail = 0
    for unit_id in ids:
        node = a.new(8)
        e.w32(node, unit_id)
        if tail:
            e.w32(tail + 4, node)
        else:
            head = node
        tail = node
    e.w32(inv + 0x2C, head)
    e.w32(inv + 0x30, tail)
    return unit, inv


def fixture(w, flag_choices=(0, 1, 0x400000, 0x800000, 5, 0x15)):
    """A unit with a changed list whose ids map (through the memo) to candidate units that have lists of their own."""
    cands = []
    for _ in range(4):
        cands.append(mk_unit(w, random.choice(flag_choices), random.sample(range(100, 140), random.randint(0, 3)))[0])
    w.cand = tuple(cands)
    ids = random.sample(range(1, 90), random.randint(0, 4))
    unit, inv = mk_unit(w, random.choice(flag_choices), ids)
    return unit, inv, ids


def list_nodes(m, inv):
    """The changed list's node addresses and their ids, from a memory copy."""
    nodes, node = [], m.r32(inv + 0x2C) if m.r32(inv) == 0x1020304 else 0
    while node:
        nodes.append((node, m.r32(node)))
        node = m.r32(node + 4)
    return nodes


def m_clear_list(w, m, inv):
    """FUN_0063cbd0 for real: free every node (line 0xcbb), clear the head and tail."""
    if m.r32(inv) != 0x1020304:
        return
    for node, _unit_id in list_nodes(m, inv):
        w.log.append(("free", node, 0xCBB))
    m.w32(inv + 0x2C, 0)
    m.w32(inv + 0x30, 0)


def m_pending(w, m, game, unit):
    """FUN_00597a20 (ECX game, EDX unit)."""
    if unit and ((m.r32(unit + FLAGS) >> 0x16) & 1 or (m.r32(unit + FLAGS) >> 0x17) & 1):
        return 1
    for _node, unit_id in list_nodes(m, m.r32(unit + 0x60)):
        if w.unit_of(unit_id, game):
            return 1
    return 0


def m_finish(w, m, game, unit):
    """FUN_00597b00 (ECX game, EDX unit)."""
    inv = m.r32(unit + 0x60)
    if not inv:
        return
    call(w, 0x621000, unit, 0)
    for _node, unit_id in list_nodes(m, inv):
        u = w.unit_of(unit_id, game)
        if not u:
            continue
        if call(w, 0x628140, u, 0x10) or call(w, 0x628140, u, 0x4000):
            call(w, 0x627D70, u, 0)
        if call(w, 0x628140, u, 0x20) and call(w, 0x6280A0, u, 0x80, 0x180, FILE_MODE):
            call(w, 0x627D70, u, 0)
        call(w, 0x5979B0, edx=u)
        if m.r32(u + FLAGS) & 1:
            call(w, 0x621000, u, 0)
            inner = m.r32(u + 0x60)
            for _n, child_id in list_nodes(m, inner):
                child = w.unit_of(child_id, game)
                if child:
                    call(w, 0x5979B0, edx=child)
            m_clear_list(w, m, inner)
        if call(w, 0x628140, u, 1):
            call(w, 0x557FD0, ecx=game, edx=u)
    m_clear_list(w, m, inv)


def m_dispatch_list(w, m, game, unit, p3, p4):
    """FUN_00597890 (ECX game, EDX unit, stack p3 = EBX of the dispatch, p4 = an inventory to walk instead)."""
    inv = p4 or m.r32(unit + 0x60)
    nodes = list_nodes(m, inv)
    if not nodes:
        return
    for _node, unit_id in nodes:
        u = w.unit_of(unit_id, game)
        if u:
            call(w, 0x5973F0, ebx=p3, esi=u, edi=unit)
            if m.r32(u + FLAGS) & 1:
                for _n, child_id in list_nodes(m, m.r32(u + 0x60)):
                    if w.unit_of(child_id, game):
                        call(w, 0x5973F0, ebx=p3, esi=u, edi=unit)   # ESI is still the outer unit (not reloaded)
    if call(w, 0x63EE90, unit):
        call(w, 0x55DF00, 0, 1, ecx=game, edx=unit)
        call(w, 0x55F4F0, 1, edx=unit)


def test_list_functions(w):
    """FUN_00597a20, 00597b00 and 00597890 on fake units with changed lists."""
    e, a = w.e, w.arena
    for case in range(800):
        a.reset()
        w.log.clear()
        w.memo.clear()
        unit, inv, ids = fixture(w)
        game = random.getrandbits(32) | 1
        # FUN_00597a20
        result, before, after, game_log = run(w, 0x597A20, ecx=game, edx=unit)
        w.log.clear()
        expect = m_pending(w, before, game, unit)
        check(result, expect, f"597a20 result {case}")
        arena_equal(before, after, "597a20 arena")
        check(game_log, list(w.log), "597a20 calls")
        # FUN_00597890
        p3, p4 = random.getrandbits(32) | 1, random.choice((0, 0, w.cand and e.r32(w.cand[0] + 0x60) or 0))
        result, before, after, game_log = run(w, 0x597890, p3, p4, ecx=game, edx=unit)
        w.log.clear()
        m_dispatch_list(w, before, game, unit, p3, p4)
        arena_equal(before, after, "597890 arena")
        check(game_log, list(w.log), "597890 calls")
        # FUN_00597b00
        result, before, after, game_log = run(w, 0x597B00, ecx=game, edx=unit)
        w.log.clear()
        m_finish(w, before, game, unit)
        arena_equal(before, after, "597b00 arena")
        check(game_log, list(w.log), "597b00 calls")


def test_clear_flags(w):
    """FUN_005979b0 (EDX unit): clear flag bits 0x14, then run the two global lists through FUN_00628170 / FUN_006280d0."""
    e, a = w.e, w.arena
    for case in range(200):
        a.reset()
        w.log.clear()
        w.memo.clear()
        unit = a.new(0x100) if random.random() < 0.9 else 0
        if unit:
            e.w32(unit + FLAGS, random.getrandbits(32))
        first = [random.getrandbits(16) for _ in range(random.randint(0, 6))]
        second = [random.getrandbits(16) for _ in range(random.randint(0, 6))]
        e.w32(0x738CC4, len(first))
        e.w32(0x738C6C, len(second))
        for n, value in enumerate(first):
            e.w32(0x738C70 + 4 * n, value)
        for n, value in enumerate(second):
            e.w32(0x738C4C + 4 * n, value)
        result, before, after, game_log = run(w, 0x5979B0, edx=unit)
        w.log.clear()
        if unit:
            before.w32(unit + FLAGS, before.r32(unit + FLAGS) & 0xFFFFFFEB)
        for value in first:
            call(w, 0x628170, unit, value, 0)
        for value in second:
            call(w, 0x6280D0, unit, value, 0)
        arena_equal(before, after, f"5979b0 arena {case}")
        check(game_log, list(w.log), "5979b0 calls")


def test_socket_move(w):
    """FUN_0063b210 (inventory, item, unused): move an item into the inventory of an item that owns it."""
    e, a = w.e, w.arena
    outcomes = set()
    for case in range(600):
        a.reset()
        w.log.clear()
        w.memo.clear()
        inv = a.inventory()
        owner = a.item(kind=random.choice((4, 4, 4, 0, 1)))
        e.w32(inv + 8, owner if random.random() < 0.9 else 0)
        e.w32(inv + 0x28, random.randint(0, 5))
        item = a.item(kind=random.choice((4, 4, 4, 3)))
        if random.random() < 0.15:
            e.w32(item + 0x14, 0)
        if random.random() < 0.1:
            e.w32(inv, 0)
        target = random.choice((inv, inv, 0, item)) if random.random() < 0.2 else inv
        extra = random.getrandbits(32)
        result, before, after, game_log = run(w, 0x63B210, target, item, extra)
        w.log.clear()
        expect = 0
        if target and before.r32(target) == 0x1020304:
            owner_ptr = before.r32(target + 8)
            if owner_ptr == 0:
                expect = 1
            elif item and before.r32(item) == 4:
                if before.r32(owner_ptr) != 4:
                    expect = 1
                else:
                    data = before.r32(item + 0x14)
                    if data:
                        call(w, 0x63AAF0, ecx=item)
                        call(w, 0x63AF20, ecx=item, esi=target)
                        call(w, 0x620470, item, before.r32(target + 0x28))
                        call(w, 0x6204C0, item, 0)
                        before.w8(data + 0x68, 0)
                        before.w8(data + 0x69, 1)
                        before.w32(target + 0x28, before.r32(target + 0x28) + 1)
                        call(w, 0x629ED0, item, 0xFFFFFFFF)
                        expect = 1
        check(result, expect, f"63b210 result {case}")
        outcomes.add((expect, len(game_log)))
        arena_equal(before, after, "63b210 arena")
        check(game_log, list(w.log), "63b210 calls")
    if len(outcomes) < 3:
        raise AssertionError(f"the socket-move fixture reached only {outcomes}")


def main():
    random.seed(0x1A7E9)
    test_dispatch(ModeWorld(real={0x5973F0}))
    test_list_functions(ModeWorld(real={0x597A20, 0x597890, 0x597B00}))
    test_clear_flags(ModeWorld(real={0x5979B0}))
    test_socket_move(ModeWorld(real={0x63B210}))
    print(f"ok: {cases()} InvMode.cpp cases")


if __name__ == "__main__":
    main()
