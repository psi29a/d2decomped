# SPDX-License-Identifier: GPL-3.0-or-later
"""Camp room1 seeds when every Blood Moor room comes up before the camp's.

    $D2_BUILD/tests/test_camp_cross_order --oracle 1 4 | uv run python camp_cross_order.py 1-4 [--mutation-check]

Builds Act 1, brings Blood Moor room2s up in list order through
FUN_0061b730, then the Rogue Encampment's.  The camp room1 seeds are
compared with test_camp_cross_order's --oracle output, read from stdin,
for the same map seeds.
The mutation check reverses the two levels in game.exe and requires at
least one camp seed to change, proving that the comparison observes the
cross-level order.  Needs $D2_MPQ_DIR, and the piped test needs
$D2_PATCH_INSTALLER.
"""

import sys

import drlg


def rooms(e, level):
    """A level's room2s in list order."""
    result = []
    room = e.r32(level + 0x10)
    while room:
        result.append(room)
        room = e.r32(room + 0x24)
    return result


def build_level(e, act, level_id):
    """Find or build one level as FUN_00642ae0/FUN_006424a0 do."""
    level = drlg.find_level(e, act, level_id) or e.call(0x642AE0, ecx=act, edx=level_id)
    if not e.r32(level + 0x10):
        e.call(0x6424A0, level)
    return level


def game_dump(e, seed, moor_first=True):
    """Camp room1 seed lines after the two levels' list-order sequence."""
    act = drlg.alloc_act(e, 0, seed, 1)
    camp = build_level(e, act, 1)
    moor = build_level(e, act, 2)
    levels = (moor, camp) if moor_first else (camp, moor)
    for level in levels:
        for room in rooms(e, level):
            e.call(0x61B730, ecx=room)
    return drlg.seeds_dump(e, camp).splitlines()


def d2d_dump(lines):
    """test_camp_cross_order --oracle's lines: seed, a tab, the answer."""
    answers = {}
    for line in lines:
        if "\t" not in line:
            continue
        seed_text, answer = line.split("\t", 1)
        answers.setdefault(int(seed_text), []).append(answer)
    return answers


def main():
    """Compare game.exe and d2d; reject a level-order mutation."""
    args = [argument for argument in sys.argv[1:] if not argument.startswith("--")]
    first, last = (int(value, 0) for value in (args[0] if args else "1-4").split("-"))
    if last < first:
        sys.exit(f"seeds {first}-{last} run backwards")
    game = drlg.boot()
    ours = d2d_dump(sys.stdin.read().splitlines())
    differences = []
    for seed in range(first, last + 1):
        expected = game_dump(game, seed)
        if expected != ours.get(seed, []):
            differences.append((seed, expected[:3], ours.get(seed, [])[:3]))
    if differences:
        for seed, expected, actual in differences[:4]:
            print(f"seed {seed}: game.exe {expected}, d2d {actual}")
        sys.exit(1)
    if "--mutation-check" in sys.argv:
        if game_dump(game, first, False) == game_dump(game, first, True):
            sys.exit("mutation check: reversing the two levels did not change a camp seed")
        print("mutation check: reversing the two levels changes the camp room1 seeds")
    print(f"ok: {last - first + 1} Blood-Moor-before-camp room1 seed dumps match")


if __name__ == "__main__":
    main()
