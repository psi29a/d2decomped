# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify small UNIT\\SUnit.cpp helpers and UNIT\\Party.cpp's GetPartyIdFromUnit in stock game.exe 1.14d.

    uv run python unit_sunit_small.py [--mutation-check]

Runs game.exe's own code on fake units (0x100-byte blocks with the fields the model reads
set) and checks each call against a model written from the disassembly (not just the
decompile, which mis-reads two of these: the stack-slot reuse in UNITS_CheckSkillTargetAlignment,
and a `(false)` literal in SKILL_CanUnitTargetAlly that is really dead code the compiler kept -
both sides of the earlier type gate are already known equal there): the return value (EAX; a
void routine is only required to return), every byte of the unit blocks after the call, the
ordered log of the hooked callees (registers and stack arguments) and the bytes `ret n`
popped. A fatal error (the line passed to 0x40_8a60) is a "halt" with that line; 0x40_8090
itself runs for real. GetPartyIdFromUnit is exercised both directly and for real as
SKILL_CanUnitTargetAlly's own callee (its party-control structures are built in memory, not
scripted), so the two-player path is a genuine two-real-function chain, not a stub.

Unit fields used (docs/research/re/structs.tsv and D2UnitDataServerSide, Union_0x64/SERVER):
+0 type, +0xc id, +0x60 inventory, +0x64 server-side dwInteractGUID, +0x68 dwInteractType,
+0x6c bInteracting, +0x78 bSparkChest, +0x80 pGame. Monster data (D2UnitDataMonsterStrc,
pointed at by unit+0x14): +0x30 pMonsterAi. SGAMEDATA +0x1d2c pPartyControl; its sub record
+0 nPartyId (word), +4 pSub (first member node), +8 pPrev (next sub record); a member node
+0 nUnitGUID, +4 pNext.

Covered: FUN_005540a0 (SetSparklyChestFlag), FUN_005542c0 (UNITS_CheckSkillTargetAlignment),
FUN_005544b0 (SetUninterruptableState), FUN_00554d20 (UNITS_CanUseSkillOnTarget),
FUN_00554de0 (SKILL_CanUnitTargetAlly), FUN_00555500 (ClearInteractOnFreePlayer),
FUN_00540710 (GetPartyIdFromUnit).

`GetMonsterNameAndAi` (0x58f090), `TXT_Skills_GetLine` (0x45c4b0), `SetState` (0x639db0),
`UNIT_GetUnitState` (0x639df0), `GetKillerUnit` (0x553010), `SERVER_KillMonster` (0x57ccb0),
`SERVER_GetHitCalculation` (0x57cee0), `EVENT_RemoveUnitTimersByType` (0x540e60),
`FindUnitFromTable` (0x552f60), `GetPartyId` (0x554630), `GetOwnerUnit` (0x58f0d0),
`IsAllyOf` (0x650d70), `FreeSpecificAiForUnit` (0x572e00), `SUNITPROXY_RemovePlayerFromNpc`
(0x536b50), `OBJMODE_OnWaypointFinish` (0x585550) and `INVENTORY_GetCursorItemGUID` (0x63d450)
are callees hooked and logged, not claimed here; `--mutation-check` breaks the model on
purpose, one defect at a time, and requires each to fail.

