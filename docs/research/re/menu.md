# Game menu and mini-panel — game.exe 1.14d

Esc's in-game menu (UI 9) with its Options submenus, and the mini-panel
(UI 0x15) with the HUD's button for it. d2d: `apps/d2d/gamemenu.{hpp,cpp}`,
wired in `town/`.

## UI flags and Esc

UI flags are `DAT_007a27c0[38]` (setter `FUN_00455f20`): 9 the menu,
0x15 the mini-panel, 10 the automap.

Esc (`0x4690b0`), first that applies:

1. an NPC modal is up: nothing more;
2. the menu is open: `FUN_0047e200` closes it and restores;
3. `FUN_00456300` closes every Esc-closable panel (if any was open);
4. `FUN_0047e090(1, 0)` opens the menu.

`FUN_0047e090` closes the UIs listed in `0x713058` and remembers those
flagged b=1 (automap 10, mini-panel 21, and 6, 7, 17, 35) for
`FUN_0047e200` to reopen. The selection starts on the menu's last row.
The list is 38 entries of `{closes, reopens, was open}` (12 bytes); the
third word stays set after the menu shuts.

The mini-panel's start: the UI reset at a game's start (`FUN_00456970`,
which zeroes the flags) calls, at 0x456bad,
`FUN_004567f0`, which turns UI 0x15 on unless the registry's
`Diablo II\Mini Panel` reads nonzero (`FUN_00414f10` at 0x456806). At
the game's end `FUN_00456d80` calls `FUN_0047f130`, which writes
`Mini Panel` = 1 when entry 21's "was open" (0x71315c) is 0, else 0:
the mini-panel's state as the game menu last opened (Save and Exit).
d2d: d2d.cfg's `mini_panel` (`Town::game_ended`).

## The view with panels open

