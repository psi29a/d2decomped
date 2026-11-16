# SPDX-License-Identifier: GPL-3.0-or-later
"""Run game.exe's shop click handler (FUN_004b3870), its repair gate (FUN_004b1f80)
and the NPC window's line select (FUN_004b82f0) for real and diff them against a model.

Run (cwd tools/emu, game.exe as in README.md):

    D2_GAME_EXE=/path/to/Game.exe python shop_action.py [--mutation-check] [--dump DIR]

FUN_004b3870 (UI/npcmenu.cpp; ECX the player, EDX the item, six stack args:
sell, x, y, ask arg, direct, repair-all) decides what a click on an item does
while an NPC session is up: buy, sell, repair, identify or nothing, which
price call it makes (FUN_0062fdc0 / FUN_0062fe60), which globals it sets
(0x7c0d31 the mode, 0x7c0de0/de1/de5 the pending item, 0x7c0d7f the price,
0x7c0d4b/4f the click) and whether it asks the server at once (FUN_004b2650)
or opens the confirm box (FUN_004b2f50). It runs for real; hooked are only
what it calls: the unit lookup (FUN_00463990), the difficulty byte
(FUN_0044dcd0), the item class (FUN_0062b400), the sell test (FUN_0062a130),
the repair gate (FUN_004b1f80, run for real in its own check), the two price
functions, the request and the box and the sound, the click clear
(FUN_00467a70). The cursor item (FUN_0063c1e0), the player getter
(FUN_00463dd0 / de0), the item flag test (FUN_006280a0), the two store flags
(FUN_00489860 / 00489870) are game.exe's own. The player, the NPC and the item
are synthetic records; GetTickCount is a variable. No MPQs are read.

FUN_004b1f80 (does a worn or carried item want a repair) runs with its leaf
reads hooked (item class, repairable, durability, max durability, stat reads,
stackable, max stack, charge test) over every combination of their answers.

FUN_004b82f0 (select a line of an NPC window; the window is a synthetic record
with the line count at +0x50, the selected line at +0x44 and each line's
selectable word at +0x174 + 0x110 * i) runs for line counts 0..10, every
selectable pattern of a few lines and the indices -3..11; the three halts
(FUN_00408a60 then FUN_00681e09) are hooked to raise with the error line.

--dump DIR writes tests/shop_action_cases.inc, shop_gate_cases.inc and
dialog_select_cases.inc for tests/test_shop_action.cpp.

# gaps-runs: 004b3870 004b1f80 004b82f0
"""
import argparse
import os
import sys

import emu
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

MASK = 0xFFFFFFFF

# NPC window and shop globals (game.exe 1.14d)
SESSION, NPC_ID, QUEST_REC = 0x7C0D29, 0x7C0D25, 0x7C0D43
MODE, PENDING, PEND_CLASS, PEND_ID = 0x7C0D31, 0x7C0DE0, 0x7C0DE1, 0x7C0DE5
PRICE, CLICK_X, CLICK_Y, TICK, DIALOG_STATE = 0x7C0D7F, 0x7C0D4B, 0x7C0D4F, 0x7C0DF1, 0x7C0C6B
REPAIR_ALL_FLAG, STORE_MODE, GAMBLE, PLAYER = 0x7BCC48, 0x7BCBF0, 0x7C0DB0, 0x7A6A70

NPC_ID_VALUE = 0x1234
SENTINEL = {MODE: 0x77, PRICE: 0x55555555, CLICK_X: 0x44444444, CLICK_Y: 0x33333333, PEND_CLASS: 0x77777777, PEND_ID: 0x66666666, DIALOG_STATE: 0x22222222}

REPAIR_CLASSES = (0x9A, 0xB2, 0xFD, 0x101, 0x1FF)
SELL_CLASSES = (0x93, 0x94, 0xB1, 0xC7, 0xCA, 0xFC, 0xFE, 0xFF, 0x195, 0x200, 0x201, 0x202)
IDENTIFY_CLASSES = (0xF4, 0xF5, 0xF6, 0x109, 0x208)
# the table edges: one either side of every group, and classes game.exe's tables send to "nothing"
OTHER_CLASSES = (0, 0x92, 0x95, 0x96, 0xB0, 0xB3, 0xC6, 0xC8, 0xF3, 0xF7, 0xFB, 0x100, 0x102, 0x10A, 0x194, 0x196, 0x1FE, 0x203, 0x207, 0x209, 0x7FFF)
CLASSES = tuple(sorted(set(REPAIR_CLASSES + SELL_CLASSES + IDENTIFY_CLASSES + OTHER_CLASSES)))


