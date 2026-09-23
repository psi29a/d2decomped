# Character panel — game.exe 1.14d

The stats page ('C'). Art: `data\global\ui\PANEL\invchar6.dc6` frames
0..3 (2x2: 256+64 wide, 256+176 tall); frames 4..7 are the inventory.
In 800x600 the left panels sit at x 80..400, y 60 (the inventory, a right
panel, at 400..720 per inventory.txt).

Layout is two tables in game.exe, panel-relative, text centred in
`[x0, x1]` around `y`:

- **Labels** at `0x724818`, 15 records of 18 bytes
  `{u32 x0, u32 y, u32 x1, u16 string id, u32 strlen+1}`:
  Level 0xfd9 (11..52, 44), Experience 0xfda (65..180, 44), Next Level
  0xfdb (193..308, 44), Strength 0xfdc (10..73, 97), Dexterity 0xfde
  (10..73, 160), Defense 0xfe0 (174..268, 207), Vitality 0xfe2 (10..73,
  245), Stamina 0xfe3, Life 0xfe4, Energy 0xfe5, Mana 0xfe6, and the four
  resistances 0xfe7..0xfea ("Fire\nResistance" — two lines).
- **Values** at `0x724928`, 18 records of 16 bytes
  `{u32 x0, u32 y, u32 x1, u32 stat id}`: level 12, experience 13, next
  level 30, str 0, dex 2, defence 31, vit 3, stamina 10/11, life 6/7,
  energy 1, mana 8/9, resistances 39/43/41/45.

Stats 0..15 come straight from the save's `gf` section (d2s_items.hpp
`parse_stats`; life/mana/stamina are 8.8 fixed point); 30, 31 and the
resistances are computed from experience.txt and equipment.

Found by searching game.exe for the label string IDs (Strength 0xfdc,
Dexterity 0xfde, Vitality 0xfe2 within a few bytes of each other).

## Drawing (FUN_004a7d00)

Every string is centred in its box as `x0 + (x1 - x0 + 1 - w) / 2`
(left-aligned at `x0` when it doesn't fit); `y` is the baseline — font
DC6 cells blit bottom-anchored like any DC6 (font6 cells are 11 px tall,
baseline on row 8; font16 16 px, row 14), so the cell top is
`y - cell_height + 1`, not `y - tbl_height`.

- **Labels**: font 6 (`font6`). A label with a `\n` (the resistances)
  draws its two halves at `y - 4` and `y + 4`.
- **Name**: box 13..161, baseline 25; font16, font8 when the name is 12–13
  chars, font6 at 14+. **Class**: font16, box 193..311, baseline 25.
- **Values**: font16. Life/mana/stamina/defence drop to font8 when > 999
  or at least as wide as the box. Colour (`local_8`): 3 blue when the
  current value beats the base, 1 red when below, 4 gold for a maxed
  resistance.
- **Next level** (30): experience.txt row `<level>` (same for every
  class); blank at max level.
- **Resistances**: stat + difficulty penalty (expansion: DifficultyLevels
  `ResistPenalty`, 0/-40/-100; classic: hard-coded 0/-20/-50), clamped to
  `[-100, min(75 + max-res stat, 95)]`.

The save's active difficulty is the `difficulty[3]` byte at d2s 0xA8
with bit 0x80 set.
