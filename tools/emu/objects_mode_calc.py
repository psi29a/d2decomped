# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the shrine-boost arithmetic helpers (OBJECTS\\ObjMode.cpp) in stock game.exe 1.14d.

    uv run python objects_mode_calc.py [--mutation-check]

Runs game.exe's own code against a hand-written model on random records in a
random-filled arena (_objects_world.py). Compared after every call: the return
value (where the function has one), the ordered log of the hooked callees
(registers and stack arguments), the fatal-error line, the `ret n` pop count,
EBX / ESI / EDI / EBP and every byte of the arena.

Claimed:
  0x582800  ECX, EDX, five stack args: builds a nine-dword request
            {ecx, edx, 0, 0, a1, a3, a2, a4, a5} and returns 0x56_e970 on it
  0x582940  ECX ctx {+8 unit}, EDX params {+4 a, +8 b}: stat 6 loses a% of
            itself (in 256ths), stat 8 gains b% of that loss
  0x583740  EAX, ECX, EDX, stack (out, level): the attack-rating helper:
            *out grows by (level - 1) * rec[+0x19c] + rec[+0x198] + two stat
            terms, in percent, and a player adds the class record's +0x3c
  0x583840  ESI unit, EBX percent, one stack arg code: the stat (or, for code
            0x13 and 0x15, a derived value) scaled by EBX / 100; codes
            0xb 0x1b 0x27 0x29 0x2b 0x2d 0x55 0xab return EBX unchanged

Callees are hooked and logged, not claimed (written 0x12_3456 here): the stat
reader 0x62_5480, adder 0x62_72B0, the unit and class helpers, the request
builder 0x56_E970 and, for 0x583840, the attack helper 0x58_3740 (real in the
machine that tests it). `--mutation-check` breaks the model on purpose, one
defect at a time, and requires each to fail. No MPQs are read.
"""
import argparse
import random
import sys

import _objects_world as ow
from _objects_world import M32, Mismatch, mix, s32

BUILD, EXCHANGE, ATTACK, SCALE = 0x582800, 0x582940, 0x583740, 0x583840
TABLE_PTR = 0x744304
PLAIN_CODES = (0xB, 0x1B, 0x27, 0x29, 0x2B, 0x2D, 0x55, 0xAB)
MUTATIONS = ("div-unsigned", "swap-args", "player-bonus", "plain-code", "loss-sign", "halt-line")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def div100(value):
    """The compiler's signed divide by 100: high word of value * 0x51eb851f, >> 5, plus the sign bit."""
    if mut("div-unsigned"):
        return (value & M32) // 100
    high = (s32(value) * 0x51EB851F) >> 32
    high >>= 5
    return (high + ((high & M32) >> 31)) & M32


class Rig:
    """The world, the hooks and the case state for this oracle."""

    def __init__(self, real=()):
        """Hook the callees that are not claimed; `real` functions stay game.exe's own."""
        self.w = w = ow.World(real)
        self.inv = 1
        self.kind = 0
        self.rec = 0
        self.test_c = 0
        self.test_d = 0
        w.m.hook(0x56_E970, self.request, 0)
        w.rec(0x62_5480, "stat", 3)
        w.rec(0x62_72B0, "add", 4, ret=0)
        w.rec(0x62_2560, "lvl", 1)
        w.rec(0x63_BEF0, "inv", 1, ret=lambda entry: self.inv and mix("inv", entry[1]))
        w.rec(0x62_B400, "kind", 1, ret=lambda entry: self.kind)
        w.rec(0x64_5830, "p", 4)
        w.rec(0x62_5500, "q", 3)
        w.rec(0x64_4140, "rec", 0, ("ecx",), ret=lambda entry: self.rec)
        w.rec(0x64_39B0, "x", 1, ("ecx", "edx"))
        w.rec(0x62_3990, "c", 2, ret=lambda entry: self.test_c)
        w.rec(0x63_D340, "d", 2, ret=lambda entry: self.test_d)
        w.m.hook(0x58_3740, self.attack_stub, 2)

    def request(self, current):
        """0x56_e970 stand-in (ECX request): log the nine dwords."""
        from unicorn.x86_const import UC_X86_REG_ECX
        words = tuple(current.r32(current.mu.reg_read(UC_X86_REG_ECX) + 4 * i) for i in range(9))
        self.w.log.append(("build",) + words)
        return mix("build", *words)

    def attack_stub(self, current):
        """0x583740 stand-in (EAX, ECX, EDX, stack out + level): log, store a mixed value through out."""
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX
        regs = tuple(current.mu.reg_read(r) for r in (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX))
        self.w.log.append(("attack",) + regs + (current.arg(1),))
        current.w32(current.arg(0), mix("attack", *regs, current.arg(1)))
        return 0


