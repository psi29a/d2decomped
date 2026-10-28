# Automap — game.exe 1.14d (`UI\automap.cpp`)

## AutoMap.txt (`FUN_0061fcf0`)

Each row:

    LevelName, TileName, Style, StartSequence, EndSequence, Type1..4 / Cel1..4

- `LevelName` indexes game.exe's level-type names at `0x6e7d50`
  (16-byte strings): 0 "None", 1 "1 Town", 2 "1 Wilderness", …,
  35 "5 Lava". These match LvlTypes ids; `FUN_0061fc10` does the lookup.
- `TileName` indexes the orientation names at `0x6e7f90` (8-byte
  strings): 0 fl, 1 wl, 2 wr, 3 wtlr, 4 wtll, 5 wtr, 6 wbl, 7 wbr,
  8 wld, 9 wrd, 10 wle, 11 wre, 12 co, 13 sh, 14 tr, 15 rf, 16 ld,
  17 rd, 18 fd, 19 fi (`FUN_0061fc80`).
- Records are 32 bytes:

      int level_type | int orientation | u8 main (0xff any) | u8 sub_lo | u8 sub_hi | u8 | int cel[4] | int n_cels

## Adding cells

`FUN_0061fff0(level_type, orientation, main, sub)` matches a record and
returns `cel[rand % n_cels]` from the game's RNG (`FUN_0045c3e0`).

`FUN_00457cf0` adds one cell per tile, once (tile flag 0x40000):
- The tile's level coordinates go through `FUN_00643310`:
  `x = (tx - ty) * 80`, `y = (tx + ty) * 40` (world pixels).
- Both are divided by 10.
- Lower walls (orientation > 15) get y + 24.
- The cell stores `{cel, x, y}`.

The lookup is by the tile's DT1 header (the room tile's +0x18):
orientation `+0x14` (`FUN_00604b60`), main index `+0x18`
(`FUN_00604b90`), sub index `+0x1c` (`FUN_00604c50`); the level type is
the level def's `+0x34` (`FUN_00642750`). The "lower wall" test reads the
room tile's own orientation (`+0x1c`).

`FUN_00458f40(room, all, layer)` runs it over the room's floors
(`FUN_00619660`), then its walls (`FUN_006196a0`), skipping tiles with
flag 8 (hidden). Unless `all` (or `DAT_007a51a0`) is set, only tiles
with flag 0x20000 count.

Flag 0x20000 is "drawn on screen". The tile draws set it, each tile
by its own draw, so a cell's floors and walls come in apart:
- floors (`FUN_004de730` -> `FUN_004de410` -> `FUN_004dde80`) and roofs
  (`FUN_004dea70`): when the draw's spot (screen x = tile px x + 0x50 -
  view x, y = tile px y - 0x50 - view y, less a roof's height: the
  buffer's top left) is inside the view's box, view `+0x14..+0x20`.
  `FUN_00476000` sets it to `{-0x50, -0x50, W + 0x50, H - 0x2f}` (W, H:
  `0x71146c`, `0x711470`); the 3D modes (`FUN_004f51d0` != 0) take 0x2d
  more off the left and 0x1e off the top, 0x1e onto the right.
- walls and lower walls (`FUN_004df1c0`, `FUN_004dedf0`): when the
  driver's wall draw (`+0xa0`, DirectDraw / GDI `FUN_005130a0`) drew a
  block: a 32 px block whose top left is in x `[-0x20, W)`, y
  `(-0x21, H + 0x20)` (`0x7c9138`, `0x7c913c`: the draw buffer's W
  and H). A side panel open takes its half: right (`FUN_0045ae90` 1)
  x `< W - W/2`, left (2) x `>= W/2 - 0x20`.

So the map only ever gets tiles that have been on screen. Callers:
- `FUN_00459020`, each time the player has moved 80 client px (an
  octagon distance on `FUN_00620650` / `FUN_006206b0`, the unit's client
  px) since the last reveal: the player's room and its near rooms
  (`FUN_00619790`) of the same Layer, `all` = 0. Tiles drawn in between
  keep the flag, so they come in at the next one.
