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

`default.map` (`FUN_00527790`, `.\Unicode\UNISYS.CPP`): `"KAMAP\n"`, a
version line ended by 0x1a within 40 bytes (the free at line 0x79 otherwise;
0x65 for another magic), four bytes, a u16 entry count, a u8 range count,
that many (first, last) byte ranges, the entries as u16 pairs and a u16 code
per entry. It keeps the ranges (`0x882b28`), the entries with only each
u16's low byte (`0x882b24`, `movzx` at `0x527953` / `0x52795a`), the codes
whole (`0x882b2c`), the two counts (`0x882b20`, `0x882b22`) and the version
up to its newline (`0x882af8`); each old table is freed just
before its replacement is allocated.
`FUN_005276e0` (byte in DL) is 1, a lead byte, for a byte from 0x20 outside
every range. `FUN_00527d50` (text ECX, limit EDX) is `strlen` when no code
table is loaded and otherwise counts characters up to the limit or the NUL,
a lead byte and the one after it as one.

English loads `data\local\font\latin\default.map` (version `ISO`, ranges
0x20..0x7e and 0xa0..0xff), so 0x7f..0x9f are lead bytes and a UTF-8
continuation byte in that range swallows the byte after it. The count only
sizes `FUN_005259c0`'s buffer of decoded units (count + 1 per value, two bytes
each): across the 9,388 English values it's never below what `FUN_00526320`
decodes, so the buffer is big enough. d2d grows its `std::u16string` as it
decodes (`d2d::tbl::utf8_units`) and needs no count. Verified by
`tools/emu/unicode_kamap.py` (the CRT `strtok` that cuts the version is
hooked).

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
| `0x525350` | `FUN_00525350` | Formats an unsigned 32-bit number with comma separators. |
| `0x525490` | `FUN_00525490` | Formats a signed 64-bit number with comma separators. |
| `0x5255c0` | `FUN_005255c0` | Formats an unsigned 64-bit number with a K/M/B/T short-form suffix. |
| `0x5259c0` | `FUN_005259c0` | Loads the selected language's tables, allocates offset and decoded-data arrays, and populates the globals below. |
| `0x525fb0` | `FUN_00525fb0` | Sibling helper: formats an integer with comma separators. |
| `0x526100` | `FUN_00526100` | Decodes one UTF-8 sequence to one 16-bit unit and returns its byte length. |
| `0x526320` | `FUN_00526320` | Converts a complete NUL-terminated UTF-8 value. |
| `0x5276e0` | `FUN_005276e0` | 1 for a lead byte: from 0x20 and outside every `default.map` range. |
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
Values go through `d2d::tbl::utf8_units`, game.exe's UTF-8 decoder
(`FUN_00526100`) described above.

## String table leaves (traced)

Checked by `tools/emu/strtable_leaves.py` (from `tools/emu`: `uv run python strtable_leaves.py`,
prints `ok: 6560 string table leaf cases`; `--mutation-check` must catch six model
defects). Callees the oracle hooks or only reads are written `0x52_xxxx` here;
the hooked ones are `0x52_4930` (string by id), `0x52_4d30` (patch lookup),
`0x52_5150` (language id), `0x52_7720` (unload tail), and the allocator, free and
fatal `0x40_b380`, `0x40_b3c0`, `0x40_8a60`. Argument notes are game.exe's
stack order; the oracle runs each function for real.

Call order for the four key helpers: `0x524c60` (TBL hash lookup) calls the
hash `0x52_49e0`, `0x524d30` calls `0x524c60`, and `0x524ac0` calls the count
`0x52_7d50` and the decoder `0x526320`.

