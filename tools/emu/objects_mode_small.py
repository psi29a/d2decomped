# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the small OBJECTS\\ObjMode.cpp helpers in stock game.exe 1.14d.

    uv run python objects_mode_small.py [--mutation-check]

Runs game.exe's own code against a hand-written model on random records in a
random-filled arena (_objects_world.py). Compared after every call: the return
value (where the function has one), the ordered log of the hooked callees
(registers and stack arguments), the fatal-error line, the `ret n` pop count,
EBX / ESI / EDI / EBP and every byte of the arena.

Claimed (stdcall args on the stack unless a register is named):
  0x582490 / 0x5824b0 / 0x5824d0 / 0x5824f0  ECX, EDX through EBX, ESI plus
            EDI = 0x14a / 0x146 / 0x149 / 0x171 to the trap-monster spawner 0x58_2420
  0x5825b0  EAX unit, EDI type: 1 when an item in the unit's inventory list is
            of that type, else 0; a unit without an inventory halts (line 0x6d3)
  0x583bd0  ECX, one stack arg: runs 0x56_e900(arg), then 0x56_de40 on ECX
  0x5843d0  ECX ctx {+4 object}: OperateFn 11, mode switches
  0x584820  ECX game, EDX player, one stack arg unit: clears a class 0x151 unit's stored id
  0x585120 / 0x5851e0  EDX unit, one stack arg: stats 8 and 9 / 6 and 7 set to base + arg * 256
  0x585160 / 0x585180 / 0x5851a0 / 0x5851c0 / 0x585220  EDX unit, one stack arg:
            adds the arg to stat 1 / 2 / 3 / 0 / 5
  0x585240  EDI seed holder, stack (ctx, row): the 21-row chance table at 0x732eb0
  0x5854d0  EDX player, one stack arg unit: the stop-interacting dispatch
  0x585550  EDX unit: clears a class 0x151 unit's stored id, else sets mode 0

