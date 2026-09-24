# Skill tree panel — game.exe 1.14d

The 'T' panel (`.\UI\skilltree.cpp` region, 0x4aa920..0x4acb00). Right-
hand panel; game.exe positions everything from its right edge
`R = W - panel x` and bottom `B = panel y + H - 480` — at 800x600 R = 720,
B = 540.

## Art

- `SPELLS\skltree_<c>_back` (c = a s n p b d i by d2s class; FUN_004aa920
  loads the player's into `0x724bf0 + 0x22 * class`). FUN_004aaae0 draws
  frames 0..3 as the 2x2 panel — bottom-left at (R-320, B-224),
  (R-64, B-224), (R-320, B-48), (R-64, B-48) — then frames 4t..4t+3 of
  tab t at the same spots.
- `SPELLS\<Cl>Skillicon` (Am So Ne Pa Ba Dr As, table at 0x724ac4): one
  48x48 icon per skill, frame `IconCel` (SkillDesc.txt), +1 while pressed.

## Tabs (FUN_004ab7e0)

`0x724bec` holds the open tab, 1..3, starting at 1. The tab strip is
x R-88..R; bands y (B-372, B-265) → 3, (B-264, B-157) → 2,
(B-156, H-49) → 1. A skill's `SkillPage` is the tab it's on.

Labels (FUN_004aace0) are hard-coded string IDs in font16 centred in
R-90..R at baseline 60 + y: "Skill Choices Remaining" 0x1083..0x1085 at
85/97/109 for every class, then per class (IDs, y):

| class | lines |
|-------|-------|
| Amazon | 1088 210, 1089 222, 1086 234, 108a 318, 108b 330, 1086 342, 108c 419, 108d 431, 1086 443 |
| Sorceress | 1099 216, 1087 228, 109a 324, 1087 336, 109b 430, 1087 442 |
| Necromancer | 1092 216, 1087 228, 1093 318, 1094 330, 1087 342, 1095 436 |
| Paladin | 108e 216, 108f 228, 1090 324, 108f 336, 1091 430, 1086 442 |
| Barbarian | 1096 222, 1097 324, 1098 336, 1097 430, 1086 442 |
| Druid | 57f0 222, 57ee 324, 57ef 336, 57ed 430 |
| Assassin | 57f4 210, 57f5 222, 57f2 324, 57f3 336, 57f1 430 |

## Skills (FUN_004ac200 / FUN_004ac4d0, one icon FUN_004abf60)

- Position: column (SkillColumn 1..3) left edge R - {305, 236, 167}
  (FUN_004aaa50); row (SkillRow 1..6) bottom B - {418, 350, 282, 214,
  145, 77} (FUN_004aa9c0). Hit box x in (left, left+48), y in
  (bottom-48, bottom).
- Colour: with skill points left, an icon that can't take one (level
  requirement, prerequisites, max level) draws with colour 5 (greyed);
  with none left, every unlearned icon does. Hovered: colour 3.
- Level: "%d" (with item bonuses) when non-zero, at (left+48, bottom+12),
  font16 — FontFormal10 and 4 px further left from 10 up; blue when items
  add levels, red when they take them away.
- Unspent points (stat 5): "%i" at x R-52, baseline B-400; a centred "0"
  in R-65..R-25 when there are none.
- Learning: press marks the icon, release on it (FUN_004abc30) checks the
  points and requirements and sends a packet via FUN_004785b0.

The save's 30 `if` bytes are the class's skills in Skills.txt order
(rows with that `charclass`), base levels — `d2s::Stats::skills`.