`FUN_0045aea0(state)` keeps the state in `0x7a5210` (`FUN_0045ae90`
reads it). `FUN_00455f20` sets it when a UI opens or shuts, and
`FUN_00456300` (Esc's close-all) does the same:

| UI | state when it opens | state when it shuts |
|---|---|---|
| 1 inventory, 4 skill tree (right) | 1; 3 when a left one is up (flags `0x7a27c8`, `0x7a2810`, `0x7a27fc`, `0x7a2800`, `0x7a2850`, `0x7a2854`, `0x7a2818`) | 0; 2 when a left one is up |
| 2, 0xf, 0x10, 0x14, 0x16, 0x24, 0x25 (left) | 2; 3 when UI 1 or 4 is up | 0; 1 when UI 1 or 4 is up |
| 0xc, 0xe, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x20 | 3 | 0 |

The inventory and tree checks are at 0x4560de..0x45613d. The left
panels' checks are at 0x4561e8..0x456206.

UI 0xc is the store (char-panel.md). 0xf is the quest log, 0x16 the
party and 0x24 the hireling. 0x14, 0x19 and 0x1a are traced (read, no
oracle run); their names first came from D2BS's UI enum:
- 0x14 the waypoints: `FUN_0049cf90` (S->C 0x63, the waypoint panel,
  act1-end.md) calls `FUN_00455f20` with ECX = 0x14 at 0x49cf9a. Its close
  (`FUN_00455ae0` case 0x14) calls `FUN_0049cf50`, which sends the 9-byte
  `FUN_004786a0(0)` (C->S 0x49 with level 0 is not traced; same send as the
  row click) once, guarded by `0x7bf085`. `FUN_00455f20` lists it with the
  left panels (state 2 / 1), per the table above.
- 0x1a the cube: `FUN_0048a460` (S->C 0x77 action 0x15, cube.md) calls
  `FUN_00455f20` with ECX = 0x1a at 0x48a469 and sets the panel state
  `0x7bcbf0` = 0xe. The open handler (`FUN_00455720` case 0x1a) loads
  `PANEL\supertransmogrifier` (`FUN_0048a4b0`); the close (`FUN_00455ae0`
  case 0x1a) runs `FUN_0048a500`: if `0x7bcbf0` is 0xe, clear it and run
  `FUN_0048a050` (sends C->S 0x4f `{0x17}` via `FUN_00478600`); free the cube
  art (`0x7bcc30`).
- 0x19 the stash: `FUN_00455f20` is called with ECX = 0x19 at 0x489e09
  (a stash function: its neighbours are stash mouse handlers that test the
  panel state `0x7bcbf0` for 0xc / 0xd). The open handler (case 0x19) loads
  `DATA\GLOBAL\ui\panel\bank` (`FUN_00489e50`; and `TradeStash` when
  `FUN_00408f20` is nonzero); the close (case 0x19) runs `FUN_00489ee0`: if
  `0x7bcbf0` is 0xc or 0xd, clear it, shut UI 0 and send `FUN_00478600(0,0)`;
  free the art (`0x7bcc2c`, `0x7bcc3c`). 0xc is the stash: `FUN_00489e00`
  (S->C 0x77 action 0x10: the 0x77 handler at 0x4b8cf0, its jump table at 0x4b8f7c, entry 16
  = 0x4b8d09, a `JMP 0x00489e00`; entry 17 = 0x4b8d0e, `JMP 0x00489f50`, the
  close that sends nothing) runs `FUN_00455f20(0)` and, if it opens, sets
  0xc. No store of 0xd exists: of the stores to 0x7bcbf0 (XrefsTo), the
  constants are 0, 1, 5, 6, 7, 0xa, 0xb, 0xc, 0xe, 0x13, and the table-driven ones
  (`FUN_00488b00`, `FUN_00488f10`) copy a value from the table at 0x7bc9ea
  whose three initialisers (`FUN_00487e20` / `e60` / `ea0`) write 1, 2, 3. 0xd
  is only tested (`FUN_00489ac0`, `FUN_00489a30`, the closes) and put back to
  0xc by `FUN_00489880` / `FUN_004898a0`, which set `0x7bce2c` to 0 / 1. So
  there is one stash state; `TradeStash` is not a second one: both arts are
  loaded in 0xc, `bank` always and `TradeStash` when `FUN_00408f20` (LoD
  installed), and the panel drawer `FUN_0048edf0` (states 0xc..0xd) takes `TradeStash` when `FUN_0044dcc0`
  (`0x7a04f4`, the S->C 0x01 expansion flag) is nonzero, else `bank`. The
  expansion stash is drawn higher (glyph top bottom - 0x19f against
  bottom - 0xdb) and its close button at x + 0x110 against x + 0x113. Not
  traced: what a nonzero `0x7bce2c` is for beyond those tests.
- The inventory beside them: the stash, the Cube and a store don't turn
  UI 1 on (`FUN_00489e00` opens 0x19 alone); the panel drawer runs
  `FUN_0048edf0` for UI 0xc, 0x19, 0x1a as for UI 1 (`FUN_00456ee0`).
  Each one's mouse-up handler takes a release over the inventory's close
  button (`FUN_00486e10`: x W - 80 - 0x12e .. - 0x10e, y H - 60 - 0x60 ..
  - 0x40, inclusive), with no belt popup under it and nothing in the hand
  (`FUN_0063c1e0`), as its own close: the stash's (`FUN_00489ac0`) shuts
  UI 0x19 (0x489b54), the Cube's (`FUN_0048a190`) UI 0x1a (0x48a208), a
  store's (`FUN_00488b00`) runs `FUN_004b3ed0`, which shuts UI 0xc then 8.
  d2d: `client::inventory_close_shuts` (storage_close.hpp).
- The panel keys beside them: Inventory, Character and Skill Tree (and
  the mini-panel's buttons, `FUN_0047ec50`) open through `FUN_00455f20`
  mode 2, which asks `FUN_00453910`. With the panel shut it walks the UIs
  that are up and reads each one's row of the table at `0x711130` (one
  38-dword row per UI; 0 pass, 1 shut that one, 2 or more refuse). The
  stash's row (`0x710a10`) and the Cube's (`0x710aa8`) refuse UI 1, 2
  and 4: I, C and T do nothing there (I seen doing nothing in 1.14d play too).
  A store's (`0x710090`) refuses 2 and 4 and passes 1, whose flag toggles
  under the store's inventory, drawn anyway. Oracle:
  `tools/emu/panel_keys.py`. d2d: `client::panel_key_refused`,
  `Town::toggle_inventory` / `toggle_character` / `toggle_skill_tree`.
- Sides: `FUN_00455f20`'s switch groups 0x14 with 2, 0xf, 0x10, 0x16, 0x24,
  0x25 (left: open 2 / 3, shut 0 / 1) and 0x19, 0x1a with 0xc, 0xe, 0x1b,
  0x1c, 0x1d, 0x20 (open 3, shut 0). d2d agrees: `view_state`
  (town.hpp:332-334) takes the waypoints as left and the stash / cube as
  covering. `FUN_00455ae0` also calls `FUN_0045aea0` after the 0x19 / 0x1a
  close (0x1a without `FUN_00453ee0`); the state it sets is hidden by the
  decompile's fastcall args and is not traced there.

The state moves the world:
- `FUN_0045aea0` sets `0x7a5214`: -W/4 for state 1, +W/4 for 2, 0
  otherwise.
- It sets the view's rect at `0x7a0640` (`FUN_00476000`): {-W/4, 0,
  W - W/4, H - 40} for 1, {W/4, 0, W + W/4, H - 40} for 2, {0, 0, W,
  H - 40} otherwise.