def model_class(cls):
    """The click kind game.exe's two switch tables give an NPC class: sell, repair, identify or none."""
    if cls in SELL_CLASSES: return "sell"
    if cls in REPAIR_CLASSES: return "repair"
    if cls in IDENTIFY_CLASSES: return "identify"
    return "none"


# case fields, in the order of the .inc rows
FIELDS = ("session", "npc", "cls", "sell", "direct", "all", "all_flag", "late", "hand", "gamble", "repair_mode", "gold", "sellable", "wants_repair", "identified")
OUT = ("ret", "mode", "pending", "price_mode", "price_for", "target", "click", "ask", "box", "sound")


def model(case, mutation=None):
    """d2d's rules::shop_action: what FUN_004b3870 returns and does for one case (a dict of FIELDS)."""
    c = case
    out = dict(ret=0, mode=-1, pending=-1, price_mode=-1, price_for=0, target=0, click=0, ask=0, box=0, sound=0)
    if not c["session"] or not c["npc"]:
        return out
    kind = model_class(c["cls"])
    if c["all"]:
        if c["cls"] not in REPAIR_CLASSES: return out
        out["ret"] = 1
        if not c["all_flag"]: return out
        out.update(mode=3, pending=1, price_mode=3 if mutation != "all-mode" else 0, price_for=1, target=1, click=1, ask=1)
        return out
    if c["gold"]: return out
    if c["direct"] and c["late"] == 0 and mutation != "no-debounce": return out   # under 500 ms since the last ask
    out["click"] = 1
    if c["sell"] != 1:
        if c["hand"] and mutation != "no-hand": return out
        out.update(mode=1, price_mode=2 if c["gamble"] else 0, price_for=2)
    else:
        if kind == "none": return out
        if kind == "identify":
            out.update(mode=4)       # the flag test compares 0x10 with 1: never taken
            if mutation == "identified-refuses" and c["identified"]: return out
        elif kind == "repair" and c["repair_mode"] == 4:
            if not c["wants_repair"]: return out
            out.update(mode=3, price_mode=3, price_for=2)
        else:
            out.update(price_mode=1, price_for=2)
            if not c["sellable"]: return out
            out.update(mode=2)
    out.update(pending=1, target=2)
    if out["mode"] in (1, 2) and c["direct"]:
        out.update(ret=1, ask=1)
    else:
        out.update(ret=1, box=1, sound=1)
    return out


def machine_for(exe):
    """A game.exe with the shop click handler's callees hooked as the header says."""
    state = dict(tick=0, calls=[], wants_repair=0, sellable=0, gold=0, diff=2)
    emu.Emu.w_GetTickCount = lambda self: (state["tick"], 0)
    m = emu.Emu(exe)
    player, npc, item, inv, data = (m.alloc(0x200) for _ in range(5))
    m.w32(player + 0x60, inv)
    m.w32(inv, 0x1020304)
    m.w32(item, 4); m.w32(item + 4, 0x321); m.w32(item + 0xC, 0x9876); m.w32(item + 0x14, data)
    m.w32(npc + 4, 0)
    ecx = lambda e: e.mu.reg_read(UC_X86_REG_ECX)
    edx = lambda e: e.mu.reg_read(UC_X86_REG_EDX)

    def find(e):
        """Hook for the NPC lookup: returns the NPC when present and asked by id."""
        state["calls"].append(("find", ecx(e), edx(e)))
        return npc if state["npc_present"] and ecx(e) == NPC_ID_VALUE and edx(e) == 1 else 0

    def price(e):
        """Hook for the single-item price call: logs its arguments."""
        args = tuple(e.arg(i) for i in range(6))
        state["calls"].append(("price", *args))
        return 0x3000 + args[5]

    def price_all(e):
        """Hook for the repair-all price call: logs its arguments."""
        args = tuple(e.arg(i) for i in range(6))
        state["calls"].append(("price_all", *args))
        return 0x2001

    hooks = (
        (0x463990, find, 0), (0x44DCD0, lambda e: state["diff"], 0),
        (0x62B400, lambda e: 4 if state["gold"] else 1, 1),
        (0x62A130, lambda e: state["sellable"], 1),
        (0x4B1F80, lambda e: state["wants_repair"], 0),
        (0x62FDC0, price, 6), (0x62FE60, price_all, 6),
        (0x467A70, lambda e: state["calls"].append(("clear", ecx(e))) or 0, 0),
        (0x4B2650, lambda e: state["calls"].append(("ask", e.arg(0), e.arg(1))) or 0, 2),
        (0x4B2F50, lambda e: state["calls"].append(("box",)) or 0, 0),
        (0x4B9A00, lambda e: state["calls"].append(("sound", ecx(e), edx(e))) or 0, 3),
    )
    for addr, fn, n in hooks: m.hook(addr, fn, n)
    state.update(machine=m, player=player, npc=npc, item=item, inv=inv, data=data, npc_present=True)
    return state


