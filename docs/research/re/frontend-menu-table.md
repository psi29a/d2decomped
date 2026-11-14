# Frontend menu table — game.exe 1.14d

Source binary: `game.exe` (1.14d monolith). Table at `0x00708e00`; asset
loader is `FUN_0042e6d0` (see [[gateway-ini]] for the wider RE workflow).

The frontend menu is data-driven: a single struct table lists every
sprite, button, and hotspot on every screen (title, char-select,
credits, cinematics selection). The drawer iterates the table each tick,
dispatching by `kind`.

## Record layout — 48 bytes

```
offset  size  field       purpose
 +0x00   u32  kind         2=sprite/bg, 3=paired-anim, 4=hitbox,
                           6=button (label + chrome), 1/8=hotspot markers
 +0x04   u32  x            top-left X (screen px)
 +0x08   u32  y            BOTTOM Y (bottom-left anchor) for every kind
                           checked so far (2, 4, 6) — TitleScreen has
                           y=599 with h=600, spans 0..599
 +0x0c   u32  w            bounding-box width
 +0x10   u32  h            bounding-box height
 +0x14   u32  flags        0 for most; 0x1b on EXIT (LoD-only variant)
 +0x18   u32  tbl_string   TBL string ID for label (0 = no text)
 +0x1c   u32  handle_slot  address of the DAT_00779xxx handle variable
                           holding this sprite's DC6 (0 for hitbox-only)
 +0x20   u32  on_click     click handler address
 +0x24   u32  sub_table    kind 3 only: pointer to a 5-entry sub-array
                           of {handle, x, y, ??, ??} (probably per-frame
                           anim state for the paired logo halves)
 +0x28   u32  pad
 +0x2c   u32  pad
```

## Handle inventory (from `FUN_0042e6d0`)

Store sites recovered by scanning for `A3 XX XX XX XX` (`mov [imm],eax`)
in the loader's byte range 0x42e77b..0x42ef45. Every DC6 the frontend
touches lives in a `DAT_007797xx` global.

| slot        | asset                                 | notes                          |
|-------------|---------------------------------------|--------------------------------|
| `0x77971c`  | `ui\CURSOR\ohand`                     | Hand cursor                    |
| `0x779704`  | `ui\FrontEnd\Diablo2`                 | Static "DIABLO II" 320×151     |
| `0x779708`  | `ui\FrontEnd\D2logoBlackLeft`         | Logo mask left  (30 frames)    |
| `0x77970c`  | `ui\FrontEnd\D2logoFireLeft`          | Logo fire fill left            |
| `0x779710`  | `ui\FrontEnd\D2logoBlackRight`        | Logo mask right                |
| `0x779714`  | `ui\FrontEnd\D2logoFireRight`         | Logo fire fill right           |
| `0x779718`  | `ui\FrontEnd\TitleScreen` / EXP       | Background — classic vs LoD    |
| `0x779720`  | `ui\CharSelect\creditsbckg` / EXP     | Credits background             |
| `0x779738`  | `ui\FrontEnd\WideButtonBlank`         | Wide button chrome             |
| `0x779754`  | `ui\FrontEnd\WideButtonBlank02`       | Wide alt chrome                |
| `0x779758`  | `ui\FrontEnd\NarrowButtonBlank`       | Narrow chrome                  |
| `0x77975c`  | `ui\FrontEnd\GWListBox`               | Gateway list box               |
| `0x77972c`  | `ui\CharSelect\Realmselectbutton`     | LoD-only realm select          |
| `0x779730`  | `ui\CharSelect\TallButtonBlank`       | LoD-only                       |
| `0x779734`  | `ui\CharSelect\characterselectscreenEXP` | LoD char-select BG          |
| `0x779774`  | `ui\FrontEnd\CinematicsSelectionEXP`  | LoD cinematics                 |
| `0x77973c`  | `ui\FrontEnd\MediumButtonBlank`       | Medium chrome                  |
| `0x779740`  | `ui\CharSelect\ShortButtonBlank`      | Short chrome (credits/cinema)  |
| `0x779748`  | `ui\FrontEnd\textbox2`                | Text-label chrome              |
| `0x77974c`  | `ui\FrontEnd\3WideButtonBlank`        | Very wide chrome               |

