# Frontend text boxes (Latin, single column)

`FUN_004fd060` is a 12-byte wrapper. It leaves the control in ECX and
text in EDX, then calls `FUN_004fc9b0` with stack arguments `(4, 0, 0)`.
It does not lay out the rows itself.

## Wrapping and placement

| Address | Evidence |
|---|---|
| `FUN_004fb5a0` | Selects extra row spacing by language/font; the English path supplies 4. |
| `FUN_004fbab0` | Computes visible-row capacity: `(inner_height - pitch) / pitch + 1`, with division toward zero and pitch = font height + spacing for these boxes. |
| `FUN_004fbf30` | Draws kind-4 linked rows. Initial baseline is bottom - height + top margin + font height. Each row advances by font height + control +0xa8, plus the selected per-row extra (+0xa0/+0xa4). A following row must fit the remaining height; the first row is drawn regardless. |
| `FUN_004fc7a0` | Creates the control, stores margins at +0x40/+0x44 and spacing at +0xa8, and installs the drawing callback. |
| `FUN_004fc9b0` | Strips leading whitespace. Wraps at measured width **greater than or equal to** width minus twice the horizontal margin, unless flag 0x20 suppresses wrapping. |
| `FUN_004fcda0` | References `.\Src\D2WinTextBox.cpp`. Splits before the first character that reaches the width, preferring the last space seen. Feeds both pieces back into the row-adder. Overlong words split; internal spaces remain. |
| `FUN_004fd060` | Adds text with colour 4 and two zero options; delegates to the row-adder. |
| `LAB_00501650` | Latin glyph record = records + character * 14. |
| `FUN_005017d0` | Measures text by summing glyph-record byte +3; ignores linefeed. |
| `FUN_00501a40` | Reads the font TBL header byte +10 as line height. |
| `FUN_00501a80` | Sends each glyph to the sprite drawer at the text baseline, advancing by its width. |

Centred rows use `x + max(0, (width - row_width) / 2)`, with integer
division toward zero. Horizontal margins constrain wrapping, not that
centering calculation. The font drawer's bottom anchor converts to each
frame's top as `baseline + offset_y - frame_height + 1`.

The port is `components/font/text_box.hpp`, used by the frontend and
`tests/test_text_box.cpp`. The frontend passes each record's margins and
uses baseline glyph drawing. This covers the Latin, static, one-column
boxes it currently renders. It does not port the original control's
interactive scrolling, selection, multiple columns or language-specific spacing.
When appending overflows a box without a scrollbar, the row-adder advances
the first visible row, keeping the newest rows within capacity. The port
does that before applying the drawing function's height limit.
One defensive difference: when no glyph fits, the port consumes one glyph
rather than following the original splitter's non-progressing recursion.

## The list accessors

Traced 2026-11-24 by reading game.exe 1.14d, not run: the decompile and
the assembly for register arguments, plus a byte scan for callers. These
are the `.\Src\D2WinTextBox.cpp` functions the gap map listed as
untouched. Each one asserts a control (the first line number in the
table) of type 4 (the next line).

```sh
python3 tools/ghidra/fn.py 004fb790 004fb870 004fb910 004fb990 004fb9d0 004fbb30 004fbc80
python3 tools/ghidra/fn.py 004fbcd0 004fbd20 004fbde0 004fc620 004fc660 004fcd40 004fcd90
```

The fields they use (kind 4):

| Offset | Field |
|---|---|
| `+0x48` | the first line. A line holds five cell pointers (`+0`..`+0x10`), a colour `+0x14`, and the next line `+0x1c` |
| `+0x54` | the line count |
| `+0x5c` | flags: bit 0, lines can be selected |
| `+0x60` | the selected line, or -1 |
| `+0x64` | the column count |
| `+0x68`..`+0x78` | a value per column (`FUN_004fbc80`) |
| `+0x7c`..`+0x8c` | a second value per column (`FUN_004fbcd0`) |
| `+0x90` | the scrollbar ([frontier-ring.md](frontier-ring.md) `textbox_destroy`) |