def run_case(s, case):
    """Set game.exe's globals for one case, call FUN_004b3870, and read back what it did."""
    m, c = s["machine"], case
    s["calls"].clear()
    s["wants_repair"], s["sellable"], s["gold"] = c["wants_repair"], c["sellable"], c["gold"]
    s["npc_present"] = bool(c["npc"])
    m.w32(s["npc"] + 4, c["cls"])
    m.w32(SESSION, c["session"]); m.w32(NPC_ID, NPC_ID_VALUE)
    m.w32(QUEST_REC, 0x5150); m.w32(REPAIR_ALL_FLAG, c["all_flag"]); m.w32(STORE_MODE, c["repair_mode"])
    m.w32(GAMBLE, c["gamble"]); m.w32(PLAYER, s["player"])
    m.w32(s["inv"] + 0x20, 0x4444 if c["hand"] else 0)
    m.w32(s["data"] + 0x18, 0x10 if c["identified"] else 0)
    m.w32(TICK, 1000); s["tick"] = 1000 + (500 if c["late"] else 499)
    for a, v in SENTINEL.items(): m.w32(a, v)
    m.mu.mem_write(PENDING, bytes([0x77]))
    ret = m.call(0x4B3870, c["sell"], 0x111, 0x222, 0x9, int(c["direct"]), int(c["all"]), ecx=s["player"], edx=s["item"])
    out = dict(ret=ret, mode=-1, pending=-1, price_mode=-1, price_for=0, target=0, click=0, ask=0, box=0, sound=0)
    if m.r32(MODE) != SENTINEL[MODE]: out["mode"] = m.r32(MODE)
    if m.read(PENDING, 1)[0] != 0x77: out["pending"] = m.read(PENDING, 1)[0]
    if m.r32(CLICK_X) != SENTINEL[CLICK_X]:
        assert (m.r32(CLICK_X), m.r32(CLICK_Y)) == (0x111, 0x222), case
        out["click"] = 1
    if m.r32(PEND_ID) != SENTINEL[PEND_ID]:
        out["target"] = 1 if m.r32(PEND_ID) == 0 else 2
        if out["target"] == 2: assert (m.r32(PEND_ID), m.r32(PEND_CLASS)) == (0x9876, 0x321), case
    for call in s["calls"]:
        if call[0] == "price":
            out["price_mode"], out["price_for"] = call[6], 2
            assert call[1:3] == (s["player"], s["item"]) and call[3] == s["diff"] and call[4:6] == (0x5150, c["cls"]), (case, call)
            assert m.r32(PRICE) == 0x3000 + call[6], case
        elif call[0] == "price_all":
            out["price_mode"], out["price_for"] = 3, 1
            assert call[1:] == (0, s["player"], c["cls"], s["diff"], 0x5150, 0), (case, call)
            assert m.r32(PRICE) == 0x2001, case
        elif call[0] == "ask":
            out["ask"] = 1
            assert call[1] == 1 and call[2] == 9, (case, call)
        elif call[0] == "box": out["box"] = 1
        elif call[0] == "sound": out["sound"] = 1; assert call[1:] == (1, 0), (case, call)
        elif call[0] == "clear": assert call[1] == 0, (case, call)
    if not out["price_for"]: assert m.r32(PRICE) == SENTINEL[PRICE], case
    if out["mode"] != -1 or out["box"] or out["ask"]: assert m.r32(DIALOG_STATE) in (SENTINEL[DIALOG_STATE], 4), case
    return out