Note: **fire.DC6** is loaded by `FUN_004326f0` (char-select loader),
NOT the main-menu loader. It's the campfire animation between the
character silhouettes on the character-select screen. Main-menu
"fire" is entirely `Diablo2.dc6` + `D2logoFire{Left,Right}.dc6` fills.

## Classic main-menu records (0x708e00..0x708fe0)

```
addr        k  x    y    w    h   tbl    handle           label
0x708e00    2  240  120  320  151  0    DIABLO_II        Diablo2.dc6 (fire fills)
0x708e30    3  400  120  181  170  0    (sub-table 0x708c30)  BlackLeft+FireLeft pair
0x708e60    3  400  120  188  177  0    (sub-table 0x708c80)  BlackRight+FireRight pair
0x708e90    2  0    599  800  600  0    TitleScreen      Background (y=599 = bottom Y for BG)
0x708ec0    6  264  224  272  35   5106 WideButton       "SINGLE PLAYER"
0x708ef0    6  264  266  272  35   5107 WideButton02     "BATTLE.NET"
0x708f20    6  264  291  272  25   0    NarrowButton     Gateway link (no label — set dynamically)
0x708f50    6  264  333  272  35   5108 WideButton       "OTHER MULTIPLAYER"
0x708f80    6  264  528  135  25   5110 ShortButton      "CREDITS"
0x708fb0    6  402  528  135  25   5111 ShortButton      "CINEMATICS"
0x708fe0    6  264  568  272  35   5109 WideButton       "EXIT DIABLO II"  (flags=0x1b)
```

TBL string IDs decoded by index against `patchstring.tbl` /
`expansionstring.tbl` at load time. Our TBL parser stores by key —
add ID-based lookup when a subsystem needs it.

## LoD variant records (0x709010..)

LoD moves four buttons LOWER. The button chrome uses the same handles;
only the y coords shift. The title adds no other LoD records: CREDITS,
CINEMATICS and EXIT are the classic ones (indices 0xd to 0xf) in both
(`FUN_004336c0`, below).

```
0x709010    6  264  324  ...  WideButton    SINGLE PLAYER  (was y=224)
0x709040    6  264  366  ...  WideButton02  BATTLE.NET     (was y=266)
0x709070    6  264  391  ...  NarrowButton  Gateway link   (was y=291)
0x7090a0    6  264  433  ...  WideButton    OTHER MULTIPLAYER (was y=333)
```

The records after them aren't the title's (which screens add them is
not traced):

```
0x709100..1c0  2 (five text-label boxes at x=319, w=169, h=26)
0x7091f0..2b0  4 (five clickable rects, w=300, h=32)
0x709370..3a0  1 (two hotspot markers)
0x7093d0..    6 (cinema/credits sub-menu buttons)
0x709520..640  4 (char-select silhouette rects, off-screen y=615)
0x7095b0      8 (kind 8 marker at (50,0), maybe click threshold?)
0x709670    2 (326×341 sprite at (237,555), handle 0x779770 — some CharSelect asset)
```

## The title screen (`FUN_004336c0`)

A screen adds a record with `FUN_0042f430(ECX index)`, the record at
`0x708d10 + index * 0x30` (tcpip-menu.md). On `FUN_004359d0`'s last path
(config `+0x35e` not 2, 3 or 4, and `+0x35d` clear) it sets the mode
`0x7795ec` to 0 (`0x435c19`) and, the first time (config `+0x35f`
clear), runs `FUN_00435230` and sets `+0x35f`; after that it builds the
title, `FUN_004336c0` (`0x435c45`). OTHER MULTIPLAYER's CANCEL builds it too
(`0x434df0`).

In order:

1. `FUN_0043b9a0`: with a Battle.net client (`0x77abf0`) up, disconnect
   it ("Disconnect from BNET"). Config `+0x37` = 0.
