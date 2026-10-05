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
with flag 0x20000 count. Callers:
- `FUN_00459020`, each time the player has moved ~80 (a distance on the
  `FUN_00620650`/`FUN_006206b0` position) since the last reveal: the
  player's room and its near rooms (`FUN_00619790`) of the same Layer,
  `all` = 0.
- `FUN_00459150`, the DRLG room callback: the whole room, `all` = 1.

Cel sets: act 1 uses `UI\AutoMap\MaxiMap` (1499 cels of 16×32); act 2
`Act2Map`; act 4 `Act4Map`; LoD town `ExTnMap`. The `…S` variants are
used when the automap-size option is "small" (`DAT_007a5150` = 1,
divisor 20 instead of 10).

## Drawing (`FUN_00459700` → `FUN_00459440`, four passes)

- `scroll = player world px / div - screen / 2 + (40, 15)`.
  - With a side panel open, x shifts by ∓W/4.
- `screen = cell * 10 / div - scroll`, drawn via `FUN_004f6510` (DC6,
  bottom-left anchoring), clipped to the screen (or to the small map's
  box).
- Cells within ~150 px of the centre use fade draw modes 0/1/2 (the
  "AutoMapFade" option).

## Unit marks (`FUN_0045a860` → `FUN_0045a7f0`)

The mark is a 13-point polyline at `0x6d6638`:

    (0,-1) (2,-2) (4,-1) (2,0) (4,1) (2,2) (0,1) (-2,2) (-4,1) (-2,0) (-4,-1) (-2,-2) (0,-1)

It is drawn doubled, as lines at `(px/div - scroll_x + 8, py/div -
scroll_y - 8)`.

Colours come from `FUN_004fb180(a, b, c)`, a nearest-palette search
against BGR palette bytes, so the arguments are (B, G, R):

| who | global | colour |
|---|---|---|
| you | `0x7a51b0` | red |
| party | `0x7a51b3` | green |
| other players | `0x7a51b1` | blue |

`FUN_00459bc0` picks the colour and decides whether the unit shows.
Your own mark sits next to the campfire and stash miniatures in the
town map, which confirms the (40, 15) scroll offsets.

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
- Tiles within 12 of the player are revealed as you walk; D2 reveals by
  room.
- A generated level reveals its `Level::picks` (the DT1 tiles game.exe's
  rooms take), by their DT1 header like `FUN_00457cf0`. A maze level's
  (caves, crypts, Jail, Cathedral, Catacombs) DS1 is blank, so reading
  the DS1 left their maps empty.
- Cel choice is a tile hash, not the game's RNG.
- Your own mark is drawn.
- Not yet: the fade, NPC/party marks, the small mode, panel shift.
- devctl `debug automap` reveals the whole level.
- Saved maps: `d2s_automap.hpp` reads and writes the files above;
  `load_automap` / `save_automap` (panels.cpp) run on entering the game,
  on a layer change and on saving. Cells are deduped like
  `FUN_00457b00`, against every cell at (x, y) rather than the tree's
  search path. d2d makes no unit icons or miniatures; a game.exe file's
  are kept and written back, the miniatures not drawn. No subdirectory
  try, no cut at a bad cel. test_automap checks the format.