Callees are hooked and logged, not claimed (they are written 0x12_3456 here):
the spawner 0x58_2420, the unit and stat helpers, the mode setter 0x62_4690, the
unit lookup 0x55_2F60 and the operate / portal functions 0x58_52E0 and 0x58_4820
(the latter is the real one in the machine that tests 0x584820).
`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail. No MPQs are read.
"""
import argparse
import random
import sys

import _objects_world as ow
from _objects_world import M32, Mismatch, mix

THUNKS = {0x582490: 0x14A, 0x5824B0: 0x146, 0x5824D0: 0x149, 0x5824F0: 0x171}
HAS_ITEM, TWO_CALLS, OP11, CLEAR_ID = 0x5825B0, 0x583BD0, 0x5843D0, 0x584820
SET_MANA, SET_LIFE, STOP, CLEAR_OR_MODE, CHANCE_ROLL = 0x585120, 0x5851E0, 0x5854D0, 0x585550, 0x585240
ADDERS = {0x585160: 1, 0x585180: 2, 0x5851A0: 3, 0x5851C0: 0, 0x585220: 5}
TABLE, TABLE_COUNT, BAD_CODE_IMPORT = 0x732EB0, 0x732FAC, 0x6CC34C
MUTATIONS = ("thunk-const", "mode-two-only", "lcg-carry", "chance-le", "stat-code", "ecx-lost")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


class Rig:
    """The world, the hooks and the case state for this oracle."""

    def __init__(self, real=()):
        """Hook the callees that are not claimed; `real` functions stay game.exe's own."""
        self.w = w = ow.World(real)
        self.firsts, self.nexts, self.matches = {}, {}, set()
        self.pp = 0
        self.lookup = 0
        self.bad = 0
        w.rec(0x58_2420, "trap", 0, ("ebx", "esi", "edi"))
        w.rec(0x63_B2C0, "first", 1, ret=lambda entry: self.firsts.get(entry[1], 0))
        w.rec(0x63_DFD0, "data", 1)
        w.rec(0x62_9BB0, "isa", 2, ret=lambda entry: 5 if (entry[1], entry[2]) in self.matches else 0)
        w.rec(0x63_DFA0, "next", 1, ret=lambda entry: self.nexts.get(entry[1], 0))
        w.rec(0x56_E900, "e900", 1)
        w.rec(0x56_DE40, "de40", 0, ("ecx",))
        w.rec(0x62_4690, "mode", 2, ret=0)
        w.rec(0x62_34F0, "pp", 0, ("ecx",), ret=lambda entry: self.pp)
        w.rec(0x55_2F60, "lookup", 1, ("ecx", "edx"), ret=lambda entry: self.lookup)
        w.rec(0x62_5D60, "base_mana", 1)
        w.rec(0x62_5D10, "base_life", 1)
        w.rec(0x62_7260, "set", 4, ret=0)
        w.rec(0x62_72B0, "add", 4, ret=0)
        w.rec(0x55_4190, "stop", 1, ret=0)
        w.rec(0x58_52E0, "operate", 3, ("ecx", "edx"), ret=0)
        w.rec(0x58_4820, "clear_id", 1, ("ecx", "edx"), ret=0)
        self.fakes = [w.fake_function(f"row{i}", 1) for i in range(6)]
        slot = w.m.r32(BAD_CODE_IMPORT)
        w.m.slots[slot] = ("IsBadCodePtr", lambda e: (self.bad, 1))


def check_thunks(rig, rng):
    """The four trap thunks: constants in EDI, ECX / EDX through EBX / ESI, the callee's EAX back."""
    for address, const in THUNKS.items():
        for _ in range(10):
            ecx, edx = rng.randrange(1 << 32), rng.randrange(1 << 32)
            want_const = const if not (mut("thunk-const") and address == 0x5824B0) else 0x14A
            rig.w.go("thunk", address, ecx=ecx, edx=edx, pops=0,
                     build=lambda m, c=want_const, e=ecx, d=edx: (mix("trap", e, d, c), [("trap", e, d, c)], None))


def check_has_item(rig, rng):
    """0x5825b0: walk the inventory's item list for the first item whose data is of type EDI."""
    a = rig.w.arena
    for _ in range(80):
        a.reset(rng)
        unit, inv = a.new(0x80), a.new(0x40)
        a.w32(unit + 0x60, rng.choice((inv, inv, inv, 0)))
        items = [a.new(0x10) for _ in range(rng.choice((0, 1, 2, 3, 5)))]
        rig.firsts = {inv: items[0] if items else 0}
        rig.nexts = {it: items[i + 1] if i + 1 < len(items) else 0 for i, it in enumerate(items)}
        kind = rng.randrange(1 << 32)
        pick = rng.choice([None] + list(range(len(items))))
        rig.matches = set() if pick is None else {(mix("data", items[pick]), kind)}

        def build(m, unit=unit, inv=inv, items=items, kind=kind, pick=pick):
            """Model: stop at the first item the type test accepts."""
            if m.r32(unit + 0x60) == 0:
                return None, [], 0x6D3
            log = [("first", inv)]
            for i, it in enumerate(items):
                log += [("data", it), ("isa", mix("data", it), kind)]
                if pick == i:
                    return 1, log, None
                log.append(("next", it))
            return 0, log, None
        rig.w.go("has-item", HAS_ITEM, regs={"eax": unit, "edi": kind}, pops=0, build=build)


def check_two_calls(rig, rng):
    """0x583bd0: the stack arg to one callee, then ECX to the next, whose EAX comes back."""
    for _ in range(10):
        arg, ecx = rng.randrange(1 << 32), rng.randrange(1 << 32)
        rig.w.go("two-calls", TWO_CALLS, arg, ecx=ecx, pops=1,
                 build=lambda m, a=arg, c=ecx: (mix("de40", c), [("e900", a), ("de40", c)], None))


def check_op11(rig, rng):
    """0x5843d0: mode 0 -> 1 (clearing flag 2), modes 1 and 2 -> 0, anything else untouched; returns 1."""
    a = rig.w.arena
    for _ in range(80):
        a.reset(rng)
        ctx, obj = a.new(0x10), a.new(0x100)
        has = rng.random() < 0.9
        a.w32(ctx + 4, obj if has else 0)
        a.w32(obj + 0x10, rng.choice((0, 1, 2, 3, 4, 0xFFFFFFFF)))

        def build(m, ctx=ctx, obj=obj, has=has):
            """Model of the three mode arms."""
            mode = m.r32(obj + 0x10) if has else 0
            if mode == 0:
                log = [("mode", obj if has else 0, 1)]
                if has:
                    m.w32(obj + 0xC4, m.r32(obj + 0xC4) & ~2)
                return 1, log, None
            if mode in ((1,) if mut("mode-two-only") else (1, 2)):
                return 1, [("mode", obj, 0)], None
            return 1, [], None
        rig.w.go("op11", OP11, ecx=ctx, pops=0, build=build)


def check_clear_id(rig, rng):
    """0x584820 (and 0x585550): clear the stored id of a class 0x151 unit when it names the player."""
    a = rig.w.arena
    for _ in range(100):
        a.reset(rng)
        game, unit, pp, p, player = a.new(0x40), a.new(0x80), a.new(8), a.new(8), a.new(0x40)
        a.w32(unit + 4, rng.choice((0x151, 0x151, 0x150, 0)))
        a.w32(pp, rng.choice((p, 0)))
        rig.pp = pp
        edx = rng.choice((0, player))
        rig.lookup = rng.choice((player, 0, p))
        arg = unit if rng.random() < 0.9 else 0

        def build(m, game=game, unit=unit, pp=pp, edx=edx, arg=arg):
            """Model: the lookup of the stored id must give the player, if one is named."""
            if arg == 0 or m.r32(arg + 4) != 0x151:
                return None, [], None
            log = [("pp", arg)]
            target = m.r32(pp)
            if edx:
                if target == 0:
                    return None, log, None
                log.append(("lookup", game, 0, m.r32(target)))
                if rig.lookup != edx:
                    return None, log, None
            if target:
                m.w32(target, M32)
            return None, log, None
        rig.w.go("clear-id", CLEAR_ID, arg, ecx=game, edx=edx, pops=1, build=build)
        # 0x585550: EDX unit, no stack args.
        unit2 = unit if rng.random() < 0.9 else 0

        def build_b(m, unit2=unit2, pp=pp):
            """Model: class 0x151 clears the stored id, anything else (also no unit) sets mode 0."""
            if unit2 and m.r32(unit2 + 4) == 0x151:
                log = [("pp", unit2)]
                target = m.r32(pp)
                if target:
                    m.w32(target, M32)
                return None, log, None
            return None, [("mode", unit2, 0)], None
        rig.w.go("clear-or-mode", CLEAR_OR_MODE, edx=unit2, pops=0, build=build_b)


def check_stat_setters(rig, rng):
    """The stat setters 0x585120 / 0x5851e0 and the five adders: return 1, call the stat helpers."""
    for _ in range(20):
        unit, arg = rng.randrange(1 << 32), rng.randrange(1 << 32)
        for address, (base, stats) in ((SET_MANA, ("base_mana", (8, 9))), (SET_LIFE, ("base_life", (6, 7)))):
            value = (mix(base, unit) + (arg << 8)) & M32
            rig.w.go("set-stats", address, arg, edx=unit, pops=1,
                     build=lambda m, b=base, s=stats, v=value, u=unit: (1, [(b, u), ("set", u, s[0], v, 0), ("set", u, s[1], v, 0)], None))
        for address, stat in ADDERS.items():
            shown = 2 if (mut("stat-code") and address == 0x585160) else stat
            rig.w.go("add-stat", address, arg, edx=unit, pops=1,
                     build=lambda m, s=shown, u=unit, v=arg: (1, [("add", u, s, v, 0)], None))


def check_stop(rig, rng):
    """0x5854d0: by the unit's class (low word): 0x50 / 0x5a operate, 0x151 clear, 0x98 mode 0, then stop."""
    a = rig.w.arena
    for _ in range(120):
        a.reset(rng)
        unit, player = a.new(0x40), a.new(0x40)
        a.w32(unit + 4, rng.choice((0x50, 0x5A, 0x151, 0x98, 0x10050, 0x99, 0)) | rng.choice((0, 0, 0x30000)))
        a.w32(unit + 0xC, rng.randrange(1 << 32))
        a.w32(player + 0xC, rng.randrange(1 << 32))
        who = rng.choice((player, player, 0))
        arg = unit if rng.random() < 0.9 else 0
        ecx = rng.randrange(1 << 32)

        def build(m, unit=unit, who=who, arg=arg, ecx=ecx):
            """Model of the dispatch; every path ends in the stop call."""
            log = []
            if arg:
                cls = m.r32(arg + 4) & 0xFFFF
                if cls in (0x50, 0x5A):
                    ident = m.r32(who + 0xC) if who else M32
                    log.append(("operate", ecx if not mut("ecx-lost") else 0, ident, m.r32(arg + 0xC), 0, 2))
                elif cls == 0x151:
                    log.append(("clear_id", ecx, who, arg))
                elif cls == 0x98:
                    log.append(("mode", arg, 0))
            log.append(("stop", who))
            return None, log, None
        rig.w.go("stop", STOP, arg, ecx=ecx, edx=who, pops=1, build=build)


def check_chance_roll(rig, rng):
    """0x585240: advance the unit's seed, roll % 100 against the row's chance, call the row's function."""
    a = rig.w.arena
    m = rig.w.m
    for _ in range(300):
        a.reset(rng)
        unit = a.new(0x40)
        count = rng.choice((0, 1, 3, 6))
        m.w32(TABLE_COUNT, count)
        for i in range(count):
            m.w32(TABLE + 12 * i, rig.fakes[i])
            m.w32(TABLE + 12 * i + 4, rng.choice((0, 100, 101, rng.randrange(101), rng.randrange(101))))
            m.w32(TABLE + 12 * i + 8, rng.randrange(1 << 32))
        index = rng.choice((0, 0, 1, 2, 5, 6, 7, 200))
        high = rng.choice((0, 0x100, 0xAB00))
        rig.bad = rng.choice((0, 0, 0, 1))
        arg = rng.randrange(1 << 32)
        rows = [(m.r32(TABLE + 12 * i + 4), m.r32(TABLE + 12 * i + 8)) for i in range(count)]
        bad = rig.bad

        def build(mm, unit=unit, count=count, index=index, arg=arg, rows=rows, bad=bad):
            """Model: a 64-bit LCG step on the unit's seed, roll = low word % 100."""
            if index >= count:
                return 0, [], None
            lo, hi = mm.r32(unit + 0x20), mm.r32(unit + 0x24)
            product = lo * 0x6AC690C5
            total = (product & M32) + hi
            new_lo = total & M32
            new_hi = (product >> 32) + (0 if mut("lcg-carry") else total >> 32)
            mm.w32(unit + 0x20, new_lo)
            mm.w32(unit + 0x24, new_hi)
            chance, param = rows[index]
            roll = new_lo % 100
            if roll > chance if mut("chance-le") else roll >= chance:
                return 0, [], None
            if bad:
                return None, [], 0x10F5
            return mix(f"row{index}", arg, unit, param), [(f"row{index}", arg, unit, param)], None
        rig.w.go("chance-roll", CHANCE_ROLL, arg, index | high, regs={"edi": unit}, pops=2, build=build)


def run_all(seed, rounds):
    """Build the rigs and run every group; returns the case count."""
    rng = random.Random(seed)
    rig, real = Rig(), Rig(real=(CLEAR_ID,))
    for _ in range(rounds):
        check_thunks(rig, rng)
        check_has_item(rig, rng)
        check_two_calls(rig, rng)
        check_op11(rig, rng)
        check_clear_id(real, rng)
        check_stat_setters(rig, rng)
        check_stop(rig, rng)
        check_chance_roll(rig, rng)
    return rig.w.cases + real.w.cases


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
    print(f"ok: {run_all(1, 2)} small ObjMode helper cases")


if __name__ == "__main__":
    main()
