#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Open Wounds (item_openwounds, stat 135): the life a hit takes over time, by game.exe's own code: the oracle for what d2d takes from a wounded monster.

    uv run python open_wounds.py gen 20000 7 > cases.tsv
    uv run python open_wounds.py game cases.tsv > g.tsv           # game.exe, under the emulator
    g++ -O2 -std=c++23 -I../../components/rules -I../../components -I../../components/d2s -I../../components/txt -o open_wounds_harness open_wounds_harness.cpp ../../build/components/rules/librules.a \\
        && ./open_wounds_harness < cases.tsv > d.tsv
    uv run python open_wounds.py compare cases.tsv g.tsv d.tsv     # exit 1 on a difference, an empty file or a malformed line

A case line: `id level kind event word`: `level` the attacker's level (stat 12), `kind` the struck unit (0 a player, 1 a
monster), `event` 5 (melee) or 6 (a missile), `word` the monster's data word +0x16 (its flags: 2 and 4 and 8 and 0x10
are the bits the spawners set; FUN_005a0180 tests a mask on it). `game` runs the item event handler for stat 135
(FUN_005bfe60: the event table at 0x7325b0, entry 15, the neighbour of Crushing Blow's FUN_005bffc0) with the chance at
100 and prints `id applied value duration stat state`: what it hands FUN_0056e970 (the timed stat change): the
stat (0x4a, life regeneration), its value (negative: 256ths of life a frame), the duration in frames and the state.
`d2d.tsv` is `id applied life` as d2d takes it: the life, in 256ths, the wound takes over its whole length (a player struck: `id -`).
The chance draw (`rand % 100` under the stat) is the one the crush handler makes; not run here.
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

HANDLER = 0x5BFE60
APPLY = 0x56E970


def gen(count, seed):
    rng = random.Random(seed)
    lines = []
    for level in range(1, 100):
        for kind, word in ((0, 0), (1, 0), (1, 2), (1, 4), (1, 8), (1, 0xA), (1, 0xC), (1, 0x10), (1, 0x12)):
            for event in (5, 6):
                lines.append((level, kind, event, word))
    for _ in range(count):
        lines.append((rng.randint(1, 99), rng.choice((0, 1, 1, 1)), rng.choice((5, 6)), rng.choice((0, 1, 2, 4, 8, 0xA, 0xC, 0x10, 0x14, rng.getrandbits(8)))))
    return [f"{index} {level} {kind} {event} {word}" for index, (level, kind, event, word) in enumerate(lines)]


def parse(line):
    words = line.split()
    if len(words) != 5:
        raise ValueError(f"bad case line {line!r}")
    return tuple(int(word) for word in words)


def read(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return [line for line in handle.read().splitlines() if line.strip()]
    except OSError as error:
        raise SystemExit(f"cannot read {path}: {error.strerror}") from error


def signed(value):
    return value - (1 << 32) if value >= 1 << 31 else value


def game(path):
    import monster_damage
    from unicorn.x86_const import UC_X86_REG_ECX

    oracle = monster_damage.Oracle()
    e = oracle.e
    seen = {}

    def chance(emu_):
        return 100 if emu_.arg(0) == seen["attacker"] else 0

    def apply(emu_):
        record = emu_.mu.reg_read(UC_X86_REG_ECX)
        seen["record"] = [emu_.r32(record + 4 * k) for k in range(8)]
        return 1

    e.hook(0x625500, chance, 3)
    e.hook(APPLY, apply, 0)
    data = e.alloc(0x40)
    attacker_addr = oracle.attacker
    seen["attacker"] = attacker_addr
    for line in read(path):
        index, level, kind, event, word = parse(line)
        defender = e.alloc(0x200)
        e.w32(defender, kind)
        e.w32(defender + 0x14, data)
        e.mu.mem_write(data + 0x16, int(word & 0xFFFF).to_bytes(2, "little"))
        oracle.stats.clear()
        oracle.stats[(attacker_addr, 12)] = level
        unit = e.view("unit", attacker_addr)
        unit.seed_low, unit.seed_high = 1234567, 7
        seen.pop("record", None)
        e.call(HANDLER, attacker_addr, defender, 0, 0x87, 0, edx=event)
        record = seen.get("record")
        if record is None:
            print(index, 0, 0, 0, 0, 0)
        else:
            print(index, 1, signed(record[6]), record[4], record[5], record[7])


def compare(cases, gfile, dfile):
    case_lines, game_lines, d2d_lines = read(cases), read(gfile), read(dfile)
    if not case_lines:
        raise SystemExit("compare: empty case file")
    if not (len(case_lines) == len(game_lines) == len(d2d_lines)):
        raise SystemExit(f"compare: lengths differ ({len(case_lines)}, {len(game_lines)}, {len(d2d_lines)})")
    bad, by_kind = 0, {}
    for case_line, g_line, d_line in zip(case_lines, game_lines, d2d_lines, strict=True):
        case, g, d = parse(case_line), g_line.split(), d_line.split()
        try:
            g_nums = [int(x) for x in g]
            if case[2] != 0:
                for x in d:
                    int(x)
        except ValueError:
            raise SystemExit(f"compare: non-numeric field near id {case[0]}") from None
        if len(g) != 6 or len(d) not in (2, 3) or g[0] != d[0] or g[0] != str(case[0]):
            raise SystemExit(f"compare: malformed or out-of-step line near id {case[0]}")
        if case[2] == 0:                                   # a player struck: d2d has no monster with Open Wounds (printed `id -`)
            if d != [str(case[0]), "-"]:
                raise SystemExit(f"compare: bad player row near id {case[0]}")
            by_kind.setdefault("player (not modelled)", [0, 0])[0] += 1
            continue
        applied, value, duration = int(g[1]), -int(g[2]), int(g[3])
        expect = [str(applied), str(value * duration if applied else 0)]
        # the record must be the one FUN_0056e970 gets: stat 0x4a (74), state 0x3e (62)
        differ = d[1:] != expect or (applied == 1 and (g_nums[4], g_nums[5]) != (74, 62))
        name = "special" if case[4] & 12 else "plain"
        row = by_kind.setdefault(name, [0, 0])
        row[0] += 1
        row[1] += differ
        bad += differ
    for name, (total, differ) in sorted(by_kind.items()):
        print(f"# {name}: {total} cases, {differ} differ")
    compared = sum(total for name, (total, _) in by_kind.items() if name != "player (not modelled)")
    print(f"# total: {compared} monster cases compared, {bad} differ")
    sys.exit(1 if bad or not compared else 0)


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("gen", "game", "compare"):
        raise SystemExit(__doc__)
    cmd = sys.argv[1]
    if cmd == "gen" and len(sys.argv) == 4:
        print("\n".join(gen(int(sys.argv[2]), int(sys.argv[3]))))
    elif cmd == "game" and len(sys.argv) == 3:
        game(sys.argv[2])
    elif cmd == "compare" and len(sys.argv) == 5:
        compare(*sys.argv[2:5])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
