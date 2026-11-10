# SPDX-License-Identifier: GPL-3.0-or-later
"""The get-hit decision (FUN_0057cee0 -> FUN_0057cb00): the oracle for `test_combat --get-hit`.

    uv run python hit_rule.py gen 100000 1 > cases.tsv
    uv run python hit_rule.py [--exe PATH] game cases.tsv > g.txt
    ../../build/tests/test_combat --get-hit < cases.tsv | diff g.txt -

A case line is one damage record landing on one unit: `id kind maxlife
damage hc rec64 fire ltng cold poison f2000 mgh stun frozen seedlo seedhi
next`. kind 0 a player, 1 a monster; maxlife and damage in 256ths (the
record's total, +0x4c); hc the record's hit class (+0x60) and rec64 its
+0x64 byte; fire / ltng / cold / poison its fields +0x10 / +0x1c / +0x24 /
+0x28; f2000 its flag 0x2000 (a doubled hit); mgh MonStats2's GH mode
(FUN_0046c140 bit 3); stun state 0x15, frozen state 1; seedlo / seedhi the
unit's seed (+0x20) at the frame's first record. `next` 1: the following
line lands on the same unit in the same frame, its seed carrying on.

`game` runs FUN_0057cee0 (the unit takes the record) natively for each
record, with FUN_0057ce30 (the hit class and its element bits), FUN_0057cb00
(the size test) and FUN_00472210 (the seed's rolls) inside it. A frame is a
get-hit when a record sets the unit's GH mode: FUN_005809d0 mode 4 for a
player, FUN_005a7e60 mode 3 for a monster. It prints `id gh -1` a frame,
id the frame's first line, as test_combat does.

Hooked: FUN_00620bb0 (the unit's room: none, so no town test),
FUN_00639df0 (a state: 1 is `frozen`, 0x36 none), FUN_006256b0 (state
0x15's record: `stun`), FUN_00625d10 (max life), FUN_0046c140 (`mgh`),
FUN_005809d0 / FUN_005a7e60 (the mode asked: recorded), and the rest of
the mode change (FUN_005a7c20, FUN_005a43a0, FUN_0064c040, FUN_005734c0:
nothing). FUN_005a54f0 is hooked to do nothing: it adds the knockback flag
for MonStats rows whose base is 0x4e, which needs the data tables. Each
record has flags 1 (it hit) and 4 (it landed), as a landed blow's do.
"""
import random
import sys

import emu

TAKE_RECORD = 0x57CEE0
ROOM, STATE, STATE_RECORD, MAX_LIFE, HAS_MODE = 0x620BB0, 0x639DF0, 0x6256B0, 0x625D10, 0x46C140
PLAYER_MODE, MONSTER_MODE, MONSTER_EVENT, MONSTER_AFTER, UNIT_EVENT, UNIT_STOP, MONSTER_FLAGS = (
    0x5809D0, 0x5A7E60, 0x5A7C20, 0x5A43A0, 0x64C040, 0x5734C0, 0x5A54F0)
PLAYER_GH, MONSTER_GH = 4, 3


