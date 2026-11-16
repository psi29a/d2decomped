# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare `tele_mana.py game` (`id did x y pays ...`) with `test_tele_mana --tele-mana` (`id did paid`).
    usage: tele_mana_diff.py game.txt d2d.txt [COST]   (COST: the hop's cost in 256ths, default 6144 = level 1)
A d2d cost within 1,024 of COST counts as COST, within 1,024 of 0 as 0 (mana regeneration in the d2d run is under 400).
Exit 0 only when every case agrees; 1 on a difference, an empty or unequal input, or a malformed line."""
import sys


def rows(path, width):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            field = line.split()
            if not field or not field[0].isdigit():
                continue                     # d2d's log lines are not answers
            if len(field) < width or not all(f.lstrip("-").isdigit() for f in (field[:2] + field[width - 1:width])):
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            out.append(field)
    return out


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    game, d2d = rows(sys.argv[1], 5), rows(sys.argv[2], 3)
    if not game or len(game) != len(d2d):
        sys.exit(f"empty or unequal: {len(game)} game, {len(d2d)} d2d (an empty file is not a pass)")
    cost = int(sys.argv[3]) if len(sys.argv) == 4 else 6144
    kinds = {}
    for a, b in zip(game, d2d, strict=True):
        if a[0] != b[0]:
            sys.exit(f"case ids out of step: game {a[0]}, d2d {b[0]}")
        game_did, game_paid = int(a[1]), cost * int(a[4])
        d2d_did, d2d_paid = int(b[1]), int(b[2])
        d2d_paid = cost if abs(d2d_paid - cost) < 1024 else 0 if abs(d2d_paid) < 1024 else d2d_paid
        same = (game_did, game_paid) == (d2d_did, d2d_paid)
        key = f"{'same' if same else 'DIFF'} game(did={game_did} paid={game_paid}) d2d(did={d2d_did} paid={d2d_paid})"
        kinds[key] = kinds.get(key, 0) + 1
    for key, count in sorted(kinds.items()):
        print(count, key)
    sys.exit(1 if any(key.startswith("DIFF") for key in kinds) else 0)


main()
