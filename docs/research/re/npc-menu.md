# NPC menus — game.exe 1.14d

Clicking a town NPC opens their menu ("talk", "trade", …, "cancel").

## The table

`0x726c48` holds 48 (`DAT_00725a74`) records of 39 bytes:

    u32 MonStats hcIdx | u32 entries (Cancel included) | u16 string[5] | u32 handler[5] | u8

`FUN_004b2e30` looks an NPC up by `unit+4` (its class, i.e. hcIdx), and
`FUN_004b4830` builds the menu from the record. Dumped verbatim into
`apps/d2d/npc_menu.hpp`.

String IDs:

| id | text |
|---|---|
| 0xd35 | talk |
| 0xd44 | trade |
| 0xd45 | hire |
| 0xd46 | gamble |
| 0xd06 | trade/repair |
| 0xd37 | go west |
| 0xd39 | sail west |
| 0xfb4 | identify items |

Handlers: 0x4b6c70 talk, 0x4b42b0 trade, 0x4b3d40 gamble, 0x4b5c60 hire,
0x4b2020 identify, 0x4b52c0 / 0x4b5260 travel.

Some entries are special-cased while building:
- 0xd09 checks the player's life (heal).
- 0xfb4 shows identify only when there's something to identify.
- 0x2ba0 depends on quest 0x29.

Quest-gated extras such as Kashya's hire or Warriv's "go east" are added
elsewhere; d2d shows the base table only.

## Building (`FUN_004b4830`)

- `FUN_004b7eb0(cx, cy, …)` creates the menu. `(cx, cy)` comes from
  `FUN_004b1c80`: the NPC's feet on screen, `y - 150`, with `y ≥ 20`.
- `FUN_004b85f0(menu, text, lineHeight, 0, colour, font, handler, selectable)`
  adds a line:
  - header: the NPC's name (`FUN_00464a60`), height 0x15, colour 4
    (gold), font 1 (font16), not selectable;
  - each entry: height 0xf, colour 0, font 1, selectable;
  - last line: "cancel" (string 0x102e).

## Layout (`FUN_004b8410`)

- Width = widest line + 20; height = sum of line heights + 15.
- `x = cx - w/2`, `y = cy - h/3`.
- Clamps, in this order:
  - if `x + w > W - 10`, then `x = W - w`;
  - if `y + h > H - 58`, then `y = H - h - 48`;
  - `x` and `y` at least 10.
- Each line's text is centred: `x + ((w - textW) + 1)/2 + 1`
  (`FUN_004b83a0`).

## Drawing (`FUN_004b8100`)

- The box: `FUN_004f6300(x, y, x+w, y+h, colour 0, draw mode 1)`, black
  at half transparency.
- Line i's baseline = `y + (heights of lines before it) + its own height`.
- The hovered entry (menu mode 2) gets `UI\CURSOR\focus16` frame
  `(draw counter % 7)`, bottom-anchored at `(textX - 24, baseline + 4)`
  and `(textX + textW + 2, baseline + 4)`.

## Hover names

The hover name comes from MonStats `NameStr` (1.14d; `namco` on the CD),
shown only when MonStats2 `isSel` is 1. The camp's guard rogues and the
chicken aren't selectable; their key "Dummy" reads "an evil force".
