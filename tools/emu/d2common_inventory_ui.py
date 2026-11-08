# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify seven inventory-panel routines of game.exe 1.14d (the D2COMMON map entries left).

    uv run python d2common_inventory_ui.py

d2common_item_ui.py covers the mode predicate, the panel rectangle and the
grid-cell helper. This covers the seven that decide things and call out:

* FUN_00486d10 the action throttle (stdcall, no arguments)
* FUN_00486da0 the gold-button hit test (EDI = x, ESI = y)
* FUN_00486e60 the drop-on-ground click (EDI = x, ESI = message, stack = y)
* FUN_00486ef0 the inventory right-click (stdcall message)
* FUN_004844a0 the body-slot highlight (EAX = inventory)
* FUN_00483c50 the grid placement highlight (EDI = unit, EBX = colour, EAX =
  grid index, stack = inventory, grid coordinates)
* FUN_00487000 the item-under-cursor update (EAX = grid layout, six stack
  arguments)

Each runs game.exe's own code on random globals, messages and layouts with
every callee hooked: a hook records its arguments and registers and answers
with a chosen value. A hand-written model of each, from the disassembly, must
give the same return value, the same ordered call log, the same writes to the
globals it touches and the same message bytes. The callees are not claimed,
only that the routine calls them in that order with those arguments. The one
exception is FUN_00486ef0, which runs FUN_00486da0 for real. No MPQs are read.
"""
import argparse
import hashlib
import itertools
import random
import struct
from pathlib import Path

from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP,
                               UC_X86_REG_ESI)

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
LIMIT = 100000
M32 = 0xFFFFFFFF

# Globals the routines read or write.
PAGE, SCREEN_X, SCREEN_Y = 0x7BCBF0, 0x71146C, 0x711470
PANEL_X, PANEL_Y, LAST_TICK = 0x7A2858, 0x7A285C, 0x7BCA48
SLOT, HAVE_ITEM, CELL_X, CELL_Y = 0x7BCC20, 0x7BCBE8, 0x721E4C, 0x721E50
CURSOR_ON, GOLD, GOLD_OPEN = 0x7BCBE4, 0x7BCE30, 0x7BCE2C
EVT_A, EVT_B = 0x7BCE90, 0x7BCE94
RECT = (0x7BCAA0, 0x7BCAA4, 0x7BCAA8, 0x7BCAAC)
HOVER_A, HOVER_B = 0x7BCBF4, 0x7BCBF8
BOX_RECT = (0x721E3C, 0x721E40, 0x721E44, 0x721E48)
WATCH = (PAGE, SCREEN_X, SCREEN_Y, PANEL_X, PANEL_Y, LAST_TICK, SLOT, HAVE_ITEM, CELL_X, CELL_Y, CURSOR_ON,
         GOLD, GOLD_OPEN, EVT_A, EVT_B, HOVER_A, HOVER_B) + RECT + BOX_RECT
TICK_IMPORT = 0x6CC260      # the GetTickCount import slot
FILE_ARG, LINE_ARG = 0x6D9B90, 0x6DC
BODY_SLOTS = 0x7BCC58       # 20-byte screen records, one per body slot
BOX = 0x20786F62            # "box ", the Horadric Cube's item code
INVENTORY_POINTER = 0x1111  # what the fake player's inventory field holds

# (name, address, stack arguments the callee pops, registers recorded)
CALLEES = (
    ("gametype", 0x44DB30, 0, ()), ("throttled", 0x4B9180, 0, ()), ("cancel_trade", 0x4B90B0, 0, ()),
    ("player", 0x463DD0, 0, ()), ("hand_item", 0x63C1E0, 1, ()), ("resolution", 0x4F5160, 0, ()),
    ("belt_slot", 0x498DC0, 0, (UC_X86_REG_ECX,)), ("border", 0x45AE90, 0, ()), ("item_type", 0x468830, 0, ()),
    ("drop_ground", 0x453EC0, 0, ()), ("break_chain", 0x420290, 1, ()),
    ("set_ui_flag", 0x455F20, 1, (UC_X86_REG_ECX, UC_X86_REG_EDX)), ("gold_dialog", 0x454150, 0, (UC_X86_REG_ECX,)),
    ("clear_clicks", 0x44DA70, 0, ()), ("mouse_x", 0x468730, 0, ()), ("mouse_y", 0x468740, 0, ()),
    ("item_size", 0x6286C0, 5, ()), ("body_fit", 0x62ED50, 2, ()), ("equip_req", 0x62EAF0, 6, ()),
    ("item_at_slot", 0x63BDE0, 2, ()), ("can_socket", 0x4843E0, 0, (UC_X86_REG_ECX, UC_X86_REG_EAX)),
    ("draw_body", 0x483A70, 0, (UC_X86_REG_EAX, UC_X86_REG_EDI)), ("grid_collide", 0x63B9D0, 8, ()),
    ("grid_occupy", 0x63BB20, 8, ()),
    ("draw_solid", 0x4839D0, 2, (UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EAX, UC_X86_REG_ESI)),
    ("items_line", 0x6335F0, 1, ()), ("x_pos", 0x45ADF0, 0, (UC_X86_REG_ECX,)),
    ("y_pos", 0x45AE20, 0, (UC_X86_REG_ECX,)), ("expansion", 0x44DCC0, 0, ()),
    ("inv_page", 0x621050, 3, ()), ("overlay", 0x4DBEA0, 3, (UC_X86_REG_ECX, UC_X86_REG_EDX)),
    ("unit_at_cell", 0x63BD10, 7, ()),
)
# Stack arguments that are pointers into the routine's frame: logged as None.
FRAME_ARGS = {"item_size": (1, 2), "overlay": (1, 2), "unit_at_cell": (3, 4), "grid_collide": (5, 6),
              "grid_occupy": (5,)}
BYTE_REGS = {"draw_solid": (0, 1)}      # CL and DL: only the low byte is an argument


def require(condition, message):
    """Stop the probe when a stock-code observation disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


