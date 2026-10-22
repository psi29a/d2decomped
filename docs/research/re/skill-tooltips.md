# Skill tooltips — game.exe 1.14d

The skill tree's and the skill buttons' hover text, built from
SkillDesc.txt. Port: `components/game/skill_text.hpp`.

## Callers

- `FUN_004abf60` draws a tree icon; hovered (48x48 up from its
  bottom-left), `FUN_004eef00` builds the tooltip (`FUN_004ef410` when
  its level argument is -1: the same with "Required Level : " (0x105b) and
  "Maximum Level Reached" (0x1111); not ported).
- `FUN_004a8d50` (the skill buttons and the picker's icons) calls
  `FUN_004ef990`.

## The string

Every builder appends to one wide string, **bottom line first**, lines
split by 0xf9e "\n"; `FUN_00502280` draws it centred, last line on top.
`FUN_004521c0(buffer, n)` puts colour code n in front of a block; codes
carry on through what follows, so a coloured block is followed by a
newline in the line's own colour.

## FUN_004eef00 (the tree), appended in this order

1. dsc3 lines 16..10 at level + 1 (synergies), then "\n" if any wrote;
2. unless the skill is at its max (`FUN_004aa8b0`): desc lines 0..5 at
   level + 1, then 0x109d "Next Level" (0x10ad "First Level" at level 0)
   and "\n\n";
3. at level > 0: desc lines 0..5 at the level, then 0x109e "Current
   Skill Level: " + the level (+ 0x1112 " (item)" when an item grants
   it), "\n\n";
4. dsc2 lines 6..9 at level + 1, "\n" if any;
5. "str long" (SkillDesc record +0xc), "\n", "str name" (+8) in colour 2;
6. 0x1113 "You have been granted this skill by an item" (after "\n\n")
   when an item gives it; 0x1114 "You have not learned this skill yet"
   when `FUN_004ed480` says the player has no points in it.

## FUN_004ef990 (the buttons)

Lines 0..9 at the level, 0x109e + level and "\n\n" if any wrote, "str
short" (+0xa), "\n", "str name". The Amazon-only 0x5722 line isn't ported.

## Lines (FUN_004edea0)

Arguments: the skill record, its id, the level, the line index (record
+0x42 + i: the format byte, +0x54 + 2i text a, +0x76 + 2i text b, +0x98 +
4i calc a, +0xdc + 4i calc b; texts default to 0x1506, "none"). The calcs
run through `FUN_00646d00` at the given level. 75 formats (jump table
0x4eedc8); the ones SkillDesc uses most:

| fmt | uses | what it writes |
|-----|------|----------------|
| 1 | 162 | `FUN_004e85c0` mana cost: ((lvl-1) x lvlmana + mana) << manashift, at least minmana x 256; "str mana" + N, or N.N (tenths; calc a > 10 scales the fraction, `FUN_004e7a90`) when manashift isn't 8, lvlmana << shift <= 256 and the cost <= 0x1900 |
| 2 / 3 | 147 / 49 | `FUN_004e7e70`: a, calc a (+ sign with 2), b |
| 4 / 5 | 10 / 23 | `FUN_004e79e0`: a, calc a (+ with 4) |
| 6 / 7 | 1 / 22 | `FUN_004e7c30`: calc a (+ with 6), a |
| 8 | 16 | a, b, then 0x10a6 "To Attack Rating: " + skill to-hit (`FUN_006449f0`) + 0x10b3 " percent" |
| 9 | 30 | a, b, then 0x10a0 "Damage: " the skill's own (`FUN_00647bc0` / `FUN_00647d00`) + calc a % + calc b |
| 10 | 52 | a, b, then the element's damage (`FUN_00644d50` / `FUN_00644e40` with mastery, >> 8) under its label (`FUN_004e6f60`: fire 0x10a1, lightning 0x10a3, magic 0x10c3, cold 0x10a2, poison 0x10a4, life steal 0xdc4) |
| 11 | 20 | a, b, then "Cold Length: " / "Poison Length: " + `FUN_00644f20` seconds |
| 12 | 55 | b, then a + calc a frames as seconds (`FUN_004edb30`: /25, tenths; " second" / " seconds") |
| 14 | 8 | a, b, then 0x2b0e "over " + the length, then the label + damage x length >> 8 |
| 15 | – | a ": " b |
| 16 | 1 | 0x10b0 "Duration: " calc a - calc b seconds |
| 17 / 38 / 52 | 1 / 7 / 5 | a calc a-calc b b (17: b first, then " per second") |
| 18 / 25 | 20 / 11 | a (25: a b) in colour calc a |
| 19 | 51 | b, then a + calc a x 20 / 3 tenths + " yard(s)" |
| 20 / 21 | – | a, calc a, " percent", b |
| 26 / 27 | 2 / 3 | 0x10e2 "Average " label damage x 25 >> 8 0x10be " per second" |
| 40 | 107 | a with b as its %s ("%s Receives Bonuses From:") in colour calc a |
| 51 / 66 | 7 / 4 | a as a format with calc a |
| 59 | 1 | b, then calc a - calc b under a (or the element's label) |
| 63 / 67 | 209 / 26 | `FUN_004ec7e0`: a ": " +calc a (63: "%") " " b — the synergy lines |
| 70 | 3 | a, b, +calc a |

Not ported (about 60 lines): 13 (a summon's life), 22 / 23 / 28..31
(missile ranges and lengths), 33..37, 39, 41..50, 53..56, 60, 61, 64, 65,
68, 69, 71..75.
