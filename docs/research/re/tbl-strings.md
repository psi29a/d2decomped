# TBL string tables — `.\StrTable\strtable.cpp`

Locale-driven string tables. `.tbl` files under `data\local\lng\<LANG>\`
map integer IDs to UTF-16 strings, used for every UI label and dialog.
The file format is well-documented (Phrozen Keep wiki — search "TBL /
Strings"); this doc covers the D2 1.14d loader specifically.

## Files loaded

Assembled per-language by `_sprintf(buf, "data\\local\\lng\\%s\\<name>.tbl", lang)`:

- `string.tbl` — required; base game strings.
- `patchstring.tbl` — required; strings introduced by later patches
  (overrides `string.tbl` IDs on collision).
- `expansionstring.tbl` — LoD-only; loaded when the loader's
  `isExpansion` argument is non-zero.

Absence of `string.tbl` → silent no-op. Absence of `patchstring.tbl` →
fatal assert (`.\BnDownload.cpp` line 0x1c3). Absence of
`expansionstring.tbl` under LoD → fatal assert (line 0x1f6).

The locale directory name (`<LANG>`) comes from the loader argument
`param_2`; the font-encoding tag it derives is separate.

## Font-encoding derivation

Independent of TBL, the loader picks a **font encoding** from the same
language string and writes it to `data\local\font\<enc>\default.map`:

| language | encoding |
|----------|----------|
| `JPN`    | `JPN`    |
| `KOR`    | `KOR`    |
| `POL`    | `LATIN2` |
| `RUS`    | `CYR`    |
| *else*   | `LATIN`  |

Latin, LATIN2, and CYR share the same base font set with a code-page swap.
JPN/KOR use a different glyph pipeline entirely (multi-byte).

## Function map

Range `0x00525900`–`0x00526000` (~1.5 KB).

| addr       | ghidra name  | signature                                              | notes                                                                                |
|------------|--------------|--------------------------------------------------------|--------------------------------------------------------------------------------------|
| `0x525920` | `FUN_00525920` | `TBL* OpenTbl()`                                     | Opens file (`FUN_00517079`), verifies magic (`header[0]`) and integrity checksum (unaligned dwords at +0x09 and +0x11). Fatal on mismatch. |
| `0x5259c0` | `FUN_005259c0` | `void __fastcall LoadStrings(void*, const char* lang, int isExpansion)` | The subsystem entry point. Loads all 3 TBLs, allocates offset table + decoded buffer per file, populates the six globals below. |
| `0x525fb0` | `FUN_00525fb0` | `void FormatIntWithCommas(u16* dst, int value, int dstCapacity)` | Helper: `-1234567 → "-1,234,567"` as UTF-16. Sibling function that lives in the same file. |

Support functions used but out of range:

- `FUN_00517079(0, ".\\StrTable\\strtable.cpp", 0xc0)` — MPQ-aware file
  open. Same signature as the Gateway loader's `FUN_0041b650`
  (`(name, out_buf, out_size, 0, 0)`) — likely the same wrapper.
- `FUN_00527d50` — read one packed string from the TBL body (used both
  for length-sizing pass and copy pass).
- `FUN_00527790` — likely `LoadFontMap(default.map)`.
- `FUN_0040b380(request_id, 0)` — Storm-tracked allocator, seen with
  ids `0x179`, `0x182`, `0x18b`, `0x1a9`, `0x1c5`, `0x1dc`, `0x1f8`, `0x20f`.

## Loader globals (six per TBL, 3 TBLs)

Written by `LoadStrings`, read by `GetString(id)`-style helpers elsewhere:

| global         | string.tbl | patchstring.tbl | expansionstring.tbl |
|----------------|:----------:|:---------------:|:-------------------:|
| loaded pointer | `0x8829b4` | `0x8829d0`      | `0x8829d4`          |
| offset table   | `0x8829b8` | `0x8829bc`      | `0x8829c0`          |
| decoded data   | `0x8829c4` | `0x8829c8`      | `0x8829cc`          |

`0x8829d8` is a load-counter (guard against re-entry / double-load).

## Callers

Three sites call the loader:

- `0x004359f2`
- `0x00441b91`
- `0x0044b911`

Three because it's called once for base strings (isExpansion=0), once for
expansion strings (isExpansion=1), and once as a re-load path when
switching language mid-session. Which is which is TBD — decompile the
containing functions when we need to trace the boot sequence.

## Would we port this?

Yes, once we hit rendering. UI can't draw a menu without `GetString`.
The TBL file format is small (~30 lines of parser). Port when we start
the main-menu work in phase 5.
