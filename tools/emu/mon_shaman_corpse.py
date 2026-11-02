# SPDX-License-Identifier: GPL-3.0-or-later
"""A unique Fallen Shaman's corpse test: game.exe's own callback FUN_005f1380 on random candidates, against d2d's rule.

    uv run python mon_shaman_corpse.py gen N SEED > cases.tsv       (class flags corpse_bit mode dx dy aip4 game_accepts)
    c++ -std=c++26 -Icomponents/rules -Icomponents/d2s -Icomponents/txt -include cmake/no_near_far.hpp -o harness tools/emu/mon_shaman_corpse_harness.cpp
    harness [--old] < cases.tsv > d2d.tsv ; uv run python mon_shaman_corpse.py compare cases.tsv d2d.tsv

The callback (the room search FUN_005dd0b0 calls it for every unit near; the shaman picks that search when its monster flags
(FUN_005a0180) have bit 8 set and bit 4 clear) takes (candidate, ctx), ctx +0xc = aip4 squared (0x5f14c8..0x5f14d2). It runs natively;
hooked around it (answers outside the claim): the class (FUN_00463860), the corpse bookkeeping (FUN_006259b0, FUN_0063a770,
FUN_0063a320: none), the squared distance (FUN_005b0bd0: dx^2 + dy^2, as upstream's mon_ais.py hooks it), the flag test (FUN_005a0180).
Its own checks: type 1, class 0x13 / 0x3a, the corpse bit (+0xc4 bit 1), mode DD (+0x10 == 0xc), squared distance <= limit, flags & 0xc == 0.
d2d side: the real rules::shaman_corpse_reach (components/rules/monsters.hpp); --old is the arithmetic ai.cpp had (ai_distance < 80,
`unique` = Boss::unique / superunique, a champion not looked at), copied into the harness. Never runs a game."""
import random
import sys

CB = 0x5f1380


def gen(n, seed):
    import emu
    from unicorn.x86_const import UC_X86_REG_EDX
    rng = random.Random(seed)
    e = emu.Emu()
    cur = {}
    data = e.alloc(0x1000)
    e.w32(0x744304, data); e.w32(data + 0xa80, 0x200)
    me, cand, ctx = e.alloc(0x200), e.alloc(0x200), e.alloc(0x40)
    e.w32(me, 1); e.w32(cand, 1)
    e.hook(0x463860, lambda e: cur["class"], 0)
    e.hook(0x6259b0, lambda e: 0, 1)
    e.hook(0x63a770, lambda e: 0, 1)
    e.hook(0x63a320, lambda e: 0, 1)
    e.hook(0x5b0bd0, lambda e: cur["dx"] ** 2 + cur["dy"] ** 2, 0)
    e.hook(0x5a0180, lambda e: int(bool(cur["flags"] & e.mu.reg_read(UC_X86_REG_EDX))), 0)
    for _ in range(n):
        aip4 = rng.choice((24, 24, 24, 26, 10, 40, rng.randint(1, 60)))
        cur["class"] = rng.choice((0x13, 0x13, 0x3a, 0x3a, 0x10, 0x12, 0x21))
        cur["flags"] = rng.choice((0, 0, 0, 4, 8, 0xc))
        corpse_bit = int(rng.random() < 0.9)
        mode = rng.choice((0xc, 0xc, 0xc, 1, 0))
        reach = rng.choice((10, 30, 60, 100, aip4 + 2))
        if rng.random() < 0.2:                                    # the edge: squared distance exactly the limit, or one over
            dx = rng.randint(0, aip4); dy = int((aip4 * aip4 - dx * dx) ** 0.5)
            cur["dx"], cur["dy"] = dx + rng.choice((0, 0, 1)), dy
        else:
            cur["dx"], cur["dy"] = rng.randint(-reach, reach), rng.randint(-reach, reach)
        e.w32(cand + 0x10, mode); e.w32(cand + 0xc4, 2 if corpse_bit else 0)
        for k in range(8): e.w32(ctx + 4 * k, 0)
        e.w32(ctx + 0xc, aip4 * aip4)
        e.call(CB, cand, ctx, edx=me)
        print(cur["class"], cur["flags"], corpse_bit, mode, cur["dx"], cur["dy"], aip4, int(e.r32(ctx + 4) == 1), sep="\t")


def compare(cases, d2d):
    rows = [l.split() for l in open(cases) if l.strip()]
    ans = [l.split() for l in open(d2d) if l.strip()]
    assert len(rows) == len(ans), (len(rows), len(ans))
    bad = [(r, a) for r, a in zip(rows, ans) if r[7] != a[0]]
    acc = sum(r[7] == "1" for r in rows)
    print(f"cases {len(rows)} game accepts {acc} d2d accepts {sum(a[0] == '1' for a in ans)} differ {len(bad)}")
    for r, a in bad[:8]:
        print("  ", *r, "d2d", a[0])
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1] == "gen":
        gen(int(sys.argv[2]), int(sys.argv[3]))
    elif sys.argv[1] == "compare":
        sys.exit(compare(*sys.argv[2:4]))
    else:
        sys.exit(__doc__)