GetPartyIdFromUnit's own null-pUnit branch (sets EAX=-1 then falls straight into reading
`pUnit->pInventory` without testing again) is not exercised: every real caller already
rejects a null unit first, and testing it here would just fault the emulator the same way a
literal null pointer would fault game.exe.
"""
import random
import struct
import sys

import _monster_world as mw
from _monster_world import require
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

MASK = 0xFFFFFFFF
ASSERT = 0x40_8A60
MUTATIONS = (
    "sparkly-wrong-offset", "align-skip-player-gate", "align-skip-skill-gate", "align-ai-not-zero-ok",
    "align-guid-wrong", "uninterrupt-skips-setstate", "uninterrupt-ignores-game-null",
    "uninterrupt-keeps-deathdelay", "uninterrupt-killer-swapped", "uninterrupt-aithink-wrong-type",
    "uninterrupt-aithink-wrong-arg", "can-use-skip-type-gate", "can-use-mask-ignored",
    "can-use-monster-name-fail-ok", "can-use-monster-ai-nonplayer-ok", "can-use-party-ffff-ok",
    "can-use-found-null-ok", "can-use-party-mismatch-ok", "ally-same-unit-false", "ally-owner-not-resolved",
    "ally-nonplayer-skips-isallyof", "ally-party-ffff-ok", "ally-party-mismatch-ok", "interact-skips-gate",
    "interact-monster-returns-early", "interact-object-no-return", "interact-wrong-reset",
    "interact-ai-offset-wrong", "party-prev-is-next", "party-guid-field-wrong",
)
mutation = None
CASES = 0
CALLED = set()
COVERED = (0x55_40A0, 0x55_42C0, 0x55_44B0, 0x55_4D20, 0x55_4DE0, 0x55_5500, 0x54_0710)


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Halt(Exception):
    """game.exe took the fatal-error path; args[0] is the source line passed to it."""


class Rig(mw.World):
    """The emulated game with the fatal path and every callee of the covered routines hooked."""

    def __init__(self):
        """Hook the assert and every callee; script keys are named after the hook."""
        super().__init__()
        mw.INSTRUCTION_LIMIT = 400000
        self.m.hook(ASSERT, self.fatal, 0)
        self.script = {}
        lg = self.logger
        lg(0x45_C4B0, 0, "skills-row", ret=lambda c: self.script.get("skills-row", 0), regs=("ecx",))
        lg(0x63_9DB0, 3, "set-state", regs=())
        lg(0x63_9DF0, 2, "get-state", ret=lambda c: self.script.get("get-state", 0), regs=())
        lg(0x55_3010, 0, "get-killer", ret=lambda c: self.script.get("get-killer", 0), regs=("ecx", "edx"))
        lg(0x57_CCB0, 2, "kill-monster", regs=("ecx", "edx"))
        lg(0x54_0E60, 2, "remove-timers", regs=("ecx", "edx"))
        lg(0x55_2F60, 1, "find-unit", ret=lambda c: self.script.get("find-unit", {}).get(
            (c.mu.reg_read(UC_X86_REG_ECX), c.mu.reg_read(UC_X86_REG_EDX), c.arg(0)), 0), regs=("ecx", "edx"))
        lg(0x55_4630, 0, "party-id", ret=lambda c: self.script.get("party-id", {}).get(
            c.mu.reg_read(UC_X86_REG_ECX), 0), regs=("ecx",))
        lg(0x58_F0D0, 0, "get-owner", ret=lambda c: self.script.get("get-owner", {}).get(
            c.mu.reg_read(UC_X86_REG_ECX), 0), regs=("ecx",))
        lg(0x65_0D70, 2, "is-ally", ret=lambda c: self.script.get("is-ally", 0), regs=())
        lg(0x57_2E00, 1, "free-ai", regs=("ecx", "edx"))
        lg(0x53_6B50, 1, "proxy-remove", regs=("ecx", "edx"))
        lg(0x58_5550, 0, "waypoint-finish", regs=("ecx", "edx"))
        lg(0x63_D450, 1, "cursor-guid", ret=lambda c: self.script.get("cursor-guid", 0), regs=())
        self.m.hook(0x58_F090, self.gmnai, 1)
        self.m.hook(0x57_CEE0, self.hit_calc, 2)

    def gmnai(self, e):
        """GetMonsterNameAndAi (ECX unit, EDX &name-out, stack0 &ai-out): scripted (name, ai_or_None)."""
        unit = e.mu.reg_read(UC_X86_REG_ECX)
        name_ptr = e.mu.reg_read(UC_X86_REG_EDX)
        ai_ptr = e.arg(0)
        self.log.append(("gmnai", unit))
        name_val, ai_val = self.script.get("gmnai", (MASK, None))
        e.w32(name_ptr, name_val)
        if ai_val is not None:
            e.w32(ai_ptr, ai_val)
        return 0

    def hit_calc(self, e):
        """SERVER_GetHitCalculation (ECX pGame, EDX killer, stack0 pUnit, stack1 &sDamage): logs the first 3."""
        pgame = e.mu.reg_read(UC_X86_REG_ECX)
        killer = e.mu.reg_read(UC_X86_REG_EDX)
        unit = e.arg(0)
        self.log.append(("get-hit-calc", pgame, killer, unit))
        return self.script.get("get-hit-calc", 0)

    def fatal(self, current):
        """0x40_8a60 (message, caller address, line): the line is the third argument."""
        raise Halt(current.arg(2))

    def go(self, address, *args, ecx=0, edx=0, regs=None, nstack=None):
        """One call; ("halt", line) when the fatal path ran, else ("ret", eax)."""
        CALLED.add(address)
        try:
            return ("ret", self.run(address, *args, ecx=ecx, edx=edx, regs=regs, nstack=nstack))
        except Halt as halt:
            return ("halt", halt.args[0])


def verify(rig, label, got, want, want_log, *scenes):
    """Compare the result (want None: a void routine, only "ret"), the callee log and every block."""
    global CASES
    CASES += 1
    if want is None:
        require(got[0] == "ret", f"{label}: game.exe {got}, model returns")
    else:
        require(got == want, f"{label}: game.exe {got}, model {want}")
    require(rig.log == want_log, f"{label}: callee log {rig.log} != {want_log}")
    for sc in scenes:
        sc.check(label)


class Scene:
    """Random blocks in emulator memory and a model copy of each, for whole-block comparison."""

    def __init__(self, rig, rng):
        """Start empty, bound to this rig and random stream."""
        self.rig, self.rng, self.blocks = rig, rng, {}

    def block(self, size):
        """A block of random bytes; its model copy is the same bytes."""
        address = self.rig.m.alloc(size + 64)
        data = bytearray(self.rng.randbytes(size))
        self.rig.write(address, data)
        self.blocks[address] = data
        return address

    def r(self, address):
        """Model dword at address (inside one of the blocks)."""
        base = max(a for a in self.blocks if a <= address)
        return struct.unpack_from("<I", self.blocks[base], address - base)[0]

    def w(self, address, value, size=4):
        """Model write of `size` bytes."""
        base = max(a for a in self.blocks if a <= address)
        fmt = {1: "<B", 4: "<I"}[size]
        struct.pack_into(fmt, self.blocks[base], address - base, value & ((1 << 8 * size) - 1))

    def put(self, address, value, size=4):
        """Set both the emulator and the model (set-up, before the call)."""
        self.w(address, value, size)
        self.rig.write(address, struct.pack({1: "<B", 4: "<I"}[size], value & ((1 << 8 * size) - 1)))

    def check(self, label):
        """Every block in emulator memory equals the model's."""
        for address, data in self.blocks.items():
            got = self.rig.m.read(address, len(data))
            if got != bytes(data):
                where = next(i for i in range(len(data)) if got[i] != data[i])
                raise mw.Mismatch(f"{label}: block {address:#x} differs at byte {where:#x}")


