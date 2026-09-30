# Waypoint panel

game.exe FUN_0049c9c0 draws it; FUN_0049c580 loads the art (ui\menu\
waygatebackground, waygatetabs or expwaygatetabs, waygateicons into
0x7bf071/75/79). Numbers below are at 800x600: game.exe adds
0x7a2858 = 80 to x and subtracts 0x7a285c = −60 from y.

## Layout

- **Background**: 4 frames, 2×2, bottoms at 316 and 510 (the left-panel
  spot, top 60).
- **Act tabs**, bottom 94:
  - expansion: 5 tabs at x 80 + {3, 67, 129, 191, 253}; hit x−80 / 64;
  - classic: 4 tabs at x 80 + {3, 81, 159, 237}; hit x−80 / 80;
  - hits only for y ≤ 90 (FUN_0049c490);
  - frame 2i when active, 2i+1 otherwise;
  - a tab is drawn only once its act is open: quest 7, 15, 23 or 26
    done (bit 0, FUN_0065c310).
- **Rows**: up to 9, from the table at 0x7224e8 (6 ints each: icon x,
  icon bottom, text x, text baseline, hit x, hit top):

  | row | icon bottom | text baseline | hit top |
  |-----|-------------|---------------|---------|
  | 0   | 89          | 84            | 60      |
  | 1   | 125         | 119           | 96      |
  | 2   | 161         | 154           | 132     |
  | 3   | 197         | 189           | 168     |
  | 4   | 234         | 224           | 205     |
  | 5   | 270         | 259           | 241     |
  | 6   | 306         | 294           | 277     |
  | 7   | 342         | 329           | 313     |
  | 8   | 378         | 364           | 349     |

  Icon x = 17, text x = 80, hit x = 17 (all +80; y +60).
  - Icon frames: 0 for the level you're in; 3 activated, 4 hovered;
    none when not activated.
  - Name: Levels.txt LevelName in font16. Colour 5 (grey) when not
    activated, 3 (blue) when hovered or the current level, else white.
  - Hover and click (FUN_0049c510): activated rows only, x in
    (17, 297), y in (top, top + 30).
- **Title**: font16, centred at x 160, baseline 48.
  - "Choose your destination" (0xf96) when another waypoint is
    activated (0x7bf08e);
  - otherwise "No Other Waypoints Activated" (0xf97).
- **Cancel**: buysellbtn frame 10 (11 pressed) at 0x111, bottom 0x1a1.
  Hovering (0x111..0x135, 0x183..0x1a5) shows "Cancel" (0x1022).

## Data

- Levels.txt: the `Waypoint` column is the index (0..38), `Act` the tab,
  `LevelName` the string key; rows are sorted by index within each act.
- d2s: `WS` at 0x279 + 6 bytes, then 24 bytes per difficulty (`02 01` +
  the activated bitfield at 0x283 + 24·d, LSB first).
- Waypoint objects: objects.txt OperateFn 23.

## d2d

- Touching a waypoint activates its level's bit (and the town's, index 0).
  Outside the towns it starts dark (InitFn 17): the first touch lights it
  (mode OP → ON) without the panel, the next opens it (FUN_00584e30).
- Tabs switch acts; Cancel or Esc closes the panel. An activated row sends
  C→S 0x49 {object, level}; the server refuses it within 10 s of the last
  level change, for the current level or an inactive one, else arrives at
  the destination waypoint's tile + 3 subtiles (≈ +0.6 cells), nearest free
  spot, and lights that waypoint if it's dark (docs/research/re/act1-end.md §3).
- The bits save with the character (d2s 0x283 + difficulty·24).
- Not yet: travel to another act (the Act 2+ levels aren't built).