def s32(value):
    """Signed view of a 32-bit value."""
    value &= M32
    return value - (1 << 32) if value >> 31 else value


def half(value):
    """x / 2 the way the code does it: (x - sign) >> 1, arithmetic, as a signed number."""
    return (s32(value) - (value >> 31 & 1)) >> 1


class Rig:
    """The emulator with every callee hooked to record and answer."""

    def __init__(self):
        exe = Path(emu.data_path()) / "bin" / "game.exe"
        require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
                "requires stock game.exe 1.14d (1.14.3.71)")
        self.m = emu.Emu(str(exe))
        self.log, self.ret, self.fx, self.ticks = [], {}, {}, []
        self.cases = 0
        for name, addr, nstack, regs in CALLEES:
            self.m.hook(addr, self._make(name, nstack, regs), nstack)
        self.m.slots[self.m.r32(TICK_IMPORT)] = ("GetTickCount", lambda e: (self._tick(), 0))

    def _tick(self):
        """The next scripted GetTickCount value."""
        require(self.ticks, "GetTickCount called more often than the model says")
        return self.ticks.pop(0)

    def _make(self, name, nstack, regs):
        """A hook for one callee: apply its output effect, log it, answer with the scripted value."""
        def hook(e):
            """Record (name, registers, stack arguments); frame pointers are logged as None."""
            regvals = [e.mu.reg_read(r) for r in regs]
            for index in BYTE_REGS.get(name, ()):
                regvals[index] &= 0xFF
            args = [e.arg(i) for i in range(nstack)]
            if name in self.fx:
                self.fx[name](e, args)
            for index in FRAME_ARGS.get(name, ()):
                args[index] = None
            self.log.append((name,) + tuple(regvals) + tuple(args))
            value = self.ret.get(name, 0)
            return value(args) if callable(value) else value
        return hook

    def run(self, label, address, stack, regs, values, model, ret=None, fx=None, ticks=(), msg=None):
        """Run stock code once; model() -> (eax or None, call log, global writes, message writes)."""
        for addr in WATCH:
            self.m.w32(addr, 0)
        for addr, value in values.items():
            self.m.w32(addr, value & M32)
        self.ret, self.fx, self.ticks, self.log = dict(ret or {}), dict(fx or {}), list(ticks), []
        before = {addr: self.m.r32(addr) for addr in WATCH}
        msg_before = self.m.read(msg, 0x40) if msg else None
        eax = self.m.call(address, *stack, regs=regs, limit=LIMIT)
        require(self.m.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{label}: did not return")
        want_eax, want_log, writes, msg_writes = model()
        require(self.log == want_log, f"{label}: calls\n  got   {self.log}\n  model {want_log}")
        expect = dict(before)
        expect.update({a: v & M32 for a, v in writes.items()})
        after = {addr: self.m.r32(addr) for addr in WATCH}
        require(after == expect, f"{label}: globals {[(hex(a), hex(after[a]), hex(expect[a])) for a in WATCH if after[a] != expect[a]]}")
        if msg:
            want = bytearray(msg_before)
            for offset, value in msg_writes.items():
                want[offset:offset + 4] = struct.pack("<I", value)
            require(self.m.read(msg, 0x40) == bytes(want), f"{label}: message bytes differ")
        if want_eax is not None:
            require(eax & M32 == want_eax & M32, f"{label}: eax {eax:#x}, model {want_eax:#x}")
        require(not self.ticks, f"{label}: GetTickCount was called {len(self.ticks)} fewer times than modelled")
        self.cases += 1


def gold_hit(sx, sy, px, py, x, y, hand):
    """FUN_00486da0's rule: no item in hand and the point inside the gold button's rectangle."""
    return int(hand == 0 and py + sy - 0x57 <= s32(y) <= py + sy - 0x45 and
               sx - px - 0xED <= s32(x) <= sx - px - 0xD9)


def main():
    """Run every case against stock game.exe 1.14d."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    r = Rig()
    m = r.m
    rng = random.Random(1140)
    msg = m.alloc(0x40)
    player = m.alloc(0x100)
    m.w32(player + 0x60, INVENTORY_POINTER)
    layout = m.alloc(0x20)
    size_of = {}

    def write_size(e, args):
        """FUN_006286c0: store the scripted (width, height) of the unit at its two byte pointers."""
        width, height = size_of.get(args[0], (1, 1))
        e.mu.mem_write(args[1], bytes((width,)))
        e.mu.mem_write(args[2], bytes((height,)))

    def screen(rr):
        """A screen size and panel offsets."""
        return rr.choice((640, 800, 1024)), rr.choice((480, 600, 768)), rr.choice((0, 100, 320)), rr.choice((0, 40))

    # ---- FUN_00486d10: the action throttle -------------------------------------------------
    for game_type, throttled, last, now, then in itertools.product(
            (0, 1, 2, 3, 5), (0, 1), (0, 1000, M32 - 100), (0, 1000, 1250, 1251, 1300, 99), (7, 8)):
        diff = (now - last) & M32
        needs = game_type not in (0, 1) and not throttled
        ticks = ([now, then] if diff > 0xFA else [now]) if needs else []

        def model(gt=game_type, thr=throttled, d=diff, t2=then):
            """Throttle model: types 0 and 1 pass; a throttled trade is cancelled; else 250 ms."""
            log = [("gametype",)]
            if gt == 0:
                return 1, log, {}, {}
            log.append(("gametype",))
            if gt == 1:
                return 1, log, {}, {}
            log.append(("throttled",))
            if thr:
                return 0, log + [("cancel_trade",)], {}, {}
            return (1, log, {LAST_TICK: t2}, {}) if d > 0xFA else (0, log, {}, {})
        r.run(f"throttle type={game_type} thr={throttled} last={last} now={now}", 0x486D10, (), {},
              {LAST_TICK: last}, model, ret={"gametype": game_type, "throttled": throttled}, ticks=ticks)

    # ---- FUN_00486da0: the gold-button hit test --------------------------------------------
    for sx, sy, px, py in ((800, 600, 100, 40), (1024, 768, 0, 0), (640, 480, 320, 5)):
        lo_y, hi_y, lo_x, hi_x = py + sy - 0x57, py + sy - 0x45, sx - px - 0xED, sx - px - 0xD9
        for x, y in ((lo_x, lo_y), (hi_x, hi_y), (lo_x - 1, lo_y), (hi_x + 1, hi_y), (lo_x, lo_y - 1),
                     (hi_x, hi_y + 1), ((lo_x + hi_x) // 2, (lo_y + hi_y) // 2), (-1, -1), (0, 0)):
            for hand in (0, 0x2222):
                r.run(f"gold x={x} y={y} hand={hand}", 0x486DA0, (), {"esi": y & M32, "edi": x & M32},
                      {SCREEN_X: sx, SCREEN_Y: sy, PANEL_X: px, PANEL_Y: py},
                      lambda x=x, y=y, hand=hand, sx=sx, sy=sy, px=px, py=py:
                      (gold_hit(sx, sy, px, py, x, y, hand), [("player",), ("hand_item", INVENTORY_POINTER)], {}, {}),
                      ret={"player": player, "hand_item": hand})

    # ---- FUN_00486e60: the drop-on-ground click ---------------------------------------------
    for _ in range(500):
        sx, sy, _px, _py = screen(rng)
        values = {SCREEN_X: sx, SCREEN_Y: sy, PAGE: rng.choice((0, 0, 1, 2)), CURSOR_ON: 7, HAVE_ITEM: 9}
        resolution, belt = rng.choice((0, 1, 2, 2, 2, 3)), rng.choice((0, 0, 0, 5))
        border_a, border_b = rng.choice((0, 1, 3, 1)), rng.choice((0, 1, 3, 3))
        item_type = rng.choice((0, 6))
        x = rng.choice((0, sx // 2 - 1, sx // 2, sx // 2 + 1, rng.randrange(sx), sx - 1))
        y = rng.choice((sy - 0x31, sy - 0x30, sy - 0x2F, rng.randrange(sy), -1 & M32))
        borders = iter((border_a, border_b))

        def model(v=values, res=resolution, bs=belt, ba=border_a, bb=border_b, it=item_type, x=x, y=y):
            """Drop-click model: high-resolution only, off the belt, above the bottom bar."""
            log = [("resolution",)]
            if res != 2:
                return None, log, {}, {}
            log.append(("belt_slot", msg))
            if bs or s32(y) > v[SCREEN_Y] - 0x30:
                return None, log, {}, {}
            log.append(("border",))
            if ba == 1 and s32(x) < half(v[SCREEN_X]):
                return None, log, {}, {}
            log.append(("item_type",))
            if it == 6:
                log.append(("drop_ground",))
            log.append(("border",))
            if bb == 3 and v[PAGE] == 0 and s32(x) < half(v[SCREEN_X]):
                return None, log, {}, {}
            return None, log + [("break_chain", msg)], {CURSOR_ON: 0, HAVE_ITEM: 0}, {0x18: 1}
        m.mu.mem_write(msg, bytes(rng.randrange(256) for _ in range(0x40)))
        r.run(f"drop click res={resolution} belt={belt} borders={border_a},{border_b} type={item_type} x={x} y={y}",
              0x486E60, (y,), {"esi": msg, "edi": x}, values, model,
              ret={"resolution": resolution, "belt_slot": belt, "border": lambda a, borders=borders: next(borders),
                   "item_type": item_type}, msg=msg)

    # ---- FUN_00486ef0: the inventory right-click --------------------------------------------
    for _ in range(800):
        sx, sy, px, py = screen(rng)
        button = ((sx - px - 0x12E, py + sy - 0x60), (sx - px - 0x10E, py + sy - 0x40),
                  (sx - px - 0x12F, py + sy - 0x60), (sx - px - 0x10D, py + sy - 0x3F),
                  (sx - px - 0xED, py + sy - 0x57), (sx - px - 0xD9, py + sy - 0x45),
                  (sx - px - 0xEE, py + sy - 0x45))
        x, y = rng.choice(button + ((rng.randrange(sx), rng.randrange(sy)),) * 3)
        belt_first, belt_second = rng.choice((0, 0, 3)), rng.choice((0, 0, 4))
        page, gold, gold_open, hand = rng.choice((0, 0, 1)), rng.choice((0, 1, 1)), rng.choice((0, 0, 1)), rng.choice((0, 0, 0x2222))
        rect = (rng.choice((x - 3, x, x + 1)), rng.choice((x + 2, x + 1, x)),
                rng.choice((y - 3, y, y + 1)), rng.choice((y + 2, y, y - 1)))
        values = {SCREEN_X: sx, PANEL_X: px, SCREEN_Y: sy, PANEL_Y: py, PAGE: page, GOLD: gold,
                  GOLD_OPEN: gold_open, EVT_A: 5, EVT_B: 6}
        values.update(zip(RECT, rect))
        m.mu.mem_write(msg, bytes(rng.randrange(256) for _ in range(0x40)))
        m.mu.mem_write(msg + 0xC, struct.pack("<HH", x, y))
        answers = iter((belt_first, belt_second))

        def model(sx=sx, px=px, sy=sy, py=py, x=x, y=y, bf=belt_first, bs=belt_second, page=page, gold=gold,
                  go=gold_open, hand=hand, rect=rect):
            """Right-click model: the shop button, the gold button, then the inventory rectangle."""
            log = [("player",), ("belt_slot", msg)]
            writes = {EVT_A: 0, EVT_B: 0}
            if not bf:
                if sx - px - 0x12E <= x <= sx - px - 0x10E and py + sy - 0x60 <= y <= py + sy - 0x40:
                    return None, log + [("set_ui_flag", 1, 1, 0)], writes, {}
                log.append(("hand_item", INVENTORY_POINTER))
            if gold and page == 0:
                writes[GOLD] = 0
                log += [("player",), ("hand_item", INVENTORY_POINTER)]
                if gold_hit(sx, sy, px, py, x, y, hand) and not go:
                    log.append(("gold_dialog", 1))
            log.append(("clear_clicks",))
            msg_writes = {}
            if rect[0] <= x < rect[1] and rect[2] <= y <= rect[3]:
                log.append(("belt_slot", msg))
                if not bs:
                    log.append(("break_chain", msg))
                    msg_writes = {0x1C: 0, 0x18: 1}
            return None, log, writes, msg_writes
        r.run(f"right click x={x} y={y} belts={belt_first},{belt_second} gold={gold} page={page}", 0x486EF0,
              (msg,), {}, values, model,
              ret={"player": player, "hand_item": hand, "belt_slot": lambda a, answers=answers: next(answers)}, msg=msg)

    # ---- FUN_004844a0: the body-slot highlight -----------------------------------------------
    inventory = 0x3333
    for _ in range(600):
        sx, sy, _px, _py = screen(rng)
        page, slot = rng.choice((0, 1, 1, 2)), rng.randrange(12)
        mouse_x = rng.choice((0, sx // 2 - 1, sx // 2, sx - 1))
        mouse_y = rng.choice((0, sy - 0x28, sy - 0x27, sy - 0x26, sy // 2))
        item, have = rng.choice((0, 0x4444, 0x4444)), rng.choice((0, 1, 1))
        fit, requirements, socket, other = (rng.choice((0, 1)), rng.choice((0, 1)), rng.choice((0, 1)),
                                            rng.choice((0, 0x5555)))
        values = {SCREEN_X: sx, SCREEN_Y: sy, PAGE: page, SLOT: slot, HAVE_ITEM: have}

        def model(page=page, mx=mouse_x, my=mouse_y, item=item, have=have, fit=fit, req=requirements,
                  socket=socket, other=other, slot=slot, sx=sx, sy=sy):
            """Body-slot model: where the cursor is, then whether the item fits, equips or sockets."""
            log = []
            if page == 1:
                log.append(("mouse_x",))
                if mx < half(sx):
                    return None, log, {}, {}
            log.append(("mouse_y",))
            if my >= sy - 0x27:
                return None, log, {}, {}
            log.append(("mouse_x",))
            if mx < half(sx):
                return None, log, {SLOT: 0}, {}
            log.append(("hand_item", inventory))
            if not item or not have:
                return None, log, {}, {}
            log += [("item_size", item, None, None, FILE_ARG, LINE_ARG), ("body_fit", item, slot)]
            verdict = None
            if fit:
                log += [("player",), ("equip_req", item, player, 0, 0, 0, 0)]
                verdict = 1 if req else None
            if verdict is None:
                log += [("item_at_slot", inventory, slot), ("can_socket", item, other)]
                verdict = 1 if socket else 0
            return None, log + [("draw_body", BODY_SLOTS + 20 * slot, verdict)], {}, {}
        r.run(f"body slot page={page} slot={slot} mouse={mouse_x},{mouse_y}", 0x4844A0, (), {"eax": inventory},
              values, model,
              ret={"mouse_x": mouse_x, "mouse_y": mouse_y, "hand_item": item, "body_fit": fit, "equip_req": requirements,
                   "can_socket": socket, "item_at_slot": other, "player": player})

    # ---- FUN_00483c50: the grid placement highlight ------------------------------------------
    plain, cube = m.alloc(0x100), m.alloc(0x100)
    m.w32(plain + 0x80, 0x12345678)
    m.w32(cube + 0x80, BOX)
    unit_a, unit_b = m.alloc(0x20), m.alloc(0x20)
    class_ids = {unit_a: 0x31, unit_b: 0x32}
    for address, class_id in class_ids.items():
        m.w32(address + 4, class_id)
    for _ in range(1500):
        sx, sy, _px, _py = screen(rng)
        colour = rng.choice((0xFF, 0x00, 0x01, 0x7F, 0x1234FF, 0xABCD00))
        unit, flag, page = rng.choice((0, 0x6000, 0x6000, 0x6000)), rng.choice((0, 1, 1)), rng.choice((0, 1, 0x13, 5))
        cell_x, cell_y = rng.choice((-1, 0, 3, 9)) & M32, rng.choice((-1, 0, 2, 7)) & M32
        mouse_x = rng.choice((0, sx // 2 - 1, sx // 2, sx - 1))
        y_first = rng.choice((0, sy - 0x28, sy - 0x27, sy // 2))
        y_second = rng.choice((y_first, y_first, 0, sy - 0x27))
        grid_index, collide, occupy = rng.randrange(1, 90), rng.choice((0, 0, 1)), rng.choice((0, 0, 1))
        collided = rng.choice((0, unit_a, unit_b))
        if occupy and not collided:
            collided = unit_a
        line_ptr = rng.choice((plain, cube))
        w1, h1, w2, h2 = (rng.randrange(1, 5) for _ in range(4))
        size_of.clear()
        size_of[unit] = (w1, h1)
        size_of[collided] = (w2, h2)
        at = (rng.randrange(10), rng.randrange(8))
        coords = 0x8000 + rng.randrange(4)
        values = {SCREEN_X: sx, SCREEN_Y: sy, PAGE: page, CURSOR_ON: flag, CELL_X: cell_x, CELL_Y: cell_y}
        ys = iter((y_first, y_second))

        def model(colour=colour, unit=unit, flag=flag, cx=cell_x, cy=cell_y, page=page, mx=mouse_x,
                  ys=(y_first, y_second), sx=sx, sy=sy, gi=grid_index, collide=collide, occupy=occupy, cu=collided,
                  lp=line_ptr, w1=w1, h1=h1, w2=w2, h2=h2, at=at, coords=coords):
            """Placement-highlight model: guards, the collision test, then the occupancy test and the cube."""
            log = []
            if colour & 0xFF == 0xFF or not unit or not flag:
                return None, log, {}, {}
            log.append(("item_size", unit, None, None, FILE_ARG, LINE_ARG))
            if s32(cx) < 0 or s32(cy) < 0:
                return None, log, {}, {}
            if page == 1:
                log.append(("mouse_x",))
                if mx < half(sx):
                    return None, log, {}, {}
            log.append(("mouse_y",))
            if ys[0] >= sy - 0x27:
                return None, log, {}, {}
            if page == 0x13:
                log.append(("mouse_x",))
                if mx < half(sx):
                    return None, log, {}, {}
            log.append(("mouse_y",))
            if ys[1] >= sy - 0x27:
                return None, log, {}, {}
            log.append(("grid_collide", inventory, unit, cx, cy, gi, None, None, colour))
            if collide:
                return None, log + [("draw_solid", h1, w1, cy, coords, cx, 1)], {}, {}
            log.append(("grid_occupy", inventory, unit, cx, cy, gi, None, colour, 1))
            if not occupy:
                if cu:
                    log.append(("items_line", class_ids[cu]))
                if not cu or lp != cube:
                    return None, log + [("draw_solid", h1, w1, cy, coords, cx, 0)], {}, {}
            log.append(("item_size", cu, None, None, FILE_ARG, LINE_ARG))
            return None, log + [("y_pos", cu), ("x_pos", cu), ("draw_solid", h2, w2, at[1], coords, at[0], 3)], {}, {}

        def store_collided(e, args, cu=collided):
            """FUN_0063bb20 stores the collided unit through its sixth argument."""
            e.w32(args[5], cu)
        r.run(f"grid highlight colour={colour:#x} unit={unit:#x} page={page} collide={collide} occupy={occupy}",
              0x483C50, (inventory, coords), {"eax": grid_index, "edi": unit, "ebx": colour}, values, model,
              ret={"mouse_x": mouse_x, "mouse_y": lambda a, q=ys: next(q), "grid_collide": collide,
                   "grid_occupy": occupy, "items_line": line_ptr, "x_pos": at[0], "y_pos": at[1]},
              fx={"item_size": write_size, "grid_occupy": store_collided})

    # ---- FUN_00487000: the item-under-cursor update -------------------------------------------
    item_a, item_b, cursor_item, unit = 0x6200, 0x6300, 0x6400, 0x6000
    for _ in range(3000):
        cols, rows = rng.randrange(1, 12), rng.randrange(1, 9)
        cell_w, cell_h = rng.randrange(1, 40), rng.randrange(1, 40)
        left, top = rng.randrange(0, 300), rng.randrange(0, 200)
        m.mu.mem_write(layout, bytes([cols, rows, 0, 0]) + struct.pack("<I", left) + bytes(4) +
                       struct.pack("<I", top) + bytes(4) + bytes([cell_w, cell_h]) + bytes(10))
        cursor = rng.choice((0, 0, cursor_item))
        mx = rng.choice((left - 4, left, left + rng.randrange(0, 400), rng.randrange(0, 1000))) & M32
        my = rng.choice((top - 4, top, top + rng.randrange(0, 300), rng.randrange(0, 800))) & M32
        page, expansion, inv_page = rng.randrange(0, 20), rng.choice((0, 1)), rng.randrange(0, 4)
        hit = rng.choice((0, item_a, item_b))
        hover = rng.choice((0, item_a, item_b))
        x_at, y_at = rng.randrange(10), rng.randrange(8)
        over_w, over_h = rng.choice((0, 8, 56, 71, 100, 200)), rng.choice((0, 8, 56, 71, 100, 200))
        size_of.clear()
        for key in (item_a, item_b, cursor_item):
            size_of[key] = (rng.randrange(1, 5) if rng.random() < 0.8 else cols, rng.randrange(1, 5) if rng.random() < 0.8 else rows)

        def write_overlay(e, args, ow=over_w, oh=over_h):
            """FUN_004dbea0: store the scripted overlay width and height through its pointers."""
            e.w32(args[1], ow)
            e.w32(args[2], oh)

        def model(cursor=cursor, mx=mx, my=my, page=page, exp=expansion, ip=inv_page, hit=hit, hover=hover,
                  cols=cols, rows=rows, cw=cell_w, ch=cell_h, left=left, top=top, xp=x_at, yp=y_at, ow=over_w, oh=over_h):
            """Item-under-cursor model: hover lookup without a cursor item, cell placement with one."""
            log = [("expansion",), ("inv_page", unit, page, exp)]
            col, row = ((mx - left) & M32) // cw, ((my - top) & M32) // ch
            if not cursor:
                log.append(("unit_at_cell", inventory, col, row, None, None, ip, page))
                if not hit:
                    return None, log, {CURSOR_ON: 0, HOVER_B: 0, HOVER_A: 0}, {}
                writes = {CURSOR_ON: 1}
                if hit == hover:
                    return None, log, writes, {}
                width, height = size_of[hit]
                log += [("item_size", hit, None, None, FILE_ARG, LINE_ARG), ("x_pos", hit), ("y_pos", hit)]
                x_value = (width * cw) // 2 + cw * xp + left
                writes.update({BOX_RECT[0]: x_value, BOX_RECT[2]: x_value, BOX_RECT[1]: ch * yp + top,
                               BOX_RECT[3]: (height + yp) * ch + top, HOVER_A: hit, HOVER_B: hit})
                return None, log, writes, {}
            width, height = size_of[cursor]
            log += [("item_size", cursor, None, None, FILE_ARG, LINE_ARG), ("overlay", cursor, 0, 0, None, None)]
            if width % 2 == 0:
                col = (((ow >> 2) - left + mx) & M32) // cw
            if width == cols:
                col = cols >> 1
            if height % 2 == 0:
                row = (((oh >> 2) - top + my) & M32) // ch
            if height == rows:
                row = rows >> 1
            if width > 1:
                col = max(s32(col - (width >> 1)), 0)
                if s32(width + col) > cols:
                    return None, log, {}, {}
            if height > 1:
                row = max(s32(row - (height >> 1)), 0)
                if s32(height + row) > rows:
                    return None, log, {}, {}
            return None, log, {CELL_Y: row, CELL_X: col, CURSOR_ON: 1, HOVER_B: 0, HOVER_A: 0}, {}
        r.run(f"item under cursor cursor={cursor:#x} mouse={mx:#x},{my:#x} page={page}", 0x487000,
              (unit, inventory, cursor, mx, my, page), {"eax": layout},
              {HOVER_B: hover, CURSOR_ON: 5, HOVER_A: 6, CELL_X: 7, CELL_Y: 8}, model,
              ret={"expansion": expansion, "inv_page": inv_page, "unit_at_cell": hit, "x_pos": x_at, "y_pos": y_at},
              fx={"item_size": write_size, "overlay": write_overlay})

    print(f"ok: {r.cases} inventory-ui cases")


if __name__ == "__main__":
    main()