def rand32(rng):
    """A dword drawn to hit zero, small, negative and large values."""
    return rng.choice((0, 1, 2, 7, 0xFF, 0x100, MASK, 0x7FFFFFFF, 0x80000000, rng.randrange(1 << 32)))


def unit_of(rig, rng, kind=None):
    """A scene with one random 0x100-byte unit block (type `kind` when given)."""
    sc = Scene(rig, rng)
    sc.unit = sc.block(0x100)
    if kind is not None:
        sc.put(sc.unit, kind)
    return sc


def halts(rig, label, address, line, *args, ecx=0, edx=0, regs=None, want_log=()):
    """A call makes the routine call the fatal path with `line`."""
    global CASES
    CASES += 1
    got = rig.go(address, *args, ecx=ecx, edx=edx, regs=regs)
    require(got == ("halt", line), f"{label}: game.exe {got}, model halt {line:#x}")
    require(rig.log == list(want_log), f"{label}: unexpected callee {rig.log}")


def check_sparkly(rig, rng):
    """FUN_005540a0 SetSparklyChestFlag: null unit halts at 0xa47, else unit+0x78 := EDX's low byte."""
    for _ in range(20):
        sc = unit_of(rig, rng)
        value = rand32(rng)
        offset = 0x7C if mut("sparkly-wrong-offset") else 0x78
        sc.w(sc.unit + offset, value & 0xFF, 1)
        got = rig.go(0x55_40A0, ecx=sc.unit, edx=value)
        verify(rig, "sparkly", got, None, [], sc)
    halts(rig, "sparkly null", 0x55_40A0, 0xA47, ecx=0, edx=rand32(rng))


