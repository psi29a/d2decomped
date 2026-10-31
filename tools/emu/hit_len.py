# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d's get-hit (hit recovery) LENGTH, by its own code: the oracle for the get-hit length.

Runs game.exe's own code under unicorn through emu.py (next to this file):

    uv run python hit_len.py gen 100000 7 > cases.tsv
    uv run python hit_len.py game cases.tsv > game.tsv            # about a minute
    g++ -O2 -std=c++20 -o hit_len_harness hit_len_harness.cpp     # d2d before the patch
    ./hit_len_harness < cases.tsv > before.tsv
    <build>/tests/test_combat --get-hit-length < cases.tsv > after.tsv    # d2d now
    uv run python hit_len.py compare game.tsv before.tsv cases.tsv    # the cases that differ, and the count
    uv run python hit_len.py compare game.tsv after.tsv cases.tsv     # 0 of 110836 differ

game.exe and the game's MPQs are found as the rest of tools/emu finds them
($D2_MPQ_DIR, or `data =` in d2d.cfg); `--exe PATH game cases.tsv` runs another
copy of game.exe. No game data is in this tree.

A case is one unit put into its get-hit mode on the server: `id kind speed
frames fhr anim s67 s68 s69 frame`: kind 0 a player (mode 4), 1 a monster
(mode 3); speed and frames the AnimData record's (+0xc, +8); fhr the unit's
stat 99 (faster hit recovery); anim d2d's monster anim_pct (an aura's
other_animrate: d2d applies it to the GH length, game.exe's GH branch never
reads it); s67 / s68 / s69 the unit's velocitypercent / attackrate /
other_animrate (noise: the GH branch reads none of them); frame the game
frame (+0xa8) at the mode set.

`game` runs, per case, two pieces of game.exe natively and prints
`id rate ticks`:

1. FUN_00623f50 (the per-tick animation rate at unit +0x4c) from 0x624020,
   the instruction after the cast-mode test (FUN_006216e0 needs the skill
   tables): the mode dispatch (block 0x624033 / 0x624038, get-hit 0x624095 /
   0x62409a, the walk, attack and other branches after it) and the get-hit
   branch 0x62409f..0x624385: FUN_00621740(unit, row 1) natively (the table
   row at 0x6e8e30: stat 99, factor 120, 120 v / (120 + v) for v != 0),
   + 50, x AnimData speed / 100, floored, at least 0, at most 0x7fff.
   Hooked: FUN_00625500 and FUN_00625480 (the stat getters: the case's value
   by stat id) and FUN_00639df0 (the state test, 0: only the block branch
   asks).