- Units draw at x - (`0x7a520c` - `0x7a5214`) (`FUN_0045afc0`,
  `FUN_00470ec0`).
- The cursor maps back to the world through the same offset
  (`FUN_0045aff0`).
- The floor draw hands the state to the driver (`FUN_004dea70`'s
  `local_10` → `FUN_004f68e0`).

So with the inventory open the player stands at x = W/4 (200 at
800 x 600), the middle of the free left half. With the character panel
open they stand at 3W/4. This also casts doubt on bugs.md #38's "the
software modes don't shift the world".

In state 3 nothing of the world is drawn (`FUN_0044c990` skips
`FUN_00476bc0`), and neither is the automap (below).

With the call's third argument set, opening or shutting one also moves
the cursor by W/4 (`FUN_00468770`, `SetCursorPos`; y stays), so it stays
over the same spot in the world:
- a right panel opens to state 1: x - W/4 when x > W/4;
- it shuts to 0: x + W/4 when x < W/2;
- a left one opens to 2: x + W/4 when x < W - W/4;
- it shuts to 0: x - W/4 when x > W/2.

No other change moves it. The keys pass 1: Character `0x468940`,
Inventory `0x468950`, Party `0x468960`, Skill Tree `0x468af0`, Hireling
`0x469170`, and the Quest Log through `FUN_004a3fe0`. Esc calls
`FUN_00456300(0, 1)` at 0x4690df. That function shuts the flags in
order, so the inventory and tree go before the left panels. The
mini-panel's and HUD's buttons pass 0.

**Draw order** (`FUN_00456ee0`, the in-game UI each frame):
1. the automap (`FUN_0045ad60`, UI 10), only while the state isn't 3;
2. the level's entry art;
3. the panels;
4. the border (`FUN_00499450`);
5. the HUD.

The panels therefore cover the map.

d2d (`rules/panel_view.hpp`, `Town::view_state`):
- It works the state out from what's open, not from the last open or
  shut.
- The camera moves by the shift (`Town::draw_view`, as the shake does).
- The weather scrolls with the unshifted view, since its own draw moves
  it (weather.md).
- `render_ingame` draws the automap before the border and the panels,
  and not at all in state 3.
- The panel keys (Character, Inventory, Skill Tree, Quest Log, Party,
  Hireling) and Esc's close-all move the cursor as game.exe does
  (`rules::panel_view_cursor_x`; main warps the OS cursor). A click on
  the HUD or mini-panel doesn't.