def check_alignment(rig, rng):
    """FUN_005542c0 UNITS_CheckSkillTargetAlignment: see the module docstring for the real control flow.

    Gate: pUnit and pTarget non-null, and (pUnit is a player or pTarget is a monster), and
    0 <= skill < nTxtSkillsCount (DataTables +0xb98 count +0xba0 table), and the row's byte at
    +7 has the fixed bit at 0x6ce26c set. Then GetMonsterNameAndAi(pTarget) writes its owner
    GUID into what was the skill slot and (only on success) its owner type into what was the
    pTarget slot (else that slot keeps pTarget's own pointer, which is never 0). Result 1 only
    when that "AI" slot is 0 (owner is a player) and the "name" slot equals pUnit's own GUID.
    This function has no fatal path.
    """
    mask = rig.m.read(0x6C_E26C, 1)[0]
    table_ptr, table_base = 0x75_1000, 0x75_3000
    for _ in range(100):
        sc = Scene(rig, rng)
        sc.unit, sc.target = sc.block(0x100), sc.block(0x100)
        sc.put(sc.unit, rng.choice((0, 0, 1, 2)))
        sc.put(sc.target, rng.choice((0, 1, 1, 2)))
        sc.put(sc.unit + 0xC, rand32(rng))
        skill = rng.choice((-1, 0, 1, 5, 7, 9))
        row_flag_hit = rng.random() < 0.6
        rig.write(0x74_4304, struct.pack("<I", table_ptr))
        rig.write(table_ptr + 0xB98, struct.pack("<I", table_base))
        rig.write(table_ptr + 0xBA0, struct.pack("<i", 8))
        row = (table_base + skill * 0x23C) & MASK if 0 <= skill < 8 else 0
        if row:
            rig.write(row + 7, bytes([mask if row_flag_hit else 0]))
        succeed = rng.random() < 0.5
        owner_type = 0 if (succeed and rng.random() < 0.6) else rng.choice((1, 2, 3))
        owner_guid = sc.r(sc.unit + 0xC) if (succeed and rng.random() < 0.7) else rand32(rng)
        rig.script["gmnai"] = (owner_guid, owner_type) if succeed else (MASK, None)

        gate = (sc.r(sc.unit) == 0) or (sc.r(sc.target) == 1)
        if mut("align-skip-player-gate"):
            gate = True
        skill_gate = 0 <= skill and bool(row) and (row_flag_hit or mut("align-skip-skill-gate"))
        want_log = []
        if not (gate and skill_gate):
            result = 0
        else:
            want_log = [("gmnai", sc.target)]
            if succeed:
                ai_slot, name_slot = owner_type, owner_guid
            else:
                ai_slot, name_slot = sc.target, MASK
            ai_ok = (ai_slot == 0) or mut("align-ai-not-zero-ok")
            name_ok = (name_slot == sc.r(sc.unit + 0xC)) or mut("align-guid-wrong")
            result = 1 if (ai_ok and name_ok) else 0
        got = rig.go(0x55_42C0, sc.target, skill & MASK, ecx=0, edx=sc.unit, nstack=2)
        verify(rig, "alignment", got, ("ret", result), want_log, sc)


def check_uninterrupt(rig, rng):
    """FUN_005544b0 SetUninterruptableState: SetState first, then the null check, pGame gate, death-delay kill."""
    for _ in range(80):
        sc = unit_of(rig, rng, rng.choice((0, 1, 1, 2)))
        bset = rng.choice((0, 1))
        game = rng.choice((0, 0x7A_0000))
        sc.put(sc.unit + 0x80, game)
        want_log = [] if mut("uninterrupt-skips-setstate") else [("set-state", sc.unit, 0x36, bset)]
        rig.script["get-state"] = rng.choice((0, 1))
        rig.script["get-killer"] = rng.choice((0, 0x7A_1000))
        rig.script["kill-monster"] = rand32(rng)
        rig.script["get-hit-calc"] = rand32(rng)
        if game and not mut("uninterrupt-ignores-game-null"):
            if bset == 0:
                want_log.append(("get-state", sc.unit, 0x5C))
                if rig.script["get-state"] and not mut("uninterrupt-keeps-deathdelay"):
                    want_log.append(("set-state", sc.unit, 0x5C, 0))
                    killer = rig.script["get-killer"]
                    want_log.append(("get-killer", game, sc.unit))
                    if (not killer) ^ mut("uninterrupt-killer-swapped"):
                        want_log.append(("kill-monster", game, sc.unit, 0, 1))
                    else:
                        want_log.append(("get-hit-calc", game, killer, sc.unit))
            monster = sc.r(sc.unit) == (1 if not mut("uninterrupt-aithink-wrong-type") else 2)
            if monster:
                etype = 3 if mut("uninterrupt-aithink-wrong-arg") else 2
                want_log.append(("remove-timers", game, sc.unit, etype, 0))
        verify(rig, "uninterrupt", rig.go(0x55_44B0, ecx=sc.unit, edx=bset), None, want_log, sc)
    halts(rig, "uninterrupt null", 0x55_44B0, 0xA29, ecx=0, edx=0, want_log=[("set-state", 0, 0x36, 0)])


def skills_row(rig, present, flag_hit, mask_byte):
    """A fake D2SkillsTxt row's address, scripted on 'skills-row'; flags byte at +7 carries the mask bit."""
    if not present:
        rig.script["skills-row"] = 0
        return
    row = rig.m.alloc(0x240)
    rig.write(row + 7, bytes([mask_byte if flag_hit else 0]))
    rig.script["skills-row"] = row


