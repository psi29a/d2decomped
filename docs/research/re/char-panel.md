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

## Attack blocks (FUN_004eda20 -> FUN_004ed570, UI\SkillDesc.cpp)

FUN_004a7d00 calls FUN_004eda20(player): the left skill (FUN_00620190 ->
FUN_00643930, skill list +8) as block 0, the right (FUN_006201d0 ->
FUN_00643960, +0xc) as block 6. Boxes `{x0, y, x1}` (i32) at 0x72d840,
six per block, panel coordinates with the same y base as the values:

| rec | left | right | what |
|---|---|---|---|
| +0 | 162, 93, 258 | 162, 117, 258 | name |
| +1 | 162, 101, 258 | 162, 125, 258 | "Damage" (0xfdd) |
| +2 | 263, 98, 307 | 263, 122, 307 | damage value |
| +3 | 162, 160, 270 | 162, 184, 270 | attack rating label |
| +4 | 162, 163, 270 | 162, 187, 270 | unused |
| +5 | 270, 160, 310 | 270, 184, 310 | attack rating value |

Languages 6..9 move records 0 and 6 up a pixel once (0x72d844 / 0x72d88c).

- **Name**: SkillDesc `str alt` (FUN_004e6e30: +0xe; 0x1506 "an evil
  force" if missing), upper-cased through the table at 0x730578
  (FUN_00452180), font6, colour 0. Always drawn.
- **Gate**: damage needs SkillDesc `descdam` (+0x12) with a handler in
  the table at 0x72d768; the rating needs `descatt` (+0x14) with one in
  0x72d7f8. Both need FUN_004d9fc0 (usable) in {0, 1, 6, 8}.
- **Damage**: "Damage" in font6 at +1, then the descdam handler draws the
  value at +2 through FUN_004e96e0: max raised to min + 1 if not above
  it, `"%d-%d"` (`"%s%d-%dK"` / `"%s%dK-%dK"` past 9999); font16, or
  font6 one pixel up when 11/7 of its font6 width overflows `x1 - x0`.
  - 1 (0x4ea170; Attack): two weapons (FUN_006235a0: barbarian /
    assassin) -> FUN_004e99f0, else FUN_004ea010.
  - 7 (0x4ea010; Jab, Bash ..): FUN_004e86f0 the weapon (stats 21/22,
    23/24 two-handed, 1-2 + 1/+2 barehanded; floors 1 / 2) x (100 + 18 /
    17 + % ) / 100 + 111 + `ddam calc2`, where % = `ddam calc1` + 25 +
    the weapon mastery (FUN_00645830) + str x StrBonus + dex x DexBonus
    (all strength barehanded), at least -90; FUN_004e89a0 its elements
    (table 0x6dbee4: fire 48/49 colour 1, lightning 50/51 colour 9, cold
    54/55 and magic 52/53 colour 3, each with its mastery, min kept <=
    max; poison 57/58 x 59 >> 8 colour 2), then min >= 1, max >= min + 1;
    plus the skill's MinDam (FUN_00647bc0) and EMin with mastery
    (FUN_00644d50), both >> 8.
  - 5 (0x4ead60; Fire Bolt, Magic Arrow ..): colour by EType
    (FUN_004e6fb0: fire 1, lightning 9, cold / magic 3, poison 2), SrcDam
    / 128 of the weapon's (FUN_004e9660), the skill's MinDam and EMin
    (x ELen for poison).
- **Attack rating**: the descatt handler fills (ar, colour, ar2, colour2).
  When either is set: `"%s\nAttack Rating"` (0xfdf; 0xfe1 `"%s\nRating"`
  for skill 0, Attack) with the upper-cased name, font6, halves at y-4 /
  y+4 of +3. One value (FUN_004e9940): `%d` at +5, font16, font8 from
  1000. Two (FUN_004e9870, dual wield): font6 at y-6 / y+2 (y-7 for
  languages 6..9).
  - 1 / 5 (0x4e9040): FUN_00622560 `(dex - 7) x 5 + 19 + ToHitFactor`,
    x (1 + (119 + the skill's ToHit, FUN_006449f0) %), + 325 when the
    skill's flag is set (colour 3).
  - 2 (0x4e9510; Attack): the same plus the weapon mastery % (FUN_00645830,
    stat 342) — FUN_004e8ec0; 0 for weapon type 0x26. Two weapons:
    FUN_004e93a0, both hands.
  - 3 (0x4e9590, FUN_004e9160) throws, 4 (0x4e95c0) the other hand.
  - Colour 3 / 1 when the states (FUN_0063a3c0 / FUN_0063a3e0, damage:
    FUN_0063a380 / FUN_0063a3a0) say so.
- Hover tooltips (chance to hit / be hit): FUN_004a7340, FUN_004a74a0,
  FUN_004a7ae0, FUN_004a7180.

d2d: `rules::attack_line` (components/rules/skills.hpp) computes the
numbers on the server in `Fight::update_fighters` from the Fighter
(`phys_lo/hi/pct`, `elem`, `ar_base/pct`), `skill_tohit`, `skill_phys`,
`elem_damage`; they reach the client in the View
(`View::attack_lines`, section 0) and `draw_char_panel` draws them.
Not done: descdam 2..4, 6, 8..24, descatt 3 / 4, dual wield, the states'
colours, stat 325, the usability gate, the K formats, barehanded strength,
tooltips.

## Stat point buttons (FUN_004a7720 press, FUN_004a78c0 release)

Shown while stat 4 (unspent stat points) is non-zero. Table at `0x724a48`,
4 records of 14 bytes `{u32 x, u32 y, u32 pressed, u16 stat}`, panel
coordinates, y = the button's bottom:

| x | y | stat |
|---|---|------|
| 117 | 105 | 0 strength |
| 117 | 167 | 2 dexterity |
| 117 | 253 | 3 vitality |
| 117 | 315 | 1 energy |

- **Hit box**: x in (x, x+40), y in (y-22, y). Press sets `pressed`;
  release on the same button sends packet `0x3a` with the u16
  `stat | (count-1) << 8` (FUN_004785b0): count 1, or with Shift held
  every unspent point in batches of 32.
- **Art** (FUN_004a7d00): `PANEL\levelsocket` frame 0 at (x+5, y+5), then
  `PANEL\level` (loaded by FUN_004a6460, also the HUD's "new stats"
  button) frame `pressed` at (x+8, y+1); DC6s anchor bottom-left.
- **Points box**: `PANEL\skillpoints` (FUN_004967f0 loads it into
  0x7bef20) at (3, 364); strings 0xfeb / 0xfec ("Stat Points" /
  "Remaining") in font6 centred in 11..88 at baselines 355 / 363; the count
  in font16 centred in 92..127 at baseline 360.

What a point does is the server's: d2d adds CharStats' per-point gains
(`LifePerVitality`, `StaminaPerVitality`, `ManaPerMagic`, in quarter
points) to current and max — `components/rules` `spend_stat_points`. The
0x3a handler itself isn't traced yet.

## Level up: the New Stats / New Skills buttons, the sound

**Server** (`FUN_00570880`, from the experience add `FUN_0057e510` →
`FUN_0057eb10`): level (stat 12) from experience (`FUN_00611860`),
stat 0x1e the next threshold; per level gained max life / stamina / mana
+= CharStats bytes +0x43 / +0x44 / +0x45 (×64), stat 4 += StatPerLevel
(+0x50), stat 5 += 1. Then **life to its max if above 0
(`FUN_00625d10`), mana and stamina to theirs (`FUN_00625d60` /
`FUN_00625db0`)**. `FUN_00553380(unit, 2, unit)` queues unit sound
event 2 (unit +0x6e = 2, +0x70 = the player, flag 0x400 at +0xc4); every
client gets packet 0x75 with the new level (`FUN_005538d0(…,
FUN_00570850)` → `FUN_0053da90`); items re-checked (`FUN_0055f500`).

**Sound**: the update pass sees flag 0x400 and `FUN_00571740` sends
S→C 0x2C (builder `FUN_0053d780`) to the levelling player only. Client:
`0x45e110` → `FUN_004cbde0`; a player's code outside 10..0x12 /
0x54..0x5d goes to `FUN_004cb9c0`, whose switch (`0x4cbc55`, table
`0x4cbd7c`) maps 2 → Sounds.txt 7 `cursor_level_up` (cursor\levelup.wav),
EDX 0: not placed (1 → 0xeb, 3..6 → 0xa..0xd). The merc's level up is
code 0x5b → Sounds.txt 8 at the player. Packet 0x5D also plays 7 for
some quest states (`FUN_004a2cb0`); that's the quests'.

**No overlay**: nothing visual. Overlay.txt, States.txt and Missiles.txt
have no level-up row, no LvlUp / LevelUp .dcc is in the MPQs, and the
client's stat 12 handler (`FUN_0045d3b0` → `FUN_004c1bc0` →
`FUN_004c1350`) only re-checks item requirements.

**Buttons** (UI 6 stats, UI 7 skills; art `Panel\Level` → `0x7c02dc`,
`Panel\Levelsocket` → `0x7c02e0`, loaded by `FUN_004a6460`).
`FUN_004a64c0` each frame (not with UI 0xb up): UI 6 off when stat 4 = 0,
on when ≠ 0 and the character panel (UI 2) is shut; UI 7 off when
stat 5 ≤ 0, on when > 0 and the tree (UI 4) is shut. The panel state
`FUN_0045ae90`: 1 right open, 2 left, 3 both.

| | stats `FUN_004a6b30` | skills `FUN_004a6e60` |
|---|---|---|
| hidden (held flag cleared) | `FUN_004a6a00`: both sides, UI 2, UI 0xc (store), UI 0x16 with 1 or 4 | `FUN_004a6d50`: both sides, UI 4, UI 0xc, UI 0x16 with 1 |
| socket x | 40; W/2+40 with a left panel | W−73; W−W/2−73 with a right panel |
| label | string 3986, bottom H−142 | 3987 |
| hit box (`FUN_004a6580` / `FUN_004a6630`) | x < mx < x+34, H−139 < my < H−102 | x < mx < x+33, H−138 < my < H−102 |
| held flag | `0x7c02e4` | `0x7c02e8` |

The label (`FUN_00502320`, colour 0, the frame's current font) is
centred on the socket: x + 1 + socketW/2 − textW/2; drawn always. Then
levelsocket frame 0 at (x, bottom H−105), level frame 0, or 1 while held
and under the cursor, at (x+3, bottom H−109). Press over it
(`FUN_004a66e0` / `FUN_004a6790`): held, Sounds.txt 4
cursor_button_click. Release still over it (`FUN_004a6840` /
`FUN_004a6920`): UI 6 off and UI 2 on (the character panel) / UI 7 off
and UI 4 on (the tree); any release lets go. With `FUN_004f5160` = 2 the
buttons sit on the control panel instead (`FUN_004a6a70` /
`FUN_004a6da0`, hit `FUN_004a65e0` / `FUN_004a6690`: W/2−194 / W/2+163,
bottom H−8, frame 2, a hover tooltip at H−50); d2d draws the other layout.