## Menu tables

A menu is `{count, step, hdr2, hdr3, hdr4}` then its rows:

| Menu | Header | Rows (select fn) |
|---|---|---|
| Main `0x713044` | 3, 50, 39, 51, 0 | Options `0x47f2a0`, Exit `0x47f2d0`, ReturnToGame `0x47f300` |
| Options `0x714210` | 5, 50, 39, 51, 0 | SoundOptions `0x47f310`, VideoOptions `0x47f340`, AutoMapOptions `0x47f3a0`, CfgOptions `0x47f400` (opens UI 0xb), Previous `0x47f430` |
| Sound `0x714224` (rows `0x715cc8`) | 8, 45, 34, 49, 36 | title, Sound, Music, 3DSound, EAX, 3DBias, NPCSpeech, SPrevious `0x47f460` |
| Video LoD `0x71875c` (classic `0x718748`) | 8, 45, 34, 49, 36 | title, Resolution (LoD only), LightQuality, BlendShadow, Perspective, Gamma, Contrast, SPrevious |
| Automap LoD `0x71d734` (classic `0x71d720`) | 7, 45, 34, 49, 36 | title, AutoMapMode (LoD only), Fade, Center, Party, PartyNames, SPrevious |

Each row's text is the image `data\local\UI\ENG\<name>.dc6`. Row types:
-1 the title, 0 a button, 1 a choice (its own images, e.g. SmallOff /
SmallOn), 2 a slider.

- Sound and Music are 21-step sliders. Select `0x47cda0` / `0x47cdf0`:
  volume = val*100/20. Init `0x47cdc0` / `0x47ce10`.
- 3DSound and EAX are choices; 3DBias is a centred slider.
- NPCSpeech is a choice: AudioOnly / TextOnly / AudioText.

Enabled fns: `0x4df880` (sound present) is true; 3D provider / EAX /
perspective `0x4f5170` are false here; gamma `0x4f5260` is the driver's
slot +0x5c: true under DirectDraw (the default) and Glide, false under
`-w`, Direct3D's by the device (video-drivers.md "Gamma and Contrast");
party names `0x4577b0` is true.

Volumes: master at `0x8817b0` (default 100), music at `0x8817b4`
(default 50). `0x4dfc84`: Music Vol sounds times music/100, then all of
them times master/100.

## Draw — FUN_0047e3d0

- top = (H-80)/2 - step*count/2. Row k at y = top + k*step; its text
  at y + hdr2.
- Draw mode 5 when enabled, else 1 (half).
- Type <= 0: centred at W/2 (align 1).
- Type 1: label at W/2-230 (align 0), choice at W/2+230 (align 2).
- Type 2: slider, `FUN_0047e260`:
  - sy = y + hdr4.
  - bar_x = W/2-0x3c with a label, W/2-0x91 without.
  - knob = bar_x + trunc(265.0/(c-1)*val).
  - Darkened strip at x = W/2-0x3b (or -0x90), y = sy-30, h = 30.
    Left part: w = knob-bar_x+12, mode centred ? 1 : 2. Right part: the
    rest of 0x121, mode centred ? 1 : 0.
  - The bar: `OptBarC` (handle `0x7bc954`) if centred, else `OptBar`; at
    W/2+0xe6 align 2 with a label, W/2 align 1 without.
  - The skull `OptSkull` (`0x7bc958`) at (knob, sy - (centred ? 0 : 1) - 1).
- Pentspin (`CURSOR\pentspin`) by the selected row, y = sel_y + hdr3.
  Left: x = W/2 - widest - 0xf9, frame f ? 8-f : 0. Right: x = W/2+0xf9,
  frame f. widest (`0x7bc95c`, `LAB_0047de80`) is the widest frame (52).
  Each frame draws at its DC6 offset (`FUN_004f6480` → the renderer):
  widths 51, 43, 27, 9, 23, 40, 50, 52 with ox −1, 3, 11, 21, 15, 7,
  1, −1 and oy 0, so it spins about x+25. f steps once a
  draw when more than 50 ms have passed since the last step
  (`FUN_00454850`, GetTickCount; no catch-up, its only caller).