def check_can_use(rig, rng):
    """FUN_00554d20 UNITS_CanUseSkillOnTarget: see the module docstring for the real control flow.

    Gate: caster is a player, target is a player or monster, Skills.txt row exists and its
    byte at +7 has the fixed bit at 0x6ce270 set. A monster target is resolved to its owner
    via GetMonsterNameAndAi (must succeed with a player owner); a player target uses its own
    GUID/type. Then the caster's party id must not be 0xffff, FindUnitFromTable must resolve
    the (owner-or-target) GUID, and the two party ids must match.
    """
    mask = rig.m.read(0x6C_E270, 1)[0]
    game = 0x7A_2000
    for _ in range(120):
        sc = Scene(rig, rng)
        sc.unit, sc.target = sc.block(0x100), sc.block(0x100)
        sc.put(sc.unit, rng.choice((0, 0, 1)))
        target_type = rng.choice((0, 1, 1, 2))
        sc.put(sc.target, target_type)
        sc.put(sc.target + 0xC, rand32(rng))
        present = rng.random() < 0.9
        flag_hit = rng.random() < 0.6
        skills_row(rig, present, flag_hit, mask)
        rig.script["find-unit"] = {}
        rig.script["party-id"] = {}
        succeed = rng.random() < 0.5
        owner_guid = rand32(rng)
        owner_type = 0 if (succeed and rng.random() < 0.6) else rng.choice((1, 2))
        rig.script["gmnai"] = (owner_guid, owner_type) if succeed else (MASK, None)
        caster_pid = rng.choice((0xFFFF, 1, 2, 5))
        rig.script["party-id"][sc.unit] = caster_pid
        target_guid = owner_guid if target_type == 1 else sc.r(sc.target + 0xC)
        target_owner_type = owner_type if target_type == 1 else 0
        found = rng.choice((0, 0x7A_3000))
        if found:
            rig.script["find-unit"][(game, target_owner_type, target_guid)] = found
            rig.script["party-id"][found] = caster_pid if rng.random() < 0.4 else rng.choice((0xFFFF, 1, 2, 9))

        gate = bool(sc.unit) and bool(sc.target) and sc.r(sc.unit) == 0 and \
            (target_type in (0, 1) or mut("can-use-skip-type-gate"))
        want_log, result = [], 0
        if gate:
            want_log.append(("skills-row", 3))
            row = rig.script["skills-row"]
            flagged = row and (rig.m.read(row + 7, 1)[0] & mask) != 0
            if row and (flagged or mut("can-use-mask-ignored")):
                if target_type == 1:
                    want_log.append(("gmnai", sc.target))
                    if succeed:
                        failed, bad_owner = (owner_guid == MASK), (owner_type != 0)
                    else:
                        failed, bad_owner = True, False
                    ok = (not failed or mut("can-use-monster-name-fail-ok")) and \
                         (not bad_owner or mut("can-use-monster-ai-nonplayer-ok"))
                    guid, otype = owner_guid, owner_type
                else:
                    ok, guid, otype = True, sc.r(sc.target + 0xC), 0
                if ok:
                    want_log.append(("party-id", sc.unit))
                    if caster_pid != 0xFFFF or mut("can-use-party-ffff-ok"):
                        want_log.append(("find-unit", game, otype, guid))
                        got_found = rig.script["find-unit"].get((game, otype, guid), 0)
                        if got_found or mut("can-use-found-null-ok"):
                            want_log.append(("party-id", got_found))
                            target_pid = rig.script["party-id"].get(got_found, 0)
                            result = 1 if (caster_pid == target_pid or mut("can-use-party-mismatch-ok")) else 0
        got = rig.go(0x55_4D20, sc.target, 3, ecx=game, edx=sc.unit, nstack=2)
        verify(rig, "can-use", got, ("ret", result), want_log, sc)


def build_party_control(rig, groups):
    """A pPartyControl block from `[(party_id, [guid, ...]), ...]`, or 0 for an empty list."""
    if not groups:
        return 0
    subs = []
    for party_id, members in groups:
        subsub_head = 0
        for guid in reversed(members):
            node = rig.m.alloc(8)
            rig.write(node, struct.pack("<II", guid & MASK, subsub_head))
            subsub_head = node
        sub = rig.m.alloc(12)
        rig.write(sub, struct.pack("<H", party_id & 0xFFFF))
        rig.write(sub + 4, struct.pack("<I", subsub_head))
        rig.write(sub + 8, struct.pack("<I", 0))
        subs.append(sub)
    for i in range(len(subs) - 1):
        rig.write(subs[i] + 8, struct.pack("<I", subs[i + 1]))
    control = rig.m.alloc(8)
    rig.write(control + 4, struct.pack("<I", subs[0]))
    return control


