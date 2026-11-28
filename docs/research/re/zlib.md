# zlib 1.2.8 in game.exe 1.14d

game.exe links zlib 1.2.8 statically, from `FUN_00427160` to `FUN_0042e120`
(71 functions). Ghidra's signatures name none of it. The version is in the
image: `" deflate 1.2.8 Copyright 1995-2013 Jean-loup Gailly and Mark Adler "`
at `0x6d0238`, `"1.2.8"` at `0x6d3870` and
`" inflate 1.2.8 Copyright 1995-2013 Mark Adler "` at `0x6d3878`. Nothing in
it is specific to the game, and d2d ports none of it: its MPQ reader
(StormLib) and its screenshots link the system zlib
(`components/mpq/CMakeLists.txt`, `components/screenshot/CMakeLists.txt`).

Read from capstone listings of the image (the Ghidra decompile for the big
ones); no oracle ran, so all of it is **cited**. game.exe carries no symbols
for zlib, so each name is a reading: the function has the shape of that
zlib 1.2.8 routine, by its error strings, its constants, the state fields it
touches and its callers and callees, as the tables list them. Each table is
in address order.

## How it is built

- **Registers, not the stack.** The compiler passed arguments in registers
  across the whole library (link-time code generation): the stream comes in
  ECX and the second argument in EDX almost everywhere (`deflate`,
  `inflate`, `deflateInit_`, `inflateInit_`), the rest on the stack. The
  `ret n` counts are therefore smaller than zlib.h's argument counts, and a
  caller outside zlib must load the registers the same way.
- **Inlined helpers.** `read_buf`, `inflateResetKeep`, `fixedtables`,
  `gf2_matrix_times` and the `send_bits` macro have no function of their own.
  `deflateSetHeader`, `inflateGetHeader`, `inflateSyncPoint`,
  `inflateUndermine`, `inflateMark`, `zError`, `get_crc_table`, the
  `inflateBack` family and the gz* file functions are not in the image.
- **Sizes.** `z_stream` is 0x38 bytes (`deflateInit2_` and `inflateInit2_`
  check `stream_size == 0x38` and only the first character of the version,
  `'1'`), the deflate state 0x16c4 and the inflate state 0x1bcc.
- **The compression levels.** `configuration_table` at `0x6d0280`, 10
  entries of 12 bytes (good, lazy, nice, chain as words, then the function),
  is zlib 1.2.8's table exactly: level 0 `deflate_stored`, 1-3
  `deflate_fast`, 4-9 `deflate_slow`. `lm_init` reads the words and
  `deflate` and `deflateParams` call through the pointers; those pointers are
  the three functions' only references.
- **The users.** Outside zlib, only `inflateInit_`, `inflate`, `inflateEnd`,
  `deflateInit_`, `deflate` and `deflateEnd` are called: by Storm's
  SCOMP.CPP compressor and unpacker (the unpacker core is `FUN_0041e250`,
  storm-file.md "SCOMP.CPP") and by the Warden module loader's inflate
  `FUN_006c79c0` (unnamed-frontier.md). The call sites are in
  `game-calls.tsv`.

## deflate.c

