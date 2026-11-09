# The two table loaders: LoadTxt and CompileTxt

Traced from game.exe 1.14d and run in the emulator: `tools/emu/txt_loaders.py`
(46 cases; the real splitter `FUN_006bd640` runs inside, see `excel.md`). The
file readers, `StoreDataTables`, the column parser and the allocator are
hooked, so this checks the loaders' own steps, not the column parsing. d2d
reads the `.txt` itself (`components/txt/txt.hpp`), so nothing here changes
d2d.

Both do the same: read the file, split it, allocate `rows * record size`,
zero it, call the column parser `FUN_006bd780` (pool-less: bin struct, column
list, out, rows, record size), free the split struct, store the row count at
`*rows` when that pointer is not null.

## LoadTxt (`FUN_00504430`, `.\Src\D2Comp.cpp`, stdcall pool, name, columns, &rows, record size)

| Flag `0x72ef2c` | What it does |
|---|---|
| 0 (the .txt) | `leveldefs` (any case, `_stricmp`) is read as `levels`; `FUN_005043e0` reads `<name>.txt`; a miss halts at line 0x9e3; records allocated at line 0x9fa |
| 1 (the .bin) | `DATA\GLOBAL\EXCEL\<name>.bin` through `FUN_00517079` (line 0x958); **the name is not mapped, so `leveldefs.bin`**; the first dword is the row count and the records follow it; nothing is parsed or copied |

## CompileTxt (`FUN_006122f0`, `.\DATATBLS\DataTbls.cpp`, same arguments)

Two flags: `0x96c8b4` (compile the .txt) and `0x744308` (read the .bin).

1. With the compile flag: read `<name>.txt` through `FUN_00612280` (`leveldefs`
   is `levels`; lines 0x8b9 / 0x8c0; a miss halts at 0x8c2), parse it
   (alloc line 0x8c9), call `StoreDataTables` (`FUN_006121f0`) with
   `<name>.bin`, the records, their byte size and the row count, and free the
   records (0x8d6). **The cache is written under the unmapped name**
   (`leveldefs.bin`), and the table is split and parsed once for the cache.
2. Then load the table for use: with the bin flag, `<name>.bin` (line 0x8de,
   unmapped), count first; without it, the `.txt` again (0x8e2 mapped /
   0x8e4), **split and parsed a second time** (alloc line 0x904). A miss
   halts at 0x8e7.

## A table with a header and no data row

`FUN_006bd640` returns 0 (it frees what it made, `excel.md`) and both loaders
pass that to `FUN_006bcde0`, which reads the row count at +8: a read of address
8, an access violation in the original.
I did not check whether any shipped table is that short. d2d's `txt::Table` reads such a file as an empty table.

## Not traced

`FUN_006bd780`, the column parser (it consumes the column lists
`tools/emu/txt_fields.py` decodes), `FUN_00612280` / `FUN_005043e0` (the
file readers), and `StoreDataTables`.

Re-check: `cd tools/emu && uv run python txt_loaders.py`.

The per-table loaders, unloaders and getters built on these two are in
[datatbls.md](datatbls.md).