def check_party(rig, rng):
    """FUN_00540710 GetPartyIdFromUnit: halts 0x27b (not a player) / 0x27c (no game); else walks
    pGame->pPartyControl (+0x1d2c) sub records (pPrev chain) and their member list (pNext chain)
    for a GUID match, searching the unit's own GUID unless it has an inventory (then the cursor
    item's GUID, via INVENTORY_GetCursorItemGUID). Not exercised: a literal null pUnit (see the
    module docstring)."""
    for _ in range(150):
        sc = Scene(rig, rng)
        sc.unit = sc.block(0x100)
        sc.put(sc.unit, 0)
        guid = rand32(rng)
        sc.put(sc.unit + 0xC, guid)
        has_inv = rng.random() < 0.3
        sc.put(sc.unit + 0x60, sc.block(0x10) if has_inv else 0)
        rig.script["cursor-guid"] = rand32(rng)
        search = rig.script["cursor-guid"] if has_inv else guid
        n_groups = rng.randrange(0, 4)
        hit_group = rng.randrange(n_groups) if n_groups and rng.random() < 0.6 else -1
        groups = []
        for i in range(n_groups):
            party_id = rng.choice((0, 1, 300, 0x7FFF))
            members = [rand32(rng) for _ in range(rng.randrange(1, 3))]
            if i == hit_group:
                members[rng.randrange(len(members))] = search
            groups.append((party_id, members))
        # The real walk scans groups and members in build order; a random member can collide with
        # `search` by chance (rand32 draws from a small set), so find the first match for real,
        # rather than trusting only the deliberately-planted hit_group.
        group_order = groups if not mut("party-prev-is-next") else list(reversed(groups))
        effective_search = search if not mut("party-guid-field-wrong") else search ^ 1
        want_id = next((pid for pid, members in group_order if effective_search in members), 0xFFFF)
        control = build_party_control(rig, groups)
        game = sc.block(0x2000)
        sc.put(game + 0x1D2C, control)
        want_log = [("cursor-guid", sc.r(sc.unit + 0x60))] if has_inv else []
        got = rig.go(0x54_0710, ecx=game, edx=sc.unit)
        got = (got[0], got[1] & 0xFFFF) if got[0] == "ret" else got  # only AX is the real int16_t result
        verify(rig, "party-id-from-unit", got, ("ret", want_id & 0xFFFF), want_log, sc)

    # Two deterministic cases, forced rather than left to random chance: a random multi-group
    # match (needed for party-prev-is-next) or a member equal to the wrong field's value (needed
    # for party-guid-field-wrong) are both too rare to trust the 150 random cases above to hit.
    sc = Scene(rig, rng)
    sc.unit = sc.block(0x100)
    sc.put(sc.unit, 0)
    sc.put(sc.unit + 0xC, 0x1234)
    sc.put(sc.unit + 0x60, 0)
    groups = [(111, [0x1234]), (222, [0x1234])]
    control = build_party_control(rig, groups)
    game = sc.block(0x2000)
    sc.put(game + 0x1D2C, control)
    group_order = groups if not mut("party-prev-is-next") else list(reversed(groups))
    predicted = next((pid for pid, members in group_order if 0x1234 in members), 0xFFFF)
    got = rig.go(0x54_0710, ecx=game, edx=sc.unit)
    got = (got[0], got[1] & 0xFFFF) if got[0] == "ret" else got
    verify(rig, "party-id-from-unit multi-group order", got, ("ret", predicted & 0xFFFF), [], sc)

    sc = Scene(rig, rng)
    sc.unit = sc.block(0x100)
    sc.put(sc.unit, 0)
    sc.put(sc.unit + 0xC, 0x1234)
    sc.put(sc.unit + 0x60, 0)
    control = build_party_control(rig, [(333, [0x1234])])
    game = sc.block(0x2000)
    sc.put(game + 0x1D2C, control)
    effective_search = 0x1234 if not mut("party-guid-field-wrong") else (0x1234 ^ 1)
    predicted = 333 if effective_search == 0x1234 else 0xFFFF
    got = rig.go(0x54_0710, ecx=game, edx=sc.unit)
    got = (got[0], got[1] & 0xFFFF) if got[0] == "ret" else got
    verify(rig, "party-id-from-unit guid field", got, ("ret", predicted & 0xFFFF), [], sc)

    sc = unit_of(rig, rng, rng.choice((1, 2)))
    inv = rng.choice((0, sc.block(0x10)))
    sc.put(sc.unit + 0x60, inv)
    rig.script["cursor-guid"] = rand32(rng)
    want_log = [("cursor-guid", inv)] if inv else []
    halts(rig, "party not player", 0x54_0710, 0x27B, ecx=0x7A_9000, edx=sc.unit, want_log=want_log)

    sc = unit_of(rig, rng, 0)
    sc.put(sc.unit + 0x60, 0)
    halts(rig, "party null game", 0x54_0710, 0x27C, ecx=0, edx=sc.unit)


