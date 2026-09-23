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

## Not yet RE'd

- Exact pixel placement of the scrollbar arrows/thumb inside the `0xa7`
  box (D2Win kind-5 widget, ctor `FUN_005084f0`, draw `FUN_00508370`).
  d2d right-aligns the 12 px bar at x = 586 over y 87..457.
- Keyboard navigation (`FUN_00439e90`, `FUN_0043a0d0`, `FUN_0043a9d0`
  read the offset/selection).
