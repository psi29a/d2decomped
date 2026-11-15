# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's player block (BL, mode 9) animation LENGTH: the oracle for a block's hold.

    set -o pipefail; ../../build/tests/test_hit_recovery --block-length | grep '^D ' > d2d.tsv
    uv run python block_len.py game d2d.tsv > game.tsv        # game.exe's own code under unicorn (a few seconds)
    uv run python block_len.py compare game.tsv d2d.tsv       # the cells that differ, and the count
    uv run python block_len.py closed d2d.tsv                 # no emulator: d2d against the closed form below

A d2d.tsv line is `D class weapon fbr holy record frames speed ticks`: the ticks Fight::set_pmode(BL) holds for a player of that
class and main-hand weapon with that FBR and Holy Shield's state 0x65 on or off (the real code; frames and speed are the AnimData
record's). `game` prints `fbr holy record rate ticks` per line, from game.exe:

1. FUN_00623f50 from 0x624020 (the instruction after the cast-mode test, as hit_len.py): mode 9 takes the block branch
   0x62403d..0x624084: FUN_00621740 (row 3, stat 102, 120 v / (120 + v)) + 50, + 50 when the state test FUN_00639df0(0x65) is set,
   x the record's speed / 100, at least 1. Hooked as in hit_len.py: the stat getters FUN_00625500 / FUN_00625480 and the state test.
2. FUN_005539b0 (the mode events) with the start frame 0, the rate at unit +0x4c and the end at +0x48 (frames << 8): the mode-end
   event (type 1, FUN_005417d0 hooked) lands at game frame + max(n, 1) + 1, n the ticks whose frame stays under the end.

`closed` is the closed form of both (rate = max(speed (50 + E + 50 holy) / 100, 1), ticks = max((256 frames - 1) / rate, 1) + 1)
that the game-side run equalled in every cell it was run on (14,448 cells of game.exe's own code, run before this oracle); the emulator run is the
oracle, `closed` only lets the d2d side be read without it.
"""
import os
import sys


def eff(v):
    return int(120 * v / (120 + v)) if v != 0 else 0          # FUN_00621740: C idiv truncates toward zero


def closed(frames, speed, fbr, holy):
    rate = max(speed * (50 + eff(fbr) + (50 if holy else 0)) // 100, 1)
    return rate, max((frames * 256 - 1) // rate, 1) + 1


def lines(path):
    for line in open(path):
        f = line.split()
        if len(f) == 9 and f[0] == "D":
            yield dict(cls=int(f[1]), weapon=f[2], fbr=int(f[3]), holy=int(f[4]), record=f[5], frames=int(f[6]), speed=int(f[7]), ticks=int(f[8]))


def game(path):
    sys.path.insert(0, os.environ.get("EMU", "."))
    import emu
    from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_ESP
    e = emu.Emu(os.environ["GAMEEXE"])
    mu = e.mu
    stats, events, state65 = {}, [], [0]
    e.hook(0x625500, lambda e_: stats.get(e_.arg(1), 0), 3)
    e.hook(0x625480, lambda e_: stats.get(e_.arg(1), 0), 3)
    e.hook(0x639df0, lambda e_: state65[0] if e_.arg(1) == 0x65 else 0, 2)
    e.hook(0x5417d0, lambda e_: events.append((e_.arg(0), e_.arg(1))) or 0, 4)
    unit, rec, gamep, wheel = e.alloc(0x200), e.alloc(0x100), e.alloc(0x2000), e.alloc(0x1000)
    e.w32(gamep + 0xb8, wheel)
    cases = list(lines(path))
    if not cases:
        raise SystemExit("no cases in %s" % path)
    for c in cases:
        mu.mem_write(unit, b"\0" * 0x200)
        mu.mem_write(rec, b"\0" * 0x100)
        e.w32(unit + 0x10, 9)
        e.w32(unit + 0x50, rec)
        e.w32(unit + 0x2c, 1)
        e.w32(rec + 0xc, c["speed"])
        e.w32(rec + 8, c["frames"])
        stats.clear()
        stats[102] = c["fbr"]
        state65[0] = c["holy"]
        frame = emu.STACK + emu.STACK_SIZE - 0x2000
        e.w32(frame + 4, emu.RET_MAGIC)
        e.w32(frame + 8, 0)            # unit type 0: a player
        e.w32(frame - 4, 0)
        e.w32(frame - 8, 9)
        e.w32(frame - 0xc, rec)
        e.w32(frame - 0x14, 0)
        e.w32(frame - 0x18, 0)
        mu.reg_write(UC_X86_REG_EBP, frame)
        mu.reg_write(UC_X86_REG_ESP, frame - 0x18)
        mu.reg_write(UC_X86_REG_ESI, rec)
        mu.reg_write(UC_X86_REG_EDI, unit)
        mu.reg_write(UC_X86_REG_EBX, 0)
        mu.emu_start(0x624020, emu.RET_MAGIC)
        rate = e.r16(unit + 0x4c)
        if rate >= 0x8000:
            rate -= 0x10000
        e.w32(unit + 0x30, 0)
        e.w32(unit + 0x44, 0)
        e.w32(unit + 0x48, c["frames"] << 8)
        e.w32(gamep + 0xa8, 1000)
        events.clear()
        e.call(0x5539b0, ecx=gamep, edx=unit)
        ends = [f for (t, f) in events if t == 1]
        if len(ends) != 1:
            raise SystemExit("events %s" % events)
        print(c["fbr"], c["holy"], c["record"], rate, ends[0] - 1000)


def compare(game_path, d2d_path):
    want = {}
    for line in open(game_path):
        f = line.split()
        if len(f) == 5:
            want[(int(f[0]), int(f[1]), f[2])] = int(f[4])
    return report(lambda c: want.get((c["fbr"], c["holy"], c["record"])), d2d_path)


def report(oracle_ticks, d2d_path):
    diff = n = 0
    for c in lines(d2d_path):
        t = oracle_ticks(c)
        if t is None:
            continue
        n += 1
        if t != c["ticks"]:
            diff += 1
            if diff <= 12:
                print("DIFF", c["record"], "fbr", c["fbr"], "holy", c["holy"], "frames", c["frames"], "speed", c["speed"], "oracle", t, "d2d", c["ticks"])
    print("%d cells, %d differ" % (n, diff))
    if n == 0:
        print("no cell compared (empty input, or the two files share no cell)", file=sys.stderr)
        return 1
    return diff


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "game":
        game(a[1])
    elif len(a) == 3 and a[0] == "compare":
        sys.exit(1 if compare(a[1], a[2]) else 0)
    elif len(a) == 2 and a[0] == "closed":
        sys.exit(1 if report(lambda c: closed(c["frames"], c["speed"], c["fbr"], c["holy"])[1], a[1]) else 0)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
