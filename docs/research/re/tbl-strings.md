# TBL string tables — `.\StrTable\strtable.cpp`

Locale-driven string tables. `.tbl` files under `data\local\lng\<LANG>\`
map keys and integer IDs to decoded 16-bit strings, used for UI labels and
dialogs. On disk, keys are NUL-terminated ASCII and values are
**NUL-terminated UTF-8**, not Latin-1 or UTF-16LE. A node's value length at
`+0x0f` counts bytes and includes the NUL.

## Value decoding

`FUN_005259c0` calls `FUN_00526320(destination, source, capacity)` for every
value. Its assembly at `0x00526329` and `0x0052636e–0x00526372` establishes
the fastcall arguments: ECX is the 16-bit destination, EDX the byte source,
and the sole stack argument is the destination capacity.

`FUN_00526320` walks the source with `FUN_00526100`, which is a table-driven
UTF-8 decoder. At `0x00526115` it zero-extends the first byte; the decode
table at `0x007309b8` supplies sequence masks and minimum values. Continuation
bytes must have their high bits equal to `10` (`0x00526144–0x00526157`). A
successful sequence emits one 16-bit unit; malformed input terminates the
converted string at the error. This path is independent of language.

The font map affects the separate sizing helper `FUN_00527d50`, not the UTF-8
algorithm. Without its multibyte table, that helper is a byte `strlen`. With
the table, it calls `FUN_005276e0` and advances by one or two bytes while
counting output characters. `FUN_00527790` loads that table from the selected
font's `default.map`.

## Oracle result

`tools/emu/tbl.py` calls game.exe's `FUN_00526320` on each installed English
value; `tools/tbl-dump` prints d2d's decoded entries in the same hexadecimal
form. Across 8,949 entries in `string.tbl`, `patchstring.tbl`, and
`expansionstring.tbl` all match, including the twelve with UTF-8 `c3 bf`
(U+00FF, `ÿ`, a colour-code prefix) that byte zero-extension used to get
wrong (`00c3 00bf`): `d2d::tbl::utf8_units` is FUN_00526100's decoder.

Reproduce the comparison:

```sh
cmake --build build --target mpq-cat tbl-dump
(cd tools/emu && uv run python tbl.py ENG) > .cache/tbl-game.txt
: > .cache/tbl-d2d.txt
for name in string patchstring expansionstring; do
  printf '[%s]\n' "$name" >> .cache/tbl-d2d.txt
  build/tools/mpq-cat/mpq-cat "$D2_MPQ_DIR" "data\\local\\lng\\ENG\\${name}.tbl" \
    | build/tools/tbl-dump/tbl-dump >> .cache/tbl-d2d.txt
done
diff -u .cache/tbl-game.txt .cache/tbl-d2d.txt
```

The diff should be empty. `mpq-cat` needs `D2_PATCH_INSTALLER` for the
1.14d `patchstring.tbl`, or a third of its rows are missing.

## Files loaded

Assembled per-language by `_sprintf(buf, "data\\local\\lng\\%s\\<name>.tbl", lang)`:

- `string.tbl` — required; base game strings.
- `patchstring.tbl` — required; strings introduced by later patches
  (overrides `string.tbl` IDs on collision).
- `expansionstring.tbl` — LoD-only; loaded when the loader's
  `isExpansion` argument is non-zero.

Absence of `string.tbl` → silent no-op. Absence of `patchstring.tbl` →
fatal assert (line `0x1c3`). Absence of `expansionstring.tbl` under LoD →
fatal assert (line `0x1f6`).

The locale directory name (`<LANG>`) comes from the loader argument
`param_2`; the font-encoding tag it derives is separate.

## Font-encoding derivation

Independent of value decoding, the loader picks a **font encoding** from the
same language string and loads `data\local\font\<enc>\default.map`:

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

| addr       | ghidra name  | role |
|------------|--------------|------|
| `0x525920` | `FUN_00525920` | Opens a TBL through `FUN_00517079`, checks its CRC and node offsets. |
| `0x5259c0` | `FUN_005259c0` | Loads the selected language's tables, allocates offset and decoded-data arrays, and populates the globals below. |
| `0x525fb0` | `FUN_00525fb0` | Sibling helper: formats an integer with comma separators. |
| `0x526100` | `FUN_00526100` | Decodes one UTF-8 sequence to one 16-bit unit and returns its byte length. |
| `0x526320` | `FUN_00526320` | Converts a complete NUL-terminated UTF-8 value. |
| `0x527790` | `FUN_00527790` | Loads `default.map` and its mapping/multibyte tables. |
| `0x527d50` | `FUN_00527d50` | Counts value characters for allocation, using the optional font multibyte table. |

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

## d2d representation

`components/tbl/tbl.hpp` parses the hash nodes into key and ID lookup maps.
Its byte zero-extension matches the original only for ASCII; the remaining
work is the UTF-8 conversion described above, not the table layout.
