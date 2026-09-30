"""game.exe's drop roller as the oracle (docs/research/re/drops.md).

    uv run python drops.py 1-20000          # diff tools/drop-dump against game.exe, one job per seed
    uv run python drops.py 1-20000 print    # just game.exe's lines
    uv run python drops.py tables           # every class's entries, game.exe vs ours

A job is (seed, class, level, ilvl, players, mf): FUN_00654e00 moves the
class on by level (as FUN_0055afa0 does past normal), then FUN_0055a6d0
rolls it off a monster whose unit seed is {seed, 666}, expansion, no
killer (FUN_005585d0, the killer's magic find, hooked to answer mf). FUN_0055a550 (make the item) is hooked to record what it's asked
for, so the unit seed is the only state the roll touches. The line is what
dropped (code:quality, *mul for gold) and the seed's low word after.

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
    def __init__(self):
        self.e = e = drlg.boot()
        self.names = class_names(e)
        self.codes = item_codes(e)
        self.index = {n: i for i, n in enumerate(self.names) if n}
        self.game, self.unit = e.alloc(0x2000), e.alloc(0x200)
        e.w32(self.game + 0x70, 1)                     # expansion
        e.w32(self.unit, 1)                            # a monster
        self.made, self.players, self.mf = [], 1, 0

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


def main():
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