2. `FUN_0043c4f0`: tear down the last screen (`FUN_0042f480`), add records
   8, 6, 7 (TitleScreen and the two logo halves; record 5, the static
   `Diablo2.dc6`, isn't added), the palette (`FUN_0042f2e0`), and
   `FUN_00432ef0` (in language 9 it moves 16 records of other screens).
3. With the expansion (`FUN_00408f20`): 0x12 (gateway, kept at
   `0x7795ac`), `FUN_00431af0`, 0x11 (BATTLE.NET), 0x13 (OTHER
   MULTIPLAYER), 0x10 (SINGLE PLAYER). Without: 0xa, 0xb (gateway,
   `0x7795ac`), `FUN_00431af0`, 9, 0xc.
4. `FUN_004fac90` (no `d2char.mpq` in the install path or the current
   folder, and `DAT_0074c82c` 0): BATTLE.NET, OTHER MULTIPLAYER and the
   gateway are disabled (`FUN_004f96f0(button, 0)`).
5. 0xd CREDITS, 0xe CINEMATICS, 0xf EXIT, and 0x115: a text box (kind 4)
   at (0, bottom 599), 200x40, font pair `0x7089c4` {1, 0} (Font16),
   alignment 2. Its text is `"v %d.%d%c"` (`0x6d48d4`) with 1, 0xe, 'd':
   "v 1.14d"; with config `+0x223` nonzero, `" %d"` (`0x6d48d0`) with
   0x47 follows. `FUN_004fcff0(record, text, 0)` sets it.
6. With `FUN_00514d80` (`DAT_008817c0`) nonzero, `FUN_005148f0(1)` turns
   the frontend playlist on (sound.md "Frontend music").

`FUN_00431af0` writes the gateway's label: the current gateway's name
(the client's +0x98, `BNGatewayAccess::Name`) put in a TBL string
(`FUN_00524a30`; which id is not traced), characters below 0x100 mapped
through `0x730578`, then `FUN_005009d0`.

The buttons' handlers (record +0x20, read from the table):

| Index | Button | Handler |
|---|---|---|
| 9 / 0x10 | SINGLE PLAYER | `0x435cd0`: mode 0; a `*.d2s` in the save folder (`FUN_00430bc0`, `FindFirstFileA`) tears down and opens character select (`FUN_0043b080`), none opens character creation (`FUN_00435580`) |
| 0xa / 0x11 | BATTLE.NET | `0x435d30`: mode 1, `FUN_00435330` (the realm and login screens, "Last BNet") |
| 0xb / 0x12 | Gateway | `FUN_00434600`: the gateway list, one line per gateway, then `FUN_00431af0` |
| 0xc / 0x13 | OTHER MULTIPLAYER | `0x430c50` (tcpip-menu.md); its OPEN BATTLE.NET, `0x435d10`, sets mode 3 and runs `FUN_00435330` |
| 0xd | CREDITS | `0x4312c0`: teardown, a palette, records 0x29 and 0x2a |
| 0xe | CINEMATICS | `FUN_00431600` |
| 0xf | EXIT DIABLO II | `0x431c30`: `0x7795e8` = 0, `FUN_00515f50(0xb4)`, `FUN_004f9190` |

d2d (`title_ui`, apps/d2d/frontend.cpp) has the seven LoD buttons, with
the LoD positions with or without the expansion. It doesn't draw the
0x115 version text. Its SINGLE PLAYER opens character select even when
the save folder has no `.d2s`.

## Button labels (`FUN_00500db0`, `FUN_00500c50`)

A kind 6 record becomes a button with `FUN_00501290`; its draw is
`FUN_00500db0` (control `+0x1c`). The control has x at `+0xc`, the bottom
y at `+0x10`, the width at `+0x14`, the height at `+0x18` and the label
at `+0x64`. After the chrome, the draw picks the label's font by the
button's height (`0x500f7f`) and how far the baseline drops:

| Height | Font (`+0x60`) | Drop |
|---|---|---|
| 0x23 and up | 9, FontExocet10 | 4 |
| 0x20 | 10, FontRidiculous | 3 |
| 0x15 to 0x22, but not 0x20 | 10, FontRidiculous | 2 |
| up to 0x14 | 10, FontRidiculous | 1 |

The font names come from the table at `0x6dc980` (`FUN_005013a0`; 9 is
`0x6dc910`, 10 is `0x6dc900`), under `DATA\LOCAL\FONT\LATIN\`. The
draw clears the colour `+0x264` to 0 every frame (`0x500fba`), so a
label never changes colour on hover, press or when disabled. It then
calls `FUN_00500c50(drop)` when the label isn't empty.

`FUN_00500c50` sets the font (`FUN_00502ef0`). It reads the font's height,
byte 10 of its `.tbl` (`FUN_00501a40`; `FUN_00501580` keeps the file at
slot `+8`). It gets the label's width with `FUN_00501820`, then:

- x = `+0xc` + (width `+0x14` − label width) / 2, or `+0xc` when the
  label is wider than the button;
- the baseline = `+0x10` − (height `+0x18` − font height) / 2 + drop;
- pressed (`+0x44` and `+0x48`, or `+0x4c`: the same test that picks
  the chrome's pressed frames), x − 2 and baseline + 2.

With flag 0x40 at `+0x40` (`0x500cb4`) the button has two lines: the
label at `+0x10` − 2 × ((height − font height) / 3) + drop, and the
string with id `+0x270` (`FUN_00524a30`) under it at `+0x10` −
(height − font height) / 3 + drop, each centred on its own width and
both moved when pressed. The halvings and thirds round toward zero. `FUN_00502360` → `FUN_00501c30` draws
the label at that point: colour 0, not centred again (0), mode 4 (not
traced). Each glyph's frame is placed with its bottom row on the baseline
(`FUN_004f64b0`).

The heights in the `.tbl` header: FontExocet10 10, FontRidiculous 7. The
capitals' frames: FontExocet10 is 18 rows, with ink in rows 2 to 14, and
FontRidiculous is 14 rows, with ink in rows 4 to 12. On the title:

| Buttons | Box rows | Baseline | Capitals' rows |
|---|---|---|---|
| SINGLE PLAYER (bottom 324, h 0x23) | 290..324 | 324 − 12 + 4 = 316 | 301..313 |
| Gateway (bottom 391, h 0x19) | 367..391 | 391 − 9 + 2 = 384 | 375..383 |

BATTLE.NET, OTHER MULTIPLAYER and EXIT follow the 0x23 row, and CREDITS
and CINEMATICS the 0x19 row, at their own bottoms.

d2d draws every frontend button's label this way (`draw_button_label`,
apps/d2d/frontend.cpp, with `button_label_spot` in
components/font/button_label.hpp). It used to draw them in Font16, whose
capitals in the Sky palette are mostly index 31 (196, 196, 196) where
FontExocet10's are mostly index 15 (12, 12, 8) with 29, 31 and 32, and
3 to 5 rows low, with a gold hover and no press offset. It still greys a
disabled button's label; game.exe keeps colour 0 there and draws the
chrome in mode 1 (not traced).

## Interpretation — layout convention

Working hypothesis after cross-checking the sprite dimensions:

- **Kind 2 sprites**: `(x, y)` is TOP-LEFT for regular sprites, BOTTOM-LEFT
  for the full-screen background. Diablo2.dc6 draws at `(240, 120)` spanning
  y=120..271 (upper third, right below the D2logo pair). TitleScreen at
  `(0, 599)` with h=600 spans y=0..599 (full screen).
- **Kind 3 paired-anim (logo)**: `(x, y)` is the shared anchor for BOTH
  halves; each DC6 frame's own `offset_x/offset_y` (bottom-left origin,
  per DCC convention) positions the frame relative to that anchor.
  Left half spans ~x=231..390, right half ~x=409..568, both roughly
  y=46..168.
- **Kind 6 button** (and kind 4 boxes): `(x, y)` is the BOTTOM-LEFT of a
  `w × h` rectangle — top row = `y - h + 1`, same as the kind-2 BG
  `(0, 599, 800, 600)`. Earlier notes said top-left; that put EXIT at
  y=568..603 (off-screen) and overlapped BATTLE.NET (y=366, h=35) with
  GATEWAY (y=391, h=25). Bottom-left stacks them flush. d2d converts with
  `rec_top()` in apps/d2d/main.cpp. The label sits as "Button labels" says.

## Follow-ups

- ID→string lookup on the TBL parser to resolve the `tbl_string` slot.
- Kind 4/8 records aren't yet mapped to concrete UI verbs — need a walk
  of the drawer function (probably `FUN_00435xxx` — click handlers cluster
  there) to see how they're consumed.
- PL2 hue-variation colormap parser so the fire fills use real orange
  instead of the additive-tint hack in `blit_fire_tinted`.
