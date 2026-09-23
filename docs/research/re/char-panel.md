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