def check_build(rig, rng):
    """0x582800: nine dwords from two registers and five stack args."""
    for _ in range(20):
        ecx, edx = rng.randrange(1 << 32), rng.randrange(1 << 32)
        a = [rng.randrange(1 << 32) for _ in range(5)]
        order = (a[0], a[2], a[1], a[3], a[4]) if not mut("swap-args") else (a[0], a[1], a[2], a[3], a[4])
        words = (ecx, edx, 0, 0) + order
        rig.w.go("build", BUILD, *a, ecx=ecx, edx=edx, pops=5,
                 build=lambda m, w=words: (mix("build", *w), [("build",) + w], None))


def check_exchange(rig, rng):
    """0x582940: lose a% of the stat-6 value, gain b% of the loss on stat 8."""
    arena = rig.w.arena
    for _ in range(80):
        arena.reset(rng)
        ctx, params, unit = arena.new(0x10), arena.new(0x10), rng.randrange(1 << 32)
        arena.w32(ctx + 8, unit)
        arena.w32(params + 4, rng.choice((0, 25, 50, 100, rng.randrange(1 << 32))))
        arena.w32(params + 8, rng.choice((0, 25, 50, 100, rng.randrange(1 << 32))))

        def build(m, ctx=ctx, params=params, unit=unit):
            """Model: unsigned divide by 100, loss in 256ths."""
            have = s32(mix("stat", unit, 6, 0)) >> 8
            loss = (((m.r32(params + 4) * have) & M32) * 0x51EB851F >> 32 >> 5) << 8 & M32
            gain = ((m.r32(params + 8) * loss) & M32) * 0x51EB851F >> 32 >> 5
            return None, [("stat", unit, 6, 0), ("add", unit, 6, loss if mut("loss-sign") else -loss & M32, 0), ("add", unit, 8, gain, 0)], None
        rig.w.go("exchange", EXCHANGE, ecx=ctx, edx=params, pops=0, build=build)


def check_attack(rig, rng):
    """0x583740: percent bonus on *out from the class record and the stat terms; a player adds record +0x3c."""
    arena = rig.w.arena
    m = rig.w.m
    for _ in range(300):
        arena.reset(rng)
        tab = arena.new(0xD00)
        m.w32(TABLE_PTR, tab)
        count = rng.choice((0, 1, 3, 6))
        arena.w32(tab + 0xBC8, count)
        arena.w32(tab + 0xBC4, arena.new(0xC4 * 6))
        out, unit = arena.new(8), arena.new(0x80)
        rig.rec = arena.new(0x1C0)
        for off in (0x198, 0x19C):
            arena.w32(rig.rec + off, rng.choice((0, 3, rng.randrange(1 << 32), 100)))
        arena.w32(unit, rng.choice((0, 0, 1)))
        arena.w32(unit + 4, rng.choice((-1, 0, 1, 2, 5, 7)) & M32)
        arena.w32(unit + 0x60, rng.randrange(1, 1 << 32))
        no_unit = rng.random() < 0.15
        edi_in = rng.choice((0, 0, rng.randrange(1, 1 << 32)))
        if no_unit:
            edi_in = rng.randrange(1, 1 << 32)
        rig.inv = rng.choice((0, 1, 1))
        rig.kind = rng.choice((0x26, 0x26, 7))
        level = rng.choice((1, 2, 5, 0, rng.randrange(1 << 32)))
        ebx = rng.choice((0, 7, rng.randrange(1 << 32)))
        esi = 0 if no_unit else unit
        base = rig.rec
        tab_ptr = tab

        def build(mm, out=out, esi=esi, edi=edi_in, ebx=ebx, level=level, base=base, tab=tab_ptr):
            """Model of the eleven steps; the fatal error is after the first store."""
            log = [("lvl", esi)]
            mm.w32(out, mix("lvl", esi))
            if ebx == 0:
                return None, log, 0x962 if mut("halt-line") else 0x963
            if edi == 0:
                inv_arg = mm.r32(esi + 0x60)
                log.append(("inv", inv_arg))
                edi = mix("inv", inv_arg) if rig.inv else 0
            if edi:
                log.append(("kind", edi))
                if rig.kind == 0x26:
                    mm.w32(out, 0)
            log.append(("p", esi, edi, ebx, 0))
            log.append(("q", esi, 0x77, 0))
            total = (mix("p", esi, edi, ebx, 0) + mix("q", esi, 0x77, 0)) & M32
            log.append(("rec", ebx))
            value = mm.r32(out)
            step = ((level - 1) * mm.r32(base + 0x19C) + mm.r32(base + 0x198) + total) & M32
            result = (value + div100((step * value) & M32)) & M32
            mm.w32(out, result)
            if esi and mm.r32(esi) == 0:
                index = mm.s32(esi + 4)
                if index >= 0 and index < mm.s32(tab + 0xBC8):
                    record = (index * 0xC4 + mm.r32(tab + 0xBC4)) & M32
                    if record:
                        mm.w32(out, (mm.r32(record + 0x3C) + result) & M32 if not mut("player-bonus") else result)
            return None, log, None
        rig.w.go("attack", ATTACK, out, level, ecx=ebx, edx=esi, regs={"eax": edi_in}, pops=2, build=build)


