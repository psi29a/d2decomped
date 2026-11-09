# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's look token table (Inventory.cpp FUN_0063d710, FUN_0063d900) on the loaded 1.14d tables.

FUN_0063d710 builds the 255-entry (code, ItemTypes row) table at 0x96cc68 once, from every Items row; FUN_0063d900
(ESI alternategfx, EDI code) returns the first entry holding either. The model is written from the decompile and
reads the real Items rows (FUN_00633590) and the static table at 0x744ca8; FUN_00629b50 (is this type, or a child of
it) is game.exe's own. NOTHING is hooked.

FUN_0063d930 (a bow's or crossbow's look slot) and FUN_0063da70 (one worn item's look slots and tint) run on fake
players with the item helpers hooked and memoised as in inventory_queries.py (FUN_0064f380 the weapon class,
FUN_0063c050 the worn item, FUN_0063d900 the table index, FUN_0062c100 the tint, FUN_00633640 / FUN_006335f0 the
item records, FUN_00628660 the component, FUN_00627d40 a type byte); their call order is compared too. FUN_0063d930
and FUN_0063da70 call a fatal-error path when an item has no record; the fixture always gives one.

Usage: D2_MPQ_DIR=<MPQ folder> D2_BUILD=<a build with tools/mpq-cat> uv run python inventory_look.py
"""
import random

from unicorn.x86_const import UC_X86_REG_EDI, UC_X86_REG_ESI

import skill_tips
from _inventory_world import check as check_equal
from inventory_queries import World, arena_equal, ask, run

TABLE = 0x96CC68
STATIC = 0x744CA8
FLAG = 0x96D464
CASES = 0


def check(actual, expected, label):
    """Count one case; raise with the label when game.exe differs from the model."""
    global CASES
    CASES += 1
    if actual != expected:
        raise AssertionError(f"{label}: game.exe {actual:#x}, model {expected:#x}")


def model_table(e):
    """The table FUN_0063d710 should build, as 255 (code, type) pairs."""
    kind_is = lambda a, b: e.call(0x629B50, a, b) != 0
    rows = e.call(0x633590)
    count, records = e.r32(rows), e.r32(rows + 4)
    table = [(0, 0)] * 256     # entry 255 is the word at 0x96d460, still 0 while the table is built
    table[1] = (0x2074696C, 3)
    table[2] = (0x2064656D, 3)
    table[3] = (0x20797668, 3)
    unused = 4
    for row in range(count):
        record = records + 0x1A8 * row
        code = e.r32(record + 0x90) or e.r32(record + 0x80)
        kind = e.r16(record + 0x11E)
        kind = kind - 0x10000 if kind & 0x8000 else kind
        good = True
        if not (kind_is(kind, 0x2D) or kind_is(kind, 3) or kind_is(kind, 0x33) or kind_is(kind, 0x25)) or kind_is(kind, 0x4B):
            good = False
        if any(table[n][0] == code for n in range(unused)):
            continue
        if good:
            index = unused
            while True:
                static_kind = e.s32(STATIC + 4 + index * 8)
                busy = (kind_is(static_kind, 0x2D) and kind_is(kind, 0x2D)) or (kind_is(static_kind, 0x32) and kind_is(kind, 0x32))
                if not (busy or table[index][0] != 0):
                    break
                index += 1
            if index > 0xFE:
                index = unused
            table[index] = (code, kind & 0xFFFFFFFF)
            if unused == index:
                unused += 1
    return table[:255]


def read_table(e):
    """The 255 (code, type) pairs game.exe holds."""
    return [(e.r32(TABLE + 8 * n), e.r32(TABLE + 8 * n + 4)) for n in range(255)]


def model_lookup(table, alternate, code):
    """FUN_0063d900: the first entry equal to ESI, or equal to EDI, from 1 to 0xfe; 0 when none."""
    for index in range(1, 0xFF):
        if table[index][0] in (alternate, code):
            return index
    return 0


class LookWorld(World):
    """The shared fixture plus hooks for the callees of FUN_0063d930 / FUN_0063da70."""

    def __init__(self):
        super().__init__()
        self.e.hook(0x64F380, self._code_of, 5)
        self.e.hook(0x63C050, self._worn, 2)
        self.e.hook(0x63D900, self._look_index, 0)
        self.e.hook(0x62C100, self._tint, 4)

    cand = ()

    def _code_of(self, e):
        """FUN_0064f380 (unit, inventory, &out, -1, 1): the weapon-class code of a unit's gear; the out word is not read."""
        key = (0x64F380, e.arg(0), e.arg(1), e.arg(3), e.arg(4))
        return self.answer_with(key, lambda: random.choice((0x20776278, 0x20776F62, 0x20776F62, 0x20776278, 0x1234)))

    def _worn(self, e):
        """FUN_0063c050 (inventory, component): one of the case's candidate items, or none."""
        key = (0x63C050, e.arg(0), e.arg(1))
        return self.answer_with(key, lambda: random.choice((0,) + tuple(self.cand)))

    def _look_index(self, e):
        """FUN_0063d900 (ESI alternategfx, EDI code): a memoised table index."""
        key = (0x63D900, e.mu.reg_read(UC_X86_REG_ESI), e.mu.reg_read(UC_X86_REG_EDI))
        return self.answer_with(key, lambda: random.choice((0, 0, random.randint(1, 40))))

    def _tint(self, e):
        """FUN_0062c100 (unit, item, &tint, 0): writes a memoised byte, returns whether there is one."""
        key = (0x62C100, e.arg(0), e.arg(1))
        found, value = self.answer_with(key, lambda: (int(random.random() < 0.7), random.randint(0, 9)), keep_tuple=True)
        e.mu.mem_write(e.arg(2), bytes([value]))
        return found


def player_kit(w):
    """A fake player (inventory at +0x60 or none), an item, the two 16-byte look arrays and the worn candidates."""
    a, e = w.arena, w.e
    inv = a.inventory() if random.random() < 0.85 else 0
    unit = a.new(0x80)
    e.w32(unit + 0x60, inv)
    items = [a.item() for _ in range(3)]
    for item in items:
        e.w32(item + 4, random.randint(0, 5))
    w.cand = tuple(items)
    arrays = []
    for _ in range(2):
        block = a.new(16)
        e.mu.mem_write(block, bytes(random.getrandbits(8) for _ in range(16)))
        arrays.append(block)
    return unit, inv, random.choice(items), arrays


COVERAGE = {name: 0 for name in ("crossbow set", "crossbow unlisted", "bow set", "bow unlisted", "tint found", "tint missing",
                                  "slot 5 wielded", "slot 6 other", "slot out of range", "slot unlisted")}


def m_d930(w, m, unit, item, first, second):
    """FUN_0063d930 (EAX player, EBX item, arrays): a crossbow's quiver slot 6 or a bow's slot 5 of the look."""
    kind = ask(w, 0x627D40, item) & 0xFF
    if kind not in (4, 5):
        return
    inv = m.r32(unit + 0x60)
    code = w.answer_with((0x64F380, unit, inv, 0xFFFFFFFF, 1), None)
    comp = ask(w, 0x628660, item) & 0xFF
    if code == 0x20776278:
        if comp in (5, 6) and inv:
            worn = w.answer_with((0x63C050, inv, 5), None)
            if worn:
                rec = ask_record(w, m.r32(worn + 4))
                index = w.answer_with((0x63D900, w.e.r32(rec + 0x90), w.e.r32(rec + 0x80)), None)
                if index > 0:
                    COVERAGE["crossbow set"] += 1
                    m.w8(first + 6, index)
                    return
                COVERAGE["crossbow unlisted"] += 1
                m.w8(first + 6, 0xFF)
                m.w8(second + 6, 0xFF)
    elif code == 0x20776F62 and comp == 6:
        rec, _klass = w.answer_code(0x2074696C)
        if rec:
            index = w.answer_with((0x63D900, w.e.r32(rec + 0x90), w.e.r32(rec + 0x80)), None)
            COVERAGE["bow unlisted" if index == 0 else "bow set"] += 1
            if index == 0:
                m.w8(first + 5, 0xFF)
                m.w8(second + 5, 0xFF)
            else:
                m.w8(first + 5, index)


def ask_record(w, klass):
    """The model's FUN_006335f0 call: the memoised record the game made, logged."""
    key = ("rec", klass)
    w.log.append(key)
    return w.memo[key]


def m_da70(w, m, item, unit, first, second):
    """FUN_0063da70 (EAX item, unit, arrays): one worn item's slot in the look arrays, then its tint."""
    inv = m.r32(unit + 0x60)
    if not inv:
        return
    w5 = w.answer_with((0x63C050, inv, 5), None)
    w6 = w.answer_with((0x63C050, inv, 6), None)
    rec = ask_record(w, m.r32(item + 4))
    slot = ask(w, 0x628660, item) & 0xFF
    index = w.answer_with((0x63D900, w.e.r32(rec + 0x90), w.e.r32(rec + 0x80)), None)
    if index == 0:
        COVERAGE["slot unlisted"] += 1
        if slot < 0x10:
            m.w8(first + slot, 0xFF)
            m.w8(second + slot, 0xFF)
        return
    if item == w5:
        COVERAGE["slot 5 wielded"] += 1
        slot = 5
    elif item == w6:
        COVERAGE["slot 6 other"] += 1
        slot = 6
    elif slot > 0xF:
        COVERAGE["slot out of range"] += 1
        return
    m.w8(first + slot, index)
    m_d930(w, m, unit, item, first, second)
    found, tint = w.answer_with((0x62C100, unit, item), None, keep_tuple=True)
    m.w8(second + slot, tint + 1 if found else 0xFF)
    COVERAGE["tint found" if found else "tint missing"] += 1


def test_callers(w):
    """FUN_0063d930 and FUN_0063da70 over random fake players."""
    a = w.arena
    for case in range(1500):
        a.reset()
        w.log.clear()
        w.memo.clear()
        unit, inv, item, (first, second) = player_kit(w)
        # a record for every item class the fixture can ask about
        result, before, after, game_log = run(w, 0x63D930, first, second, regs={"eax": unit, "ebx": item})
        w.log.clear()
        m_d930(w, before, unit, item, first, second)
        arena_equal(before, after, f"63d930 {case}")
        check_equal(game_log, list(w.log), "63d930 calls")
        a.reset()
        w.log.clear()
        w.memo.clear()
        unit, inv, item, (first, second) = player_kit(w)
        result, before, after, game_log = run(w, 0x63DA70, unit, first, second, regs={"eax": item})
        w.log.clear()
        m_da70(w, before, item, unit, first, second)
        arena_equal(before, after, f"63da70 {case}")
        check_equal(game_log, list(w.log), "63da70 calls")
    missing = [name for name, count in COVERAGE.items() if not count]
    if missing:
        raise AssertionError(f"the look fixture never reached: {missing}")


def main():
    random.seed(0x1A7E8)
    e = skill_tips.boot()
    check(e.r32(FLAG), 0, "table built before the first call")
    expected = model_table(e)
    e.call(0x63D710)
    check(e.r32(FLAG), 1, "built flag")
    check(e.r32(0x96D460), 0xFF, "limit word")
    table = read_table(e)
    for index, (pair, want) in enumerate(zip(table, expected)):
        check(pair[0], want[0], f"entry {index} code")
        check(pair[1], want[1], f"entry {index} type")
    check(sum(1 for code, _kind in table if code), sum(1 for code, _kind in expected if code), "entries filled")
    e.call(0x63D710)
    check(read_table(e) == table, True, "a second call changes nothing")
    rows = e.call(0x633590)
    count, records = e.r32(rows), e.r32(rows + 4)
    pairs = [(e.r32(records + 0x1A8 * n + 0x90), e.r32(records + 0x1A8 * n + 0x80)) for n in range(count)]
    codes = [code for code, _kind in table if code]
    pairs += [(0, c) for c in codes] + [(c, 0) for c in codes] + [(0, 0), (0xFFFFFFFF, 0xFFFFFFFF)]
    pairs += [(random.getrandbits(32), random.getrandbits(32)) for _ in range(300)]
    pairs += [(random.choice(codes), random.choice(codes)) for _ in range(300)]
    empty = sum(1 for n in range(1, 0xFF) if table[n][0] == 0)
    first_empty = next(n for n in range(1, 0xFF) if table[n][0] == 0)
    # the quirk: a zero ESI (an item without alternategfx) equals any empty slot, so an unlisted code finds the first one
    check(e.call(0x63D900, regs={"esi": 0, "edi": 0xDEADBEEF}), first_empty, "zero alternate, unlisted code")
    check(model_lookup(table, 0xDEADBEEF, 0xCAFEF00D), 0, "both unlisted gives 0")
    check(e.call(0x63D900, regs={"esi": 0xDEADBEEF, "edi": 0xCAFEF00D}), 0, "both unlisted")
    for alternate, code in pairs:
        check(e.call(0x63D900, regs={"esi": alternate, "edi": code}), model_lookup(table, alternate, code), f"lookup {alternate:#x} {code:#x}")
    table_cases = CASES
    test_callers(LookWorld())
    from _inventory_world import cases
    print(f"ok: {table_cases} look token table cases ({sum(1 for c, _k in table if c)} entries, {empty} empty slots) "
          f"and {cases()} look slot cases")


if __name__ == "__main__":
    main()