def shop_cases():
    """Cases: every class with every flag at the edges, plus a seeded spread of the rest."""
    cases, seed = [], 12345
    def rnd(n):
        """Small deterministic generator (LCG) for the case rows."""
        nonlocal seed
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        return (seed >> 8) % n
    for cls in CLASSES:
        for sell in (0, 1, 2):
            for repair_mode in (4, 5):
                for direct in (0, 1):
                    for allp in (0, 1):
                        base = dict(session=1, npc=1, cls=cls, sell=sell, direct=direct, all=allp, all_flag=1, late=1, hand=0, gamble=0,
                                    repair_mode=repair_mode, gold=0, sellable=1, wants_repair=1, identified=0)
                        cases.append(base)
                        flip = dict(base)
                        for name in ("sellable", "wants_repair", "hand", "gamble", "identified", "late", "gold", "all_flag", "session", "npc"):
                            flip = dict(base); flip[name] = int(not base[name]); cases.append(flip)
    for _ in range(1500):
        cases.append(dict(session=int(rnd(8) != 0), npc=int(rnd(8) != 0), cls=CLASSES[rnd(len(CLASSES))], sell=(0, 1, 1, 2)[rnd(4)],
                          direct=rnd(2), all=int(rnd(5) == 0), all_flag=rnd(2), late=rnd(2), hand=rnd(2), gamble=rnd(2),
                          repair_mode=(4, 5, 0, 13)[rnd(4)], gold=int(rnd(6) == 0), sellable=rnd(2), wants_repair=rnd(2), identified=rnd(2)))
    seen, out = set(), []
    for case in cases:
        key = tuple(case[f] for f in FIELDS)
        if key not in seen: seen.add(key); out.append(case)
    return out


def check_shop(exe, mutation=None, rows=None):
    """Run FUN_004b3870 on every case and compare with the model; rows collects the .inc lines."""
    s = machine_for(exe)
    n, kept = 0, set()
    for case in shop_cases():
        got = run_case(s, case)
        want = model(case, mutation)
        assert got == want, (case, got, want)
        # the fixture keeps one case per (class kind, flags) and every sixth of the rest, so it stays small
        key = (model_class(case["cls"]),) + tuple(case[f] for f in FIELDS if f not in ("cls", "identified"))
        if rows is not None and (key not in kept or n % 6 == 0):
            kept.add(key)
            rows.append("{" + ", ".join(str(case[f]) for f in FIELDS) + ", " + ", ".join(str(got[f]) for f in OUT) + "},")
        n += 1
    return n


# ---- FUN_004b1f80 ---------------------------------------------------------------------------
GATE_FIELDS = ("gold", "repairable", "has_dur", "max_dur", "dur", "stackable", "qty", "max_qty", "charge_missing", "charge_zero")


def gate_model(g, mutation=None):
    """d2d's rules::repair_click_gate: does an item want a repair (FUN_004b1f80)."""
    if g["gold"] or not g["repairable"]: return 0
    if g["has_dur"] and g["max_dur"] != 0 and g["dur"] < g["max_dur"]: return 1 if mutation != "gate-dur" else 0
    if g["stackable"] and g["qty"] < g["max_qty"]: return 1
    if g["charge_missing"] and g["charge_zero"] == 0: return 1 if mutation != "gate-charge" else 0
    return 0


def gate_cases():
    """Every combination of the leaf answers, with the numbers at the signed edges."""
    out = []
    for gold in (0, 1):
        for rep in (0, 1):
            for has in (0, 1):
                for max_dur, dur in ((0, 0), (0, 5), (10, 9), (10, 10), (10, 11), (10, -1), (-3, -5)):
                    for stack in (0, 1):
                        for qty, max_qty in ((5, 6), (6, 6), (7, 6), (-1, 0), (0, 0)):
                            for miss in (0, 1):
                                for zero in (0, 1):
                                    out.append(dict(gold=gold, repairable=rep, has_dur=has, max_dur=max_dur, dur=dur, stackable=stack, qty=qty, max_qty=max_qty,
                                                    charge_missing=miss, charge_zero=zero))
    return out