def check_scale(rig, rng):
    """0x583840: the stat for a code scaled by EBX / 100, with the two derived codes and the plain ones."""
    arena = rig.w.arena
    for _ in range(400):
        arena.reset(rng)
        unit = arena.new(0x80)
        has_unit = rng.random() < 0.9
        arena.w32(unit + 0x60, rng.choice((0, rng.randrange(1, 1 << 32))))
        rig.inv = rng.choice((0, 1))
        rig.test_c = rng.choice((0, 1))
        rig.test_d = rng.choice((0, 1, 2))
        code = rng.choice(PLAIN_CODES + (0x13, 0x15, 0x14, 0x0A, 0xAC, 0x100, 0, 0x3F, 0xFFFFFFF5))
        ebx = rng.choice((0, 25, 100, rng.randrange(1 << 32)))
        esi = unit if has_unit or code == 0x15 else 0   # code 0x15 reads unit +0x60: a NULL unit would fault

        def build(m, unit=esi, code=code, ebx=ebx):
            """Model: the dispatch on code - 0xb, then the shared divide by 100."""
            log, inv = [], 0
            if unit and m.r32(unit + 0x60):
                log.append(("inv", m.r32(unit + 0x60)))
                inv = mix("inv", m.r32(unit + 0x60)) if rig.inv else 0
            index = (code - 0xB) & M32
            if index <= 0xA0 and (code in PLAIN_CODES and not mut("plain-code")):
                return ebx, log, None
            if index <= 0xA0 and code == 0x13:
                log.append(("x", unit, 0, M32))
                held = mix("x", unit, 0, M32)
                level = (inv, held, unit, 1)
                log.append(("attack",) + level)
                value = mix("attack", *level)
                return div100((value * ebx) & M32), log, None
            stat = code
            if index <= 0xA0 and code == 0x15:
                log.append(("c", unit, 1))
                if rig.test_c or inv:
                    log.append(("d", unit, m.r32(unit + 0x60)))
                    stat = 0x17 if rig.test_d == 2 else 0x15
                else:
                    stat = 0x15
            log.append(("stat", unit, stat, 0))
            return div100((mix("stat", unit, stat, 0) * ebx) & M32), log, None
        rig.w.go("scale", SCALE, code, regs={"esi": esi, "ebx": ebx}, pops=1, build=build)


def run_all(seed, rounds):
    """Build the rigs and run every group; returns the case count."""
    rng = random.Random(seed)
    rig, real = Rig(), Rig(real=(ATTACK,))
    for _ in range(rounds):
        check_build(rig, rng)
        check_exchange(rig, rng)
        check_attack(real, rng)
        check_scale(rig, rng)
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
    print(f"ok: {run_all(1, 2)} shrine-boost arithmetic helper cases")


if __name__ == "__main__":
    main()
