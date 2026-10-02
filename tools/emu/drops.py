# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's drop roller as the oracle (docs/research/re/drops.md).

    uv run python drops.py 1-20000          # diff tools/drop-dump against game.exe, one job per seed
    uv run python drops.py 1-20000 print    # just game.exe's lines
    uv run python drops.py tables           # every class's entries, game.exe vs ours
    uv run python drops.py items 1-20000    # made items (seeds, gold, base rolls, unique / set picks) vs ours
    uv run python drops.py objects 1-20000  # containers opened (chests, caskets, urns, barrels, corpses, bookshelves) vs ours
    uv run python drops.py stands 1-20000   # armor stands' / weapon racks' items vs ours

A job is (seed, class, level, ilvl, players, mf): FUN_00654e00 moves the
class on by level (as FUN_0055afa0 does past normal), then FUN_0055a6d0
rolls it off a monster whose unit seed is {seed, 666}, expansion, no
killer (FUN_005585d0, the killer's magic find, hooked to answer mf). FUN_0055a550 (make the item) is hooked to record what it's asked
for, so the unit seed is the only state the roll touches. The line is what
dropped (code:quality, *mul for gold) and the seed's low word after.

Items: a job is (seed, code, ilvl, quality, bovine). A fake item unit
takes its seeds off a game seed {seed, 666} as FUN_00555230 does
(FUN_00552df0: unit +0x20; FUN_00552e90: item data +4), version 101 and
the ilvl; FUN_00557ab0 rolls its coins / stack / durability / defence
(FUN_00627260, set stat, hooked to record them), then FUN_005566b0 (unique)
or FUN_005c2940 (set) picks off its own seed on a game whose one-per-game
list (+0x1b24) carries on from job to job. A failed pick triples (unique)
or doubles (set) the durability, as FUN_00557450 goes on to. One that
took shows its mods (FUN_00627030 add / FUN_00627150 set on the item's
list), each set bonus list (states 0xa5..0xa9, as "i[...]"), ethereal
(FUN_00556ca0) and its own seed after.

Objects: a job is (object seed, unit seed, OperateFn, objects.txt id, level,
difficulty, locked, sparkling). The OperateFn (FUN_00585f60 chest,
FUN_00586410 casket, FUN_005866c0 urn, FUN_005868a0 barrel, FUN_005867a0
corpse, FUN_00584060 bookshelf) runs on a fake object (unit +0x20 its seed,
+0x14 data +4 the lock bit 0x80, +0x78 sparkle) with ctx +0xc the object
seed, no player, the room and level hooked to the job's; its rounds
(FUN_00585b90) roll for real, FUN_0055a550 recorded as above. The magic
check (FUN_0062a0f0) answers on the made quality: the asked one through
FUN_00557450's type / base flags, read up front off game.exe's own
getters. FUN_00585970 (gold / potions) and FUN_00559a30 (the bookshelf's
book) are recorded. The line: drops | extras [shut] -> both seeds' low.

Stands: a job is (seed, weapon, ilvl). FUN_00559630 (rack) / FUN_005594c0
(stand) picks off a room whose seed (+0x6c) is {seed, 666}, on a level of
area level ilvl + 1 (it takes one off); FUN_00558d90 (make) is hooked to
read the base; the item then takes its seeds off the room's (FUN_00552df0
/ FUN_00552e90) and FUN_00556f60 rolls its quality, made as above.

