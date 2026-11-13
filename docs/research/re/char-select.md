# Char-select list + scrollbar — game.exe 1.14d

How the LoD character-select screen lays out and scrolls the saved
characters. Companion to [[char-create-table]] and [[frontend-menu-table]]
(same 48-byte record pool at `DAT_00708d10`, index = `(addr - 0x708d10) / 48`).

## Globals

| Addr          | Meaning                                                    |
|---------------|------------------------------------------------------------|
| `0x0070cc00`  | Selected character index (static init -1).                 |
| `0x0070cc0c`  | Visible slot count. 8 on LoD (`FUN_0043ae30`); 4/5 on the classic one-column layouts (`FUN_0043b080`). Static init 5. |
| `0x007799fc`  | `int[8]` slot top-y table, filled at init.                 |
| `0x00779c84`  | Scrollbar widget (record `0xa7`).                          |
| `0x00779dc4`  | Total characters in the list.                              |
| `0x00779dc8`  | Scroll offset = index of the character in slot 0.          |
| `0x00779dcc`  | Scrollbar position last applied (for delta).               |

`FUN_00408f20` = "is LoD installed" (probes `d2exp.mpq`); the drawer
`FUN_00438560` defers to the LoD drawer `FUN_004380f0` when it's true.

## LoD layout (`FUN_0043ae30` init, `FUN_004380f0` draw)

- 8 slots, row-major, 2 columns: x alternates `0x25` / `0x135` (37 / 309).
- `slot_y[i] = (i / 2) * 0x5d + 0xb2` → row BOTTOMS at 178, 271, 364,
  457 (record y is bottom-left), i.e. tops 86, 179, 272, 365: exactly the
  4 × 93 interior of the BG panel (y 86..457).
- Per slot, two kind-4 records registered in a loop:
  `0x84 + i` = 200×92 text box at (x, y); `0x8c + i` = 72×93 cell at
  (x + 200, y) — the portrait sits on the RIGHT of the name/level text.
  Both click to `0x43a9d0`.
- Slot chrome: `charselectbox` / `charselectboxgrey` (handles `0x779778` /
  `0x77977c`, record `0x96`), 256 + 16 px halves, 93 tall.
- Characters with index outside `[offset, offset + visible)` are hidden.
- Other registered records: `0xa2` OK, `0xa3` EXIT, `0xa4..0xa6` bottom
  row TallButtons (tbl `0x2a50`, `0x58cc` CREATE, `0x1498` DELETE),
  `0x9c` = 466×42 text box at (85, 78) — the header line.

## Scrolling

