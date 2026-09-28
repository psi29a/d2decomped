# Char-select list + scrollbar — game.exe 1.14d

How the LoD character-select screen lays out and scrolls the saved
characters. Companion to [[char-create-table]] and [[frontend-menu-table]]
(same 48-byte record pool at `DAT_00708d10`, index = `(addr - 0x708d10) / 48`).

## Globals

| Addr          | Meaning                                                    |
|---------------|------------------------------------------------------------|
| `0x0070cc0c`  | Visible slot count. 8 on LoD (`FUN_0043ae30`); 4/5 on the classic one-column layouts (`FUN_0043b080`). Static init 5. |
| `0x00779dc4`  | Total characters in the list.                              |
| `0x00779dc8`  | Scroll offset = index of the character in slot 0.          |
| `0x0070cc00`  | Selected character index (static init -1).                 |
| `0x007799fc`  | `int[8]` slot top-y table, filled at init.                 |
| `0x00779c84`  | Scrollbar widget (record `0xa7`).                          |
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

## Not yet RE'd
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
  - walk/run: take it straight.

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