| Address | Reading | Callers |
|---|---|---|
| `FUN_004fb790` | ECX box, EDX wide text; stack line, column (`ret 8`). The column must be below `+0x64` (fatal 0x1fb). The text is copied into a 0x400-unit stack buffer (`FUN_005267e0`, no bound). It walks to the line, and returns 1 with nothing stored when the list is shorter. The cell must be empty (fatal 0x20f). The copy is stored in a new block of length × 2 + 2 bytes (`FUN_0040b380`, line 0x210). Returns 1 | `FUN_004fcd40`, `FUN_004fcd90` |
| `FUN_004fb870` | ECX box, EDX line (at most `+0x54`): frees the line's five cells (line 0x231) and clears them. A line past the list's end is fatal | `FUN_004403f0` (`0x440571`) |
| `FUN_004fb910` | ECX box: the line count `+0x54` (fatal 0x23e / 0x23f) | `LAB_0044288a` |
| `FUN_004fb990` | ECX box: the selected line `+0x60` (fatal 0x256 / 599) | `FUN_00434700`, two chat-screen functions |
| `FUN_004fb9d0` | ECX box, EDX line: sets the selection. A negative line gives -1, and one at or past the last line gives the last (-1 for an empty list) | `FUN_00434600` and four other sites |
| `FUN_004fbb30` | ECX box: the selected line's first cell. Returns 0 with no selection, or when `+0x5c` bit 0 is clear | `FUN_0043b4c0` and three chat-screen sites |
| `FUN_004fbc80` | ECX box, EDX column, stack value: `+0x68`[column]. A column outside 0..`+0x64` − 1 is fatal (0x2f2) | the chat lists' setup (`0x4407f4` .. `0x440866`, `0x445f98`) |
| `FUN_004fbcd0` | ECX box, EDX column, stack value: `+0x7c`[column]. Only a negative column is fatal (0x2ff): there is no upper bound, and column 5 would be `+0x90`, the scrollbar. Its three calls (`0x440803`, `0x440851`, `0x440878`) pass columns 0, 3 and 4 with 0x10 | the chat lists' setup (three sites) |
| `FUN_004fbd20` | ECX box: walks the lines and returns their number less one (0 for an empty list) | one chat-screen function |
| `FUN_004fbde0` | ECX box, EDX line, stack colour: sets line `+0x14`, or every line's for -1. A line past `+0x54`, or past the list, is fatal (0x378, 0x382, 899) | `FUN_004403f0` and five `LAB_` sites |
| `FUN_004fc620` | ECX box: the scrollbar's position (`+0x90`, `scrollbar_pos`) | `LAB_00439df6`, `FUN_00439e60` |
| `FUN_004fc660` | ECX box, EDX position: sets it (`scrollbar_set_pos`) | `FUN_00439e60` |
| `FUN_004fcd40` | ECX box, EDX narrow text; stack line, column (`ret 8`): widened (`FUN_00526f20`, 0x400), then `FUN_004fb790` | `FUN_004403f0` (four), one chat-screen function |
| `FUN_004fcd90` | A tail jump to `FUN_004fb790` with the same arguments | `FUN_004403f0` (`0x4406ce`) |

## Actual records and live check

Record pool: `0x708d10`, stride 0x30. The join prompt's record 0x10c is
`(300, bottom 250, 200 x 50)`, zero margins, font 1 (font16), centred.
"Enter Host IP Address to" measures exactly 200 pixels. Original v1.14d
shows "Enter Host IP Address" / "to Join Game"; d2d v0.9.0 showed
"Enter Host IP Address to" / "Join Game". Both were checked live in
windowed mode on 2026-11-05, without submitting the form.

Font16's line height is 10 but its DC6 frame height is 16: the original
baselines are 210 and 224, and the frame tops 195 and 209. Font24's line
height/frame height are 15/26; FontFormal11's are 16/16. Font42's line
height is 27. The four checked TBL
header heights equal the maxima d2d's font reader currently calculates.

Delete-confirmation record 0xd9 uses font16, not font24, with horizontal
and vertical margins 10 and 8. Popup record 0xe0 uses font24 with the same
margins. The TCP/IP host/join explanatory boxes 0x108/0x109 use
FontFormal11; the join explanation has horizontal margin 2.

## Re-check against original instructions

Extract your own Latin font16.tbl, font24.tbl, font42.tbl and fontformal11.tbl into
`/private/fonts` with mpq-cat; do not commit them. Build `test_text_box`,
then run from the repository root:

```sh
set -eu
build/tests/test_text_box
uv run --project tools/emu python tools/emu/text_box.py --make-cases > cases.tsv
build/tests/test_text_box --dump /private/fonts < cases.tsv > port.txt
cd tools/emu
uv run python text_box.py --game-exe /private/game.exe --font-dir /private/fonts --cases ../../cases.tsv > ../../original.txt
cd ../..
diff -u original.txt port.txt
```

The 720 cases cover four actual fonts, five widths (including the prompt's
exact boundary), four height/margin combinations (including negative
baselines), and nine strings: empty text, ordinary sentences, long words,
leading and repeated spaces, vertical tabs and form feeds.
The oracle executes the original glyph lookup, widths, string handling,
row-adder, splitter, height checks, centering and baseline placement.
It isolates allocation, English language selection, ASCII whitespace
classification, palette lookup and final glyph drawing. Original glyph
rasterization and non-Latin text are not claimed by this differential.
The C++ regression also checks baseline pixels with a synthetic sprite,
so it needs no proprietary data for that part.