- Record `0xa7`: kind 4, box (564, 457, 34, 371) → x 564..597, y 87..457
  (the rail drawn into the BG's right edge), sprite handle
  `0x7797cc` = `ui\FrontEnd\joingamescrollbars.dc6` (6 frames, 12×14:
  f0/f2 up normal/pressed, f1/f3 down normal/pressed, f4 thumb, f5 blank).
  Change callback wired by `FUN_004fc6e0(0xa7, 0x439df0)`; enabled via
  `FUN_004fc5e0` only when `total - visible > 0`.
- `0x439df0` (on change): `offset += 2 * (pos - last_pos)`, clamp to
  `[0, total - 1]`, redraw. **One scrollbar step = one row = 2 characters.**
- `0x439e60(n)`: `pos += n` then set — the wheel / key nudge entry.
- Classic arrows (records `0x99`/`0x9a` at (634, 50) / (634, 555), 29×20,
  `selectchararrows.dc6`) step the offset by 1: `0x439d60` up (if > 0),
  `0x439da0` down (if `visible + offset < total`), also dragging the
  selection along.

## Slot contents (`FUN_004380f0`, list built by `FUN_00438ad0`)

- Kind-4 records are text boxes. The dispatcher (`FUN_004f93c0`) passes
  x/y in ECX/EDX (fastcall) and pushes: `+0x14` left margin, `+0x18` top
  margin, `+0x2c` flags (2 centre, 0x10 right, 4 = has scrollbar). Slot
  boxes `0x84+i`: 200x92, **left margin 76, top margin 3**, flags 0x20.
- Lines, top to bottom: `[title ]name` in red (hardcore) / gold, then
  `"Level" (string 0xfd9) N ClassName` in white, then string 22731
  "EXPANSION CHARACTER" in green if status & 0x20, then string 10927 in
  gold if the ladder byte is set; Battle.net adds an expiry line.
- Title: `FUN_005068a0(class, progression, hc, lod)` → tier (classic
  <4/<8/<12, LoD <5/<10/<15; hardcore +3), `FUN_00505640` → hard-coded
  "Sir/Dame … King/Queen", "Slayer … Patriarch/Matriarch … Guardian".
- String IDs are banked: <10000 string.tbl, <20000 patchstring
  (id-10000), else expansionstring (id-20000).
- Portrait: per-char composite `FUN_005066c0(class, mode, appearance,
  tints)` from the .d2s appearance bytes (0x88/0x98), drawn at
  (slot x + 30, slot bottom - 13). Mode TN, NU for living hardcore; dead
  hardcore uses ghost class 8 (female: Amazon, Sorceress, Assassin) / 9
  (male), both token RH in the class token table (0x72e050: AM SO NE PA
  BA DZ AI RO RH RH ...); past class 6 the root is `monsters`, so the
  ghost is `monsters\RH\COF\RHTNHTH.cof`: one TR layer (lit), 16
  frames, one direction. `FUN_005066c0` stores the tint bytes minus one
  (compcode.md "Tints").
- Order: newest first — inserted by last-played time, descending.

## Scrollbar geometry (`FUN_005084f0` / draw `FUN_00508370`)

- Child of record `0xa7` with geometry `{585, 457, 363}` from `0x708d00`
  (x, bottom y, height). Cels are bottom-left anchored: down arrow at
  y-1 (443..456), track (f5) every 10 px above it, up arrow at
  (y-h)+9 (90..103), thumb at `(h-30)*pos/max - h + 19 + y`.

## Delete

- DELETE's click (`0x439cc0`) shows records 0xd6..0xd9: `PopUpOKCancel2`
  at (268, 350) 264x176, NO (0x142f, `CancelButtonBlank`, 281, 337,
  96x32, `0x439d40` closes), YES (0x142e, 421, 337, `0x43b6b0`) and the
  text box 0xd9 (268, 320, 264x120) with string 0x142b ("Are you sure
  that you want to delete this character? ...").
- YES (`0x43b6b0`): `FUN_0043c750` deletes every file `<save dir><name>.*`
  (not directories), then `FUN_0043b080` rebuilds the list.

## Select Difficulty

- OK (`FUN_00439840`) on a save whose progression (`+0x324` high byte
  & 0x1f) is >= 4 classic or >= 5 LoD opens the box, but only in single
  player (`DAT_007795ec` 0) or when hosting TCP/IP (mode 2, config
  `+0x19` gametype 8); a joiner plays the host's difficulty.
- `FUN_00439780(ECX progression, EDX status & 0x20)` makes the frontend
  records (table `0x708d10`, 0x30 bytes each):
  - 0xac `DifficultyLevels.dc6` (2 frames, 256 + 70 wide) at (237, 400
    bottom), 326x200;
  - 0xab text box (264, 260) 272x35, string 0x2723 "SELECT DIFFICULTY",
    colour 7;
  - 0xa8 / 0xa9 / 0xaa NORMAL / NIGHTMARE / HELL (0x2722 / 0x2721 /
    0x2720), `WideButtonBlank` 272x35 at x 264, bottoms 297 / 340 / 383,
    keys R / N / H;
  - 0xad, an off-screen button on Esc (`0x432ee0`: `FUN_0042f3f0` closes
    the box).
- HELL is made disabled (`FUN_004f96f0(button, 0)`), then enabled when
  progression >= 8 classic or >= 10 LoD. NORMAL and NIGHTMARE are always
  live.
- The buttons (`0x439b80` / `0x439ba0` / `0x439bc0`) call
  `FUN_00439af0(difficulty)`: config `+0x210` = 0 / 1 / 2, then the game
  starts as OK does.
- d2d: `CharSelectUI::pick_difficulty` (main.cpp sets the picked
  difficulty as the save's current one, its act kept), not when joining.

## Building the list (`FUN_0043adb0`)

`FUN_0043adb0` rebuilds the list (from `FUN_0043b4c0`, among others). It
clears `0x779dd8`, then reads the mode `DAT_007795ec` (written by
`FUN_004359d0` with 0 to 3, and 2 by `FUN_00434790`, the TCP host):

- 1: `FUN_004fb6a0`, `FUN_004fcff0(0)`, then `FUN_0043ad10`, a character
  list from a server, not the save folder (below).
- 0, 2 or 3: `FUN_00438ea0` empties the list (it frees each entry and
  clears `0x779dbc` head, `0x779dc4` count, `0x779dc8` offset,
  `0x779dd8`). Without the expansion (`FUN_00408f20` 0), `FUN_0043a5d0`
  adds a line (`FUN_0043a460(.., .., 1, 0xc)`, `0x70cc04` = the count).
  Then `FUN_00438f70` scans the save folder.
- Every mode then runs `FUN_00439210` and the drawer `FUN_00438560`.

The save folder scan, `FUN_00438f70`:

- `FindFirstFileA("<save dir>*.d2s")` (the folder from `FUN_00407050`,
  the pattern `%s*.d2s` at `0x6d41d4`); `0x779de0` counts what it keeps.
- The name is the file name up to its first `.`, copied to `0x779b68`.
  It's kept when it's 2 to 15 characters long (`0x43901e`: length - 2
  unsigned, at most 0xd), its last character isn't `-` (`0x439026`), its
  first isn't `_` (`0x43902f`), and `FUN_0043c8a0` reads its header.
- `FUN_00438ad0(name, stat string, 0)` adds it, with the file's
  `ftLastWriteTime` (the find data's +0x14 and +0x18, `0x43904c`) as the
  last two arguments.

The header read, `FUN_0043c8a0(ECX name, EDX out)`:

- Opens `<save dir><name>.d2s` (`%s%s.d2s`) and reads up to 0x2000 bytes.
  It needs more than 7 bytes and the magic `0xaa55aa55` at 0.
- The version at +4: below 0x5c it needs at least 0x82 bytes read and a
  version of at least 0x47, and reads the old layout (status word +0x18,
  class +0x22, level +0x24, appearance +0x26, colours +0x36). From 0x5c it
  needs at least 0x14f bytes and a version of at most 0x60 (status word
  +0x24, class +0x28, level +0x2b, appearance +0x88, colours +0x98).
  Anything else returns 0 and the file isn't listed.
- `out` gets a stat string of 0x22 bytes, its words in 7 bits with the
  high bit set (`FUN_006bbf40`; read back by `FUN_006bbf20`): +0 the word
  0xe; +2 11 bytes of appearance (the defaults at `0x70ccc8` when the
  first is 0); +0xd class + 1; +0xe 11 bytes of colours; +0x19 level;
  +0x1a the status word; +0x1c the word 0; +0x1e..+0x20 0xff; +0x21 0.

The entry, `FUN_00438ad0(EAX, name, stat string, extra, time low, time
high)`: a 0x414-byte entry; +0x340 / +0x344 the time, +0x348 the extra,
+0x300 / +0x310 appearance and colours (16 each, padded with 0xff),
+0x320 class, +0x322 level, +0x324 status, +0x34c the next entry. With
the expansion it goes in newest first by the 64-bit time (high +0x344,
then low +0x340); without, at the end, in the order the folder scan found
the files. Then the count `0x779dc4` goes up and the list redraws.

From a server, `FUN_0043ad10`: it empties the list and looks up a hash
of a name (`FUN_00413ce0`) in a cache at `0x779de8` (entries of 0x10:
hash, the packet bytes, the time they expire, the next). An entry whose
time hasn't passed (`GetTickCount`) is listed with `FUN_0043aab0`.
Otherwise it asks through the object at `0x77abf0` (its +0x68), and sets
`0x779dac` = 1 when that call returns nonzero.

- `FUN_0043aab0(key, packet, store)` reads a packet whose first byte is
  its id: +1, +3 and +7 words. It lists min(+3, +7) characters, each a
  name then its stat string; for id 0x19 each has 4 more bytes before the
  name, passed as the extra. A character `FUN_00438ad0` refuses lowers
  the count, runs `FUN_00437b90` and stops the caching. With `store`,
  `FUN_00437ab0` caches the packet. A count reaching the +1 word sets
  `0x779dd8`; +7 below +3 adds a line (`FUN_0043a460(.., .., 2, 0x12)`).
- `FUN_00437ab0(ECX key, packet, size)` copies the packet into the entry
  with that hash, or a new one at the head (`.\Src\CharSel.cpp` lines
  0xf1 to 0xfc), and sets its expiry to `GetTickCount() + 300000`: five
  minutes.

d2d (`load_saves`, apps/d2d/load.cpp) lists each `.d2s` whose header
`parse_header` accepts (the magic, versions 0x5c to 0x60, a name), with
the name from its header (+0x14), and sorts newest first by the header's
+0x30, not the file's last write time. The classic list isn't left in
folder order.

## Input

- `FUN_00439e90` (LoD keys; `FUN_0043a0d0` is the one-column classic
  variant): VK 0x24 Home → first, 0x23 End → last (scroll to the end),
  0x25 Left only from the right column, 0x27 Right only from the left
  column, 0x26/0x28 Up/Down by a row (±2); the offset follows the pick.
- `FUN_0043a9d0` (slot click): selection = offset + slot; a second
  0x201 press on the same character within 500 ms (GetTickCount) calls
  `FUN_00439840` — OK, i.e. double-click plays.

## Unit animation timing (in game)

- `FUN_005533d0` (SUnit.cpp) starts a unit's mode: it zeroes the 8.8
  frame counter (unit+0x30) and sets the end to `frames_per_dir << 8`
  (unit+0x48), so every mode plays from frame 0.
- `FUN_00620f00` stores the mode's animdata.d2 record at unit+0x50.
  Records are looked up by COF name (token + mode + weapon class,
  uppercased, bucketed by byte sum) in `FUN_0066a8f0`. Missing names
  fall back to 8 frames at speed 256.
- `FUN_00623f50` sets the per-tick rate (unit+0x4c):
  - base: the animdata speed;
  - attack modes: scaled by IAS;
  - cast modes: scaled by FCR;
  - get-hit: scaled by FHR + 50;
  - block: scaled by FBR (+50 or +100);
  - other modes: scaled by stat 69 `other_animrate`, clamped to 15..175 %;
  - walk / run / town walk (PlrMode +4): `FUN_006213d0`'s base, RN 0x65,
    WL and TW 0xd5 (AnimData's speed isn't read), x the walk % (stat 67
    velocitypercent + effective FRW, at least 25) / 100. The client's
    player tick `FUN_00463390` steps these modes (0x711e04 class 0) with
    `FUN_00623e00` at that rate, so a run's cycle (8 frames) takes 2048 /
    151 = 14 ticks against a walk's 10. Ported: `rules::moving_anim_rate`
    (`tools/emu/walk_rates.py` diffs it).

  The frame advances by rate/256 each 40 ms tick.

## Movement speed (in game)

- CharStats record (0xc4 bytes): `WalkVelocity` at +0x40, `RunVelocity`
  at +0x41 (field table at `0x613260`). The getter is `FUN_00620e40`.
- Running (`FUN_00620e80`) adds `run * 100 / walk - 100` to stat 67
  `velocitypercent`, i.e. +50% with the stock 6/9.
- Path velocity is `(walk << 8) * velocitypercent / 100` (`FUN_00462a20`),
  stored at path+0x7c.
- The client's arrival estimate (`0x4c86a3`) is
  `(dist_subtiles << 16) / ((v >> 8) << 12)` ticks, so a unit covers
  `v / 4096` subtiles per 40 ms tick:
  - walk 6: 0.375 subtiles/tick, 1.875 cells/s
  - run: 2.81 cells/s
- Monsters and NPCs use MonStats `Velocity` the same way (Akara 1,
  Kashya 3, Charsi 4, rogues 8). Tested only against town NPCs.
