# NPC menus — game.exe 1.14d

Clicking a town NPC opens their menu ("talk", "trade", …, "cancel").

## The table

`0x726c48` holds 48 (`DAT_00725a74`) records of 39 bytes:

    u32 MonStats hcIdx | u32 entries (Cancel included) | u16 string[5] | u32 handler[5] | u8

`FUN_004b2e30` looks an NPC up by `unit+4` (its class, i.e. hcIdx), and
`FUN_004b4830` builds the menu from the record. Dumped verbatim into
`components/game/npc/npc_menu.hpp`.

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
- The act hire NPCs (hcIdx 0x96 Kashya, 0xc6 Greiz, 0xfc Asheara, 0x16f
  Tyrael, 0x203 Qual-Kehk): FUN_004b6440 adds entry 0x1507 (handler
  FUN_004b1dd0) ahead of "hire", or before "cancel" with no hire, while
  the client has a dead merc's name (DAT_00725494 != 0xffff). See
  "Resurrect" below.
- 0xfb4 "identify items" only shows when there's something to identify.
Warriv's "go east" still comes from elsewhere.

## Hire (FUN_004b5c60)

A 490x350 NPC text window: header 0xd24 "Your Gold: %d     Hire which
Mercenary?" (carried + stash gold) in gold, a list box (FUN_004bf8f0,
490x280 at ((W-490)/2, H/2-160); "The list box" below) of the offers the
server sent (0x7c0c85), and 0xd48 "cancel". A row is two lines (the list's
vtable +0x30 takes them, 0x4b62fe):
- the merc's name string, 0xf9b 0xf9c 0xf9b (" - "), then 0xd28 "Lvl",
  0xd26 "Life", 0xd27 "Def" and 0xd29 "Cost" (`FUN_006637f0`'s [1], [2],
  [7], [5]), each its label's first 20 characters (`FUN_00526790(.., 0x14)`),
  0xf9d 0xf9b (": ") and the value by `wsprintfA("%2u")` widened at most 9
  characters (`FUN_00526f20(.., 9)`: a longer one is cut to 8), two 0xf9b
  between them;
- two 0xf9b, then the hireling's description: `FUN_00663720([12])`, [12]
  the hireling.txt row's byte +0xd2, HireDesc (`FUN_00655720`'s field
  {"hiredesc", 0xd, +0xd2}, linked through hiredesc.txt's Code, 0x96bce8:
  its row, None 0 .. Lightning Spells 8), the string at table 0x6ee410
  (0, 0xd2b, 0xd2c, 0x2b52, 0x2b50, 0x2b51, 0xd31, 0xd30, 0xd2f; 0: none),
  formatted by `FUN_0048be80` with "a0:%0" (`FUN_0048be00` cuts it at a
  '['). A Barbarian (no HireDesc) has the two spaces alone.

No offer at all: one row, 0xd25. Name ID 0x421 shows 0x2b0d instead.