def check_gate(exe, mutation=None, rows=None):
    """Run FUN_004b1f80 for real with its leaf reads answered from each case."""
    m = emu.Emu(exe)
    item = m.alloc(0x40)
    cur = {}
    def stat(e):
        """Hook for the item stat read: durability (0x48) or quantity (0x46)."""
        which = e.arg(1)
        assert e.arg(0) == item and e.arg(2) == 0 and which in (0x48, 0x46), which
        return cur["dur"] if which == 0x48 else cur["qty"]
    def charge(e):
        """Hook for the charge read: writes the case's charge value."""
        assert e.arg(0) == item
        e.w32(e.arg(1), cur["charge_zero"])
        return cur["charge_missing"]
    def item_arg(fn):
        """Wrap a hook so it asserts its first argument is the test item."""
        def wrapped(e):
            """Check the item argument, then run the wrapped hook."""
            assert e.arg(0) == item
            return fn()
        return wrapped
    for addr, fn, n in ((0x62B400, item_arg(lambda: 4 if cur["gold"] else 1), 1), (0x62E660, item_arg(lambda: cur["repairable"]), 1),
                        (0x629930, item_arg(lambda: cur["has_dur"]), 1), (0x625E00, item_arg(lambda: cur["max_dur"]), 1),
                        (0x625480, stat, 3), (0x6289F0, item_arg(lambda: cur["stackable"]), 1), (0x6295B0, item_arg(lambda: cur["max_qty"]), 1),
                        (0x62A840, charge, 2)):
        m.hook(addr, fn, n)
    count = 0
    for g in gate_cases():
        cur.clear(); cur.update(g)
        got = m.call(0x4B1F80, regs={"esi": item}) & 0xFF
        want = gate_model(g, mutation)
        assert got == want, (g, got, want)
        if rows is not None: rows.append("{" + ", ".join(str(g[f]) for f in GATE_FIELDS) + f", {got}" + "},")
        count += 1
    return count


# ---- FUN_004b82f0 ---------------------------------------------------------------------------
SELECT_ERRORS = {0x4E7: "null", 0x4ED: "past", 0x50F: "unselectable", 0x513: "missing"}


def select_model(lines, selectable, index, mutation=None):
    """d2d's NpcMenuState::select_line: the new selected line, or the halt's source line."""
    if index >= len(lines) and mutation != "allow-end": return ("halt", 0x4ED)
    if index == -1: return ("set", -1)
    if index < -1: return ("halt", 0x513)
    if not selectable[index] and mutation != "any-line": return ("halt", 0x50F)
    return ("set", index)


def select_cases():
    """Line counts 0..10 with a few selectable patterns each, every index -3..11."""
    out = []
    for count in range(0, 11):
        patterns = {tuple([0] * count), tuple([1] * count), tuple(i % 2 for i in range(count)), tuple((i + 1) % 2 for i in range(count))}
        if count: patterns |= {tuple([1] + [0] * (count - 1)), tuple([0] * (count - 1) + [1])}
        for pattern in sorted(patterns):
            for index in range(-3, 12):
                out.append((count, pattern, index))
    return out


def check_select(exe, mutation=None, rows=None):
    """Run FUN_004b82f0 for real; the halt is FUN_00408a60 (hooked to record the line FUN_00408090 gave)."""
    m = emu.Emu(exe)
    window = m.alloc(0xB10)
    halts = []
    m.hook(0x408090, lambda e: e.arg(0), 1)       # the error site: its line number is the argument
    def halt(e):
        """Hook for the error site: records its line argument and stops the run."""
        halts.append(e.arg(1))
        raise StopIteration
    m.hook(0x408A60, lambda e: halts.append(e.arg(1)) or 0, 3)
    m.hook(0x681E09, lambda e: (_ for _ in ()).throw(SystemExit("halt")), 1)
    n = 0
    for count, pattern, index in select_cases():
        halts.clear()
        m.mu.mem_write(window, bytes(0xB10))
        m.w32(window + 0x44, 0x99); m.w32(window + 0x50, count)
        for i, flag in enumerate(pattern): m.w32(window + 0x174 + 0x110 * i, flag)
        try:
            m.call(0x4B82F0, ecx=window, edx=index & MASK, limit=100000)
            got = ("set", m.s32(window + 0x44))
            assert not halts
        except SystemExit:
            got = ("halt", halts[-1])
            assert m.r32(window + 0x44) == 0x99
        want = select_model(range(count), pattern, index, mutation)
        assert got == want, (count, pattern, index, got, want)
        if rows is not None: rows.append("{" + f"{count}, 0x{sum(f << i for i, f in enumerate(pattern)):x}, {index}, {1 if got[0] == 'set' else 0}, {got[1]}" + "},")
        n += 1
    # a null window halts at 0x4e7
    halts.clear()
    try:
        m.call(0x4B82F0, ecx=0, edx=0, limit=100000)
        raise AssertionError("null window did not halt")
    except SystemExit:
        assert halts[-1] == 0x4E7, halts
    return n + 1


