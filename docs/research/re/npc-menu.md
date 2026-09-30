# NPC menus — game.exe 1.14d

Clicking a town NPC opens their menu ("talk", "trade", …, "cancel").

## The table

`0x726c48` holds 48 (`DAT_00725a74`) records of 39 bytes:

    u32 MonStats hcIdx | u32 entries (Cancel included) | u16 string[5] | u32 handler[5] | u8

`FUN_004b2e30` looks an NPC up by `unit+4` (its class, i.e. hcIdx), and
`FUN_004b4830` builds the menu from the record. Dumped verbatim into
`apps/d2d/npc_menu.hpp`.

String IDs:

| id | text |
|---|---|
| 0xd35 | talk |
| 0xd44 | trade |
| 0xd45 | hire |
| 0xd46 | gamble |
| 0xd06 | trade/repair |
| 0xd37 | go west |
| 0xd39 | sail west |
| 0xfb4 | identify items |

Handlers: 0x4b6c70 talk, 0x4b42b0 trade, 0x4b3d40 gamble, 0x4b5c60 hire,
0x4b2020 identify, 0x4b52c0 / 0x4b5260 travel.

Some entries are special-cased while building:
- 0xd09 checks the player's life (heal).
- 0xfb4 shows identify only when there's something to identify.
- 0x2ba0 depends on quest 0x29.

Runtime changes to the table (FUN_004b66b0, opening an NPC):
- Kashya (hcIdx 0x96): above character level 7, FUN_004b6410 rewrites
  her record to 3 entries with string[1] = 0xd45 "hire", handler
  0x4b5c60. Not quest-gated: FUN_004b66b0 reads only stat 0xc (level);
  Sisters' Burial Grounds' free merc (FUN_00579180) needs no entry.
- The act hire NPCs (0xfc, 0xc6, 0x16f, 0x203, and Kashya):
  FUN_004b6440 adds "resurrect" (0x1507? handler FUN_004b1dd0) ahead of
  hire while the merc is dead.
- 0xfb4 "identify items" only shows when there's something to identify.
Warriv's "go east" still comes from elsewhere.

## Hire (FUN_004b5c60)

A 490x350 NPC text window: header 0xd24 "Your Gold: %d     Hire which
Mercenary?" (carried + stash gold) in gold, a list widget (FUN_004bf8f0,
490x280 at ((W-490)/2, H/2-160), rows 0x23 high) of the offers the server
sent (0x7c0c85), and 0xd48 "cancel". A row: the merc's name string, " - ",
then 0xd28 "Lvl", 0xd26 "Life", 0xd27 "Def", 0xd29 "Cost", each ": %u".
Name ID 0x421 shows 0x2b0d instead.

Offer stats (FUN_006637f0, from the offer's seed): FUN_00656580 lists the
hireling.txt rows of the act, difficulty and version (100 LoD, 0 classic)
that share the first row's Level; one is picked; level = clvl − 5 +
rand(5), at least 2; with d = level − row Level: life = HP + HP/Lvl·d
(≥ 40), str/dex = base + (per-level·d >> 3) (≥ 10), cost = Gold·(15d +
100)/100 (≥ Gold), exp = (level + 1)·Exp/Lvl·level², defence = Def +
Def/Lvl·d, damage = Dmg-Min/Max + (Dmg/Lvl·d >> 3).

d2d: `rules::merc_offer` / `hire`; five offers per opening (the server's
count isn't traced), one line each.

## Identify (FUN_004b2020)

Sends the request (FUN_00478680) and the server identifies the items.
d2d identifies carried, worn and belt items (`rules::identify_all`).
Cain is cain5 (hcIdx 265 = 0x109, whose record has 0xfb4). a1q4.cpp spawns
him in camp on the rescue where the player came back through the portal
(FUN_00596de0 → FUN_00592960); where a game with the quest already done
places him isn't located — d2d stands him 3 subtiles off the town start.

## Building (`FUN_004b4830`)

- `FUN_004b7eb0(cx, cy, …)` creates the menu. `(cx, cy)` comes from
  `FUN_004b1c80`: the NPC's feet on screen, `y - 150`, with `y ≥ 20`.
- `FUN_004b85f0(menu, text, lineHeight, 0, colour, font, handler, selectable)`
  adds a line:
  - header: the NPC's name (`FUN_00464a60`), height 0x15, colour 4
    (gold), font 1 (font16), not selectable;
  - each entry: height 0xf, colour 0, font 1, selectable;
  - last line: "cancel" (string 0x102e).

## Layout (`FUN_004b8410`)

- Width = widest line + 20; height = sum of line heights + 15.
- `x = cx - w/2`, `y = cy - h/3`.
- Clamps, in this order:
  - if `x + w > W - 10`, then `x = W - w`;
  - if `y + h > H - 58`, then `y = H - h - 48`;
  - `x` and `y` at least 10.
- Each line's text is centred: `x + ((w - textW) + 1)/2 + 1`
  (`FUN_004b83a0`).

## Drawing (`FUN_004b8100`)

- The box: `FUN_004f6300(x, y, x+w, y+h, colour 0, draw mode 1)`, black
  at half transparency.
- Line i's baseline = `y + (heights of lines before it) + its own height`.
- The hovered entry (menu mode 2) gets `UI\CURSOR\focus16` frame
  `(draw counter % 7)`, bottom-anchored at `(textX - 24, baseline + 4)`
  and `(textX + textW + 2, baseline + 4)`.

## Hover names

The hover name comes from MonStats `NameStr` (1.14d; `namco` on the CD),
shown only when MonStats2 `isSel` is 1. The camp's guard rogues and the
chicken aren't selectable; their key "Dummy" reads "an evil force".