| Address | zlib 1.2.8 | What shows it | Callers |
|---|---|---|---|
| `FUN_00427160` | `deflateResetKeep` | status 42 or 113 (`0x71`) by `wrap`; `adler` from `crc32` or `adler32` of nothing; then `_tr_init` | `deflateReset` |
| `FUN_00427220` | `deflatePending` | `pending` (+0x14) and `bi_valid` (+0x16bc) to the two out pointers; -2 without a state | none |
| `FUN_00427260` | `deflatePrime` | -5 when `pending_buf` would overrun; ORs at most 16 bits a time into `bi_buf` (+0x16b8), then `_tr_flush_bits` | none |
| `FUN_004272f0` | `deflateTune` | stores `good_match` +0x8c, `max_lazy_match` +0x80, `nice_match` +0x90, `max_chain_length` +0x7c | none |
| `FUN_00427330` | `deflateBound` | `sourceLen + ((sourceLen + 7) >> 3) + ((sourceLen + 63) >> 6) + 5`, or with `w_bits` and `hash_bits` both 15 the tight `sourceLen + (sourceLen >> 12) + (sourceLen >> 14) + (sourceLen >> 25) + 7`; plus the wrapper: 0 raw, 6 zlib (10 with a dictionary started), 18 and up gzip (extra, name, comment, header crc) | none |
| `FUN_00427420` | `putShortMSB` | two bytes into `pending_buf`, high byte first | `deflate` |
| `FUN_00427450` | `flush_pending` | `_tr_flush_bits`, then `memcpy` of the pending bytes to `next_out` | `deflate`, the five `deflate_*` |
| `FUN_004274a0` | `deflateEnd` | status must be 42, 69, 73, 91, 103, 113 or 666, else -2; frees `pending_buf`, `head`, `prev`, `window` and the state through `zfree`; -3 when the status was 113 (busy) | `deflateCopy`, `deflateInit2_`, SCOMP.CPP |
| `FUN_00427560` | `deflateCopy` | copies the stream (0x38) and the state (0x16c4), allocates and copies `window`, `prev`, `head`, `pending_buf`; `deflateEnd` when one fails | none |
| `FUN_00427770` | `lm_init` | clears `head` (`memset`) and loads the four words of `configuration_table[level]` | `deflateReset` |
| `FUN_00427800` | `longest_match` | `MAX_MATCH` 258 (0x102) and `MIN_LOOKAHEAD` 262 (0x106) | `deflate_fast`, `deflate_slow` |
| `FUN_00427980` | `fill_window` | `read_buf` inlined (`memcpy`, then `adler32` or `crc32` by `wrap`), slides the window, zero-fills past the data (`memset`) | the five `deflate_*`, `deflateSetDictionary` |
| `FUN_00427bb0` | `deflate_stored` | blocks of at most 0xffff; `configuration_table` entry 0 | the table (`0x6d0288`) |
| `FUN_00427dd0` | `deflate_fast` | `longest_match`, `_tr_flush_block` | the table, entries 1-3 |
| `FUN_00428150` | `deflate_slow` | `longest_match`, the lazy match | the table, entries 4-9 |
| `FUN_004285f0` | `deflate_rle` | runs at distance 1 up to 258 | `deflate` (strategy 3, `Z_RLE`) |
| `FUN_00428900` | `deflate_huff` | literals only | `deflate` (strategy 2, `Z_HUFFMAN_ONLY`) |
| `FUN_00428ab0` | `deflateSetDictionary` | -2 for gzip (`wrap` 2), for zlib after status 42, or once `lookahead` is set; `adler32` of the dictionary for zlib; feeds it through `fill_window` and the hash chains | none |
| `FUN_00428c40` | `deflateReset` | `deflateResetKeep`, then `lm_init` when it returned 0 | `deflateInit2_` |
| `FUN_00428c60` | `deflate` | the gzip header (`0x1f 0x8b 8`) and the zlib header (`putShortMSB`), finish state 666 (0x29a), the `deflate_rle` / `deflate_huff` / table dispatch, `_tr_align`, `_tr_stored_block` | `deflateParams`, SCOMP.CPP |
| `FUN_004294c0` | `deflateInit2_` | the version and 0x38 checks, `zcalloc` / `zcfree` when `zalloc` / `zfree` are 0, `windowBits` 8 raised to 9, the 0x16c4 state, `lit_bufsize = 1 << (memLevel + 6)`; `deflateReset`, or `deflateEnd` when an allocation fails | `deflateInit_` |
| `FUN_00429700` | `deflateParams` | level -1 becomes 6, then 0-9 and strategy 0-4 checks; `deflate(Z_BLOCK)` when the strategy or the level's table function changes and `total_in` is nonzero; then the level's four table words | none |
| `FUN_004297f0` | `deflateInit_` | `deflateInit2_` with method 8, `windowBits` 15, `memLevel` 8, strategy 0 | SCOMP.CPP |

## inflate.c, inffast.c and inftrees.c