- How often that draw runs (the pause below): under UI 9 the client
  draws every pass of the message loop (`FUN_00451bb0`), and the loop's
  `Sleep(10)` is skipped while the window is active (`0x70f7e0`, set by
  the WM_ACTIVATEAPP handler `0x451ab0`; default 1). So the step comes on
  the first draw past 50 ms: 62.5 ms on Windows (GetTickCount's 15.6 ms
  ticks), ~51 ms with a 1 ms clock (Wine). d2d checks once an app frame
  (~60 fps): ~64–67 ms.
- The only other pentspin is the front end's (`0x87e96c`, loaded by
  `FUN_005070d0`): list control type 7's draw (`FUN_00507160`, installed by
  `FUN_005075e0`) puts one either side of the selected entry and steps
  its frame (+0x5c, wrapping at 8) every draw, untimed. The NPC menu has
  none. d2d has no type 7 list; which screen uses it isn't traced.
- `FUN_00502680` draws a multi-frame image 0x100 apart. Align 1:
  x += -1 - (totalW >> 1); align 2: x -= totalW.

## Input

- Hit `FUN_0047d520`: my in (mid - step*count/2, mid + step*count/2),
  exclusive. It picks the last k with my >= y_k + count (quirk: count,
  not step). Titles and disabled rows give -1.
- LMB down `0x47d7f0`: select the hit row unless it's -1, call
  `0x47d670`, held = 1.
- LMB up `0x47d840`: if held and the hit row is the selected one,
  activate (`0x47d5c0`). Then held = drag = 0.
- Activate `0x47d5c0`, enabled rows only:
  - type 0: the select fn, sound 2;
  - type 1: val+1, wrapping to 0 past count-1, then the select fn,
    sound 1.
- Slider `0x47d670`. It needs drag, or the hit row being the selected
  one, on an enabled type 2 row.
  - strip = W/2-0x3b (or -0x90). base = W/2-0x3c (or -0x91), +12.
  - Only while dragging, or with mx in (strip, strip+0x121).
  - mx < base → 0. mx > base+0x109 → c-1. Else
    val = trunc((mx-base) / (float)(265.0/(c-1)*0.5) + 1.0) / 2.
  - drag = 1. If the value changed: the select fn, sound 1.
- Keys:
  - Up / Down (`0x47d920` / `0x47d8a0`) skip titles and disabled rows.
  - Left / Right (`0x47d9a0` / `0x47da90`): a choice wraps, a slider
    clamps.
  - Enter (`0x47db80`) activates.

## Mini-panel — FUN_0047f710

- Panel (`PANEL\minipanel_s`), bottom H-0x2f, at x:
  - no side panel open: W/2-0x4a-3;
  - a right panel open: W/2-0xcd;
  - a left panel open: W/2+0x38-2;
  - both open: hidden.
  - "left" is FUN_0047ea60: UI 2 character, 0xf quests, 0x14 waypoint,
    0x24 hire, 0xc store, 0x19 stash, 0x1a cube, 0x17 trade, ...;
    "right" is FUN_0047eb50: UI 1 inventory, 4 skill tree, and the
    store / stash / cube / trade (they open the inventory too). The
    belt popup counts as one too (0x47ebbe): UI 0x1f, or the popup
    drawn (`FUN_004979e0`: 0x7befa0, set by `FUN_00499040`), with a
    belt of more than one row (`FUN_004979f0`: 1 with none). So the
    bar moves left while the popup is up.
    The bar moves to stay centred over the HUD strip the panels leave.
- Buttons (`PANEL\minipanelbtn`):
  - x0 = W/2-0x4a (R: W/2-0xca, L: W/2+0x39), step 0x15, bottom H-0x32;
  - frame 2*id (+1 held), ids 0, 1, 2, 4, 5, 6, 7.
