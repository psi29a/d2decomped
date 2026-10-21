# The Hireling screen — game.exe 1.14d

UI 0x24. The key config's Hireling Screen (`0x469170`, O by default)
toggles it in an expansion game whose player has a merc
(`FUN_00478f20(player, 7)`). It is a left-hand panel. d2d:
`apps/d2d/hireling.{hpp,cpp}`; the World side is `cmd::MercItem` in
`world.cpp`, `rules::merc_slot` in `merc.hpp`, and `View::MercSheet`.

## Drawing (FUN_004929f0)

At 800×600 the panel spot is x + 0x50, y + 0x3c (`0x7a2858`,
`-0x7a285c`).

- `PANEL\NPCInv` frames 0..3, bottoms at H − 0xe0 and H − 0x30 (each
  less 0x3c), x 0x50 and 0x150. These assets are loaded by
  `FUN_004868b0`, as are `inv_armor`, `inv_helm_glove` and
  `inv_weapons`.
- The close button: `buysellbtn` frame 10 (11 held) at (0x50 + 0x110,
  bottom H − 0x3f − 0x3c).
- `FUN_0048a7d0`: the merc's name, cut to 0xa0 wide, baseline 0xd6.
- `FUN_0048a890`: the labels (`0x6d9ae8`, 14 bytes: x, x2, y, string).
  They are Experience, Level, Next Level, Strength, Dexterity, Damage,
  Defense, the four resistances (two lines) and Life.
- `FUN_0048a990`: the values (`0x7221a8`, 20 bytes: x0, x1, y, width,
  stat). Experience (13), level (12), the next level's experience (30),
  strength (0), dexterity (2), damage (21: min–max with the weapon, its
  enhanced damage, elemental adds), defense (31), fire / cold /
  lightning / poison resistance (39 / 43 / 41 / 45: the difficulty's
  penalty, at most 75 + max), and life "%d / %d" (6). Colours: blue
  above the base, red below it, gold at the cap.
- `FUN_0048b0a0`: the empty slots' pictures. These are armour,
  `inv_helm_glove` frame 1, and `inv_weapons` for each hand, at a box's
  left and bottom plus `0x7220b0`'s offsets.
- `FUN_0048b540`: the items, centred in their boxes. The boxes come
  from inventory.txt `Hireling2`: head, torso, right and left arm.
- `FUN_0048b3f0`: with an item in hand, the box under the mouse is
  tinted with one colour when the merc would wear it, another when not.
- `FUN_0048dd90`: an item's hover text, with the merc's stats against
  its requirements.

## Wearing (C→S 0x61, FUN_0054d430)

A u16 slot. With nothing in hand, slot 1..10 takes that item off the
merc into the hand (`FUN_0054d130`). With an item in hand
(`FUN_0054d230`):

- A healing, antidote or thawing potion (types 0x4c, 0x50, 0x51): the
  merc drinks it (`FUN_005bf240`).
- Anything else must pass `FUN_0048b290` (the client checks the same
  rule). It must be identified (flag 0x10) and not broken (0x100). It
  must be a helm or body armour, or by the merc's MonStats row:
  - 271 (rogue): a bow;
  - 338 (desert guard): a spear or polearm;
  - 359 (iron wolf): a shield, or a one-handed sword;
  - 560 (barbarian): a one-handed axe;
  - 561 (barbarian): a sword;
  - 560 and 561: also a primal helm.

  Then come the merc's own strength, dexterity and level against the
  item's (`FUN_0062eaf0`). It goes to the head, the torso, the left
  hand (a shield) or the right hand, and what was there comes into the
  hand.

## d2d's ceilings

- The values aren't coloured blue / red against the base; resistances
  are, by the cap of 75.
- The tint is green or red at a quarter. game.exe's colours
  (`0x7bcad8` / `0x7bcad9`) aren't traced.
- The weapon pictures are drawn in both hands whenever a hand is empty.
  game.exe's two-hander test (`FUN_0063d340`) isn't ported.
- `FUN_0062eaf0`'s further checks (ethereal, "requirements −x%") aren't
  applied, as with the player's `can_wear`.
- In a joined game d2d sends 0x61 to the host. The merc's items as the
  host has them (its item packets for the merc) aren't read back yet.