Offer stats (FUN_006637f0, from the offer's seed): FUN_00656580 lists the
hireling.txt rows of the act, difficulty and version (100 LoD, 0 classic)
that share the first row's Level; one is picked; level = clvl − 5 +
rand(5), at least 2; with d = level − row Level: life = HP + HP/Lvl·d
(≥ 40), str/dex = base + (per-level·d >> 3) (≥ 10), cost = Gold·(15d +
100)/100 (≥ Gold), exp = (level + 1)·Exp/Lvl·level², defence = Def +
Def/Lvl·d, damage = Dmg-Min/Max + (Dmg/Lvl·d >> 3).

**The list** (server, SUNITNPC.CPP `FUN_00576070`, 2026-10-10): the
seller's first hireling.txt row for the difficulty and version
(FUN_006564d0: Seller +0x14, Difficulty +0x10) gives its name range
(NameFirst..NameLast, +0x114 / +0x116; FUN_00575ff0); one 16-byte entry
per name `{u16 name, seed, hired (+8), offered (+0xc)}`, each seed a step
of the sunitproxy seed (game +0x1d24 +8, FUN_00536b40: made at game start
by FUN_00536070 from a game seed step); then ten picks of rand(names) on
it, each moving on (wrapping) past a taken entry, offered = 1;
back at the start the picking stops. FUN_00577010: once no entry is
offered and unhired, the list is freed and rolled anew. An offer's stats
come from its entry (FUN_005770e0 → FUN_00663750: the name's row's act;
FUN_006637f0 on {entry seed, 666}): the row pick, then the level roll —
no other draw; the name is the entry's. The window's list box
(FUN_004bf8f0) shows seven of them and scrolls.

**The window** (`FUN_004b7eb0` with ECX (W − 490) / 2, EDX H / 2 − 0xc3,
width 0x1ea, height 0x15e, +0x58 0): with +0x58 0, `FUN_004b85f0` centres
each line (`FUN_004b83a0`) and lays out nothing, so the window stays
490 × 350 at ((W − 490) / 2, H / 2 − 0xc3). Its lines: the header (height
0x15, colour 4, font 1), "cancel" (height 0x13b, colour 0, font 1,
selectable): baselines 0x15 and 0x150 down (`FUN_004b8100`).
`FUN_004b8370(window, 1)` sets menu mode 1: the hovered line draws in
colour 3, without focus16. `FUN_004b7c00` hangs the list box on the
window; `FUN_004b8100` draws it after the box.

**The list box** (D2Win's ListBoxImplementation, vtable 0x6daaec,
`FUN_004bf650`; the hire window's `FUN_004bf8f0((W − 490) / 2, H / 2 −
0xa0, 0x1ea, 0x118, 6, 0, 1, 0x4b3660, 0)`): x +0x180708, y +0x18070c,
width +0x180710, height +0x180714, its default colour +0x180740 (0,
white) and font +0x180744 (1, font16), the row gap +0x18076a (6), each
line's height +0x180766 (0xe), the scroll bar's x +0x18075a (x + width −
0x12) and y +0x18075e (y + 0x10), the row callback +0x18074a (0x4b3660).
Up to 0x80 rows of six lines (row r at r × 0x300e, line k at +0x8 + k ×
0x800); the add (`FUN_004be500`, the wrapper's vtable +0x30 → +0x3c)
takes the lines, the row's value, its colour (or the default) and font
(0xe: the default). The hire window gives each offer two lines, its index
and the defaults.
- Rows that fit (`FUN_004be340`): from the first shown (+0x180730), each
  row's lines with text × 0xe + the gap summed; at the row that takes the
  sum past height − 0x14, the rows before it (at least 1); all fitting,
  the whole count. Two-line rows in the hire box: 34 each, so 7 (not 8).
- The draw (`FUN_004bee90`): those rows, each line with text at (x +
  0x20, y + 0x19 + 6 × rows before + 0xe × lines before) (`FUN_004be410`:
  a line as wide as width − 0x32 or wider loses its last letters till it
  isn't wider), in the row's colour, or 3 for the row at +0x180738 when
  there's a callback.
- The row under the mouse (`FUN_004bea50`, its event's u16 x +0xc, y
  +0xe): none past the scroll bar's x − 0xf; from the first shown, a row's
  lines span y + 0xc + its top .. + lines × 0xe (edges out), the gaps no
  row's; it doesn't stop at the rows that fit, so the box's bottom can
  name a row not drawn. Kept at +0x180738: by a mouse move inside the box
  (`FUN_004beb70`, edges out; outside, it stands), a press, a release. The
  first add sets it to 0 (from −1).
- A press inside the box (`FUN_004bf430`; not on the scroll bar's parts)
  keeps the row under it (+0x180734); the release (`FUN_004bf530`) calls
  the callback when the row under it is that row. Enter (`FUN_004bebc0`,
  key 0xd) calls it for the row at +0x180738.

Oracle: `tools/emu/hire_list_box.py` runs `FUN_004be340` and
`FUN_004bea50` on generated boxes and rows; `test_hire_list_box` gives
d2d's `rules::list_rows_shown` / `list_row_at` the same cases (fixture
`hire_list_box_cases.inc`).

d2d: `rules::hire_entries` / `for_hire` / `hire_names` / `merc_offer`
(rules.hpp), `World::hire_list` / `hire_offers` (per act, on the proxy
seed: `fight.spawning.proxy`), `open_hire_menu(..., scroll)` (the wheel).
Checked: `tools/emu/hire.py` — 720 offers (seeds × acts 1–2 × difficulties
× clvl 1 / 9 / 30 / 75) against `drlg-dump ... hire`, and 400 lists
(entries_dump, 41 and 21 names) against `drlg-dump ... hirelist`: all
identical, the game seed after too.

## Resurrect (FUN_004b1dd0, server FUN_00579c00)

- Text (FUN_004b4830): entry 0x1507 isn't drawn from its id (string.tbl's
  5383 is a "Dummy"); the line is 0x58a8 "Resurrect %s: %d" with the merc's
  name string (DAT_00725494; 0x421 → 0x2b0d, as the hire list) and the cost
  the server sent (DAT_007c0dd0).
- Click: C→S 0x62 (5 bytes, u32 NPC id; FUN_0054bc00 needs game +0x70 != 0
  (an expansion game), then length 5, else it answers 3) →
  FUN_00579c00(player, npc id). Refused unless the NPC is the one being
  talked to and one of the five above; then the player's dead merc
  (FUN_00574ec0(7, 1)).
- Cost (FUN_006637b0): merc level (stat 0xc)² / 2 × 15, at most 50,000.
- Pay (FUN_00576d90): carried gold (stat 0xe), then the stash (0xf);
  refused when both together fall short.
- Done: flag 0x10000 off at merc +0xc4, full life (stat 6 = max), and
  FUN_00579aa0 puts it by the player with its states cleared.
- d2d: `rules::merc_resurrect_cost` / `resurrect_merc`, `cmd::ResurrectMerc`,
  `ui.cpp resurrect_line`. ponytail: the refusal / done messages back to the
  client aren't sent; a joined game doesn't send 0x62.

## Identify (FUN_004b2020)

Sends the request (FUN_00478680) and the server identifies the items.
d2d identifies inventory, cube and worn items, as FUN_0062a530 counts them (`rules::identify_all`).
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