| Address | zlib 1.2.8 | What shows it | Callers |
|---|---|---|---|
| `FUN_00429810` | `inflateReset` (`inflateResetKeep` inlined) | `wsize`, `whave`, `wnext` 0, `dmax` 0x8000, `sane` 1 (+0x1bc0), `back` -1 (+0x1bc4) | `inflateReset2`, `inflateSync` |
| `FUN_004298b0` | `inflateReset2` | a negative `windowBits` for raw inflate, below 48 masked to 4 bits, then 0 or 8-15; frees the window when its size changes | `inflateInit2_` |
| `FUN_00429950` | `inflateInit2_` | the version and 0x38 checks, `zcalloc` / `zcfree` defaults, the 0x1bcc state | `inflateInit_` |
| `FUN_00429a10` | `inflateInit_` | `inflateInit2_` with `windowBits` 15 | SCOMP.CPP, `FUN_006c79c0` |
| `FUN_00429a30` | `inflatePrime` | `bits` < 0 clears `hold` and `bits`; more than 16, or a total over 32, gives -2 | none |
| `FUN_00429ac0` | `updatewindow` | allocates the window on first use, then copies the latest output into it | `inflate`, `inflateSetDictionary` |
| `FUN_00429bb0` | `inflate` | the mode machine and its messages: "incorrect header check", "unknown compression method", "invalid window size", "unknown header flags set", "header crc mismatch", "invalid block type", "invalid stored block lengths", "invalid code lengths set", "invalid bit length repeat", "invalid literal/lengths set", "invalid distances set", "incorrect data check", "incorrect length check" and the rest; `inflate_fast`, `inflate_table` | SCOMP.CPP, `FUN_006c79c0` |
| `FUN_0042b170` | `inflateEnd` | frees the window (+0x34) when set, then the state | SCOMP.CPP, `FUN_006c79c0` |
| `FUN_0042b1b0` | `inflateGetDictionary` | two `memcpy` of the window from `wnext`, length `whave` (+0x2c) | none |
| `FUN_0042b220` | `inflateSetDictionary` | mode 10 (`DICT`) check, `adler32` of the dictionary against `check`, `updatewindow`; mode 30 (`MEM`) when that fails | none |
| `FUN_0042b300` | `syncsearch` | counts the `00 00 FF FF` pattern | `inflateSync` |
| `FUN_0042b360` | `inflateSync` | mode 31 (`SYNC`), `syncsearch` over the bit buffer then the input, `inflateReset`, mode 11 (`TYPE`) | none |
| `FUN_0042b490` | `inflateCopy` | allocates the state (0x1bcc) and, when there is one, the window (`1 << wbits`), freeing the state if that fails (-4); copies the stream and the state, then moves `lencode` / `distcode` into the copy when they point into the old state's `codes` | none |
| `FUN_0042dca0` | `inflate_fast` | "invalid distance too far back", "invalid distance code", "invalid literal/length code" | `inflate` |
| `FUN_0042e120` | `inflate_table` | `ENOUGH_LENS` 852 (0x354), `ENOUGH_DISTS` 592 (0x250), the base and extra tables from `0x6d38a8` | `inflate` |

## trees.c

| Address | zlib 1.2.8 | What shows it | Callers |
|---|---|---|---|
| `FUN_0042b9e0` | `init_block` | zeroes the literal, distance and bit-length counts; `opt_len`, `static_len`, `last_lit`, `matches` 0 | `_tr_init`, `_tr_flush_block` |
| `FUN_0042ba50` | `pqdownheap` | the heap sift by frequency, then depth | `build_tree` |
| `FUN_0042bb30` | `gen_bitlen` | `HEAP_SIZE` 573 (0x23d), `MAX_BITS` 15 | `build_tree` |
| `FUN_0042bd30` | `scan_tree` | the run limits 138 (0x8a) / 3, 6 / 3, 7 / 4 | `build_bl_tree` |
| `FUN_0042be10` | `send_tree` | the same limits; sends the repeat codes 16, 17, 18 | `send_all_trees` |
| `FUN_0042c390` | `send_all_trees` | `lcodes - 257` and `dcodes - 1` in 5 bits, `blcodes - 4` in 4; `send_tree` | `_tr_flush_block` |
| `FUN_0042c600` | `_tr_tally` | distance 0 counts a literal, else `matches`++ and the `_length_code` / `_dist_code` tables (`0x6d15d8`, `0x6d13d8`); returns `last_lit == lit_bufsize - 1`. The compressors use the inline macros instead | none |
| `FUN_0042c6a0` | `compress_block` | the two trees as stack arguments (`ret 8`), `END_BLOCK` 256 | `_tr_flush_block` |
| `FUN_0042cab0` | `detect_data_type` | the black-list mask `0xf3ffc07f`, the white-listed bytes 9, 10, 13 | `_tr_flush_block` |
| `FUN_0042cb40` | `bi_flush` | `bi_valid` 16: a short out; 8 or more: a byte out | `_tr_flush_bits`, `_tr_align` |
| `FUN_0042cbc0` | `bi_windup` | `bi_valid` over 8: a short out; over 0: a byte | `copy_block`, `_tr_flush_block` |
| `FUN_0042cc40` | `copy_block` | `bi_windup`, the length and its complement, then the bytes | `_tr_stored_block` |
| `FUN_0042ccd0` | `_tr_init` | the three tree descriptors with `static_l_desc`, `static_d_desc`, `static_bl_desc` (`0x7088f4`, `0x708908`, `0x70891c`); `bi_buf`, `bi_valid` 0; a tail jump to `init_block` | `deflateResetKeep` |
| `FUN_0042cd30` | `gen_codes` | the stack `next_code[16]` (hence the security cookie) | `build_tree` |
| `FUN_0042cdd0` | `build_tree` | `pqdownheap`, `gen_bitlen`, `gen_codes` | `build_bl_tree`, `_tr_flush_block` |
| `FUN_0042cfd0` | `build_bl_tree` | `scan_tree` twice, `build_tree`, then counts down `bl_order` (`0x6d0e00`) from 18 to 3 and adds `3 * (max_blindex + 1) + 14` to `opt_len` | `_tr_flush_block` |
| `FUN_0042d0a0` | `_tr_stored_block` | `(STORED_BLOCK << 1) + last` in 3 bits, `copy_block` | `deflate`, `_tr_flush_block` |
| `FUN_0042d150` | `_tr_flush_bits` | a tail jump to `bi_flush` | `deflatePrime`, `flush_pending`, the five `deflate_*` |
| `FUN_0042d160` | `_tr_align` | `STATIC_TREES << 1` in 3 bits, `END_BLOCK`, `bi_flush` | `deflate` |
| `FUN_0042d250` | `_tr_flush_block` | `detect_data_type`, `build_tree`, `build_bl_tree`, the stored / static (`static_ltree` `0x6d0ee0`, `static_dtree` `0x6d1360`) / dynamic choice, `init_block`, `bi_windup` on the last block | the five `deflate_*` |

