# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify how a stat change travels up the lists in stock game.exe 1.14d (Stats\\StatsEx.cpp).

    uv run python stats_propagate.py [--mutation-check]

Runs game.exe's own code against a hand-written model and compares the return
value, the ordered callee log (arguments in call order) and the bytes the
`ret n` pops, on random lists, random ItemStatCost rows and random hooked
answers (see _stats_world.py):

  FUN_00626920 ECX list, EDX key, [delta, ctx]: push a change up the parent chain
      (add it, or refresh a derived stat) until a list that stops the walk
  FUN_006266c0 ECX list, EDX key, [row, ctx]: recompute a derived stat, its
      up-to-3 dependents (row +0x58) and the 64 stats at row +0x5e

Two machines run: the first hooks the callees of FUN_006266c0 (the value
getter 0x62_6200, the store 0x62_5150, the stat lookup 0x62_4f60 and the unit
stat lookup 0x62_5480), the second hooks the callees of FUN_00626920 (the add
0x62_50b0 and FUN_006266c0 itself is then the second claim's hook). The hooks
answer from a hash of their arguments, so the model sees the same values.
`--mutation-check` breaks the model on purpose, one defect at a time, and
requires each to fail. No MPQs are read.
"""
import argparse
import random
import struct
import sys
import zlib

from unicorn.x86_const import UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX

import _stats_world as sw
from _stats_world import EXPANDED, L, Mismatch, require

PUSH_UP, REFRESH = 0x626920, 0x6266C0
# Hooked callees (not claims).
VALUE, STORE, LOOKUP, UNIT_LOOKUP, ADD = 0x62_6200, 0x62_5150, 0x62_4F60, 0x62_5480, 0x62_50B0
ACTIVE = 16
MUTATIONS = ("owner-any", "unit-type-any", "default-allows", "zero-not-stored", "chain-after-block",
             "mask-ignored", "start-at-parent", "stop-flag-ignored")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def pick(kind, salt, *values):
    """A hooked callee's answer: a hash of its arguments mapped onto a few values, so branches both ways occur."""
    digest = zlib.crc32(struct.pack(f"<{len(values) + 2}I", kind, salt, *[v & 0xFFFFFFFF for v in values]))
    return (0, 0, 1, 0xFFFFFFFF, 7, 0x80000000)[digest % 6]


class Rig:
    """One machine with its hooks installed for one of the two claims."""

    def __init__(self, refresh_claim):
        """Hook the callees of FUN_006266c0 (refresh_claim) or of FUN_00626920."""
        self.w = sw.World()
        self.cases = 0
        self.salt = 0
        machine = self.w.m
        if refresh_claim:
            machine.hook(VALUE, self.value, 0)
            machine.hook(STORE, self.store, 4)
            machine.hook(LOOKUP, self.lookup, 1)
            machine.hook(UNIT_LOOKUP, self.unit_lookup, 3)
        else:
            machine.hook(ADD, self.add, 4)
            machine.hook(REFRESH, self.refresh_hook, 2)

    def value(self, current):
        """FUN_00626200 stand-in (ECX list, EDX key)."""
        args = (current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX))
        self.w.log.append(("value",) + args)
        return pick(1, self.salt, *args)

    def store(self, current):
        """FUN_00625150 stand-in (EBX list, stack key, value, row, ctx)."""
        self.w.log.append(("store", current.mu.reg_read(UC_X86_REG_EBX)) + tuple(current.arg(i) for i in range(4)))
        return 0

    def lookup(self, current):
        """FUN_00624f60 stand-in (EBX list, EDI key, stack row)."""
        args = (current.mu.reg_read(UC_X86_REG_EBX), current.mu.reg_read(UC_X86_REG_EDI), current.arg(0))
        self.w.log.append(("lookup",) + args)
        return pick(2, self.salt, *args[:2])

    def unit_lookup(self, current):
        """FUN_00625480 stand-in (stack unit, stat, layer)."""
        args = tuple(current.arg(i) for i in range(3))
        self.w.log.append(("unit",) + args)
        return pick(3, self.salt, *args)

    def add(self, current):
        """FUN_006250b0 stand-in (EBX list, stack key, delta, row, ctx)."""
        self.w.log.append(("add", current.mu.reg_read(UC_X86_REG_EBX)) + tuple(current.arg(i) for i in range(4)))
        return 0

    def refresh_hook(self, current):
        """FUN_006266c0 stand-in (ECX list, EDX key, stack row, ctx)."""
        self.w.log.append(("refresh", current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX),
                           current.arg(0), current.arg(1)))
        return 0