def check_ally(rig, rng):
    """FUN_00554de0 SKILL_CanUnitTargetAlly: see the module docstring for the real control flow.

    Monsters are resolved to their owner first; the same unit (after resolution) is always an
    ally. Otherwise, if both sides are players, same non-0xffff GetPartyIdFromUnit party id
    (or no game -> 0xffff on either side) decides; else IsAllyOf(resolved pUnit, resolved pOther).
    """
    def run_case(kind_u, kind_o, force_same_owner=False):
        """One case; `force_same_owner` makes both sides monsters owned by the one allocated
        player unit, the only way to reach "same unit after resolution" (every Scene block is
        a fresh, unique address, so it never happens by random chance)."""
        sc = Scene(rig, rng)
        sc.unit, sc.other = sc.block(0x100), sc.block(0x100)
        sc.put(sc.unit, kind_u)
        sc.put(sc.other, kind_o)
        rig.script["get-owner"] = {}
        want_log = []
        shared_owner = None
        if force_same_owner:
            shared_owner = sc.block(0x100)
            # Type 2 (not a player): when mutated past the same-unit shortcut, this reaches the
            # IsAllyOf fallback (which logs a call real game.exe never makes), a log-shape
            # mismatch rather than one that depends on the "both players" branch's own RNG draws.
            sc.put(shared_owner, 2)

        def resolve(unit, kind):
            """(modeled, real) unit after owner resolution for a merc/pet kind (shared_owner,
            else none). `real` is what the hooked GetOwnerUnit call always returns (owner, or
            unit if it has none) - the address the real game.exe call actually operates on.
            `modeled` carries the ally-owner-not-resolved defect instead, when active."""
            if kind != 1:
                return unit, unit
            if shared_owner is not None:
                owner = shared_owner
            else:
                owner = sc.block(0x100) if rng.random() < 0.7 else 0
                if owner:
                    sc.put(owner, rng.choice((0, 0, 2)))
            rig.script["get-owner"][unit] = owner
            want_log.append(("get-owner", unit))
            real = owner if owner else unit
            return (real if not mut("ally-owner-not-resolved") else unit), real

        resolved_u, real_u = resolve(sc.unit, kind_u)
        resolved_o, real_o = resolve(sc.other, kind_o)

        if resolved_u == resolved_o and not mut("ally-same-unit-false"):
            result = 1
        else:
            tu = sc.r(resolved_u) if resolved_u in sc.blocks else 0
            to = sc.r(resolved_o) if resolved_o in sc.blocks else 0
            both_players = bool(resolved_u) and bool(resolved_o) and tu == 0 and to == 0
            real_tu = sc.r(real_u) if real_u in sc.blocks else 0
            real_to = sc.r(real_o) if real_o in sc.blocks else 0
            real_both_players = bool(real_u) and bool(real_o) and real_tu == 0 and real_to == 0
            # The real call's own memory setup (+0x60, +0x80, the party-control blocks) runs,
            # at the real (unmutated) owner addresses, whenever those real owners are
            # structurally both players - regardless of what the model believes: game.exe
            # doesn't know about the mutation and still takes the real party-id path when both
            # real owners are players, and it will fault dereferencing an unset +0x80 pGame at
            # the real address if this setup instead targeted the model's resolved address.
            if real_both_players:
                sc.put(real_u + 0x60, 0)
                sc.put(real_o + 0x60, 0)
                guid_u, guid_o = sc.r(real_u + 0xC), sc.r(real_o + 0xC)
                pid_choice = rng.choice((1, 2, 5))

                def make_game(guid, want_id):
                    """A game block, with pPartyControl built so GetPartyIdFromUnit finds want_id for guid (or none)."""
                    if rng.random() < 0.2:
                        return 0, 0xFFFF
                    control = build_party_control(rig, [(want_id, [guid])]) if rng.random() < 0.8 else 0
                    g = sc.block(0x2000)
                    sc.put(g + 0x1D2C, control)
                    return g, (want_id if control else 0xFFFF)

                game_u, pid_u = make_game(guid_u, pid_choice)
                game_o, pid_o = make_game(guid_o, pid_choice if rng.random() < 0.5 else (pid_choice + 1) & 0xFFFF)
                sc.put(real_u + 0x80, game_u)
                sc.put(real_o + 0x80, game_o)
            if both_players and not mut("ally-nonplayer-skips-isallyof"):
                result = 1 if (pid_u == pid_o and pid_u != 0xFFFF) else 0
                if mut("ally-party-ffff-ok"):
                    result = 1 if pid_u == pid_o else result
                if mut("ally-party-mismatch-ok"):
                    result = 1
            else:
                rig.script["is-ally"] = rng.choice((0, 1))
                want_log.append(("is-ally", resolved_u, resolved_o))
                result = rig.script["is-ally"]
        got = rig.go(0x55_4DE0, sc.other, ecx=0, edx=sc.unit, nstack=1)
        verify(rig, "ally", got, ("ret", result), want_log, sc)

    for _ in range(100):
        kind_u, kind_o = rng.choice((0, 0, 1, 2)), rng.choice((0, 0, 1, 2))
        run_case(kind_u, kind_o)
    run_case(1, 1, force_same_owner=True)