## adler32.c, crc32.c and zutil.c

| Address | zlib 1.2.8 | What shows it | Callers |
|---|---|---|---|
| `FUN_0042b660` | `adler32` | `BASE` 65521 (0xfff1), `NMAX` 5552 (0x15b0), the modulo by multiply (0x80078071) | `deflateResetKeep`, `deflateCopy`, `fill_window`, `deflateSetDictionary`, `deflate`, `inflate`, `inflateSetDictionary` |
| `FUN_0042b8e0` | `adler32_combine_` | `len2 % BASE` through `__allrem` | the two below |
| `FUN_0042b990` | `adler32_combine` | `adler32_combine_` with `len2` sign-extended | none |
| `FUN_0042b9b0` | `adler32_combine64` | `adler32_combine_` with a 64-bit `len2` | none |
| `FUN_0042d460` | `crc32_little` | `crc_table[0]` at `0x6d17c8`, the four-table loop (`0x6d1bc8` …) | `crc32` |
| `FUN_0042d700` | `crc32_big` | byte-swaps the crc first (`ZSWAP32` as shifts and masks) | none |
| `FUN_0042da10` | `gf2_matrix_square` | `gf2_matrix_times` inlined, 32 rows | `crc32_combine_` |
| `FUN_0042da50` | `crc32_combine_` | `gf2_matrix_square`, `len2 >>= 1` through `__allshr` | the two below |
| `FUN_0042dbc0` | `crc32_combine` | `crc32_combine_` with `len2` sign-extended | none |
| `FUN_0042dbe0` | `crc32_combine64` | `crc32_combine_` with a 64-bit `len2` | none |
| `FUN_0042dc00` | `crc32` | a null buffer returns 0, else `crc32_little` (the endian test folded away, which leaves `crc32_big` uncalled) | `deflateResetKeep`, `deflateCopy`, `fill_window`, `deflate`, `inflate` |
| `FUN_0042dc70` | `zcalloc` | `malloc(items * size)` | `deflateInit2_`, `inflateInit2_` (stored as the default `zalloc`) |
| `FUN_0042dc90` | `zcfree` | `free(ptr)` | the same, as the default `zfree` |

## No caller

"none" in the Callers column means no call, jump or pointer to the function
anywhere in the image (every rel32 `E8`/`E9` and every aligned or unaligned
dword in all sections was scanned): `deflatePending`, `deflatePrime`,
`deflateTune`, `deflateBound`, `deflateCopy`, `deflateSetDictionary`,
`deflateParams`, `inflatePrime`, `inflateGetDictionary`,
`inflateSetDictionary`, `inflateSync`, `inflateCopy`, `_tr_tally`,
`adler32_combine`, `adler32_combine64`, `crc32_big`, `crc32_combine` and
`crc32_combine64`. `syncsearch`, `adler32_combine_`, `crc32_combine_` and
`gf2_matrix_square` are reached only from those. They are the unused parts
of the library, linked in whole; like the CRT's (crt.md), they get no
stubs.md rows.
