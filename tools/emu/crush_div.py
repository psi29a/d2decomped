#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The life divisor of a Crushing Blow on a MONSTER defender, by game.exe's own code: FUN_005bffc0's branch on the struck unit.

    cd <d2decomped>/tools/emu
    uv run python crush_div.py gen [PLAYERS_MAX] > cases.tsv
    uv run python crush_div.py game cases.tsv > game.tsv            # id crush, 256ths (game.exe, every table loaded)
    <d2d side: test_crush_divisor --crush cases.tsv | grep -E '^[0-9]+ -?[0-9]+$' > d2d.tsv>
    uv run python crush_div.py compare cases.tsv game.tsv d2d.tsv   # exit 1 on a difference, an empty or malformed file

A case line is `id row kind players`: `row` a MonStats row (game.exe's table index), `kind` how the monster was made (0 plain, 1
champion, 2 unique, 3 superunique, 4 minion: the data word at +0x16 the creators leave, V read: FUN_005a48c0 and FUN_005a0320 set
1 | 4 | 8 for a champion, FUN_005a4940 and FUN_005a0320 set 1 | 8 for a unique, FUN_005a49b0 (a SuperUniques row, 0x5a4a99) and
FUN_005a4440 (a stored monster made again, 0x5a4599, when its record has 0x800, which 0x54230e sets from bit 2 only) add 2, a
minion has 0x11), `players` the game's player count (the monster's stat 0x64). `game` runs FUN_005bffc0 natively (a melee
strike, the attacker's Crushing Blow stat 100) on a type-1 unit of that row, with game.exe's MonStats row and FUN_0063ee90,
FUN_0063e9f0, FUN_005a0180 and FUN_005738f0 all running for real, on a unit at a life of 262,144 points and prints `id crush`: the life it takes, whole 256ths. The d2d side is the `crush_divisor` `Monster::target` gives `player_blow` for the same row, kind
and player count, the crush = life / divisor. Compared byte for byte.
"""
import re
import sys

M32 = 0xFFFFFFFF
LIFE = 1 << 26    # 256ths of a point: 262,144 points; no resistance, so MulDiv is not reached
FLAGS = {0: 0x0, 1: 0xD, 2: 0x9, 3: 0xB, 4: 0x11}
COLS = "id row kind players".split()


def s32(v): return v - (1 << 32) if v >> 31 else v


def rows():
    import emu
    import csv
    text = emu.mpq_cache().read("data\\global\\excel\\monstats.txt")
    table = list(csv.reader(text.decode("latin1").splitlines(), delimiter="\t"))
    return [r[0] for r in table[1:] if r and r[0] and r[0] != "Expansion"]


def gen(players_max):
    count = len(rows())
    print("#", " ".join(COLS), f"rows={count}", f"players=1..{players_max}")
    n = 0
    for row in range(count):
        for kind in FLAGS:
            for players in range(1, players_max + 1):
                print(f"{n}\t{row}\t{kind}\t{players}")
                n += 1


def read_cases(path):
    out = []
    try:
        lines = open(path, encoding="utf-8").read().splitlines()
    except OSError as err:
        raise SystemExit(f"cannot read {path}: {err.strerror}") from None
    for k, line in enumerate(lines, 1):
        if line.startswith("#") or not line.strip(): continue
        try:
            values = [int(v) for v in line.split()]
        except ValueError:
            values = []
        if len(values) != len(COLS): raise SystemExit(f"{path}:{k}: malformed case line: {line!r}")
        out.append(dict(zip(COLS, values)))
    if not out: raise SystemExit(f"{path}: no case lines")
    return out


def game(path):
    import drlg
    cases = read_cases(path)
    e = drlg.boot()
    st = {}
    attacker = e.alloc(0x100)
    target = e.alloc(0x200)
    data = e.alloc(0x40)
    rec = e.alloc(0x100)
    e.hook(0x625480, lambda e_: st.get((e_.arg(0), e_.arg(1)), 0) & M32, 3)
    e.hook(0x625500, lambda e_: st.get((e_.arg(0), e_.arg(1)), 0) & M32, 3)
    e.hook(0x627260, lambda e_: st.__setitem__((e_.arg(0), e_.arg(1)), s32(e_.arg(2))) or 0, 4)
    e.hook(0x621e40, lambda e_: 0, 3)
    for c in cases:
        st.clear()
        e.mu.mem_write(target, b"\0" * 0x200)
        e.mu.mem_write(data, b"\0" * 0x40)
        e.w32(target, 1); e.w32(target + 4, c["row"]); e.w32(target + 0x14, data)
        e.mu.mem_write(data + 0x16, FLAGS[c["kind"]].to_bytes(2, "little"))
        st[(attacker, 136)] = 100                                  # Crushing Blow 100 %: the draw always lands under it
        st[(target, 6)] = st[(target, 7)] = LIFE
        st[(target, 0x24)] = 0
        st[(target, 0x64)] = c["players"]
        e.w32(attacker + 0x20, 12345); e.w32(attacker + 0x24, 678)
        e.w32(rec + 4, 1)
        ok = e.call(0x5bffc0, attacker, target, rec, (136 << 16) | 0, edx=5)
        print(c["id"], LIFE - st[(target, 6)] if ok else 0)


def compare(cases_path, game_path, d2d_path):
    cases = read_cases(cases_path)
    def lines(path):
        try:
            return [l.split() for l in open(path, encoding="utf-8").read().splitlines() if l.strip()]
        except OSError as err:
            raise SystemExit(f"cannot read {path}: {err.strerror}") from None
    g, d = lines(game_path), lines(d2d_path)
    if not (len(cases) == len(g) == len(d)):
        raise SystemExit(f"compare: lengths differ ({len(cases)}, {len(g)}, {len(d)})")
    bad, kinds = 0, {}
    for c, gl, dl in zip(cases, g, d, strict=True):
        if len(gl) != 2 or len(dl) != 2 or gl[0] != dl[0] or gl[0] != str(c["id"]) or not (re.fullmatch(r"-?[0-9]+", gl[1]) and re.fullmatch(r"-?[0-9]+", dl[1])):
            raise SystemExit(f"compare: malformed or out-of-step line near id {c['id']}")
        differ = gl[1] != dl[1]
        row = kinds.setdefault(c["kind"], [0, 0])
        row[0] += 1; row[1] += differ; bad += differ
        if differ and bad <= 8: print(f"# differ: id {c['id']} row {c['row']} kind {c['kind']} players {c['players']}: game {gl[1]} d2d {dl[1]}")
    names = {0: "plain", 1: "champion", 2: "unique", 3: "superunique", 4: "minion"}
    for kind, (total, differ) in sorted(kinds.items()):
        print(f"# {names[kind]}: {total} cases, {differ} differ")
    print(f"# total: {len(cases)} cases, {bad} differ")
    sys.exit(1 if bad else 0)


def main():
    argv = sys.argv[1:]
    if argv[:1] == ["gen"] and len(argv) in (1, 2):
        gen(int(argv[1]) if len(argv) == 2 else 8)
    elif argv[:1] == ["game"] and len(argv) == 2:
        game(argv[1])
    elif argv[:1] == ["compare"] and len(argv) == 4:
        compare(*argv[1:])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
