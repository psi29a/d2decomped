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
   it: `FUN_006442a0` gives a level with item bonuses (flag 1) but none
   without), "\n\n";
4. dsc2 lines 6..9 at level + 1, "\n" if any;
5. "str long" (SkillDesc record +0xc), "\n", "str name" (+8) in colour 2;
6. 0x1113 "You have been granted this skill by an item" (after "\n\n")
   when an item gives it; 0x1114 "You have not learned this skill yet"
   while `FUN_004ed480` finds no skill of the player's class with points
   (any of them, not this one; the answer is kept in 0x7c8ca0 once yes).

"Maxed" (step 2) compares the points without items (`FUN_006442a0`
flag 0) with the max level (`FUN_004aa8b0`).

Item-granted skills (`FUN_0055b800`, ItemMode.cpp): stat 97 (oskill)
for any skill, or 107 for a skill of the player's class, adds the skill
at 0 points. A skill's item bonus (`FUN_00644180`, a player's): 127 for
any skill; for its own class 83 (class), 188 (tab) and 97 at most 3,
another class's 97 in full; 126 by its EType (1 fire, 2 lightning, 3
magic, 4 cold, 5 poison); 107. `rules::item_skill_bonus` /
`item_grants_skill`.

## FUN_004ef990 (the buttons)

Lines 0..9 at the level, 0x109e + level and "\n\n" if any wrote, "str
short" (+0xa), "\n", "str name". The Amazon-only 0x5722 line isn't ported.

## Lines (FUN_004edea0)

Arguments: the skill record, its id, the level, an item (0 here; the
mana line asks `FUN_004e84f0` for a charge's cost), the line index (record
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

The rest SkillDesc uses (one to twenty lines each): 13 the summon's
(Skills +0xbc) average life (`FUN_006538a0` at its Level, min + max / 2)
+ calc a % + calc b, "Life: "; 22 descmissile1's damage (`FUN_0064b100`
/ `FUN_0064b1d0`: EMin + MinELev brackets + its EDmgSymPerCalc %, <<
HitShift) x 75 with the skill's fire / lightning mastery, as an average;
23 / 30 descmissile1's (30: its SubMissile1's) Range + LevRange x lvl as
seconds; 27 the skill's average x 75 (26's x 25); 28 descmissile1's
Param1 as a radius; 29 its (Range + LevRange x (lvl - 1)) x 2 / 3 "
yards"; 31 calc a / DifficultyLevels AiCurseDivisor as seconds; 33 a b
from level 2; 34 / 39 the summon's A1 damage (flag 8), + its aurastats'
and passivestats' stat 25 % (at least -90; 34 adds stat 111 flat first)
"Damage: "; 35 "a: calc a-calc b"; 36 calc a then a for 1 else b; 37
calc a x 10 / 3 tenths as yards (19 and 28 x 20 / 3; " yard" for 1
whole, whatever the tenth; at least 1 whole); 41 the Fire Golem's A1 min
+ calc a to max + calc b under the EType label; 42 "a: +calc a.calc b
b"; 43 256ths as whole.tenth "a lo-hi b"; 47 / 49 `FUN_004e80f0` a
range (49 the skill's own damage, "+", a space); 48 length, element,
damage and attack rating (expansion); 50 descmissile1's damage under a;
53..56 the scroll / tome count, drawn in its own box at (width - 128,
height - 52) by `FUN_004ed3f0`, "Quantity : %d" over the item's name;
the count's search (`FUN_004a9a90`..) always returns 0 (bugs.md); 61
calc a in 256ths, its fraction x (calc b or 10); 68 calc a a b; 71 "a:
" b formatted with calc a; 72 / 73 "[+]calc a/calc b a" (nothing at 0).
Unused: 44..46, 64, 65, 69, 74, 75.

**What counts as written**: `FUN_004edea0` returns 1 for every format it
knows whatever was appended (the tree's headers and blank lines hang on
it), and 0 only for an unknown format, a missing descmissile (23, 28..30),
text a missing (40, 71; 71 and 40 b too) or 72 / 73 at 0.

**Calc operands only tooltips use** (`FUN_00646460`): m1en..m3el (26..34:
descmissile n's min / max >> 8, length), m1rn..m3rn (35..37: Range +
LevRange x lvl), m1eo..me3y (43..48, 256ths; the missile is picked by
(code - 43) / 3, so m2eo / m2ey read missile 1 and me3o / me3y missile
2), len / rng / pets (the auralen / aurarange / petmax calcs), sklvl(s,
at, op) (`FUN_00646c60`: s's op at this skill's `at`) and miss(row, op)
(`FUN_0064b340`, misscalc operands; inside miss() a name is first a
Missiles.txt row).

## The oracle

`tools/emu/skill_tips.py [levels] [difficulty]` loads the string tables
(`FUN_005259c0("ENG", 1)`) and every excel table, then runs
`FUN_004edea0` with no unit for every SkillDesc line;
`test_game --skill-tips [levels] [difficulty]` prints d2d's the same way.
All 17 lines of every skill at levels 1..20 match on all three
difficulties (22,800 lines each).
