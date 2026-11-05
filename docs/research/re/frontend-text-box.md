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
build/tests/test_text_box
python3 tools/emu/text_box.py --make-cases > cases.tsv
build/tests/test_text_box --dump /private/fonts < cases.tsv > port.txt
cd tools/emu
uv run python text_box.py --game-exe /private/game.exe --font-dir /private/fonts --cases ../../cases.tsv > ../../original.txt
cd ../..
diff -u original.txt port.txt
```

The 480 cases cover four actual fonts, five widths (including the prompt's
exact boundary), three height/margin combinations, and eight strings:
empty text, ordinary sentences, long words, leading and repeated spaces.
The oracle executes the original glyph lookup, widths, string handling,
row-adder, splitter, height checks, centering and baseline placement.
It isolates allocation, English language selection, ASCII whitespace
classification, palette lookup and final glyph drawing. Original glyph
rasterization and non-Latin text are not claimed by this differential.
The C++ regression also checks baseline pixels with a synthetic sprite,
so it needs no proprietary data for that part.
