# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify 31 small Quests\\aXqY.cpp / QUESTS.CPP helpers in stock game.exe 1.14d.

    uv run python quests_small.py [--mutation-check]

Runs game.exe's own code for 31 functions (the 11 per-player "quest update"
notifiers, the Horadric Staff check and notifier, the A2 / A4 / A5 data
helpers and filters, the Diablo seal glows) against a hand-written model on
random quest records, quest data blocks and units (see _dungeon_rig.py):
return value, ordered callee log (registers, then stack arguments), the whole
arena afterwards and the bytes `ret n` pops. Callees are hooked and logged,
not claimed: the player data FUN_006221a0, the quest-bit test FUN_0065c310 and
set FUN_0065c360, the quest lookup FUN_00543640, the quest-update send
FUN_00544190 and log FUN_00544300, the unit iterator FUN_005537d0, the timer
FUN_005417d0, the update-flag setter FUN_00624690 and the seal-glow maker
FUN_005b6ad0.

Claimed: which quest id and update message each notifier uses and when it
sends (it sends unless the quest is Completed/FinishedInPreviousGame without
JustCompleted/CannotComplete), the quest-record fields each helper reads and
writes, the null checks and the register conventions.
`--mutation-check` breaks the model thirteen ways and requires each to fail.
"""
import argparse
import sys
from collections import deque

import _dungeon_rig as rig_mod
from _dungeon_rig import M32, s32

MUT = set()

REC, DATA, GAME, UNIT, PC, OBJ, ARGS, BOX = 0x000, 0x100, 0x300, 0x400, 0x500, 0x600, 0x700, 0x800


def mut(name):
    """True while the model carries this deliberate defect."""
    return name in MUT


def put(img, off, value, size=4):
    """Write a little-endian `size`-byte value at arena offset `off`."""
    img[off:off + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")


def get(img, off, size=4):
    """Read a little-endian `size`-byte value at arena offset `off`."""
    return int.from_bytes(img[off:off + size], "little")


# (function, quest id, update message) of each per-player notifier.
NOTIFIERS = (
    (0x5879D0, 0x24, 0x20), (0x588EB0, 0x25, 0x21), (0x58AEA0, 0x26, 0x22), (0x58BE20, 0x27, 0x23),
    (0x58D770, 0x28, 0x24), (0x5988B0, 0x09, 0x08), (0x599F50, 0x0B, 0x0A), (0x59AD50, 0x0C, 0x0B),
    (0x59BCB0, 0x0D, 0x0C), (0x59C650, 0x0E, 0x0D), (0x5B7340, 0x11, 0x0F),
)


class Suite:
    """Builds and runs every case."""

    def __init__(self, rig):
        """Keep the rig, declare the hooked callees and their canned answers."""
        self.rig = rig
        self.plan = deque()
        self.pick = {}
        base = rig.arena
        self.base = base
        for name, addr, nstack, regs in (
                ("pcdata", 0x6221A0, 1, ()), ("qs", 0x65C310, 3, ()), ("qset", 0x65C360, 3, ()),
                ("rec", 0x543640, 0, ("ecx", "edx")), ("send", 0x544190, 1, ("ecx", "edx")),
                ("log", 0x544300, 3, ("ecx", "edx")), ("iter", 0x5537D0, 2, ("ecx", "edx")),
                ("event", 0x5417D0, 4, ("ecx", "edx")), ("flag", 0x624690, 2, ()),
                ("taint", 0x61C450, 1, ()), ("dist", 0x6416D0, 2, ()),
                ("spoken", 0x5723C0, 1, ("ecx", "edx")), ("coords", 0x620870, 2, ()),
                ("glow", 0x5B6AD0, 0, ("ebx", "esi", "edi")), ("stat", 0x6253B0, 3, ()),
                ("room", 0x620BB0, 1, ()), ("level", 0x61A1B0, 1, ()), ("act", 0x6427F0, 1, ()),
                ("reward", 0x58C5C0, 1, ("esi",))):
            rig.callee(name, addr, nstack, regs)
        rig.custom["pcdata"] = lambda e: base + PC
        rig.custom["qs"] = self.next_bit
        rig.custom["rec"] = lambda e: self.pick.get("rec", base + REC)
        rig.custom["dist"] = lambda e: self.pick["dist"]
        rig.custom["spoken"] = lambda e: self.pick["spoken"]
        rig.custom["stat"] = lambda e: self.pick["stat"]
        rig.custom["room"] = lambda e: self.pick["room"]
        rig.custom["level"] = lambda e: self.pick["level"]
        rig.custom["act"] = lambda e: self.pick["act"]

    def next_bit(self, e):
        """The planned answer of the next quest-bit test: 0 or a nonzero value."""
        bit = self.plan.popleft() if self.plan else 0
        return self.rig.rng.choice((1, 1, 2, 0x80000000)) if bit else 0

    def bits(self, count):
        """Plan the next case's answers from the case counter, so every combination recurs."""
        n = self.rig.cases % (1 << count)
        self.plan = deque((n >> i) & 1 for i in range(count))
        return n

    def run(self, name, addr, build, model, nstack, mask=M32, ret=True, twice=False):
        """Run one function through the rig (twice in a row when `twice`, to cover 32 combinations)."""
        for _ in range(2 if twice else 1):
            self.rig.run(name, addr, build, model, nstack, mask, ret)

    # ---- model pieces ------------------------------------------------------------
    def quiet(self, m, buf, q):
        """The notifier test: Completed or FinishedInPreviousGame, but neither JustCompleted nor CannotComplete."""
        if not (m.call("qs", buf, q, 0) or m.call("qs", buf, q, 0xF)):
            return mut("notify-flip")
        if m.call("qs", buf, q, 0xD):
            return mut("notify-flip")
        if not mut("notify-no-cannot") and m.call("qs", buf, q, 0xE):
            return mut("notify-flip")
        return not mut("notify-flip")

    def needs_update(self, m, game, unit, q):
        """True when the quest update must be sent: the quiet test fails (the reads FUN_006221a0 / FUN_0065c310 are logged)."""
        pc = m.call("pcdata", unit)
        diff = m.r(game + 0x6D, 1)
        return not self.quiet(m, m.r(pc + 0x10 + 4 * diff), q)

    def unit_game(self, img, rng):
        """Fill game difficulty byte and return (game, unit) addresses."""
        diff = rng.choice((0, 1, 2, 3, rng.randrange(256)))
        put(img, GAME + 0x6D, diff, 1)
        return self.base + GAME, self.base + UNIT

    # ---- the groups --------------------------------------------------------------
    def notifiers(self):
        """The 11 per-player notifiers, fastcall (game, unit, arg), `ret 4`."""
        for addr, q, msg in NOTIFIERS:
            def build(img, rng):
                """Plan the four bit answers; random game / unit / argument."""
                self.bits(4)
                game, unit = self.unit_game(img, rng)
                return {"ecx": game, "edx": unit, "stack": [rng.randrange(1 << 32)]}

            def model(m, a, q=q, msg=msg):
                """Send message `msg` unless the quiet test of quest `q` holds; always return 0."""
                if self.needs_update(m, a["ecx"], a["edx"], q):
                    m.call("send", a["ecx"], a["edx"], msg)
                return 0
            self.run(f"FUN_{addr:08x}", addr, build, model, 1)

    def horadric(self):
        """FUN_00599510 (quest 0xa test, eax = unit, stack game) and FUN_00599570 (esi = unit, edi = game)."""
        def build_check(img, rng):
            """Plan the four answers; unit in eax, game on the stack."""
            self.bits(4)
            game, unit = self.unit_game(img, rng)
            return {"regs": {"eax": unit}, "stack": [game], "game": game, "unit": unit}

        def check(m, a):
            """1 when the update is needed, else 0."""
            return int(self.needs_update(m, a["game"], a["unit"], 0xA))
        self.run("FUN_00599510", 0x599510, build_check, check, 1)

        def build_notify(img, rng):
            """Plan the answers; unit in esi, game in edi."""
            self.bits(4)
            game, unit = self.unit_game(img, rng)
            return {"regs": {"esi": unit, "edi": game}, "game": game, "unit": unit}

        def notify(m, a):
            """Send message 9 when quest 0xa needs an update; return 0."""
            if self.needs_update(m, a["game"], a["unit"], 0xA):
                m.call("send", a["game"], a["unit"], 9)
            return 0
        self.run("FUN_00599570", 0x599570, build_notify, notify, 0)

    def a5q2(self):
        """FUN_00588c50 (prisoners left) and FUN_00588ca0 (prisoner timer)."""
        def build_count(img, rng):
            """A quest record whose data block has random group flags and counters."""
            put(img, REC + 0x18, self.base + DATA)
            for off in (0x86, 0x87, 0x88):
                put(img, DATA + off, rng.choice((0, 0, 1, rng.randrange(256))), 1)
            for off in (0xA4, 0xA8, 0xAC):
                put(img, DATA + off, rng.randrange(-4, 14))
            return {"ecx": self.base + REC}

        def count(m, a):
            """5 per group not yet freed, plus total - freed - killed, clamped at 0."""
            d = m.r(a["ecx"] + 0x18)
            n = sum(5 for off in (0x86, 0x87, 0x88) if m.r(d + off, 1) == 0)
            n += s32(m.r(d + 0xA4)) - s32(m.r(d + 0xAC)) - s32(m.r(d + 0xA8))
            return n if (n >= 0 or mut("prisoners-no-clamp")) else 0
        self.run("FUN_00588c50", 0x588C50, build_count, count, 0)

        def build_timer(img, rng):
            """A prisoner unit (sometimes null, sometimes unmatched) in one of three groups."""
            put(img, REC + 0x04, self.base + GAME)
            put(img, REC + 0x18, self.base + DATA)
            put(img, GAME + 0xA8, rng.randrange(1 << 32))
            guid = rng.randrange(1 << 32)
            slot = rng.randrange(3)
            kind = rng.choice(("hit", "hit", "hit", "null", "miss"))
            unit = 0 if kind == "null" else self.base + UNIT
            if kind != "null":
                put(img, UNIT + 0xC, guid)
                put(img, UNIT + 0x10, rng.choice((0, 1, 2, 2, 2, 2, 2, 2, 3, 4, rng.randrange(1 << 32))))
            put(img, DATA + 0xEC + 4 * slot, 0xFFFFFFFF if kind == "null" else guid)
            if kind == "miss":
                put(img, DATA + 0xEC + 4 * slot, guid ^ 1)
            for i in range(3):
                put(img, DATA + 0xFC + 4 * i, rng.choice((-1, 0, 3, 4, 4, 5, 5, 6, 7)))
                put(img, DATA + 0x108 + i, rng.choice((0, 1, 1, 2)), 1)
                put(img, DATA + 0x10B + i, rng.choice((0, 0, 1, 2)), 1)
            return {"ecx": self.base + REC, "edx": unit}

        def timer(m, a):
            """Advance the matched prisoner's walk count / arrival flag, set its update flag, re-arm the event."""
            rec, unit = a["ecx"], a["edx"]
            d, game = m.r(rec + 0x18), m.r(rec + 4)
            guid = 0xFFFFFFFF if unit == 0 else m.r(unit + 0xC)
            idx = next((i for i in range(3) if m.r(d + 0xEC + 4 * i) == guid), None)
            if idx is None:
                return None
            mode = 0 if unit == 0 else m.r(unit + 0x10)
            newmode = None
            if mode == 1:
                newmode = 2
            elif mode == 2:
                if m.r(d + 0x108 + idx, 1) == 1:
                    m.w(d + 0xFC + 4 * idx, m.r(d + 0xFC + 4 * idx) + 1)
                over = s32(m.r(d + 0xFC + 4 * idx)) >= 5 if mut("walk-ge") else s32(m.r(d + 0xFC + 4 * idx)) > 5
                if over:
                    m.w(d + 0x10B + idx, 1, 1)
                if m.r(d + 0x10B + idx, 1) != 0:
                    newmode = 3
            elif mode == 3:
                newmode = 4
            if newmode is not None:
                m.call("flag", unit, newmode)
            m.call("event", game, unit, 7, (m.r(game + 0xA8) + 0x19) & M32, 0, 0)
            return None
        self.run("FUN_00588ca0", 0x588CA0, build_timer, timer, 0, ret=False)

    def predicates(self):
        """FUN_00590810, FUN_0059dfb0, FUN_00599f10, FUN_0059ac80: quest record reads."""
        def build_bool(img, rng):
            """Random introduction byte and state."""
            put(img, REC + 9, rng.choice((0, 1, 1, rng.randrange(256))), 1)
            put(img, REC + 0xC, rng.choice((0, 3, 4, 5, rng.randrange(256))), 1)
            return {"ecx": self.base + REC}

        def bool_model(m, a):
            """Introduced and state >= 4."""
            r = a["ecx"]
            return int(m.r(r + 9, 1) != 0 and m.r(r + 0xC, 1) > (4 if mut("bool-gt") else 3))
        self.run("FUN_00590810", 0x590810, build_bool, bool_model, 0, 0xFF)

        def build_game(img, rng):
            """Record found or not; random introduction byte and state."""
            self.pick["rec"] = 0 if rng.random() < 0.2 else self.base + REC
            put(img, REC + 9, rng.choice((0, 1, 1, rng.randrange(256))), 1)
            put(img, REC + 0xC, rng.choice((0, 3, 4, 5, rng.randrange(256))), 1)
            put(img, REC + 0x18, self.base + DATA)
            put(img, REC + 4, self.base + GAME)
            return {"ecx": self.base + GAME}

        def tombs(m, a):
            """False only while quest 0xd is introduced and its state is below 4."""
            r = m.call("rec", a["ecx"], 0xD)
            if mut("q14-same"):
                return int(r != 0 and m.r(r + 9, 1) != 0 and m.r(r + 0xC, 1) >= 4)
            return int(not (r != 0 and m.r(r + 9, 1) != 0 and m.r(r + 0xC, 1) < 4))
        self.run("FUN_0059dfb0", 0x59DFB0, build_game, tombs, 0, 0xFF)

        def build_info(img, rng):
            """Box count and info flag outputs in the arena; record found or not."""
            build_game(img, rng)
            put(img, DATA + 0x20, rng.randrange(1, 1 << 32))
            return {"ecx": self.base + GAME, "edx": self.base + BOX, "stack": [self.base + BOX + 8]}

        def info(m, a):
            """*box = 0 and *info = true whether or not the record exists (the data word is read and dropped)."""
            r = m.call("rec", a["ecx"], 9)
            box = a["edx"]
            m.w(box, m.r(m.r(r + 0x18) + 0x20) if (r and mut("box-count")) else 0)
            m.w(a["stack"][0], 1, 1)
            return None
        self.run("FUN_00599f10", 0x599F10, build_info, info, 1, ret=False)

        def build_sun(img, rng):
            """Caller record whose +4 is the game; data byte +4 random."""
            build_game(img, rng)
            put(img, REC + 4, self.base + GAME)
            put(img, DATA + 4, rng.randrange(256), 1)
            return {"ecx": self.base + REC}

        def sun(m, a):
            """Byte +4 of quest 0xa's data, or 0 without the record."""
            r = m.call("rec", m.r(a["ecx"] + 4), 0xA)
            return m.r(m.r(r + 0x18) + 4, 1) if r else 0
        self.run("FUN_0059ac80", 0x59AC80, build_sun, sun, 0, 0xFF)

    def a2(self):
        """FUN_0059ac40 (tainted sun init) and the Arcane Sanctuary object-operate state FUN_0059baf0."""
        def build_init(img, rng):
            """Act number in dl (1 half the time); pending byte +3 0 / 1 / other."""
            self.pick["rec"] = 0 if rng.random() < 0.2 else self.base + REC
            put(img, REC + 0x18, self.base + DATA)
            put(img, DATA + 2, rng.randrange(256), 1)
            put(img, DATA + 3, rng.choice((0, 1, 1, rng.randrange(256))), 1)
            put(img, GAME + 0xC0, rng.randrange(1 << 32))
            return {"ecx": self.base + GAME, "edx": rng.choice((1, 1, 0, 2, rng.randrange(1 << 32), 0x101))}

        def init(m, a):
            """On act 1 with the pending byte set: darken the act, clear pending, set darkened."""
            game = a["ecx"]
            if a["edx"] & 0xFF != 1:
                return None
            r = m.call("rec", game, 0xA)
            if r:
                d = m.r(r + 0x18)
                if m.r(d + 3, 1) == 1:
                    m.call("taint", m.r(game + 0xC0))
                    m.w(d + 3, 0, 1)
                    m.w(d + 2, 1, 1)
            return None
        self.run("FUN_0059ac40", 0x59AC40, build_init, init, 0, ret=False)

        def build_op(img, rng):
            """Level 0x4a / 0x36 / other; the two paired words 0 or 1 / 2."""
            self.pick["rec"] = 0 if rng.random() < 0.15 else self.base + REC
            put(img, REC + 0x18, self.base + DATA)
            put(img, DATA + 0x42, rng.choice((0, 0, 1, 2)), 2)
            put(img, DATA + 0x44, rng.choice((0, 0, 1, 2)), 2)
            return {"ecx": self.base + GAME, "edx": rng.choice((0x4A, 0x4A, 0x36, 0x36, 0, rng.randrange(1 << 32)))}

        def operate(m, a):
            """Level 0x4a: if word +0x44 is 0 set it 1 and +0x42 2; level 0x36: the mirror image."""
            r = m.call("rec", a["ecx"], 0xB)
            if not r:
                return None
            d, lvl = m.r(r + 0x18), a["edx"]
            lo, hi = (0x44, 0x42) if not mut("operate-swap") else (0x42, 0x44)
            if lvl == 0x4A:
                if m.r(d + lo, 2) == 0:
                    m.w(d + lo, 1, 2)
                    m.w(d + hi, 2, 2)
            elif lvl == 0x36:
                if m.r(d + hi, 2) == 0:
                    m.w(d + hi, 1, 2)
                    m.w(d + lo, 2, 2)
            return None
        self.run("FUN_0059baf0", 0x59BAF0, build_op, operate, 0, ret=False)

    def a4(self):
        """FUN_005b43d0, 005b4440, 005b6e20, 005b6e30, 005b6930 and the seal glows 005b6bc0 / 005b6c10."""
        def build_izual(img, rng):
            """Data block with Izual's unit and a flag byte; the distance answer around 5."""
            self.pick["dist"] = rng.choice((-3, 0, 4, 5, 6, 100, 0x80000000, rng.randrange(1 << 32)))
            put(img, DATA + 0x14, rng.randrange(1 << 32))
            put(img, DATA + 0x18, rng.randrange(256), 1)
            return {"edx": self.base + UNIT, "stack": [self.base + DATA]}

        def izual(m, a):
            """Flag +0x18 = 1 when the unit is within distance 4 (signed) of the Izual unit."""
            data = a["stack"][0]
            d = m.call("dist", a["edx"], m.r(data + 0x14))
            if s32(d) < 5:
                m.w(data + 0x18, 1, 1)
            return None
        self.run("FUN_005b43d0", 0x5B43D0, build_izual, izual, 1, ret=False)

        def build_game_only(img, rng):
            """Only the game in ecx."""
            return {"ecx": self.base + GAME}

        def sound(m, a):
            """Iterate every player or monster with the complete-sound callback 0x5b3d30."""
            m.call("iter", a["ecx"], 0, 0, 0x5B3D30)
            return None
        self.run("FUN_005b4440", 0x5B4440, build_game_only, sound, 0, ret=False)

        def build_status(img, rng):
            """Status byte pointer is the third stack argument."""
            return {"ecx": rng.randrange(1 << 32), "edx": rng.randrange(1 << 32),
                    "stack": [rng.randrange(1 << 32), rng.randrange(1 << 32), self.base + BOX]}

        def status(m, a):
            """Clear *status and report false."""
            m.w(a["stack"][2], 1 if mut("status-one") else 0, 1)
            return 0
        self.run("FUN_005b6e20", 0x5B6E20, build_status, status, 3, 0xFF)

        def build_active(img, rng):
            """Target class 0xf5 half of the time; the spoken-with answer 0 / 1 / other."""
            self.pick["spoken"] = rng.choice((0, 0, 1, 7))
            put(img, REC + 4, self.base + GAME)
            return {"ecx": self.base + REC, "edx": rng.choice((0xF5, 0xF5, 0xF4, rng.randrange(1 << 32))),
                    "stack": [self.base + UNIT, rng.randrange(1 << 32), rng.randrange(1 << 32)]}

        def active(m, a):
            """True for class 0xf5 when the player has not spoken with that NPC."""
            if a["edx"] != 0xF5:
                return 0
            r = m.call("spoken", a["stack"][0], m.r(a["ecx"] + 4), 0xF5)
            return int((r != 0) if mut("spoken-nonzero") else (r == 0))
        self.run("FUN_005b6e30", 0x5B6E30, build_active, active, 3, 0xFF)

        self.run("FUN_005b6930", 0x5B6930, lambda img, rng: {}, lambda m, a: None, 0, ret=False)

        for addr, qoff, dy, dx, tag in ((0x5B6BC0, 0x2C, 0x21, -0x27, 3), (0x5B6C10, 0x34, 0x10, 0x20, 5)):
            def build_glow(img, rng):
                """Operate arg {game, victim}; victim null / dead / alive; record found or not."""
                self.pick["rec"] = 0 if rng.random() < 0.2 else self.base + REC
                put(img, REC + 0x18, self.base + DATA)
                put(img, ARGS, self.base + GAME)
                kind = rng.choice(("null", "dead", "dead", "alive"))
                put(img, ARGS + 4, 0 if kind == "null" else self.base + UNIT)
                put(img, UNIT + 0x10, 0 if kind == "dead" else rng.randrange(1, 9))
                return {"ecx": self.base + ARGS}

            def model(m, a, qoff=qoff, dy=dy, dx=dx, tag=tag):
                """Dead or missing victim: add the seal offset to the stored coordinates and make the glow."""
                game, victim = m.r(a["ecx"]), m.r(a["ecx"] + 4)
                if victim and m.r(victim + 0x10) != 0:
                    return 0
                r = m.call("rec", game, 0x17)
                if not r:
                    return 0
                d = m.r(r + 0x18)
                m.call("coords", victim, d + qoff)
                m.w(d + qoff + 4, m.r(d + qoff + 4) + dy - (1 if mut("glow-y") else 0))
                m.w(d + qoff, m.r(d + qoff) + dx)
                m.call("glow", a["ecx"], d + qoff, tag)
                return 0
            self.run(f"FUN_{addr:08x}", addr, build_glow, model, 0)

    def a5(self):
        """FUN_0058d640 (summit door init), FUN_005913c0 (A1 Charsi event) and FUN_0058c6d0 (Rite of Passage party completion)."""
        def build_door(img, rng):
            """Init arg {game, object}; mode word +0x60, ancients flag byte +0."""
            put(img, ARGS, self.base + GAME)
            put(img, ARGS + 4, 0 if rng.random() < 0.25 else self.base + OBJ)
            put(img, OBJ + 0xC, rng.randrange(1 << 32))
            put(img, REC + 0x18, self.base + DATA)
            put(img, DATA, rng.choice((0, 0, 1, 2)), 1)
            put(img, DATA + 0x60, rng.choice((0, 1, 2, 2, 3)))
            return {"ecx": self.base + ARGS}

        def door(m, a):
            """Mark initialised, store the object id (-1 for none), push its mode, force mode 2 while the ancients live."""
            game, obj = m.r(a["ecx"]), m.r(a["ecx"] + 4)
            r = m.call("rec", game, 0x23)
            d = m.r(r + 0x18)
            m.w(d + 0x58, 1, 1)
            m.w(d + 0x5C, 0xFFFFFFFF if obj == 0 else m.r(obj + 0xC))
            m.call("flag", obj, m.r(d + 0x60))
            if m.r(d, 1) == 0 and m.r(d + 0x60) != 2:
                m.w(d + 0x60, 2)
                m.call("flag", obj, 1)
            return None
        self.run("FUN_0058d640", 0x58D640, build_door, door, 0, ret=False)

        def build_charsi(img, rng):
            """Event args whose target (Charsi 0x9a or not, sometimes none) is at +8; data bytes +2 / +3."""
            put(img, REC + 4, self.base + GAME)
            put(img, REC + 0x18, self.base + DATA)
            put(img, REC + 0x14, rng.randrange(1, 256), 1)
            kind = rng.choice(("charsi", "charsi", "charsi", "other", "none"))
            put(img, ARGS + 8, 0 if kind == "none" else self.base + OBJ)
            put(img, OBJ + 4, 0x9A if kind == "charsi" else rng.randrange(0x10000), 2)
            put(img, DATA + 2, rng.choice((0, 1, 1, 2)), 1)
            put(img, DATA + 3, rng.choice((0, 1, 1, 2)), 1)
            return {"ecx": self.base + REC, "edx": self.base + ARGS}

        def charsi(m, a):
            """On Charsi: byte +2 broadcasts state 1 after iterating players, else byte +3 broadcasts state 0xd."""
            rec, args = a["ecx"], a["edx"]
            target = m.r(args + 8)
            if target == 0 or m.r(target + 4, 2) != 0x9A:
                return None
            game, d = m.r(rec + 4), m.r(rec + 0x18)
            if m.r(d + 2, 1) == 1:
                m.call("iter", game, 0, 0, 0x591340)
                m.w(rec + 0x14, 0, 1)
                m.call("log", rec, 1, 0, 0x5912E0, 1)
                m.w(d + 2, 0, 1)
            elif m.r(d + 3, 1) == 1:
                m.w(rec + 0x14, 0, 1)
                m.call("log", rec, 0xD, 0, 0x5912E0, 1)
                m.w(d + 3, 0, 1)
            return None
        self.run("FUN_005913c0", 0x5913C0, build_charsi, charsi, 0, ret=False)

        def build_party(img, rng):
            """Plan the answers; the level threshold and every following answer chosen from the case number."""
            self.bits(1)
            game, unit = self.unit_game(img, rng)
            diff = get(img, GAME + 0x6D, 1)
            thr = (diff + 1) * 20
            r = self.rig.cases
            self.pick["stat"] = (thr - 1, thr, thr + 1, rng.randrange(1 << 32))[(r >> 1) % 4] & M32
            self.pick["room"] = (0, self.base + OBJ)[(r >> 3) & 1]
            self.pick["level"] = (0, 0x2D)[(r >> 4) & 1]
            self.pick["act"] = rng.choice((4, 4, 0x104, 3, 5, 0, rng.randrange(1 << 32)))
            return {"ecx": game, "edx": unit, "stack": [rng.randrange(1 << 32)], "diff": diff}

        def party(m, a):
            """Complete quest 0x27 and give the reward for a member of level >= (difficulty+1)*20 standing in act 5."""
            game, unit = a["ecx"], a["edx"]
            pc = m.call("pcdata", unit)
            diff = m.r(game + 0x6D, 1)
            buf = m.r(pc + 0x10 + 4 * diff)
            if m.call("qs", buf, 0x27, 0):
                return None
            lvl = s32(m.call("stat", unit, 0xC, 0))
            thr = (diff + 1) * 20
            if lvl < thr or (mut("level-le") and lvl <= thr):
                return None
            room = m.call("room", unit)
            if room == 0:
                return None
            lv = m.call("level", room)
            if lv == 0:
                return None
            act = m.call("act", lv)
            if (act if mut("act-dword") else act & 0xFF) != 4:
                return None
            m.call("qset", buf, 0x27, 0)
            m.call("qset", buf, 0x27, 0xD)
            m.call("reward", unit, game)
            return None
        self.run("FUN_0058c6d0", 0x58C6D0, build_party, party, 1, ret=False, twice=True)


def suite(rig):
    """Run every group; returns the number of cases."""
    s = Suite(rig)
    s.notifiers()
    s.horadric()
    s.a5q2()
    s.predicates()
    s.a2()
    s.a4()
    s.a5()
    return rig.cases


def _mutation(name):
    """A setter that adds / removes `name` from the active deliberate defects."""
    return lambda on: MUT.add(name) if on else MUT.discard(name)


MUTATIONS = {name: _mutation(name) for name in (
    "notify-no-cannot", "notify-flip", "bool-gt", "q14-same", "prisoners-no-clamp", "walk-ge",
    "box-count", "glow-y", "level-le", "act-dword", "operate-swap", "status-one", "spoken-nonzero")}


def main():
    """Parse the flag and run the suite or the mutation check."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, MUTATIONS, "Quests small-helper", args.mutation_check)


if __name__ == "__main__":
    sys.exit(main())
