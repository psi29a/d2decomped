"""game.exe's drop roller as the oracle (docs/research/re/drops.md).

    uv run python drops.py 1-20000          # diff tools/drop-dump against game.exe, one job per seed
    uv run python drops.py 1-20000 print    # just game.exe's lines
    uv run python drops.py tables           # every class's entries, game.exe vs ours
    uv run python drops.py items 1-20000    # made items (seeds, gold, base rolls, unique / set picks) vs ours

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
or doubles (set) the durability, as FUN_00557450 goes on to.

Runtime classes: DAT_0096c5ec, stride 0x2c; 0 empty, 1..160 the auto
classes (bow weap mele armo abow x 3..96), 161 + TreasureClassEx row after.
"""
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX

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
            self.stats = {}
            self.item, self.data, self.args, self.seed = e.alloc(0x200), e.alloc(0x200), e.alloc(0x100), e.alloc(8)
            e.hook(0x627260, lambda e: self.stats.__setitem__(e.arg(1), e.arg(2)), 4)   # set stat: recorded
            e.hook(0x625480, lambda e: self.stats.get(e.arg(1), 0), 3)
            e.hook(0x65fec0, lambda e: 0, 6)           # the item's event / packet
            return

        def make(e):                                   # FUN_0055a550(ecx item; game, quality, unique/set index, flags)
            self.made.append([self.codes[e.mu.reg_read(UC_X86_REG_ECX) & 0xFFFF], e.arg(1), 0])
            return len(self.made)
        e.hook(0x55a550, make, 4)
        e.hook(0x629bb0, lambda e: int(self.made[e.arg(0) - 1][0] == "gld"), 2)   # is it gold
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

    def make(self, seed, code, ilvl, quality, bovine):
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
        self.stats = {}
        e.call(0x557ab0, args, 0, ecx=self.game, edx=args + 0xfc)
        pick = -1
        if quality in (5, 7):
            ok = e.call(0x5566b0, self.game, item, args) if quality == 7 else e.call(0x5c2940, ecx=item, edx=args)
            if ok & 0xFF: pick = struct.unpack("<i", struct.pack("<I", e.call(0x629da0, item)))[0]
            elif self.stats.get(0x49, 0):
                for s in (0x48, 0x49): self.stats[s] = min(self.stats[s] * (3 if quality == 7 else 2), 255)
        st = self.stats
        return (f"{seed:08x} {code} i{ilvl} q{quality}{' cow' if bovine else ''}: seeds {unit_seed:08x} {own_seed:08x} gold {st.get(0xe, 0)}"
                f" qty {st.get(0x46, 0)} dur {st.get(0x48, 0)}/{st.get(0x49, 0)} def {st.get(0x1f, 0)} pick {pick} -> {e.r32(gs):08x}")

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
        if s % 2: yield seed, o.codes[s // 2 % len(o.codes)], ilvl, (2, 4, 6, 3)[s // 2 % 4], 0
        elif s % 4: yield seed, uniques[s // 4 % len(uniques)], ilvl, 7, 0
        else: yield seed, sets[s // 4 % len(sets)], ilvl, 5, int(s % 3 == 0)


def main():
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
