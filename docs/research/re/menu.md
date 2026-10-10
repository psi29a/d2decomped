# Game menu and mini-panel — game.exe 1.14d

Esc's in-game menu (UI 9) with its Options submenus, and the mini-panel
(UI 0x15) with the HUD's button for it. d2d: `apps/d2d/gamemenu.{hpp,cpp}`,
wired in `town.cpp`.

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
perspective `0x4f5170` are false here; gamma `0x4f5260` is true; party
names `0x4577b0` is true.

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
  frame f. f steps once a draw when more than 50 ms have passed
  since the last step (`FUN_00454850`, GetTickCount; no catch-up).
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
    store / stash / cube / trade (they open the inventory too).
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

## The pause

A local game pauses under the menu: the client's frame (`FUN_0044efa0`)
in game mode 0 / 1 (`DAT_007a0610`, single player), with UI 9 (this
menu) or 0xb (Configure Controls) up and the player in a room
(`FUN_004646a0`), only draws (`DAT_007a0484`) and plays sounds
(`FUN_00482c20`); the server's frame (`FUN_0052fc20`) doesn't run. d2d:
the World doesn't tick while the menu is open without a host.

## d2d's ceilings

- Video and Automap options are drawn and change, but nothing applies
  them. The 3D sound rows are greyed out. Resolution and AutoMapMode are
  greyed out because d2d is 800x600 with the full map.
- Configure Controls (UI 0xb) isn't drawn: it closes the menu.
- The mini-panel has no hover tooltips; Messages does nothing.
- The darkened strip's modes 0 / 2 are taken as keeping (3-mode)/4 of
  the colour (unverified). Gamma's and 3DBias's starting values aren't
  traced: they start at 0.
- The volumes persist as `master_volume` / `music_volume` in
  `<user dir>/d2d.cfg`. The master is OpenAL's listener gain, so it
  also scales the cinematics.