def gen(count, seed):
    """Print `count` case lines from `seed`: a frame of one to four records on one unit, near the size bands."""
    rng = random.Random(seed)
    seq = 0
    while seq < count:
        kind = rng.randint(0, 1)
        maxlife = rng.choice((rng.randint(1, 60), rng.randint(1, 600), rng.randint(1, 8000))) << 8 | rng.randint(0, 255)
        mgh = 1 if kind == 0 else int(rng.random() < 0.8)
        stun = int(rng.random() < 0.05)
        frozen = int(rng.random() < 0.08)
        seed_low, seed_high = rng.getrandbits(32), rng.getrandbits(32)
        records = 1 if rng.random() < 0.75 else rng.randint(2, 4)
        for record in range(records):
            hit_class = rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, rng.randint(0, 0x5f)))
            divisor = 16 if hit_class not in (2, 4, 5, 6, 8, 10, 11) else {2: 8, 6: 8, 10: 8, 11: 8, 4: 32, 8: 32, 5: 64}[hit_class]
            band = rng.choice((maxlife // divisor, maxlife // (divisor // 2), maxlife // (divisor // 4), maxlife // 16, maxlife // 8))
            damage = max(0, band + rng.choice((-1, 0, 1, rng.randint(-300, 300))))
            if rng.random() < 0.15:
                damage = rng.randint(0, maxlife)
            elements = [0, 0, 0, 0]
            if rng.random() < 0.3:
                elements[rng.randint(0, 3)] = rng.choice((1, -1, rng.randint(1, max(1, damage))))
            fire, ltng, cold, poison = elements
            if rng.random() < 0.05:
                poison = damage
            f2000 = int(rng.random() < 0.1)
            rec64 = int(rng.random() < 0.1)
            last = record == records - 1
            print(seq, kind, maxlife, damage, hit_class, rec64, fire, ltng, cold, poison, f2000, mgh, stun, frozen,
                  seed_low, seed_high, 0 if last else 1)
            seq += 1


def game(path, exe=None):
    """Run each case line of `path` through FUN_0057cee0 and print `id gh -1` a frame."""
    e = emu.Emu(exe=exe)
    case = {}
    asked = []

    def state(e):
        """FUN_00639df0(unit, state)"""
        return case["frozen"] if e.arg(1) == 1 else 0

    e.hook(ROOM, lambda e: 0, 1)
    e.hook(STATE, state, 2)
    e.hook(STATE_RECORD, lambda e: case["stun"] if e.arg(1) == 0x15 else 0, 2)
    e.hook(MAX_LIFE, lambda e: case["maxlife"], 1)
    e.hook(HAS_MODE, lambda e: case["mgh"] if e.mu.reg_read(emu.UC_X86_REG_EDX) == MONSTER_GH else 0, 0)
    e.hook(PLAYER_MODE, lambda e: asked.append(e.arg(1)) or 0, 5)
    e.hook(MONSTER_MODE, lambda e: asked.append(e.arg(1)) or 0, 3)
    e.stub(0, {MONSTER_EVENT: 3, MONSTER_AFTER: 0, UNIT_EVENT: 1, UNIT_STOP: 0, MONSTER_FLAGS: 2})

    game_ptr, unit, record = e.alloc(0x100), e.alloc(0x100), e.alloc(0x100)
    carry, frame_id, frame_gh = False, 0, False
    with open(path) as cases:
        lines = cases.readlines()
    for line in lines:
        if line.startswith("#"):
            continue
        fields = list(map(int, line.split()))
        if len(fields) != 17:
            continue
        (seq, kind, maxlife, damage, hit_class, rec64, fire, ltng, cold, poison, f2000, mgh, stun, frozen,
         seed_low, seed_high, nxt) = fields
        case.update(maxlife=maxlife, mgh=mgh, stun=stun, frozen=frozen)
        if not carry:
            e.mu.mem_write(unit, b"\0" * 0x100)
            e.w32(unit + 0x20, seed_low)
            e.w32(unit + 0x24, seed_high)
            frame_id, frame_gh = seq, False
        e.w32(unit, kind)                                      # unit type
        e.w32(unit + 0x10, 1)                                  # a live mode (not 0 death, 0xc dead)
        e.mu.mem_write(record, b"\0" * 0x100)
        e.mu.mem_write(record + 4, (1 | 4 | (0x2000 if f2000 else 0)).to_bytes(2, "little"))
        for offset, value in ((0x10, fire), (0x1C, ltng), (0x24, cold), (0x28, poison), (0x4C, damage), (0x60, hit_class)):
            e.w32(record + offset, value)
        e.mu.mem_write(record + 0x64, bytes([rec64]))
        asked.clear()
        e.call(TAKE_RECORD, unit, record, ecx=game_ptr, edx=0)
        frame_gh = frame_gh or (PLAYER_GH if kind == 0 else MONSTER_GH) in asked
        carry = nxt != 0
        if not carry:
            print(frame_id, int(frame_gh), -1)


def main():
    """`gen N SEED` or `[--exe PATH] game CASES`."""
    args = sys.argv[1:]
    exe = None
    if "--exe" in args:
        at = args.index("--exe")
        exe = args[at + 1]
        del args[at:at + 2]
    if len(args) == 3 and args[0] == "gen":
        gen(int(args[1]), int(args[2]))
    elif len(args) == 2 and args[0] == "game":
        game(args[1], exe)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