def s16_at(row, offset):
    """An unsigned word of a row's bytes."""
    return int.from_bytes(row[offset:offset + 2], "little")


def make_rows(rig, rng):
    """Randomise the first ACTIVE rows: dependents only point to later rows, so recursion ends."""
    w = rig.w
    w.clear_rows()
    for index in range(ACTIVE):
        later = list(range(index + 1, ACTIVE))
        deps = rng.sample(later, min(len(later), rng.randrange(0, 3))) if later else []
        pairs = [rng.choice([0xFFFF] + list(range(ACTIVE))) for _ in range(3)]
        list_ids = rng.sample(later, min(len(later), rng.randrange(0, 4))) if later else []
        w.set_row(index, b04=rng.randrange(256), b51=rng.choice((0, 1, 1)), b52=rng.choice((0, 1)),
                  b54=rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15)))
        for i in range(3):
            w.set_row(index, **{f"w{0x58 + 2 * i:x}": deps[i] if i < len(deps) else 0xFFFF})
            w.set_row(index, **{f"w{0xDE + 6 * i:x}": pairs[i]})
        for i in range(64):
            w.set_row(index, **{f"w{0x5E + 2 * i:x}": list_ids[i] if i < len(list_ids) else 0xFFFF})
            if i > len(list_ids):
                break


def make_chain(rig, rng, depth):
    """A list with `depth` parents; each has a unit for the owner type; returns the child's address."""
    w = rig.w
    parent = 0
    for _ in range(depth):
        image = L()
        image.f[0x10] = rng.choice((0, 0, EXPANDED, 0x2000, 0x40000000, EXPANDED | 0x40000000, rng.randrange(1 << 32)))
        image.f[0x34] = parent
        image.f[0x08] = rng.choice((0, 1, 2, 4, 6))
        unit = w.m.alloc(16)
        w.m.w32(unit, rng.choice((0, 1, 2, 4)))
        image.f[0x04] = rng.choice((unit, 0))
        parent = w.install(image)
    return parent


def model_refresh(rig, lst, key, row_addr, ctx, log):
    """FUN_006266c0 as read from the disassembly; returns the value."""
    w = rig.w
    row = w.m.read(row_addr, sw.ROW_SIZE)
    image = w.models[lst]
    value = pick(1, rig.salt, lst, key)
    log.append(("value", lst, key))
    if row[0x51] == 0:
        log.append(("store", lst, key, value, row_addr, ctx))
        return value
    if value == 0 and not mut("zero-not-stored"):
        log.append(("store", lst, key, 0, row_addr, ctx))
    ok = True
    for i in range(3):
        dep = s16_at(row, 0x58 + 2 * i)
        if dep == 0xFFFF:
            break
        dep_row = w.row(dep)
        dep_key = dep << 16
        dep_value = model_refresh(rig, lst, dep_key, dep_row, ctx, log)
        log.append(("store", lst, dep_key, dep_value, dep_row, ctx))
        if dep_value == 0:
            continue
        mode = row[0x54]
        pair = s16_at(row, 0xDE + 6 * i)
        owner = image.f[0x08]
        if mode == 0 or 8 <= mode <= 11:
            continue
        if mode in (2, 3):
            if owner in (0, 1) or mut("owner-any"):
                if pair != 0xFFFF:
                    log.append(("lookup", lst, pair << 16, 0))
                    got = pick(2, rig.salt, lst, pair << 16)
                    if sw.s32(got) > 0:
                        ok = False
        elif mode in (4, 5):
            unit = image.f[0x04]
            if unit:
                unit_type = w.m.r32(unit)
                if unit_type in (0, 1) or mut("unit-type-any"):
                    if pair != 0xFFFF:
                        log.append(("unit", unit, pair, 0))
                        if sw.s32(pick(3, rig.salt, unit, pair, 0)) > 0:
                            ok = False
        elif mode in (6, 7):
            if owner in (0, 1):
                ok = False
        elif mode == 13:
            if owner == 4:
                ok = False
        elif mode == 12 and mut("default-allows"):
            pass
        else:
            ok = False
        if not ok and mut("chain-after-block"):
            ok = True
    if ok and value != 0:
        log.append(("store", lst, key, value, row_addr, ctx))
        for i in range(64):
            dep = s16_at(row, 0x5E + 2 * i)
            if dep == 0xFFFF:
                break
            dep_row = w.row(dep)
            dep_value = model_refresh(rig, lst, dep << 16, dep_row, ctx, log)
            log.append(("store", lst, dep << 16, dep_value, dep_row, ctx))
    return value


