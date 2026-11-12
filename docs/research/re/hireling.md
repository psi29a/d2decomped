# The Hireling screen — game.exe 1.14d

UI 0x24. The key config's Hireling Screen (`0x469170`, O by default)
toggles it in an expansion game whose player has a merc
(`FUN_00478f20(player, 7)`). It is a left-hand panel. d2d:
`apps/d2d/hireling.{hpp,cpp}`; the World side is `cmd::MercItem` in
`world/`, `rules::merc_slot` in `merc.hpp`, and `View::MercSheet`.

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

## The first hire's hint (PopupHireling)

`FUN_004939b0` (each game's start, from `FUN_00453d90`) arms
`DAT_007beee4` while the registry's `Diablo II\PopupHireling` is 0. The
portraits (`FUN_00494020`), past their panel checks, then open UI 0x24
once (`FUN_00455f20(0x24, 0)`, an expansion game) and write 1. With no merc
the panel shuts itself on its first draw, the hint spent all the same.
d2d: d2d.cfg's `popup_hireling`.

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

## In the save (`FUN_005699a0`, PlrSave2.cpp)

After the corpse list, an expansion game (`+0x70`) writes "jf", then,
when the player has a hireling pet (`FUN_00574ec0(7, 1)`), its item list
(`FUN_00531bb0`: "JM", u16 count, the items); then `FUN_00569a00` writes
"kf" and the golem (`FUN_00574ec0(3, 1)`). The header's merc fields
(+0xb1 dead, +0xb3 seed, +0xb7 name, +0xb9 Hireling.txt Id, +0xbb
experience) are read back by `FUN_00533c70`, which wants the Id in
Hireling.txt ("Unable to load merc.") and steps the level up from its
experience.

d2d wrote no merc list until issue #139: a character made in d2d kept
the "jf" "kf" it was made with after a merc was hired, which game.exe
refused. `d2s::write_save` now writes "jf" + "JM" whenever
`header.merc_seed` is set (`World::save` passes `merc_items`), and a save
saved again mends.

## Spawning the merc (FUN_005774f0, 0x17a bytes)

Both readers of a merc record end up here: `FUN_00533c70`'s header parse
(above) calls `FUN_005774f0(level, header, id, row+8, expansion)` once it
has `d2d_merc_hireling_row`'s row; so does `FUN_005394a0` (the per-client
join walk over `+0x18`'s pending list, SUNITNPC.CPP), which calls
`FUN_005774f0(name, id, 0xffff, 0, 0)` for each entry whose flag word
(`FUN_0063d670`) is 0 — so a joining client's own saved merc is recreated
the same way a load restores one. `__fastcall`, `ECX` the player (`+0x70`
the game, `+0x6d` class, read from raw disassembly since the decompile
shows a bare `FUN_005774f0();`), `EDX` a second record whose byte `+0x18`
is the fallback class when the expansion check below doesn't apply;
three stack words follow: the level (clamped below), a word at
`record+6` (unread here), the merc name id (`ushort`, `0xffff` from the
client-join caller means "no particular level"), the hireling row's `+8`
dword, and the expansion flag:

- `FUN_00663750(name id)` gets the id's act; `FUN_005531c0(0x735)` /
  `(0x737)` is a quest-bit style check (not traced further this batch)
  that picks the class byte from `EDX+0x18` or, when it's set, from
  `FUN_005382b0() & 0xff` instead.
- With the player's game (`+0x70`) or a resolved act: the row is either
  `d2d_merc_hireling_row(game, name id & 0xffff, 1)` (an exact id — the
  save/join path) or, when the level word is `-1` (`(short)param_5`, no
  id given), a fresh `FUN_00656580(game, act, class, 0)` pick. No row:
  the fatal path (`FUN_00408090(0x744)`, `FUN_00408a60`,
  `FUN_00681e09(-1)`).
- The level clamps to the row's name range (`+0x114`..`+0x116`, as the
  hire list's entries, npc-menu.md "The list"): below `+0x114` or above
  `+0x116`, it's set to `+0x114`.
- The sunitproxy name-list entry for that (clamped level, class) pair
  (`FUN_00535ea0`'s `+0x10` table, `+0x10` sized stride) is marked
  "hired" (`+8 = 1`) when it wasn't already (`+0xc == 0`) — the same
  entries the hire list rolls from (npc-menu.md "The list").
- The unit itself comes from `FUN_00555230(0,0,player,0,1,mode,0)`, mode
  1 or 0xc depending on the expansion flag; nonzero, `FUN_00573270(unit,
  name id, {level, (unread word)}, expansion)` applies it (stats/skills,
  MonsterAI.cpp) and the unit is returned.

Not traced this batch: `FUN_005531c0`'s 0x735/0x737 codes, and exactly
what the unread word (`record+6`) and the hireling row's `+8` dword feed
into `FUN_00573270`. d2d: not ported; `cmd::MercItem` / `World::hire_list`
(npc-menu.md) cover the item and hire-list side only. No oracle run this
batch — read from the decompile and raw disassembly, cited, not matched
against game.exe with `tools/emu`.