2. FUN_005539b0 (the server's mode events, SUnit): with the start frame
   FUN_00623b10 (natively; 0 for GH) << 8, the rate at +0x4c and the end at
   +0x48 (frames << 8, as FUN_005533d0 leaves it) it walks the animation a
   tick at a time and queues the mode-end event (type 1) through
   FUN_005417d0 (hooked: recorded) at game frame + max(n, 1) + 1 where n is
   the number of ticks whose frame stays under the end; FUN_00581020 (event
   1, player) then plays NU through FUN_0057f020; a monster's FUN_005a8030
   goes to NU through FUN_005a7c20. `ticks` is that event frame minus the
   case's frame: how many server ticks the get-hit mode holds.

`gen N SEED` writes the cases: every AnimData get-hit record of the Act 1
cast (Sorceress 8/256 .. Zombie 5/152) and a grid of speeds and frame
counts at every FHR 0..300 (boundary), then N random ones. `compare` prints
the cases whose ticks differ and the count.
"""
import random
import sys

RECORDS = [(256, 8), (256, 7), (152, 5), (256, 5), (256, 4), (256, 6), (256, 9), (256, 10), (256, 11), (256, 12), (256, 3),
           (208, 8), (224, 7), (176, 6), (320, 8), (192, 10), (512, 8), (128, 8)]


def main():
    args = sys.argv[1:]
    exe = None
    if "--exe" in args:
        at = args.index("--exe")
        exe = args[at + 1]
        del args[at:at + 2]
    what = args[0] if args else ""
    if what == "gen":
        gen(int(args[1]), int(args[2]))
    elif what == "game":
        game(exe, args[1])
    elif what == "compare":
        compare(args[1], args[2], args[3])
    else:
        raise SystemExit(__doc__)


def gen(n, seed):
    rng = random.Random(seed)
    print("# id kind speed frames fhr anim s67 s68 s69 frame")
    i = 0
    for kind in (0, 1):
        for speed, frames in RECORDS:
            for fhr in range(0, 301):
                print(i, kind, speed, frames, fhr, 0, 0, 0, 0, 1000 + i % 977)
                i += 1
    for _ in range(n):
        kind = rng.randrange(2)
        r = rng.random()
        if r < 0.4:
            speed, frames = rng.choice(RECORDS)
        elif r < 0.7:
            speed, frames = rng.choice((128, 152, 176, 192, 208, 224, 240, 256, 288, 320, 384, 448, 512)), rng.randrange(1, 31)
        else:
            speed, frames = rng.randrange(1, 1025), rng.randrange(1, 61)
        r = rng.random()
        fhr = 0 if r < 0.3 else rng.randrange(0, 101) if r < 0.7 else rng.randrange(0, 1001)
        anim = 0 if kind == 0 or rng.random() < 0.8 else rng.choice((10, 20, 30, 40, 50, 75, 100, -30, -50))
        s67, s68, s69 = (rng.choice((0, -50, 20, 100)) for _ in range(3))
        print(i, kind, speed, frames, fhr, anim, s67, s68, s69, rng.randrange(1, 1 << 20))
        i += 1


def cases(path):
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            v = [int(x) for x in line.split()]
            yield dict(zip("id kind speed frames fhr anim s67 s68 s69 frame".split(), v))


def game(exe, path):
    import emu
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_ESP
    e = emu.Emu(exe)
    mu = e.mu
    stats = {}
    events = []

    def stat_getter(e_):
        return stats.get(e_.arg(1), 0)
    e.hook(0x625500, stat_getter, 3)
    e.hook(0x625480, stat_getter, 3)
    e.hook(0x639df0, lambda e_: 0, 2)
    e.hook(0x5417d0, lambda e_: events.append((e_.arg(0), e_.arg(1))) or 0, 4)
    unit = e.alloc(0x200)
    rec = e.alloc(0x100)
    gamep = e.alloc(0x2000)
    wheel = e.alloc(0x1000)
    e.w32(gamep + 0xb8, wheel)
    print("# id rate ticks")
    for c in cases(path):
        mode = 4 if c["kind"] == 0 else 3
        mu.mem_write(unit, b"\0" * 0x200)
        mu.mem_write(rec, b"\0" * 0x100)
        e.w32(unit, c["kind"])
        e.w32(unit + 4, 0)
        e.w32(unit + 0x10, mode)
        e.w32(unit + 0x50, rec)
        e.w32(unit + 0x2c, 1)              # a path: FUN_005533d0's branch for a moving unit (not run here)
        e.w32(rec + 0xc, c["speed"])
        e.w32(rec + 8, c["frames"])
        stats.clear()
        stats.update({99: c["fhr"], 67: c["s67"], 68: c["s68"], 69: c["s69"]})
        # 1. FUN_00623f50 from 0x624020: the frame as the prologue left it
        frame = emu.STACK + emu.STACK_SIZE - 0x2000
        e.w32(frame + 4, emu.RET_MAGIC)
        e.w32(frame + 8, c["kind"])        # [ebp+8]: the unit type
        e.w32(frame - 4, 0)                # [ebp-4]: the class
        e.w32(frame - 8, mode)             # [ebp-8]: the mode
        e.w32(frame - 0xc, rec)            # [ebp-0xc]: the AnimData record
        e.w32(frame - 0x14, 0)             # pushed edi
        e.w32(frame - 0x18, 0)             # pushed esi
        mu.reg_write(UC_X86_REG_EBP, frame)
        mu.reg_write(UC_X86_REG_ESP, frame - 0x18)
        mu.reg_write(UC_X86_REG_ESI, rec)
        mu.reg_write(UC_X86_REG_EDI, unit)
        mu.reg_write(UC_X86_REG_EBX, 0)
        mu.emu_start(0x624020, emu.RET_MAGIC)
        rate = e.r16(unit + 0x4c)
        if rate >= 0x8000:
            rate -= 0x10000
        # 2. FUN_005533d0's state for a plain mode, then FUN_005539b0
        e.w32(unit + 0x30, 0)
        e.w32(unit + 0x44, 0)
        e.w32(unit + 0x48, c["frames"] << 8)
        e.w32(gamep + 0xa8, c["frame"])
        events.clear()
        e.call(0x5539b0, ecx=gamep, edx=unit)
        ends = [f for (t, f) in events if t == 1]
        if len(ends) != 1 or any(t != 1 for (t, _) in events):
            raise SystemExit(f"case {c['id']}: events {events}")
        print(c["id"], rate, ends[0] - c["frame"])


def compare(a, b, path):
    def load(p):
        out = {}
        with open(p) as f:
            for line in f:
                if line.startswith("#") or not line.strip():
                    continue
                v = line.split()
                out[int(v[0])] = int(v[-1])
        return out
    ga, gb = load(a), load(b)
    by_id = {c["id"]: c for c in cases(path)}
    differ = 0
    shown = 0
    for i in sorted(ga):
        if i not in gb:
            raise SystemExit(f"case {i} missing in {b}")
        if ga[i] != gb[i]:
            differ += 1
            if shown < 20:
                c = by_id[i]
                print(f"differ id {i} kind {c['kind']} speed {c['speed']} frames {c['frames']} fhr {c['fhr']} anim {c['anim']}: game {ga[i]} other {gb[i]}")
                shown += 1
    print(f"{differ} of {len(ga)} differ")


if __name__ == "__main__":
    main()