def check_charge_status(exe):
    """Run FUN_0062a840 with synthetic stat 0xcc entries from its leaf reader."""
    machine = emu.Emu(exe)
    item = machine.alloc(0x40)
    output = machine.alloc(4)
    cases = [[], [(4, 4)], [(0, 4)], [(3, 4)], [(5, 4)],
             [(4, 4), (3, 4)], [(3, 4), (4, 4)], [(255, 255)], [(254, 255)],
             [(4, 4)] * 63 + [(0, 4)]]
    entries = []

    def read_charges(engine):
        assert engine.arg(0) == item and engine.arg(1) == 0xCC and engine.arg(3) == 64
        for index, (current, maximum) in enumerate(entries):
            address = engine.arg(2) + index * 8
            engine.w32(address, 0xCC0000 + index)
            engine.w32(address + 4, current | (maximum << 8))
        return len(entries)

    machine.hook(0x6261D0, read_charges, 4)
    for entries in cases:
        machine.w32(output, 0xFFFFFFFF)
        present = machine.call(0x62A840, item, output)
        full = machine.r32(output)
        expected = (int(bool(entries)), int(not any(current < maximum for current, maximum in entries)))
        assert (present, full) == expected, (entries, present, full, expected)
        assert machine.call(0x62A840, item, 0) == expected[0]
    return len(cases)


MUTATIONS = ("all-mode", "no-debounce", "no-hand", "identified-refuses", "gate-dur", "gate-charge", "allow-end", "any-line")


def main():
    """Run the shop, repair and dialog-select cases in game.exe and diff them against d2d."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default=os.environ.get("D2_GAME_EXE"))
    parser.add_argument("--mutation-check", action="store_true")
    parser.add_argument("--dump", help="directory for the three .inc fixtures (tests/)")
    args = parser.parse_args()
    shop_rows, gate_rows, select_rows = [], [], []
    shop = check_shop(args.exe, rows=shop_rows)
    gate = check_gate(args.exe, rows=gate_rows)
    select = check_select(args.exe, rows=select_rows)
    charges = check_charge_status(args.exe)
    print(f"ok: {charges} FUN_0062a840 cases with and without an output pointer")
    print(f"ok: {shop} FUN_004b3870 cases, {gate} FUN_004b1f80 cases, {select} FUN_004b82f0 cases")
    if args.dump:
        for name, head, rows in (("shop_action_cases.inc", "tools/emu/shop_action.py: FUN_004b3870 " + ", ".join(FIELDS) + " | " + ", ".join(OUT), shop_rows),
                                 ("shop_gate_cases.inc", "tools/emu/shop_action.py: FUN_004b1f80 " + ", ".join(GATE_FIELDS) + " | wants repair", gate_rows),
                                 ("dialog_select_cases.inc", "tools/emu/shop_action.py: FUN_004b82f0 line count, selectable bits, index | set (1) or halt (0), new line or halt line", select_rows)):
            with open(os.path.join(args.dump, name), "w", newline="\n") as out:
                out.write("// " + head + "\n" + "\n".join(rows) + "\n")
    if args.mutation_check:
        for mutation in MUTATIONS:
            try:
                check_shop(args.exe, mutation); check_gate(args.exe, mutation); check_select(args.exe, mutation)
            except (AssertionError, IndexError):
                continue
            sys.exit(f"mutation {mutation} was not caught")
        print(f"mutation check ok ({len(MUTATIONS)} ways)")


if __name__ == "__main__":
    main()
