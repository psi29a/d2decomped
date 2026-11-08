# Excel/Excel.cpp: txt name links and the txt splitter

Traced from game.exe 1.14d (source file `.\Src\Excel\Excel.cpp`, 8 functions,
`game-srcfiles.tsv`) and run in the emulator: `tools/emu/excel_links.py`
(923 cases, against a model). d2d reads the `.txt` tables itself
(`components/txt/txt.hpp`); this module is game.exe's loader half, so nothing
here changes d2d's output. The matches and differences are below.

## The splitter (`FUN_006bd640`, stdcall data, size)

Callers `LoadTxt` and `CompileTxt` (the table compilers) hand it the whole
file. It makes a 16-byte struct (alloc line 0x4d): +0 the data, +4 the first
data row, +8 the row count, +0xc the column count, and splits the buffer in
place: every TAB and every CR becomes a NUL. The end is `data + size` (the
decompiler's `size - 8` is wrong).

- **Header:** columns = TABs + 1, counted to the first CRLF. A CR without LF
  halts (line 0x5c); more than 0x118 columns halts (0x67); a header with no
  row after it, or no CRLF at all, frees the buffer (client free, line 0x9a)
  and the struct (0x9b) and returns 0 (`FUN_006bcda0` is that pair).
- **Rows:** counted per CRLF; a CR without LF halts (0x75). **A last row with
  no CRLF is not counted.**
- **"Expansion" rows** (first cell exactly `Expansion`, case-sensitive, compared
  with `SStrCmp(row, "Expansion", 10)`) are cut out of the buffer (the rest
  moves down) and the count goes down by one. d2d's `txt::Table` agrees (exact
  match). If such a row is the last one and has no CRLF it was never counted,
  so the count goes below the real rows (0 rows becomes 0xffffffff).
- d2d drops blank-first-cell rows too (`txt.hpp:44`); game.exe's splitter
  does not, so the compiled tables keep them. They are not skipped here.

## The name link (`FUN_006bd500`, `FUN_006bd5a0`, `FUN_006bd490`, `FUN_006bd3c0`)

How a cell's name becomes a row number (`Skills.txt` skill names, item codes
in the code lists, and so on). A link (16 bytes): +0 record count, +4 array
capacity, +8 array, +0xc tree root. Tree node (0x2c): +0 name (0x20), +0x20
row, +0x24 left, +0x28 right.

- Add (`FUN_006bd500` stdcall link, text; `FUN_006bd5a0` fastcall ECX link,
  EDX text, which looks up first): the text is cut to 31 characters
  (`SStrCopy` n = 0x20) and **lower-cased** (`0x4113c0`, which Ghidra names
  `SMemStringToUppercase`; it lowercases), the row is the record count, which
  then goes up by one.
- `FUN_006bd490` (EAX = slot, EDX = node; not fastcall) walks the tree by an
  unsigned byte compare and hangs the node. **A name already in the tree
  frees the new node (line 0x4a7), but `FUN_006bd500` still returns the new
  row and counts it**; `FUN_006bd5a0` never gets there, as it looks up first.
- Lookup `FUN_006bd3c0` (stdcall link, name, unused): the same cut and
  lower-casing, the row, or -1. A nonzero count with no tree halts (0x475).
- Halts: 0x4b2 / 0x4b6 (add: null link / link has an array), 0x4cc / 0x4d0
  (add-or-find the same).

## The code array (`FUN_006bd230`, stdcall link, code)

The other kind of link: a sorted array of {code, row} (8 bytes), for 4-letter
item codes. Capacity grows by 0x40 elements (realloc line 0x3df, the new
0x200 bytes cleared); the code is placed by binary search, **a code already
there is bumped up by one until it is free** (so "abcd" and a repeat get
neighbouring codes); the row is the record count. Halts 0x3d1 (null),
0x3d8 (the link has a tree).

## Frees

`FUN_006bd070` (fastcall node) frees left, right, then the node (line 0x346);
`FUN_006bd0b0` (stdcall link, null ok) frees the tree, the array (0x35d) and
the link (0x360).

Re-check: `cd tools/emu && uv sync && uv run python excel_links.py`.
