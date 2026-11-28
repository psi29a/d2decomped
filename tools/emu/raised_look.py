# SPDX-License-Identifier: GPL-3.0-or-later
"""A raised skeleton's or skeletal mage's look and the owner's seed, by game.exe 1.14d's own FUN_005c4430.

    uv run python raised_look.py gen [N] [SEED] > cases.txt     # synthetic cases (default 4000)
    uv run python raised_look.py game cases.txt > game.txt      # game.exe's answers, needs D2_MPQ_DIR
    uv run python raised_look.py inc cases.txt game.txt > ../../tests/raised_look_cases.inc

tests/test_raised_look.cpp runs rules::raised_look on the same cases against
the .inc (no game data). A case: `id mage mastery level par1 owner_low
owner_high look`: the pet a skeleton (MonStats 0x16b) or a mage (0x16c), the
owner's Skeleton Mastery level (0: not in state 0x61, skel_mastery), Raise's
level and its Param1 (the skill row's +0x148, written into Raise Skeleton's
row for the case), the owner's seed and the sixteen look bytes the pet's stat
init left (monster data +4.., hex).

Game side, every table loaded (drlg.boot): FUN_005c4430(ECX owner, EDX pet,
the skill id, the level) runs for real, with FUN_005c4300 / FUN_005c43d0 and
d2d_pet_variant (FUN_005c4180). Hooked (not the claim): FUN_00639df0 (the
owner in state 0x61: the case's mastery over 0), FUN_006256b0 (that state's
list: a marker), FUN_00625d00 (the list's stat 0x15f: the mastery; 0x15e: 0)
and FUN_005b0d70 (the AI parameter d2d_pet_variant sets last).

Output `id | look owner_low owner_high`.
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LIST = 0x4C000000
RAISE_SKELETON, RAISE_MAGE = 70, 80


def gen(n, seed):
    rng = random.Random(seed)
    for i in range(n):
        mage = int(rng.random() < 0.5)
        mastery = rng.choice((0, 0, 1, 2, 3, 5, 8, 10, 11, 20, 40))
        level = rng.choice((1, 2, 3, 4, 7, 12, 20, 35))
        par1 = rng.choice((5, 5, 0, 1, 50, 99, 100))
        look = bytes(rng.randrange(4) for _ in range(16)).hex()
        print(i, mage, mastery, level, par1, rng.getrandbits(32), rng.choice((666, rng.getrandbits(32))), look)


def read(path):
    for line in open(path):
        f = line.split()
        if f: yield f


def game(path):
    import drlg
    e = drlg.boot()
    tables = e.r32(0x744304)
    skills = e.r32(tables + 0xb98)
    owner, pet, data = e.alloc(0x100), e.alloc(0x100), e.alloc(0x40)
    state = {"mastery": 0}
    e.hook(0x639df0, lambda m: int(state["mastery"] > 0), 2)
    e.hook(0x6256b0, lambda m: LIST, 2)
    e.hook(0x625d00, lambda m: state["mastery"] if m.arg(1) == 0x15f else 0, 3)
    e.hook(0x5b0d70, lambda m: 0, 2)
    for f in read(path):
        i, mage, mastery, level, par1, low, high = (int(v) for v in f[:7])
        look = bytes.fromhex(f[7])
        state["mastery"] = mastery
        e.w32(skills + RAISE_SKELETON * 0x23c + 0x148, par1)
        e.mu.mem_write(owner, b"\0" * 0x100); e.mu.mem_write(pet, b"\0" * 0x100); e.mu.mem_write(data, b"\0" * 0x40)
        e.w32(owner + 0x20, low); e.w32(owner + 0x24, high)
        e.w32(pet, 1); e.w32(pet + 4, 0x16c if mage else 0x16b); e.w32(pet + 0x14, data)
        e.mu.mem_write(data + 4, look)
        e.call(0x5c4430, RAISE_MAGE if mage else RAISE_SKELETON, level, ecx=owner, edx=pet)
        print(i, "|", bytes(e.mu.mem_read(data + 4, 16)).hex(), e.r32(owner + 0x20), e.r32(owner + 0x24))


def inc(cases_path, game_path):
    answers = {}
    for f in read(game_path):
        answers[f[0]] = f[2:]
    print("// tools/emu/raised_look.py inc: game.exe 1.14d's FUN_005c4430 on each case (see the script)")
    print("// {mage, mastery, level, par1, owner low, owner high, look, look after, owner low after, owner high after}")
    for f in read(cases_path):
        after, low, high = answers[f[0]]
        print(f'{{ {f[1]}, {f[2]}, {f[3]}, {f[4]}, {f[5]}u, {f[6]}u, "{f[7]}", "{after}", {low}u, {high}u }},')


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "gen": gen(int(sys.argv[2]) if len(sys.argv) > 2 else 4000, int(sys.argv[3]) if len(sys.argv) > 3 else 20261127)
    elif mode == "game": game(sys.argv[2])
    elif mode == "inc": inc(sys.argv[2], sys.argv[3])
    else: sys.exit(__doc__)