- `FUN_00459150`, the DRLG room callback: the whole room, `all` = 1.

Cel sets: act 1 uses `UI\AutoMap\MaxiMap` (1499 cels of 16×32); act 2
`Act2Map`; act 4 `Act4Map`; LoD town `ExTnMap`. The `…S` variants are
used when the automap-size option is "small" (`DAT_007a5150` = 1,
divisor 20 instead of 10).

## Drawing (`FUN_00459700` → `FUN_00459440`, four passes)

- The scroll (`0x7a5198`, `0x7a519c`) starts from the view's top left,
  not the player: `FUN_0045b440` sets `0x7a520c` = player client x - W/2
  and `0x7a5208` = client y - H/2 + 16. Then
  - x = `0x7a520c` / div - `0x7a5220` / 2 - shift + 0x28 + `0x7a5188`;
  - y = `0x7a5208` / div - `0x7a521c` / 2 + 0xf + `0x7a518c`.
  - `0x7a5220` = W and `0x7a521c` = H - 40 (`FUN_0044ba20`, at the
    resolution change).
  - `0x7a5188` / `0x7a518c` are 0 full size; mini, W/3 - box x - 16 and
    H/3 - box y - 16 (`FUN_00457640`, the box from `FUN_00457520`).
  - shift is -W/4 / +W/4 with a side panel open (`FUN_0045ae90` 1 / 2),
    full size only.
- So the player's cell lands on their feet, (400, 294) at 800 x 600; mini
  at (663, 174), about the box's middle. `tools/emu/automap_scroll.py`
  runs these three on 96 cases; `rules::automap_scroll` matches all
  (test_automap_scroll).
- `screen = cell * 10 / div - scroll`, drawn via `FUN_004f6510` (DC6,
  bottom-left anchoring), clipped to the screen (or to the small map's
  box).
- Cells within ~150 px of the centre use fade draw modes 0/1/2 (the
  "AutoMapFade" option). The centre is (`0x7a5220` / 2, `0x7a521c` / 2),
  y 280 at 800 x 600.

## Unit marks (`FUN_0045a860` → `FUN_0045a7f0`)

The mark is a 13-point polyline at `0x6d6638`:

    (0,-1) (2,-2) (4,-1) (2,0) (4,1) (2,2) (0,1) (-2,2) (-4,1) (-2,0) (-4,-1) (-2,-2) (0,-1)

It is drawn doubled, as lines at `(px/div - scroll_x + 8, py/div -
scroll_y - 8)`.