def check_refresh(rig, rng):
    """FUN_006266c0 on random lists, rows and hooked answers."""
    w = rig.w
    for _ in range(400):
        w.models.clear()
        rig.salt = rng.randrange(1 << 32)
        make_rows(rig, rng)
        lst = make_chain(rig, rng, 1)
        stat = rng.randrange(ACTIVE)
        key = (stat << 16) | rng.randrange(3)
        ctx = rng.randrange(1 << 32)
        log = []
        expected = model_refresh(rig, lst, key, w.row(stat), ctx, log)
        value, _ = w.run(REFRESH, w.row(stat), ctx, ecx=lst, edx=key, nstack=2)
        rig.cases += 1
        require(value == expected, f"refresh returned {value:#x}, expected {expected:#x} (stat {stat})")
        require(w.log == log, f"refresh log differs (stat {stat}):\n  got  {w.log!r}\n  want {log!r}")
        w.check_lists("refresh")


def check_push_up(rig, rng):
    """FUN_00626920 on random parent chains."""
    w = rig.w
    for _ in range(600):
        w.models.clear()
        w.clear_rows()
        for index in range(ACTIVE):
            w.set_row(index, b04=rng.randrange(256), b51=rng.choice((0, 0, 1)), b52=rng.choice((0, 0, 1)))
        w.set_masks(*[rng.choice((0, 1, 2, 4, 8, 0x10)) for _ in range(3)])
        lst = make_chain(rig, rng, rng.randrange(1, 5))
        stat = rng.choice(list(range(ACTIVE)) + [ACTIVE + 5, sw.ROWS - 1, sw.ROWS + 3])
        key = (stat << 16) | rng.randrange(3)
        delta = rng.choice((0, 1, 5, 0xFFFFFFFF, rng.randrange(1 << 32)))
        ctx = rng.randrange(1 << 32)
        log = []
        flags = w.models[lst].f[0x10]
        if delta and stat < sw.ROWS and not flags & 0x2000:
            row = w.m.read(w.row(stat), sw.ROW_SIZE)
            mask = row[4] & w.m.read(sw.MASK_STAT, 1)[0]
            if mut("mask-ignored"):
                mask = 0
            cur = lst if flags & EXPANDED and not mut("start-at-parent") else w.models[lst].f[0x34]
            while cur:
                if row[0x51] == 0 and row[0x52] == 0:
                    log.append(("add", cur, key, delta, w.row(stat), ctx))
                else:
                    log.append(("refresh", cur, key, w.row(stat), ctx))
                cur_flags = w.models[cur].f[0x10]
                nxt = w.models[cur].f[0x34]
                if cur_flags & 0x2000 and not mut("stop-flag-ignored"):
                    break
                if mask and cur_flags & 0x40000000:
                    break
                cur = nxt
        w.run(PUSH_UP, delta, ctx, ecx=lst, edx=key, nstack=2)
        rig.cases += 1
        require(w.log == log, f"push-up log differs:\n  got  {w.log!r}\n  want {log!r}")
        w.check_lists("push-up")


def run_all(seed, rounds):
    """Run both claims; returns the case count."""
    rng = random.Random(seed)
    sw.INSTRUCTION_LIMIT = 400000
    refresh_rig = Rig(True)
    push_rig = Rig(False)
    for _ in range(rounds):
        check_refresh(refresh_rig, rng)
        check_push_up(push_rig, rng)
    return refresh_rig.cases + push_rig.cases


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
    print(f"ok: {run_all(1, 3)} stat refresh and change push-up cases")


if __name__ == "__main__":
    main()
