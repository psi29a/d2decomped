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
 +0x08   u32  y            top-left Y for kind 2/6; BOTTOM Y for kind 2
                           BG assets that draw full-screen (TitleScreen
                           has y=599 with h=600, spans 0..599)
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

LoD moves the buttons LOWER and adds separate text-label records that
overlay the button chrome. The button chrome uses the same handles;
only the y coords shift:

```
0x709010    6  264  324  ...  WideButton    SINGLE PLAYER  (was y=224)
0x709040    6  264  366  ...  WideButton02  BATTLE.NET     (was y=266)
0x709070    6  264  391  ...  NarrowButton  Gateway link   (was y=291)
0x7090a0    6  264  433  ...  WideButton    OTHER MULTIPLAYER (was y=333)
0x709100..1c0  2 (five text-label boxes at x=319, w=169, h=26)
0x7091f0..2b0  4 (five clickable rects, w=300, h=32)
0x709370..3a0  1 (two hotspot markers)
0x7093d0..    6 (cinema/credits sub-menu buttons)
0x709520..640  4 (char-select silhouette rects, off-screen y=615)
0x7095b0      8 (kind 8 marker at (50,0), maybe click threshold?)
0x709670    2 (326×341 sprite at (237,555), handle 0x779770 — some CharSelect asset)
```

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
- **Kind 6 button**: `(x, y)` is TOP-LEFT of a `w × h` chrome rectangle.
  The label centres inside it.

## Follow-ups

- ID→string lookup on the TBL parser to resolve the `tbl_string` slot.
- Kind 4/8 records aren't yet mapped to concrete UI verbs — need a walk
  of the drawer function (probably `FUN_00435xxx` — click handlers cluster
  there) to see how they're consumed.
- PL2 hue-variation colormap parser so the fire fills use real orange
  instead of the additive-tint hack in `blit_fire_tinted`.
