# SPDX-License-Identifier: GPL-3.0-or-later
"""A summon's own life regeneration in game.exe as the oracle: its stat 0x4a and its unit event 3, frame by frame.

    uv run python pet_regen.py gen 100000 1 > cases.txt     # `roll damage_regen life max_life frames`
    uv run python pet_regen.py game < cases.txt > game.txt   # each case and `| regen life_after filed`
    ../../build/tests/test_pet_regen --pet-regen < cases.txt | grep ' | ' > d2d.txt
    uv run python pet_regen.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: the life the pet was made with (`roll`, whole points: its MonStats roll), its MonStats DamageRegen, its
life now (256ths, under its max after a hurt) and its max life (256ths: the roll with the summoning skill's
bonus, FUN_005c4470; at most 0x7fffff00), then `frames` frames of its event 3, which the hurt filed (FUN_0057c6c0 0x57c8fd..0x57c92f).
A summon is made as a monster (unit type 1: FUN_00555230 -> FUN_00574250 -> FUN_00573cb0). Both run for real:
- FUN_00573cb0's regeneration (0x57401f..0x574052): stat 0x4a = the roll (256ths) x DamageRegen >> 12, the life
  shifted first when it's over 0x7fffffff / DamageRegen;
- FUN_005a6920, a monster's event 3: life (stat 6) += stat 0x4a each frame; over the max life (FUN_00625d10)
  the max, and the event removed.
Each line: the case, then the regeneration (256ths a frame), the life after the run (256ths) and `filed` (1 while
the event is still filed). Where a frame could carry the life past 2^31 the case has 0 frames: the regeneration's
formula alone (no summon's life comes near: past 2^31 game.exe's sum wraps negative and the unit dies). The event's rig is merc_regen.py's (its hooks: stats, max life, room, town, events,
kill); a pet is out of town, its state lookups find no state.
"""
import random
import sys

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI

USAGE = "usage: pet_regen.py gen N SEED | game < CASES | compare GAME D2D"


def gen(count, seed):
    rng = random.Random(seed)
    cap = 0x7fffff00                     # the most a life (stat 6, 256ths) can hold: 0x7fffff points

    def case(roll, damage_regen, life, max_life, frames):
        fine = roll << 8                 # frames 0 where a frame could carry life past 2^31: the regen's formula alone
        regen = (fine >> 12) * damage_regen if damage_regen and 0x7fffffff // damage_regen < fine else damage_regen * fine >> 12
        print(roll, damage_regen, life, max_life, frames if max_life + regen <= 0x7fffffff else 0)

    for roll, damage_regen in ((21, 4), (61, 4), (100, 3), (313, 3), (201, 3), (306, 3), (1, 1), (15, 0), (0x7fffff, 0xfff), (524288, 0x100), (524287, 0xfff)):
        case(roll, damage_regen, 256, min(roll * 256 * 4, cap), 40)
    for _ in range(count):
        roll = rng.choice((rng.randint(1, 60), rng.randint(1, 1000), rng.randint(1, 0x7fffff)))
        damage_regen = rng.choice((3, 4, rng.randint(0, 40), rng.randint(0, 0xfff)))
        max_life = min(rng.choice((roll * 256, roll * 256 * rng.randint(1, 40), rng.randint(256, 1 << 24))), cap)
        case(roll, damage_regen, rng.randint(1, max_life), max_life, rng.choice((1, rng.randint(1, 50), rng.randint(1, 400))))


def regen_of(e, roll, damage_regen):
    """FUN_00573cb0's stat 0x4a, the block at 0x57401f..0x574052 run for real."""
    mu = e.mu
    mu.reg_write(UC_X86_REG_ECX, damage_regen)
    mu.reg_write(UC_X86_REG_EDI, (roll << 8) & 0xffffffff)
    mu.reg_write(UC_X86_REG_EAX, 0)
    mu.emu_start(0x57401f, 0x574052)
    value = mu.reg_read(UC_X86_REG_EAX)
    return value - (1 << 32) if value & 0x80000000 else value


def game():
    import merc_regen
    rig = merc_regen.Rig(merc_regen.boot())
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        roll, damage_regen, life, max_life, frames = (int(f) for f in field[:5])
        regen = regen_of(rig.e, roll, damage_regen)
        after, filed = life, 1
        if regen != 0:
            after, filed, _dead = rig.run(regen, life, max_life, 0, frames)
        print(line.strip(), "|", regen, after, filed)


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            case, bar, result = line.partition("|")
            case, result = case.split(), result.split()
            if not bar or len(case) != 5 or len(result) != 3 or not all(f.lstrip("-").isdigit() for f in case + result):
                sys.exit(f"malformed line in {path}: {line.strip()!r} (5 case fields, then regen life filed)")
            out.append((case, result))
    return out


def compare(game_path, d2d_path):
    game_rows, d2d_rows = rows(game_path), rows(d2d_path)
    if not game_rows or len(game_rows) != len(d2d_rows):
        sys.exit(f"empty or unequal: {len(game_rows)} game, {len(d2d_rows)} d2d (an empty file is not a pass)")
    differ = 0
    for (g_case, g), (d_case, d) in zip(game_rows, d2d_rows, strict=True):
        if g_case != d_case:
            sys.exit(f"cases out of step: game {' '.join(g_case)}, d2d {' '.join(d_case)}")
        if g != d:
            differ += 1
            if differ <= 20:
                print(f"DIFF {' '.join(g_case)}: game {' '.join(g)}, d2d {' '.join(d)}")
    print(f"{len(game_rows)} cases, {differ} differ")
    sys.exit(1 if differ else 0)


def main():
    a = sys.argv[1:]
    if not a or (a[0], len(a)) not in {("gen", 3), ("game", 1), ("compare", 3)}:
        sys.exit(USAGE)
    if a[0] == "gen":
        gen(int(a[1]), int(a[2]))
    elif a[0] == "game":
        game()
    else:
        compare(a[1], a[2])


if __name__ == "__main__":
    main()