Colours come from `FUN_004fb180(r, g, b)`, a nearest-palette search
(the same call makes the stamina bar's (f4, c0, 4c) gold and the
portraits' (0, 80, 0) green). A playtest's 1.14d screenshots show your
own mark blue and the merc's teal, so the arguments are (R, G, B); an
earlier reading as (B, G, R) drew you red.

| who | global | colour |
|---|---|---|
| you | `0x7a51b0` | blue |
| party | `0x7a51b3` | green |
| other players | `0x7a51b1` | red |

`FUN_00459bc0` picks the colour and decides whether the unit shows.
Your own mark sits next to the campfire and stash miniatures in the
town map, which confirms the (40, 15) scroll offsets.

### Which units get a mark (`FUN_0045ac90` → `FUN_0045a860` → `FUN_00459bc0`)

`FUN_0045ac90` walks the units of the player's room and its near list
(`FUN_00619790`), then (Party on) the party's members wherever they are
(`FUN_0045ab60`). `FUN_00459bc0` decides whether a unit shows and in
which colour; the colours are made once (`0x7a51b0..b9`, by
`FUN_004fb180(R, G, B)`):

| unit | when | colour (R, G, B) |
|---|---|---|
| player, you | | blue (0, 0, ff) `0x7a51b0` |
| player, your party | Party on | green (0, ff, 0) `0x7a51b3` |
| player, anyone else | | red (ff, 0, 0) `0x7a51b1` |
| player in mode 0x11 (a corpse) | its inventory's owner (`FUN_0063d450(+0x60)`) is itself | magenta (ff, 0, ff) `0x7a51b2` |
| monster, a pet (`FUN_00478d90`: the client's pet list `0x7bb5bc`, PetType automap) | ours | teal (44, 70, 74) `0x7a51b4` |
| monster, a party member's pet | Party on | green (48, a0, 34) `0x7a51b5` |
| monster with MonStats flag 9 (interact) | not 0x219..0x21b (the Ancient statues) | white (f4, f4, f4) `0x7a51b6` |
| object 0x3b (town portal) | | yellow (f4, f4, 0) `0x7a51b8` |
| object 0x3c (red portal) | not on levels 0x6f, 0x70, 0x75, 0x7d..0x7f | yellow |
| object 0x10b (the bank) | Names on | its name, no mark |

A dead monster (mode 0xc) or one with flag 0x200000 (+0xc4) gets none.
An interacting NPC's name (Names on) goes in Font6, colour 4 (gold),
centred, 10 above the mark (`FUN_00464a60`). A monster morphed to a
player's look (`FUN_00645270`, States.txt gfxtype) is judged as one.

### The top right (`FUN_0045ad60`)

With the map open, Font16 (`FUN_00502ef0(1)`), colour 4, right-aligned
16 px in from the screen's right edge, baselines from 0x18 down every 0x10:

1. the game's name and password (`FUN_00459e90`, a joined game);
2. the level's name (`FUN_0045a050`: `FUN_00453e70` of the level id);
3. "v 1.14d" (`FUN_0045a0a0`: "v %d.%d%c", 1, 0xe, 'd');
4. on Nightmare / Hell, string 0x1057 ("Difficulty: %s") of 0x1423
   ("Nightmare") / 0x1422 ("Hell") (`FUN_0045a190`);
5. a line for game types 6 and 8 (`FUN_0045a120`, `FUN_0040df60`;
   untraced);
6. on an expansion game, 0x58ca ("EXPANSION") (`FUN_00459ff0`).

## Saved maps (`Name.map`, `Name.ma0`..`ma3`)

The automap is kept beside the save, per map seed, not per difficulty.
All file I/O goes through thin Win32 wrappers: `FUN_00406960` CreateFileA,
`FUN_00406990` DeleteFileA, `FUN_004069a0` CloseHandle, `FUN_004069b0`
ReadFile, `FUN_004069d0` WriteFile, `FUN_004069f0` SetFilePointer.

### Picking the file (`FUN_00457f40`)

- Directory: the save path (`FUN_00407050`, registry "NewSavePath" /
  "Save Path"). If `DAT_007a0500` (a subdirectory) is set, it tries
  `<save>\<sub>\` first (made with `FUN_00406a10`) and falls back to
  the save dir. The name is `DAT_007a05c4`, the character's.
- `"%s%s.map"` (`0x6d6710`) opened read/write, OPEN_ALWAYS. It's 24 bytes:

      u32 version (12) | u32 next | u32 seed[4]

- If 24 bytes came back and version is 12:
  - the map seed (`*DAT_007a0638`, game +0x7c from the 0x03 load-act
    packet; `FUN_0044e100` stores it) is looked for in `seed[]`. Found at
    i: slot i, the file isn't touched.
  - Not found: slot = next, `next = (next + 1) % 4` (signed), `seed[slot]`
    = seed, `"%s%s.ma%d"` (`0x6d6704`) of the slot is deleted, the 24
    bytes written back at 0.
- Otherwise: version 12, next 1, `seed[0]` = seed (seed[1..3] keep what
  was read), all four `.ma0..3` deleted, slot 0.
- `.ma<slot>` is opened read/write, OPEN_ALWAYS, and its handle returned.

So the last four seeds keep their maps. A character keeps its seed game
after game (drlg.md), so the map stays until the seed changes.

### The .ma file

    u32 head[100]            // by layer: offset of its first record, 0 none
    records...               // appended

A record:

    u32 next                 // offset of the layer's next record, 0 last
    u32 layer
    u32 cel_file             // the layer's +4: 0 the act's cels, 1..3 a town miniature's
    u32 object_seed          // game +0x80 (DAT_007a0638[1])
    u32 bytes[4]             // byte counts of the four lists
    u16 cel, x, y ...        // list 0, then 1, 2, 3

The layer is the level's Levels.txt Layer (level def +8, via
`FUN_0061e470`). A layer's map (`FUN_00458cf0`/`FUN_00458d40`, 0x1c
bytes: id, cel file, four cell trees, next) holds four AVL trees of 0x14-
byte cells (`FUN_00457c30`): `+0` saved flag, `+4` s16 cel, `+6` s16 x,
`+8` s16 y, `+0xa` balance, `+0xc`/`+0x10` children. The lists:

| list | layer + | added by | what |
|---|---|---|---|
| 0 | 0x8 | `FUN_00458f40` → `FUN_00457cf0` over the room's floors (`FUN_00619660`) | floor cells |
| 1 | 0xc | the same over its walls (`FUN_006196a0`) | wall cells |
| 2 | 0x10 | `FUN_00458dc0` → `FUN_00457e80` | units' icons: objects with an Objects.txt automap cel (`+0x1bc`; 0x10b, 0x16e, 0x192 have conditions), monsters with one at their MonStats2 row's `+0x118` (via MonStats `+0x18`) |
| 3 | 0x14 | `FUN_004591a0` | the town miniatures: level 0x28 (cel file 1), 0x67 (2), 0x6d (3) |

`FUN_00457b00` inserts by y, then x; at the same (x, y) a cel whose
group (`DAT_007a3150`, filled by `FUN_0045a4c0` from the {cel, group}
pairs at `0x711258..0x7113e0`) is -1 or the same as the cell there is
dropped, otherwise the cel decides.

**Writing** (`FUN_004584c0` → `FUN_00458200`):
- Only cells with flag 0 (added since the layer was loaded) go out:
  `FUN_00458440` counts 6 bytes each, `FUN_00458470` copies cel, x, y in
  order (in-order walk: y, x, cel). With all four lists empty the file
  isn't opened.
- The 400-byte head is read; short, it's zeroed and written.
- The record's offset is the file size. head[layer] is set to it, or the
  layer's chain is walked to its last record, whose `next` gets it.
- head written at 0, then the record at the end.

**Reading** (`FUN_00458750`):
- head short: nothing. Otherwise from head[layer] along `next`:
  - a record of another layer cuts the chain (`FUN_004586e0`: the previous
    record's next, or head[layer], set to 0) and stops;
  - list 2 is read only if the record's object seed is the game's (game
    +0x80, `FUN_00546c60`'s draw); else skipped;
  - a cel at or past its cel file's count (`DAT_007a5178[0]`, list 3:
    `[cel_file]`) cuts the chain at that record;
  - a short read is fatal;
  - each cell goes in its tree with flag 1.

**When:**
- `FUN_00458d40` (switching to a layer): save the current one, free its
  trees (`FUN_00458680`), load the new one. Revealing a room of another
  layer (`FUN_00459150`, the DRLG room callback set up in `FUN_0044e100`)
  switches there and back.
- `FUN_0045a5c0`, from leaving the game (`FUN_00456d80`): save, free.

## d2d

- Tab toggles the automap.
- What render_world drew last frame (`g_drawn`, with the level it
  drew) is revealed, as game.exe's flag 0x20000, tile by tile (each
  `DrawnCell` names its pick or DS1 layer cell): a floor or roof by the
  view box, a wall by its DT1 blocks (`dt1::Tile::blocks`) with the side
  panel's half, each above. Before 2026-10-28 d2d took every tile
  within 12 of the player, a square about three screens across (a
  tester saw the map run past what's in view). W is the screen's width,
  as game.exe's box takes `0x71146c`: a wider screen reveals more there
  too.
- A generated level reveals its `Level::picks` (the DT1 tiles game.exe's
  rooms take), by their DT1 header like `FUN_00457cf0`. A maze level's
  (caves, crypts, Jail, Cathedral, Catacombs) DS1 is blank, so reading
  the DS1 left their maps empty.
- Cel choice is a tile hash, not the game's RNG.
- Your own mark is drawn.
- The options (Options → Automap, the key config's automap functions;
  kept in d2d.cfg `automap_*` as game.exe keeps them in the registry):
  - Mini (`DAT_007a5150`): divisor 20 and `MaxiMapS`. The map is in a
    third of the screen's corner (`FUN_00457520`): top right at
    (W·2/3, 0x4e), or with "AutoMap Left" (Toggle MiniMap, `0x468ad0`)
    top left at (0, 0x60 with portraits up, else 0). Cels are clipped
    to `{W-0x119, 0x39} + (0x117, 0xe1)`, or the left one.
  - With a side panel open, the full map moves a quarter screen away
    (`0x7a5210`: 1 right, 2 left) and the mini map takes the other
    corner.
  - Fade (`FUN_004576c0`, Fade Automap cycles it): Centre gives cels
    within an octagon of 150 round the centre draw mode 2 / 1 / 0 (from
    100 / 50 in). Everything is mode 1. Auto is 2 in town (mini: 1).
    Mini takes Centre as Everything.
  - Marks (`Town::automap_frame`) as the table above: the player's
    corpses, our merc and pets (PetType automap), interacting NPCs with
    their names in gold (Names on), town portals and red ones, the other
    players. Party names (Names and Party on) are in font6, centred, 10
    above the mark (`FUN_0045a760`).
  - Object icons (`automap_icons`): objects with an Objects.txt AutoMap
    cel (shrines, wells, the Cairn Stones, the Tree of Inifuss, ...) go
    in list 2 at their px / 10 + (1, -3), with `FUN_00458dc0`'s
    conditions, once drawn: game.exe's unit flag 0x10000000, set by
    `FUN_004dc7b0` for each unit of the view's draw list (`FUN_004df1c0`;
    0x20000000 then marks it added). `FUN_00471ec0` returns 1 whenever
    the unit has a frame (0 only for Objects.txt Draw off or no graphics),
    so "drawn" is "in the list", not "on screen". `FUN_004ddb70` ->
    `FUN_004dd7c0` fill it from the player's room's near list
    (`FUN_00619bd0`): a room whose client px box (`FUN_00619c40`, corners
    by `FUN_006433c0`) meets the view's, widened 200 left and 400 down,
    puts each unit whose tile (`FUN_00643340`) is in the view's unit grid:
    W/160 + 11 + (H-40)/80 + 11 tiles square (`FUN_004dce60`), from the
    view's tile less 3 in x and less W/160 + 11 in y. At 800 x 600 that is
    x -9..+24, y -17..+16 tiles round the player: well past the screen
    toward its bottom right. `rules::in_unit_grid` / `room_in_view`, and
    `automap_icons` over the near rooms (`game::missile_rooms`), the
    neighbour levels' objects too.
  - The top right's lines (above), but the game name / password and the
    game type 6 / 8 line.
- Not yet: other players' pets' marks, the bank's name, the miniatures.
  Cel lists draw a pass each (lists 0, 1, 2). The fade applies to every
  cel list; game.exe's to one (`DAT_007a5168`, untraced which). The
  fade's inner distances read the octagon's half (`ECX` there, unverified).
  Center Automap does nothing: d2d's mini map box never moves.
- The scroll is `rules::automap_scroll`, game.exe's to the pixel. Before
  2026-10-28 d2d started it from the player instead of the view's top
  left, so the map sat 40 px left and 8 up of the player (each mark on
  its cell, the map off the world).
- devctl `debug automap` reveals the whole level.
- Saved maps: `d2s_automap.hpp` reads and writes the files above;
  `load_automap` / `save_automap` (panels.cpp) run on entering the game,
  on a layer change and on saving. Cells are deduped like
  `FUN_00457b00`, against every cell at (x, y) rather than the tree's
  search path. d2d makes no miniatures; a game.exe file's are kept and
  written back, not drawn. No subdirectory
  try, no cut at a bad cel. test_automap checks the format.