Runtime classes: DAT_0096c5ec, stride 0x2c; 0 empty, 1..160 the auto
classes (bow weap mele armo abow x 3..96), 161 + TreasureClassEx row after.
"""
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import drlg
import emu

# ponytail: unique / set item entries (Annihilus: flag 1, uber Diablo only) aren't ours.
KNOWN = {"ROP (N)", "ROP (H)"}
AUTO = ("bow", "weap", "mele", "armo", "abow")   # ItemTypes rows with TreasureClass, in order


def txt_rows(e, name):
    lines = e.mpq_file(f"data\\global\\excel\\{name}.txt").decode("latin-1").split("\r\n")
    head = lines[0].split("\t")
    return [dict(zip(head, l.split("\t"))) for l in lines[1:] if l]


def class_names(e):
    names = [""] + [f"{t}{n}" for t in AUTO for n in range(3, 97, 3)]
    names += [r["Treasure Class"] for r in txt_rows(e, "TreasureClassEx") if r["Treasure Class"]]
    assert len(names) == e.r32(0x96c5f0), (len(names), e.r32(0x96c5f0))
    return names


def jobs(names, first, last):
    """The same mix test_game rolls: every class in turn, levels, ilvls, players and magic find varied."""
    for s in range(first, last + 1):
        yield ((s * 0x9E3779B1) & 0xFFFFFFFF, names[1 + s % (len(names) - 1)], s * 37 % 100, 1 + s * 13 % 99, 1 + s % 8,
               (0, 0, 0, 50, 110, 250, 600, -100)[s // 8 % 8])


def item_codes(e):
    """Every item's code by index (FUN_006335f0 record +0x80), read up front: a hook can't call back into the emu."""
    codes = []
    while rec := e.call(0x6335f0, len(codes)):
        codes.append(e.read(rec + 0x80, 4).decode("latin-1").rstrip(" \0"))
    return codes


def item_flags(e, codes):
    """Per item index: always magic, quest, can rare, only unique, always normal (read up front off game.exe's getters)."""
    item, flags = e.alloc(0x100), []
    for i in range(len(codes)):
        e.w32(item, 4); e.w32(item + 4, i)
        flags.append([e.call(f, item, ecx=item) & 0xFF for f in (0x62e9e0, 0x628cd0, 0x62e990, 0x628c70, 0x62ea30)])
    return flags


def made_quality(flags, quality):
    """FUN_00557450's flags on an asked quality."""
    magic, quest, rare, unique, normal = flags
    if magic: quality = 7 if quest else quality if 4 <= quality <= 9 else 4
    if not rare and quality == 6: quality = 4
    if unique: quality = 7
    return 2 if normal else quality


class Oracle:
    def __init__(self, items=False):
        self.e = e = drlg.boot()
        self.names = class_names(e)
        self.codes = item_codes(e)
        self.index = {n: i for i, n in enumerate(self.names) if n}
        self.game, self.unit = e.alloc(0x2000), e.alloc(0x200)
        e.w32(self.game + 0x70, 1)                     # expansion
        e.w32(self.game + 0x78, 101)                   # the item version (expansion)
        e.w32(self.unit, 1)                            # a monster
        self.made, self.players, self.mf = [], 1, 0
        if items:
            # A fake stat store: the item's own (base) stats, set / read by
            # FUN_00627260 / FUN_006253b0 / FUN_00625500, and its one modifier
            # list (FUN_00625790 / FUN_006257d0 find it) that the property funcs
            # (FUN_00627030 add, FUN_00627150 set) and staffmods write.
            self.flags = item_flags(e, self.codes)     # before the stat hooks: the getters read stats
            self.stats, self.mods, self.sets, LIST = {}, {}, {}, 0x5157
            self.shift = {int(r["ID"]): int(r["ValShift"] or 0) for r in txt_rows(e, "ItemStatCost") if r.get("ID")}
            self.item, self.data, self.args, self.seed = e.alloc(0x200), e.alloc(0x200), e.alloc(0x100), e.alloc(8)
            def total(e):
                stat, param = e.arg(1), e.arg(2)
                value = self.stats.get(stat, 0) + self.mods.get((stat, param), 0)
                return value + (self.maxdur() - self.stats.get(0x49, 0) - self.mods.get((0x49, 0), 0) if stat == 0x49 else 0)
            def kept(e):                               # the item's list, or a set's bonus list (states 0xa5..0xa9)
                return self.mods if e.arg(0) == LIST else self.sets.setdefault(e.arg(0) - LIST - 0xa5, {})
            def add(e):
                kept(e)[(e.arg(1), e.arg(3))] = kept(e).get((e.arg(1), e.arg(3)), 0) + e.arg(2)
            def put(e):
                kept(e)[(e.arg(1), e.arg(3))] = e.arg(2)
            e.hook(0x627260, lambda e: self.stats.__setitem__(e.arg(1), e.arg(2)), 4)   # set stat: recorded
            for a in (0x6253b0, 0x625500): e.hook(a, lambda e: self.stats.get(e.arg(1), 0), 3)
            e.hook(0x625480, total, 3)
            for a in (0x625790, 0x6257d0): e.hook(a, lambda e: LIST + e.arg(1), 3)   # a state's list (a set's bonuses) apart
            e.hook(0x627030, add, 4)
            e.hook(0x627150, put, 4)
            e.hook(0x625d00, lambda e: self.mods.get((e.arg(1), e.arg(2)), 0) if e.arg(0) == LIST else 0, 3)
            e.hook(0x625e00, lambda e: self.maxdur(), 1)   # max durability with item_maxdurability_percent (op 13)
            self.gear = {r["code"] for f in ("Weapons", "Armor") for r in txt_rows(e, f)}
            affixes = e.call(0x633ed0)                 # {count, rows, prefixes, suffixes, automagic}: data+0x36 is a row + 1
            self.auto0 = (e.r32(affixes + 16) - e.r32(affixes + 4)) // 0x90
            return

        def make(e):                                   # FUN_0055a550(ecx item; game, quality, unique/set index, flags)
            self.made.append([self.codes[e.mu.reg_read(UC_X86_REG_ECX) & 0xFFFF], e.arg(1), 0])
            return len(self.made)
        e.hook(0x55a550, make, 4)
        # is it gold; a real item (a stand's, asked if misc, type 0x34) never is
        e.hook(0x629bb0, lambda e: int(e.arg(0) <= len(self.made) and self.made[e.arg(0) - 1][0] == "gld"), 2)
        e.hook(0x625480, lambda e: 256, 3)             # its coins (stat 0xe): 256, so the mul shows as is
        e.hook(0x530ea0, lambda e: self.made[-1].__setitem__(2, e.arg(0)), 1)
        e.hook(0x5589a0, lambda e: 0, 0)               # gold find: no killer
        e.hook(0x535790, lambda e: self.players, 0)    # the /players count
        e.hook(0x44be50, lambda e: 0, 0)
        e.hook(0x5585d0, lambda e: self.mf, 0)

    def roll(self, seed, tc, lvl, ilvl, players, mf):
        e = self.e
        rec = e.call(0x654e00, self.index[tc], lvl)
        up = self.names[(rec - e.r32(0x96c5ec)) // 0x2c]
        e.w32(self.unit + 0x20, seed)
        e.w32(self.unit + 0x24, 666)
        self.made, self.players, self.mf = [], players, mf
        e.call(0x55a6d0, 0, rec, 0, ilvl, 0, 0, 0, 0, ecx=self.game, edx=self.unit)
        items = "".join(f" {c}:{q}" + (f"*{m}" if m else "") for c, q, m in self.made)
        return f"{seed:08x} {tc}@{lvl}>{up} i{ilvl} p{players} m{mf}:{items} -> {e.r32(self.unit + 0x20):08x}"

    def maxdur(self):
        base = self.stats.get(0x49, 0) + self.mods.get((0x49, 0), 0)
        return base + base * self.mods.get((0x4b, 0), 0) // 100

    def make(self, seed, code, ilvl, quality, bovine, diff=0):
        e = self.e
        item, data, args, gs = self.item, self.data, self.args, self.seed
        for a, n in ((item, 0x200), (data, 0x200), (args, 0x100)): e.mu.mem_write(a, b"\0" * n)
        e.w32(item, 4)
        e.w32(item + 4, self.codes.index(code))
        e.w32(item + 0x14, data)
        e.w32(gs, seed)
        e.w32(gs + 4, 666)
        e.call(0x552df0, ecx=item, edx=gs)
        e.call(0x552e90, ecx=item, edx=gs)
        unit_seed, own_seed = e.r32(item + 0x20), e.r32(data + 4)
        e.call(0x62a6c0, item, 101)
        e.call(0x628220, item, ilvl)
        e.w32(args + 0xc, ilvl)
        e.w32(args + 0x80, bovine)
        e.w32(args + 0xfc, item)                       # FUN_00557ab0's EDX: where the item's pointer is
        self.stats, self.mods, self.sets = {}, {}, {}
        e.mu.mem_write(self.game + 0x6d, bytes([diff]))
        if quality <= 3:                               # FUN_00557450 too: the quality's own rolls, ethereal, sockets, automagic
            e.w32(args + 0x30, quality)
            e.call(0x557ab0, args, 1, ecx=self.game, edx=args + 0xfc)
            st = self.stats
            props = ",".join(f"{s}:{p}={((v & 0xffffffff ^ 0x80000000) - 0x80000000) >> self.shift.get(s, 0)}" for (s, p), v in sorted(self.mods.items()) if v)
            return (f"{seed:08x} {code} i{ilvl} q{quality} d{diff}: seeds {unit_seed:08x} {own_seed:08x} qty {st.get(0x46, 0)}"
                    f" dur {st.get(0x48, 0)}/{st.get(0x49, 0)} def {st.get(0x1f, 0)} | q{e.r32(data)} sub {e.s32(data + 0x28)}"
                    f" flags {e.r32(data + 0x18) & 0x400800:x} sock {st.get(0xc2, 0)} pic {e.read(data + 0x49, 1)[0]}"
                    f" auto {max(e.r16(data + 0x36) - self.auto0, 0)} [{props}] -> {e.r32(gs):08x} {e.r32(item + 0x20):08x} {e.r32(data + 4):08x}")
        e.call(0x557ab0, args, 0, ecx=self.game, edx=args + 0xfc)
        pick, made = -1, ""
        if quality in (5, 7):
            e.call(0x629df0, item, 0xFFFFFFFF)         # no unique / set yet (a quest unique without a row keeps it)
            ok = e.call(0x5566b0, self.game, item, args) if quality == 7 else e.call(0x5c2940, ecx=item, edx=args)
            if ok & 0xFF:
                pick = struct.unpack("<i", struct.pack("<I", e.call(0x629da0, item)))[0]
                e.w32(data, quality)
                e.call(0x556ca0, regs={"ebx": args, "esi": item})   # FUN_00557450's tail: ethereal (not a set)
                props = ",".join(f"{s}:{p}={((v & 0xffffffff ^ 0x80000000) - 0x80000000) >> self.shift.get(s, 0)}" for (s, p), v in sorted(self.mods.items()) if v)
                sets = "".join(f" {i}[" + ",".join(f"{s}:{p}={((v & 0xffffffff ^ 0x80000000) - 0x80000000) >> self.shift.get(s, 0)}" for (s, p), v in sorted(self.sets[i].items()) if v) + "]" for i in sorted(self.sets))
                made = f" flags {e.r32(data + 0x18) & 0x400000:x} [{props}]{sets} {e.r32(data + 4):08x}"
            elif self.stats.get(0x49, 0):
                for s in (0x48, 0x49): self.stats[s] = min(self.stats[s] * (3 if quality == 7 else 2), 255)
        st = self.stats
        return (f"{seed:08x} {code} i{ilvl} q{quality}{' cow' if bovine else ''}: seeds {unit_seed:08x} {own_seed:08x} gold {st.get(0xe, 0)}"
                f" qty {st.get(0x46, 0)} dur {st.get(0x48, 0)}/{st.get(0x49, 0)} def {st.get(0x1f, 0)} pick {pick}{made} -> {e.r32(gs):08x}")

    def entries(self, i):
        """Class i as `name:prob` over its entries (the expansion cumulative, +0xc total)."""
        e = self.e
        rec = e.r32(0x96c5ec) + i * 0x2c
        n, total, ents = e.s32(rec + 4), e.s32(rec + 0xc), e.r32(rec + 0x28)
        out = []
        for k in range(n):
            ent = ents + k * 0x1c
            start = e.s32(ent + 4)
            end = e.s32(ent + 0x1c + 4) if k + 1 < n else total
            idx, mul, flags = struct.unpack("<HHB", e.read(ent + 8, 5))
            name = self.names[idx] if flags & 4 else "?" if idx == 0xFFFF else self.codes[idx]
            if name == "gld" and mul: name += f",mul={mul}"
            out.append(f"{name}:{end - start}")
        return out


class Objects:
    def __init__(self, oracle):
        self.o = o = oracle
        e = o.e
        self.flags = item_flags(e, o.codes)
        self.lid, self.extra = 2, []
        self.room, self.obj, self.data, self.rgn = e.alloc(0x100), e.alloc(0x200), e.alloc(0x40), e.alloc(8)
        for addr, n, fn in ((0x620bb0, 1, lambda e: self.room), (0x61a1b0, 1, lambda e: self.lid), (0x621b30, 1, lambda e: 1),
                            (0x554010, 0, lambda e: o.game), (0x55f140, 0, lambda e: 1), (0x624690, 2, None), (0x5417d0, 4, None),
                            (0x623830, 1, None), (0x582510, 1, None), (0x5474c0, 0, None), (0x582280, 0, None), (0x620870, 2, None),
                            (0x553380, 1, None), (0x5531c0, 1, None), (0x571740, 0, None), (0x620c10, 2, None)):
            e.hook(addr, fn or (lambda e: 0), n)
        e.hook(0x585970, lambda e: self.extra.append(struct.pack("<I", e.arg(0)).decode("latin-1").strip()) or 0, 2)
        e.hook(0x559a30, lambda e: self.extra.append(e.read(self.obj + 0xb8, 4).decode("latin-1").strip()) or 0, 5)
        e.hook(0x62a0f0, lambda e: int(4 <= self.made(*o.made[e.arg(0) - 1][:2]) <= 9), 1)
        self.base = None
        e.hook(0x558d90, lambda e: self.__setattr__("base", e.s32(e.mu.reg_read(UC_X86_REG_EDX) + 0x14)) or 0, 1)
        e.hook(0x555da0, lambda e: 1, 3)
        e.hook(0x64e810, lambda e: 1, 7)
        e.hook(0x463740, lambda e: 0, 1)
        self.stand_levels = {}                         # ilvl: a (difficulty, level) of area level ilvl + 1 (1: 1)
        for d in range(3):
            for l in range(1, 137):
                a = e.call(0x61dca0, l, d, 1)
                if a: self.stand_levels.setdefault(a - (a > 1), (d, l))

    def made(self, code, quality):
        return made_quality(self.flags[self.o.codes.index(code)], quality)

    def open(self, seed, unit, op, cls, lid, diff, locked, sparkle):
        o, e = self.o, self.o.e
        fn = {1: 0x586410, 3: 0x5866c0, 4: 0x585f60, 5: 0x5868a0, 14: 0x5867a0, 26: 0x584060}[op]
        e.mu.mem_write(self.obj, b"\0" * 0x200)
        e.w32(self.obj, 2); e.w32(self.obj + 4, cls); e.w32(self.obj + 0x14, self.data)
        e.w32(self.data, 1); e.w32(self.data + 4, 0x80 if locked else 0)
        e.w32(self.obj + 0x20, unit); e.w32(self.obj + 0x24, 666); e.w32(self.obj + 0x78, sparkle)
        e.w32(self.rgn, seed); e.w32(self.rgn + 4, 666)
        e.mu.mem_write(o.game + 0x6d, bytes([diff]))
        ctx = e.alloc(0x14)
        for k, v in enumerate((o.game, self.obj, 0, self.rgn, cls)): e.w32(ctx + 4 * k, v)
        self.lid, self.extra, o.made, o.players, o.mf = lid, [], [], 1, 0
        e.call(fn, ecx=ctx)
        items = "".join(f" {c}:{q}" + (f"*{m}" if m else "") for c, q, m in o.made)
        shut = " shut" if op == 1 and not o.made else ""
        return (f"{seed:08x} {cls} L{lid} d{diff} {'L' if locked else ''}{'S' if sparkle else ''} op{op}:{items} |"
                + "".join(f" {x}" for x in self.extra) + f"{shut} -> {e.r32(self.rgn):08x} {e.r32(self.obj + 0x20):08x}")

    def stand(self, seed, weapon, ilvl):
        o, e = self.o, self.o.e
        e.w32(self.room + 0x6c, seed); e.w32(self.room + 0x70, 666)
        diff, self.lid = self.stand_levels[ilvl]
        e.mu.mem_write(o.game + 0x6d, bytes([diff]))
        self.base = None
        xy = e.alloc(8)
        e.call(0x559630 if weapon else 0x5594c0, xy, 0xFFFFFFFF, 0, 0, ecx=o.game, edx=self.room)
        if self.base is None: return f"{seed:08x} {'w' if weapon else 'a'} i{ilvl}: :0 -> {e.r32(self.room + 0x6c):08x}"
        after = e.r32(self.room + 0x6c)
        item, data, args = e.alloc(0x100), e.alloc(0x100), e.alloc(0x100)
        e.w32(item, 4); e.w32(item + 4, self.base); e.w32(item + 0x14, data)
        e.call(0x552df0, ecx=item, edx=self.room + 0x6c)
        e.call(0x552e90, ecx=item, edx=self.room + 0x6c)
        e.call(0x62a6c0, item, 101)
        e.w32(args + 0xc, ilvl); e.w32(args + 0x80, 0x40)
        code = o.codes[self.base]
        quality = self.made(code, e.call(0x556f60, item, args))
        return f"{seed:08x} {'w' if weapon else 'a'} i{ilvl}: {code}:{quality} -> {after:08x}"


def object_jobs(e, first, last):
    """Every container class in turn, at varied levels, difficulties, locks and sparkle; seeds spread."""
    rows = [r for r in txt_rows(e, "objects") if r.get("OperateFn") in ("1", "3", "4", "5", "14", "26")]
    for s in range(first, last + 1):
        r = rows[s % len(rows)]
        op, init = int(r["OperateFn"]), r.get("InitFn")
        locked = int(op == 4 and r.get("Lockable") == "1" and s // len(rows) % 2 == 1)
        sparkle = int(init == "57" and s // len(rows) % 3 != 0)
        yield (s * 0x9E3779B1) & 0xFFFFFFFF, (s * 0x85EBCA6B + 7) & 0xFFFFFFFF, op, int(r["Id"]), 2 + s % 38, s // 7 % 3, locked, sparkle


def run_ours(args, stdin):
    """drop-dump's answer, a line per input line (the tail: loading logs to stdout first)."""
    env = dict(os.environ)
    env.setdefault("D2_PATCH_INSTALLER", str(Path.home() / "Downloads/Diablo II + LoD/patch/LODPatch_114d.exe"))
    return subprocess.run([str(emu.ROOT / "build/tools/drop-dump/drop-dump"), emu.data_path()] + args, input=stdin, env=env,
                          capture_output=True, text=True, check=True).stdout.splitlines()[-stdin.count("\n"):]


def item_jobs(o, first, last):
    """Every base in turn at varied ilvls and qualities; every other job a unique or set base, picked."""
    uniques = sorted({r["code"] for r in txt_rows(o.e, "UniqueItems") if r.get("code") in o.codes})
    sets = sorted({r["item"] for r in txt_rows(o.e, "SetItems") if r.get("item") in o.codes})
    for s in range(first, last + 1):
        seed, ilvl = (s * 0x9E3779B1) & 0xFFFFFFFF, 1 + s * 13 % 99
        if s % 2:
            i, quality = s // 2 % len(o.codes), (2, 4, 6, 3, 1, 3)[s // 2 % 6]
            if quality <= 3:                           # as FUN_00557450 makes it (low: weapons / armor only)
                quality = made_quality(o.flags[i], quality if quality > 1 or o.codes[i] in o.gear else 2)
                if quality <= 3:
                    if o.codes[i] != "elx":            # not elixirs: FUN_00556a60's magic affix
                        yield seed, o.codes[i], ilvl, quality, 0, s // 12 % 3
                    continue
            yield seed, o.codes[i], ilvl, quality, 0
        elif s % 4: yield seed, uniques[s // 4 % len(uniques)], ilvl, 7, 0
        else: yield seed, sets[s // 4 % len(sets)], ilvl, 5, int(s % 3 == 0)


def main():
    if sys.argv[1] in ("objects", "stands"):
        o = Oracle()
        objects = Objects(o)
        first, last = (int(v, 0) for v in sys.argv[2].split("-"))
        if sys.argv[1] == "objects": todo = list(object_jobs(o.e, first, last))
        else:
            ilvls = sorted(objects.stand_levels)
            todo = [((s * 0x9E3779B1) & 0xFFFFFFFF, s % 2, ilvls[s // 2 % len(ilvls)]) for s in range(first, last + 1)]
        game = [(objects.open if sys.argv[1] == "objects" else objects.stand)(*j) for j in todo]
        if sys.argv[-1] == "print":
            print("\n".join(game))
            return
        ours = run_ours([sys.argv[1]], "".join("\t".join(map(str, j)) + "\n" for j in todo))
        bad = [(g, u) for g, u in zip(game, ours) if g != u]
        for g, u in bad[:5]: print(f"game: {g}\nours: {u}")
        print(f"{len(game) - len(bad)}/{len(game)} {sys.argv[1]} match game.exe")
        sys.exit(1 if bad or len(game) != len(ours) else 0)
    if sys.argv[1] == "items":
        o = Oracle(items=True)
        first, last = (int(v, 0) for v in sys.argv[2].split("-"))
        todo = list(item_jobs(o, first, last))
        game = [o.make(*j) for j in todo]
        if sys.argv[-1] == "print":
            print("\n".join(game))
            return
        ours = run_ours(["items"], "".join("\t".join(map(str, j)) + "\n" for j in todo))
        bad = [(g, u) for g, u in zip(game, ours) if g != u]
        for g, u in bad[:5]: print(f"game: {g}\nours: {u}")
        print(f"{len(game) - len(bad)}/{len(game)} items match game.exe")
        sys.exit(1 if bad or len(game) != len(ours) else 0)
    o = Oracle()
    if sys.argv[1] == "tables":
        game = [f"{n} {o.index[n]}:" + "".join(" " + x for x in o.entries(o.index[n])) for n in o.names if n]
        ours = run_ours(["tables"], "\n".join(n for n in o.names if n) + "\n")
        bad = [(g, u) for g, u in zip(game, ours) if g.split(":", 1)[1] != u.split(":", 1)[1] and u.split(":", 1)[0] not in KNOWN]
        for g, u in bad[:5]: print(f"game: {g}\nours: {u}")
        print(f"{len(game) - len(bad)}/{len(game)} classes match game.exe (not checked: {', '.join(sorted(KNOWN))})")
        sys.exit(1 if bad or len(game) != len(ours) else 0)
    if sys.argv[1] == "act1":                        # tests/test_game.cpp's lines and their FNV-1a
        act1 = sorted(n for n in o.names if n.startswith(("Act 1 ", "Andariel", "Countess", "Cow")))
        text = "".join(o.roll(*j) + "\n" for j in jobs([""] + act1, 1, 3000))
        h = 0xcbf29ce484222325
        for b in text.encode("latin-1"): h = (h ^ b) * 0x100000001b3 & 0xFFFFFFFFFFFFFFFF
        print(f"{text}{h:016x}")
        return
    first, last = (int(v, 0) for v in sys.argv[1].split("-"))
    todo = list(jobs(o.names, first, last))
    game = [o.roll(*j) for j in todo]
    if sys.argv[-1] == "print":
        print("\n".join(game))
        return
    ours = run_ours([], "".join("\t".join(map(str, j)) + "\n" for j in todo))
    bad = [(g, u) for g, u, j in zip(game, ours, todo) if g != u and j[1] not in KNOWN]
    for g, u in bad[:5]: print(f"game: {g}\nours: {u}")
    print(f"{len(game) - len(bad)}/{len(game)} rolls match game.exe (not checked: {', '.join(sorted(KNOWN))})")
    sys.exit(1 if bad or len(game) != len(ours) else 0)


if __name__ == "__main__":
    main()