def reset_interact(sc):
    """The trailing field reset ClearInteractOnFreePlayer applies on every non-empty path.

    Model-only (sc.w, not sc.put): this predicts the real call's own write to sc.unit's
    fields for the post-call block comparison. Writing it to live emulator memory before
    the call actually runs would pre-clear bInteracting and make the real call take the
    early-exit path every time."""
    sc.w(sc.unit + 0x64, MASK)
    sc.w(sc.unit + 0x68, 6)
    sc.w(sc.unit + 0x6C, 0, 1)


def check_interact(rig, rng):
    """FUN_00555500 ClearInteractOnFreePlayer: see the module docstring for the real control flow.

    No-op unless bInteracting (+0x6c) is set and FindUnitFromTable resolves the interacted
    unit; a resolved monster frees its AI and removes the player from its NPC proxy list, an
    object finishes its waypoint, and only the object branch returns before the trailing
    field reset (the monster branch falls through into it too).
    """
    for _ in range(150):
        sc = Scene(rig, rng)
        sc.unit = sc.block(0x100)
        game = rng.choice((0, 0x7A_8000))
        interacting = rng.choice((0, 1))
        sc.put(sc.unit + 0x6C, interacting, 1)
        guid = rand32(rng)
        etype = rng.choice((0, 1, 2, 3, 6))
        sc.put(sc.unit + 0x64, guid)
        sc.put(sc.unit + 0x68, etype)
        found = sc.block(0x100) if (interacting and rng.random() < 0.8) else 0
        ftype = rng.choice((0, 1, 1, 2, 3)) if found else 0
        if found:
            sc.put(found, ftype)
        rig.script["find-unit"] = {(game, etype, guid): found}
        want_log = []
        if interacting and not mut("interact-skips-gate"):
            want_log.append(("find-unit", game, etype, guid))
            if found:
                monster = ftype == (1 if not mut("interact-monster-returns-early") else 2)
                if monster:
                    mdata = sc.block(0x40) if rng.random() < 0.7 else 0
                    sc.put(found + 0x14, mdata)
                    ai = 0
                    if mdata:
                        ai = rand32(rng)
                        off = 0x34 if mut("interact-ai-offset-wrong") else 0x30
                        sc.put(mdata + off, ai)
                    want_log.append(("free-ai", game, sc.unit, ai))
                    want_log.append(("proxy-remove", game, sc.unit, found))
                    if not mut("interact-wrong-reset"):
                        reset_interact(sc)
                elif ftype == 2 and not mut("interact-object-no-return"):
                    want_log.append(("waypoint-finish", game, found))
                    if not mut("interact-wrong-reset"):
                        reset_interact(sc)
                elif not mut("interact-wrong-reset"):
                    reset_interact(sc)
        got = rig.go(0x55_5500, ecx=0, edx=0, regs={"esi": sc.unit, "ebx": game}, nstack=0)
        verify(rig, "interact", got, None if got[0] == "ret" else got, want_log, sc)


def main():
    """Command line entry."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "SUnit.cpp / Party.cpp small-helper cases")


def run_all():
    """Run every check; returns the case count."""
    global CASES
    CASES = 0
    CALLED.clear()
    rng = random.Random(20261112)
    rig = Rig()
    for check in (check_sparkly, check_alignment, check_uninterrupt, check_can_use, check_party,
                  check_ally, check_interact):
        check(rig, rng)
    missing = [f"{a:#x}" for a in COVERED if a not in CALLED]
    require(not missing, f"functions never called: {missing}")
    return CASES


if __name__ == "__main__":
    main()
