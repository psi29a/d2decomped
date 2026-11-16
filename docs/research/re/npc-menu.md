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
The line under the mouse (`FUN_004b7180`, kept at +0x44 and +0x48 by
`FUN_004b7200` on a move, -1 off every line): a selectable line (+0x174)
whose x + 0xf < mouse x < x + width − 0xf and baseline − 0xb < mouse y <
baseline + 4, edges out, the first. Only that band counts, whatever the
line's height, so "cancel" (0x13b high) is taken near its baseline, below
the list box (box bottom H / 2 + 0x78, cancel's band from H / 2 + 0x82).
d2d: `NpcMenuState::line_at`, `tests/test_npc_menu_hover.cpp`.

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
  key 0xd) calls it for the row at +0x180738 unless that's −1, once the
  box has been drawn twice (+0x180772: `FUN_004bf650` zeroes it,
  `FUN_004bee90` adds one a draw). With a callback every key it gets is
  the box's (the event's +0x18 = 1, +0x1c = 0, `FUN_00420290`). Oracle:
  `tools/emu/hire_list_enter.py` (`rules::list_enter`).
  Those keys are characters: the menu's handler table (0x7273e4, rows
  {type, message, handler}) has `FUN_004b7a90` on {0, 0x102} (WM_CHAR).
  It walks the menu's widgets (+0xb08) and calls each one's vtable +0x18
  (the list box's wrapper `FUN_004bedb0` -> `FUN_004bebc0`), stopping at
  the first that returns non-zero; with none, character 0xd goes to
  `FUN_004b79d0` and 0x1b to `FUN_004b76d0`. Key-downs have their own
  rows (type 3): 0x26 `FUN_004b7870`, 0x28 `FUN_004b78f0`, 0x27
  `FUN_004b7770`, 0x25 `FUN_004b77f0`, 0xd `FUN_004b79d0`, 0x1b
  `FUN_004b76d0`. So the box never sees a key-down. Not traced: whether
  this table's WM_CHAR row runs before the chat box's when both are up.

- The scroll bar (`FUN_004bee90`, `UI\menu	extslid`, `FUN_004be020`;
  each cel by its bottom-left through `FUN_004f6480`): the up arrow (frame
  9, 11 while pressed) at (scroll x, scroll y), the track's cells (13) 12
  apart below it, (height − 0x10) / 12 − 1 of them (+0x180762; 1 under
  0x20 high), the down arrow (8, 10 pressed) on the last cell. The thumb
  (`FUN_004be110`, the vtable's +0x30, after the rows): the rows wholly
  shown (+0x18074e: from the first shown, each row whose running sum is
  still under height − 0x14); with fewer than all, its cells of 0x12
  (+0x180718) from `top` to `bottom` (+0x180752 / +0x180756): share =
  whole × 128 / rows; first 0: 0 .. 0x12 × share >> 7; first under rows −
  whole: 0x12 × first / rows .. that + 0x12 × share >> 7 (at most 0x12);
  else 0x12 − 0x12 × share >> 7 .. 0x12. Drawn at scroll x − 1: frame 16
  on scroll y + 0xd + 12 × top, 14 on each cell to bottom, 15 one past.
- The scroll bar's parts (edges out): up arrow (`FUN_004be720`) scroll x ..
  + 0x10 by scroll y − 0xc .. scroll y; down arrow (`FUN_004be770`) the
  same 12 above scroll y + 12 × cells; track (`FUN_004be7c0`) between. A
  press on one keeps it (+0x180720 / +0x180724 / +0x180728, the count
  +0x18076e at 0); each draw while an arrow is pressed and under the mouse
  the count goes on one and the arrow steps unless it's a multiple of 10
  (0x4bf252..0x4bf26b); the release on it steps once more
  (`FUN_004bf530`).
- A step (`FUN_004be910` up, `FUN_004be990` down; with a callback): the
  selected row +0x180738 (the row under the mouse) moves one within 0 ..
  rows − 1 (`FUN_004b9a00(0, 0, 0)`: nothing for 0), and the first shown
  follows it (back to it, or on one when it passes the rows wholly
  shown; `FUN_004be8d0` the same). The wheel (`FUN_004be860`, the event's
  s16 +0xa): up selects the first shown row and steps up; down selects the
  last wholly shown and steps down. A release on the track
  (`FUN_004bf2f0`), below scroll y and with a thumb (top ≠ bottom): above
  the thumb (scroll y + 0xd + 12 × top) the selection goes back the rows
  wholly shown (0 at least), below its bottom on as many (rows − 1 at
  most, then rows − whole with fewer left); that row shows first.

Oracles: `tools/emu/hire_list_box.py` runs `FUN_004be340` and
`FUN_004bea50` on generated boxes and rows; `test_hire_list_box` gives
d2d's `rules::list_rows_shown` / `list_row_at` the same cases (fixture
`hire_list_box_cases.inc`). `tools/emu/hire_list_scroll.py` runs
`FUN_004be110` (its cels recorded), then a track release, a step or a
wheel notch; `test_hire_list_scroll` gives d2d's `rules::list_thumb`,
`list_page`, `list_step` and `list_wheel` the same (fixture
`hire_list_scroll_cases.inc`).

d2d: `rules::hire_entries` / `for_hire` / `hire_names` / `merc_offer`
(rules.hpp), `World::hire_list` / `hire_offers` (per act, on the proxy
seed: `fight.spawning.proxy`), `open_hire_menu` (its list box: rules/hire_list.hpp).
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
  FUN_00579aa0 puts it by the player with its states cleared. "Cleared"
  is `FUN_00577470` (0x79 bytes, `__thiscall` ECX the merc; no EDX or
  stack args — confirmed from raw disassembly, since the decompile shows
  a bare `FUN_00577470();`): with the merc and its inventory (`+0x60`)
  both nonzero, it walks that inventory (`FUN_0063b2c0`/`FUN_0063dfa0`/
  `FUN_0063dfd0`, as `FUN_0063aaf0` and `FUN_0055f500` do, inventory.md
  and items-mode-late.md), and for each child unit of type 4 (an item)
  whose `FUN_00625820(item, &layer)` finds an attached stat list,
  detaches it from the merc (`FUN_006277f0(merc, item)`) and reattaches
  it at that same layer (`FUN_00627910(merc, item, layer)`) — the same
  detach/relink idiom `FUN_0055f500` uses for a unit's whole inventory,
  here run once over the resurrected merc's own gear so its item stats
  are current. Returns 1 if it ran, 0 for a null merc or inventory.
  Right after this call `FUN_00579aa0` inlines the same x/y lookup
  hireling.md's "Repositioning it" (`FUN_005773d0`) factors into a
  standalone helper elsewhere, to put the merc beside the player; see
  also hireling.md "Spawning the merc" (`FUN_005774f0`), which creates
  the unit a dead-merc resurrect here is reviving.
- d2d: `rules::merc_resurrect_cost` / `resurrect_merc`, `cmd::ResurrectMerc`,
  `ui.cpp resurrect_line`. ponytail: the refusal / done messages back to the
  client aren't sent; a joined game doesn't send 0x62. The stat relink
  (`FUN_00577470`) and the spawn/reposition helpers aren't ported; d2d's
  stats presumably stay attached since nothing detaches them, so no
  disagreement is expected, just unported.

## Identify (FUN_004b2020)

Sends the request (FUN_00478680) and the server identifies the items.
d2d identifies inventory, cube and worn items, as FUN_0062a530 counts them (`rules::identify_all`).
Cain is cain5 (hcIdx 265 = 0x109, whose record has 0xfb4). a1q4.cpp makes
him in camp at the "cain start position", object 385 (DS1 preset 110),
however he comes: the rescue, a join with the quest done or the way into
town (FUN_005940e0 / FUN_00596de0 → FUN_00592960; quests-act1.md "Cain in
camp"). d2d stands him on FUN_00545340's size-2 spot there.

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

## Shop clicks (FUN_004b3870, UI/npcmenu.cpp)

`FUN_004b3870` (733 bytes; ECX the player, EDX the item, six stack args; ret
0x18) is what a click on an item does while an NPC session is up. Eight
callers: `0x487ba0`, the shop panel's click `0x488b00` (its call at
`0x488c80`), `0x48ffe0`, `0x490780`, `0x490ba0`, `0x490fc0`, `0x491ad0`,
`0x491d20` (inventory and equipment clicks, right clicks, `0x4912a0`'s
default). The stack arguments, in order: `sell` (exactly 1: the click is on
the player's own item; else a vendor's stock item), the click's x and y
(`0x7c0d4b` / `0x7c0d4f`), a fourth the ask passes on (`FUN_004b2650`'s
second argument, Shift's multibuy flag byte), `direct` (ask the server at
once), `repair_all`. The callers push these as: stock right click
`0x491cca` sell 0, direct 1; inventory left click `0x491ece` sell 1, direct 1;
the item clicks `0x49032e`, `0x4909ca`, `0x490def`, `0x491200` direct 0;
`0x487ba0` repair_all 0 and its own direct flag (ESI); Repair All is the shop panel's button of frame 0x12 (`0x488c80`): `0x7bcc48`, sell and repair_all are all its press state (nonzero), set before the call and cleared after (`0x488c85`).

Steps, in order (the oracle runs them all):
1. Nothing happens (returns 0) unless the session flag `0x7c0d29` is on and
   the NPC (`FUN_00463990`: unit id `0x7c0d25`, unit type 1) exists. Its class
   is unit +4.
2. **Repair All** (`repair_all`): only for the five repairers, classes 0x9a
   Charsi, 0xb2 Fara, 0xfd Hratli, 0x101 Halbu, 0x1ff Larzuk; any other
   class returns 0. With the flag `0x7bcc48` (`FUN_00489870`) clear it returns
   1 and does nothing. Otherwise the price is `FUN_0062fe60` over the player
   (ECX), difficulty (`FUN_0044dcd0`, `0x7a060c`) and the quest record
   `0x7c0d43` (store.md "Repair"), stored at `0x7c0d7f`; mode `0x7c0d31` = 3,
   pending `0x7c0de0` = 1, pending item `0x7c0de5` = 0, the click kept,
   `FUN_00467a70(0)`, and the request goes at once (`FUN_004b2650(1, arg)`).
3. A gold item (item type 4, `FUN_0062b400`) returns 0.
4. With `direct`, a click less than 500 ms after the last ask (`GetTickCount`
   against `0x7c0df1`, which `FUN_004b2650` sets) returns 0. From here the
   click's x and y are kept at `0x7c0d4b` / `4f` even if the click is then
   refused.
5. **Stock item (sell != 1): a buy.** With an item on the cursor (`FUN_0063c1e0`
   on the player's inventory, +0x60) it returns 0, no message, nothing sent.
   Mode 1; the price mode is 2 in gamble mode (`0x7c0db0`, `FUN_004b3500`),
   else 0.
6. **Own item (sell == 1)**, by the NPC's class (two byte tables, 0x4b3b60 /
   0x4b3be8, over jump tables 0x4b3b50 / 0x4b3bd8):
   - vendors 0x93 0x94 0xb1 0xc7 0xca 0xfc 0xfe 0xff 0x195 0x200 0x201 0x202
     (and a repairer when the repair button is off, `0x7bcbf0` != 4,
     `FUN_00489860`): price mode 1 is asked first and stored at `0x7c0d7f`
     (even for a refusal), then `FUN_0062a130` (flag 0x1000 or the
     Items.txt quest/ques rules, store.md "Refusals") must take the item,
     else returns 0; mode 2;
   - repairers with the repair button on (`0x7bcbf0` == 4): `FUN_004b1f80`
     (the gate below) must say the item wants a repair, else returns 0; mode
     3, price mode 3;
   - Cain, classes 0xf4 0xf5 0xf6 0x109 0x208: mode 4, no price. The code
     tests `FUN_006280a0(item, 0x10, ...)` (identified) and returns 0 on `== 1`,
     but that function returns the masked flags, 0 or 0x10, so it never
     refuses (bugs.md 119);
   - every other class returns 0.
7. Common tail: pending `0x7c0de0` = 1, `0x7c0de5` the item's id (+0xc),
   `0x7c0de1` its class (+4), `FUN_00467a70(0)`. With mode 1 or 2 and `direct`
   the request goes at once (`FUN_004b2650(1, arg)`, which sends C->S 0x32 buy
   / 0x33 sell, store.md); otherwise the click sound (`FUN_004b9a00(1, ...)`),
   the dialog state `0x7c0c6b` = 4 and the confirm box `FUN_004b2f50` (mode 3
   and 4 always take this way; its state 5 is the waiting box).
   Returns 1.

`FUN_004b1f80` (137 bytes, `ESI` the item): a gold item, or one `FUN_0062e660`
refuses (identified, not ethereal, then a charge missing or a type with
Repair that restocks or has durability), wants none; else yes if it has
durability (`FUN_00629930`), a max (`FUN_00625e00`) and stat 0x48 is
**below** it (signed), or it is stackable (`FUN_006289f0`, Items.txt +0x132)
with stat 0x46 below `FUN_006295b0`, or `FUN_0062a840` finds a missing charge
and its second answer is 0. Repair All's `FUN_0062fe60` is not the same test:
it counts durability `!=` max and restocks only a throwable stack
(`FUN_0062e5d0`).

d2d: `rules::shop_click`, `rules::shop_kind`, `rules::repair_gate` /
`repair_click_gate` (components/rules/shop_action.hpp). The town update asks
`shop_click` before sending a buy: a held item stops it, as at 0x4b3a77 (d2d's
server would have bought with it). Oracle:
`tools/emu/shop_action.py [--mutation-check]` runs `FUN_004b3870` for real
(callees hooked: unit lookup, difficulty, item class, sell test, the repair
gate, both price calls, the ask, the box, the sound, the click clear) over
12,814 cases, `FUN_004b1f80` for real over 2,240 leaf-answer combinations;
`test_shop_action` gives d2d the same cases (fixtures `shop_action_cases.inc`,
`shop_gate_cases.inc`).
Not traced, not claimed: what the confirm box (`FUN_004b2f50`, 714 bytes:
text 0xbf cut at a newline, a number from `FUN_004b2ad0`, handlers
`0x4b2e70` / `0x4b2f00`) and `FUN_004b2650`'s packet fields do beyond what
store.md has; `FUN_0062fdc0` / `FUN_0062fe60` prices are hooked (their
formulas are store.md's, oracle `price_oracle.py`); `FUN_0062a840`'s second
answer (d2d treats a missing charge as both answers 0); d2d has no confirm
box (a left click on a stock item with Buy toggled on, or on an item with Sell
or Repair on, sends at once where `0x49032e` etc. pass direct 0 and open the
box), no 500 ms gate, and no Cain item mode 4.

## The selected line (FUN_004b82f0)

`FUN_004b82f0` (114 bytes, ECX the NPC window, EDX a line): the window's
selected line, +0x44 (drawn lit by `FUN_004b8100`: colour 3 in menu mode 1,
the focus16 flames in mode 2, the default). A null window halts (line 0x4e7);
an index at or past the line count (+0x50) halts (0x4ed, signed: -1 passes); -1
stores -1; a lower index finds no line and halts (0x513); a line whose
selectable word (+0x174 + 0x110 * line) is 0 halts (0x50f); else it stores the
index. A halt is `FUN_00408a60` ("Unrecoverable internal error") and
`FUN_00681e09(-1)`. Callers: `FUN_004b85f0` (adding a line: when +0x44 is
-1 and the line is selectable the new line is selected, so the first choosable
line of a menu is lit as it opens; `FUN_004b7eb0` makes it -1), selects of -1
at the end of the hire window (`0x4b6356`), the Agree-to-trade windows
(`0x4b8b8c`, `0x4b8ca2`) and the message log (`0x4a018b`), and
`FUN_004b4db0`'s talk window (`0x4b4fba`: its line index + 1). A mouse move
(`FUN_004b7200`) sets +0x44 and +0x48 to the line under the mouse
(`FUN_004b7180`), -1 off every line, and plays sound 0 when it changes to a
line. Enter (`FUN_004b79d0`) runs the selected line's handler (+0x170); when
none is selected it selects the first choosable one. d2d:
`rules::select_line` (components/rules/shop_action.hpp). Oracle:
`tools/emu/shop_action.py` runs `FUN_004b82f0` for real over 826 windows (line
counts 0..10, selectable patterns, indices -3..11, the null window);
`test_shop_action` gives d2d the cases. Not ported (ponytail): the keys (`FUN_004b7870` / `4b78f0` up and down,
`FUN_004b79d0` Enter: the table at 0x7273e4) and the move's sound 0.