- `0x524fd0`: cdecl, `(u16* crc)`. Stores `0xffff`. No return value.
- `0x524fe0`: cdecl, `(u16* crc, byte c)`. One CRC-16 step: the high byte of `crc` XOR `c` indexes the 256-word table at `0x7301d8`; the new word is `((crc & 0xff) << 8) ^ table[index]`.
- `0x525020`: stdcall 4, `(u16* out, byte* data, start, end)`. Stores the CRC-16 of `data[start..end)` (init `0xffff`, the step above). Halts `0x3ea` when `start >= end`.
- `0x524e20`: fastcall, ECX = key. Calls `0x52_4d30(key, &local)` and returns the pointer that call wrote to `local`.
- `0x525090`: fastcall, ECX = buffer. If the buffer equals a row's key (+0), it is overwritten with that row's code (+4); otherwise with row 0's code, `ENG`. Writes in place. Thirteen rows, `0x7303d8`, 24 bytes each.
- `0x525100`: fastcall, ECX = buffer. Returns the matching row's id (+0x14), or 0 when no key matches.
- `0x525260`: stdcall 2, `(u32* out, mode)`. Calls `0x52_5150` for an id. The row is the one with that id; an id of 14 or more uses row 0; an id below 14 that no row carries uses row 13, which is the `K/M/B/T` word area, not a row (not exercised). Mode 1 copies the row's +8 string, mode 2 its +0xc string, mode 3 writes the dword at +0x10 to `*out`, and any other mode copies the +4 string. Halts `0x490` when row 0's id is not zero (not exercised).
- `0x5251c0`: cdecl, no arguments. Opens `HKCU\Control Panel\International`, queries `sLanguage` into `0x8829f8` and returns `0x52_5100` of that text when it is 1 to 3 characters; else 0. Only the success path calls `RegCloseKey`. The buffer-size argument is the stack slot `[ebp-8]`, which the function does not write before the query.
- `0x524c60`: stdcall 3, `(u32* arr, key, u32* out)`, with EDI = the TBL. Linear probing from `hash(key) % size` for at most `count` probes; a valid entry (type byte 1) with the key returns its u16 id (+1) and writes `arr[h]` to `*out`; no match returns `0xffff`. Halts `0x296` when the found `arr[h]` is 0.
- `0x524ac0`: fastcall, ECX = key. Returns a u16 pointer to a cached decode of the key followed by the 23 bytes at `0x730520` (` -not xlated call ken w`). The decode writes one unit fewer than the counted length, so the final `w` is dropped. The cache is a list at `0x8829b0` with new nodes at the head; a hit returns the same pointer and allocates nothing.
- `0x524e40`: cdecl, no arguments. Does nothing when no string table is loaded. Otherwise it counts `0x8829d8` down, and only at zero frees the string, patch and expansion tables and the cache nodes (decoded text, key copy, node, in that order), clears their globals (when the offsets pointer at `0x8829b8` is null, the string data pointer at `0x8829c4` is neither freed nor cleared), and tail-jumps to `0x52_7720`.
- `0x525770`: cdecl, no arguments. Halts `0x246` when no string table is loaded. Fills the four words `0x730510`, `0x730514`, `0x730518`, `0x73051c` with `K`, `M`, `B`, `T` from string ids `0x14d0` to `0x14d3`: the first character of the first four characters of the string when that is one character or none (`0` for none), else the default letter.

## Number formatting

The three number formatters were run in stock `Game.exe` 1.14d (SHA-256
`631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`).
`tools/emu/strtable_formatters.py` runs the formatter boundary cases.

- `0x525350`: stdcall 3, `(u16* out, u32 value, capacity)`. It calls the
  unsigned decimal converter, so `0xffffffff` formats as `4,294,967,295`; it
  inserts a comma after each group of three digits from the right. `capacity`
  counts UTF-16 units; the routine reserves one slot beyond the output and its
  terminator. A capacity at or below 2 halts at `0x4ed`; an output that does
  not fit takes the decoded fallback at `0x6d5830`.
- `0x525490`: stdcall 4, `(u16* out, low, high, capacity)`, with a signed
  64-bit value formed from `high:low`. It inserts commas the same way and
  prepends `-` for negative values. The INT64_MIN path negates the value
  through the signed decimal converter after writing its own minus sign;
  stock execution therefore returns `--9,223,372,036,854,775,808` for that
  one input. It reserves one slot beyond the output and its terminator, halts
  at `0x519` for capacity at or below 2, and uses the decoded fallback at
  `0x6d5830` when the formatted result does not fit.
- `0x5255c0`: stdcall 4, `(u16* out, low, high, capacity)`, with an unsigned
  64-bit value. Up to six digits stay unscaled. At seven or more digits it
  keeps the leading one-to-four digit groups, inserts commas, and uses the
  suffix word at `0x730510`, `0x730514`, `0x730518` or `0x73051c` (K, M, B,
  T); the scale is capped at T. For example, one million is `1,000K` and
  1,234,567,890 is `1,234M`. The capacity includes the suffix and terminator,
  with one extra slot reserved. It halts at `0x54b` for capacity at or below
  2 and uses the decoded fallback at `0x6d5830` when the result does not fit.
  The switch table at `0x525754` maps suffix groups 1–4 to the four words;
  group 0 writes no suffix.