- Press `FUN_0047ef30`:
  - box centre cx = W/2 + (R ? -0x76 : 0) + (L ? 0x77 : 0);
  - x in (cx-0x54, cx+0x59), y in (H-0x45, H-0x2f);
  - sound 4.
- Release `FUN_0047ed90` → `FUN_0047ec50`: 0 character, 1 inventory,
  2 skills, 4 automap, 5 messages, 6 quests, 7 the game menu.
- HUD menu button `FUN_004977c0` (`PANEL\menubutton`):
  - at (W/2-8, bottom H-16), frames 0/1 closed, 2/3 open;
  - hit `FUN_00497780`: x [W/2-8, W/2+5], y [H-0x27, H-0xd];
  - press `0x499621` plays sound 4; release `0x499a82` toggles the
    mini-panel.

## The belt popup

The popup (the belt's upper rows) is up while `DAT_007bef98` is set.
Two things set it:

- UI 0x1f, the Show Belt key's toggle: `FUN_00456ee0` sets 0x7bef98
  each frame while it's on (`FUN_00498e90`).
- The mouse. The HUD's mouse-move handler `FUN_00499bb0` does nothing
  for a dead player (`FUN_00463df0`: mode 0x11) or with either of two
  client flags set (`DAT_007a0620` by `FUN_0044da30`, `DAT_007a066c`
  by `FUN_0044bfe0`; not traced, d2d doesn't test them). With the popup shut, the mouse
  inside x [W/2+0x17, W/2+0x91], y [H-0x27, H-10] (423..545 ×
  561..590 at 800×600), and UI 9 off (0x499ca5), it sets 0x7bef98 and
  0x7bef9c (brought up by the mouse). With it up, the mouse outside
  the popup's boxes (`FUN_00498dc0`: box n-4's left and top to box 3's
  right and bottom) clears 0x7bef9c, and 0x7bef98 too unless UI 0x1f
  is on.

The belt's draw (`FUN_00499040`) clears 0x7bef98 when neither UI 0x1f
nor 0x7bef9c is set (0x4990ce), or UI 9 is up (0x4990e2). A click on
the strip (`FUN_004996a0`, the mouse-up handler) uses or takes the
box's item (`FUN_00498870`); it doesn't toggle the popup. d2d:
`apps/d2d/belt_popup.hpp`, `Town::belt_up`; the mini-panel's move:
`belt_popup::moves_mini_panel`, `Town::mini_right`.

## The pause

A local game pauses under the menu: the client's frame (`FUN_0044efa0`)
in game mode 0 / 1 (`DAT_007a0610`, single player), with UI 9 (this
menu) or 0xb (Configure Controls) up and the player in a room
(`FUN_004646a0`), only draws (`DAT_007a0484`) and plays sounds
(`FUN_00482c20`), unpaced (the 40 ms frame, `0x70ef1c`, is skipped);
the server's frame (`FUN_0052fc20`) doesn't run. d2d:
the World doesn't tick while the menu is open without a host.

## d2d's ceilings

- Video and Automap options are drawn and change, but nothing applies
  them. The 3D sound rows are greyed out. Resolution and AutoMapMode are
  greyed out because d2d is 800x600 with the full map.
- Configure Controls (UI 0xb) is its own screen: controls.md.
- The mini-panel has no hover tooltips; Messages does nothing.
- The darkened strip's modes 0 / 2 are taken as keeping (3-mode)/4 of
  the colour (unverified). 3DBias's starting value isn't traced: it
  starts at 0. Gamma starts at 10 and Contrast at 99, where game.exe's
  defaults (155 and 100) put them (video-drivers.md "Gamma and Contrast").
- The volumes persist as `master_volume` / `music_volume` in
  `<user dir>/d2d.cfg`. The master scales each game sound, not the
  cinematics, as game.exe (sound.md "The master volume reaches nothing
  else").
