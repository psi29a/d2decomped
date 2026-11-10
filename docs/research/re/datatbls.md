# DATATBLS: the table loaders and getters — game.exe 1.14d

The loaders, unloaders and small getters in `.\DATATBLS\*.cpp`
(`game-srcfiles.tsv`) that no other page covers. Every loader builds a
column list on the stack and calls CompileTxt (`FUN_006122f0`, see
[table-loaders.md](table-loaders.md)) with the table's name and record
size; the table's slot is in the data-tables block at `0x96bc30`
(`PTR_DAT_00744304`), given here as `+offset`. "Frees" is
`FUN_006125c0` (DataTbls.cpp line 0x924 with the .bin flag `0x744308`,
else 0x926) plus `FUN_006bd0b0` for a name link ([excel.md](excel.md)).
Strings named in a table go through `FUN_00524d30` (a .tbl key to its
string index: patch + 10000, expansion + 20000, else base;
[tbl-strings.md](tbl-strings.md)). d2d reads the `.txt` files itself
(`components/txt/`), so none of this changes d2d. Re-check with
`tools/ghidra/fn.py <address>`. "No caller" rows were byte-scanned and
are in [stubs.md](stubs.md).

## The table slots

From the CompileTxt calls (`grep "= FUN_006122f0(" game_all.c`):

| Slot | Table | Slot | Table |
|---|---|---|---|
| `+0x0` | playerclass | `+0xb98` | skills (rows) |
| `+0x8` | bodylocs | `+0xbbc` | overlay |
| `+0x10` | storepage | `+0xbc4` | charstats |
| `+0x18` | elemtypes | `+0xbcc` | itemstatcost |
| `+0x20` | hitclass | `+0xbe0` | monequip |
| `+0x28` | monmode (codes) | `+0xbe8` | pettype |
| `+0x30` | plrmode (codes) | `+0xbf8` | itemtypes |
| `+0x38` | skillcalc | `+0xc0c` | sets |
| `+0x58` | misscalc | `+0xc18` | setitems |
| `+0x6c` | skills (codes) | `+0xc24` | uniqueitems |
| `+0x74` | events | `+0xc30` | monprop |
| `+0x7c` | compcode | `+0xc3c` | montype |
| `+0x88` | monai | `+0xc50` | monumod |
| `+0xa4` | properties | `+0xc58` | levels |
| `+0xb4` | hiredesc | `+0xc60` | leveldefs |
| `+0xbc` | states | `+0xc64` | lvlprest |
| `+0x194` | sounds | `+0xc78` | experience |
| `+0x1a0` | hireling | `+0xc7c` | difficultylevels |
| `+0x9a8` | npc | `+0xdc0` | lvltypes |
| `+0x9b0` | colors | `+0xdd0` | lvlwarp |
| `+0xa78` | monstats | `+0xdd8` | lvlmaze |
| `+0xa84` | monsounds | `+0xe64` | gems |
| `+0xa90` | monstats2 | `+0xe6c` | runes |
| `+0xa9c` | monplace | `+0x101c` | itemratio |
| `+0xad4` | superuniques | `+0x1024` | qualityitems |
| `+0xb64` | missiles | `+0x102c` | lowqualityitems |
| `+0xb70` | monlvl | `+0x1034` | books |
| `+0xb78` | monseq | `+0x1838` / `+0x1840` / `+0x1848` | shrines, objects, objgroup |
| `+0xb8c` | skilldesc | `+0x1854` | monitempercent |
| `+0x1858..+0x1864` | uniquetitle, uniqueprefix, uniquesuffix, uniqueappellation | `+0x188c` | monmode (rows) |
| `+0x18b8` / `+0x18bc` | armtype, composit | `+0x18c4` / `+0x18c8` | inventory, belts |
| `+0x19d0` / `+0x19d8` | chartemplate, arena | `+0x19f8` | cubemain |

## DataTbls.cpp

| Address | What |
|---|---|
| `FUN_00611d20` | Returns `0x96c8a4` (callers `FUN_0066a8f0`, `FUN_0066a9b0`, `FUN_0066aa80`, AnimTbls.cpp) |
| `FUN_00611d60` | (ECX mask, bit): sets mask bit n for every States row (0x3c bytes) whose flag bit `bit` (at `+0x10`) is set |
| `FUN_00611dc0` | (&list, &count, bit): a word list of the States rows with that flag (line 0x64b) |
| `FUN_00611e60` | (&list, &count, offset): a word list of the States rows whose word at `offset` is positive (line 0x662) |
| `FUN_00611ee0` | A player class's name (CharStats `+0x20`, 0xc4-byte rows), else `player %d`; ESI the size, EDI the buffer |
| `FUN_00611f40` | A monster class's name (MonStats `+6` string, not 0x1506), else `monster %d` |
| `FUN_00611fa0` | An object class's name (Objects.txt row), else `object %d` |
| `FUN_00611fe0` | An item class's name (Items `+0xf4` string, not 0x1506), else `item %d` |
| `FUN_00612030` | `missile %d` |
| `FUN_00612050` | A unit's name by type through the five above; `NULL Unit`, `Invalid Unit`. No caller |
| `FUN_00612120` | The same by (type, class); `Invalid Unit Type`. No caller |
| `FUN_006125a0` | The compile flag `0x96c8b4` = (arg == 0); called at `0x44d9cc` |
| `FUN_006125c0` | Frees one table (line 0x924 / 0x926) |
| `FUN_00612600` | Fills each row's string field from its .tbl key (wide → narrow, `FUN_00524e20`, `FUN_00526700`, `FUN_00526790`). Caller `FUN_00613260` |
| `FUN_00612ea0` | Frees compcode, and with the compile flag playerclass, bodylocs, storepage, elemtypes, hitclass, colors, hiredesc, monmode, plrmode, monai, monplace, skillcalc, misscalc, skills codes and events |
| `FUN_00613d30` | Loads experience (0x20-byte rows: Amazon .. Druid, ExpRatio) |
| `FUN_00613e60` | ExpRatio for a level (row level + 1), row 0's for level 0 and below; 0 past the last |
| `FUN_00613e90` | With the compile flag, writes a table to `DATA\GLOBAL\EXCEL\<name>.bin` and frees it (line 0x275), then reads the .bin back (`FUN_00612280`, 0x27a). Callers `FUN_00613f80`, `FUN_006315d0`, `FUN_00661b20` |
| `FUN_00617c30` | Frees the skills block: three side arrays (lines 0x5b8..0x5d4), skills and skilldesc |
| `FUN_00617d90` | Loads difficultylevels (0x58); anything but 3 rows is fatal (0x61f) |
| `FUN_00618c90` | Frees states and its derived lists (lines 0x6e8..0x6fc, 0xa0 bytes at `0x96bcfc`) |
| `FUN_00618d90` | Loads pettype (0xe0; more than 0xff rows is fatal, 0x72b) |
| `FUN_00619140` | Unloads every table (the list above, then the other files' unloaders) |

## LvlTbls.cpp

| Address | What |
|---|---|
| `FUN_0061ddf0` | Returns `0x96c9f4` and puts `0x96c9f8` in ECX's slot |
| `FUN_0061de00` | Loads leveldefs (0x9c), then `FUN_0061dd00` |
| `FUN_0061e440` | Frees leveldefs and `0x96c9f4` (line 0x1d9) |
| `FUN_0061e4a0` | A lvltypes file name: `/` to `\`, then `<prefix>\<name>` in place |
| `FUN_0061e520` | Loads lvltypes (0x788: 32 file names of 0x3c, `+0x780` act, `+0x784` expansion), fixing each name |
| `FUN_0061ea90` | Frees lvltypes |
| `FUN_0061eaa0` | A lvltypes row (fatal 0x251 unloaded) |
| `FUN_0061eae0` | Is a lvltypes row's act (`+0x780`) arg |
| `FUN_0061eb40` | Copies a lvltypes row's file n (n < 32) |
| `FUN_0061f030` | Frees lvlprest and its cached DS1s (`0x96ca20`, 6 a row) |
| `FUN_0061f0b0` | A lvlprest row (0x1b0), 0 out of range |
| `FUN_0061f130` | Loads lvlwarp (0x30) |
| `FUN_0061f300` | Frees lvlwarp |
| `FUN_0061f380` | Loads lvlmaze (0x1c: Level, Rooms, Rooms(N), Rooms(H), SizeX, SizeY, Merge) |
| `FUN_0061f480` | Frees lvlmaze |
| `FUN_0061f9c0` | Frees lvlsub (0x15c rows at `0x96ca10`), its DS1s and its index (line 1000) |
| `FUN_0061fa90` | A lvlsub row by type through the index `0x96ca18` (fatal 0x3f1..0x3f3) |
| `FUN_0061fae0` | Builds a 0x1000-byte DT1 set per lvltypes row (`FUN_00604a40` per file not named `0`, skipping expansion files in classic, `FUN_00408f20`; lines 0x410, 0x413) |
| `FUN_0061fbf0` | Frees it (0x43a) |
| `FUN_006200d0` | Frees `0x96ca28` (line 0x551) |

## ItemTbls.cpp

| Address | What |
|---|---|
| `FUN_00633540` | Frees the items tables (link, rows `0x96ca5c`, the index `0x96ca78`; lines 0x1c8, 0x1cb) |
| `FUN_00633590` | The items count's address (`0x96ca58`) |
| `FUN_006335a0` | The items index word n (fatal 0x1e2); -1 out of range |
| `FUN_006336a0` | A column callback: a number, else a name in skills, montype or states. Used through pointers by five loaders |
| `FUN_00633730` | Loads magicsuffix, magicprefix and automagic (0x90 each) into one array (line 0x297), names to .tbl at `+0x20` |
| `FUN_00633ea0` | Frees it (0x2b7) |
| `FUN_00633f40` | Loads raresuffix and rareprefix (0x48) into one array (0x2f4), names at `+0xc` |
| `FUN_00634220` | Frees it (0x30b) |
| `FUN_00634250` | The rare affix count's address |
| `FUN_006342b0` | Loads uniqueitems (0x14c; more than 0x7ffe is fatal, 0x37c): the row number at `+0`, the name link, the name's string at `+0x22` (0x1507 when missing) |
| `FUN_00634d90` | Frees uniqueitems |
| `FUN_00636730` | Frees sets and setitems |
| `FUN_00636770` | Loads qualityitems (0x70), the two effect strings at `+0x6c` / `+0x6e` |
| `FUN_00636b00` / `FUN_00636b10` / `FUN_00636b20` | Free / count address / row (fatal 0x4f2, 0x4f3) |
| `FUN_00636b70` | Loads gems (0xc0): the letter's string at `+0x2c`; each gem's item (`+0x28`) gets its gem row in Items `+0xf0` (-1 for the rest) |
| `FUN_006372a0` | Frees gems |
| `FUN_00637320` | Loads books (0x20) |
| `FUN_00637490` / `FUN_006374a0` | Free / count address |
| `FUN_00637500` | Loads lowqualityitems (0x22), the name's string at `+0x20` |
| `FUN_006375a0` / `FUN_006375b0` / `FUN_006375c0` | Free / count address / row (fatal 0x5d0, 0x5d1) |
| `FUN_00637620` | Loads itemratio (0x44) |
| `FUN_006378f0` | Frees it |
| `FUN_006379d0` | qsort compare on the word at `+2` (descpriority) |
| `FUN_00637a00` | Loads itemstatcost (0x144; more than 0x1ff is fatal, 0x6b6; `+0x140` of row 0 the bits per stat, 1..8): the op chains (op 1..13 base stats link their derived stats, flags at `+0x51..+0x55`), then the stats with a descpriority sorted into `0x96c808` (line 0x748) |
| `FUN_006385d0` | Frees it (0x763) |
| `FUN_00638630` | Loads properties (0x2e) |
| `FUN_00638aa0` | Frees it |
| `FUN_00638ae0` | Loads gamble (0xc): each code's Items row and level (Items `+0xfd`, fatal 0x7e0), sorted by level into `0x96cab4` (0x7db), and the first row past each level 1..99 at `0x96cabc` |
| `FUN_00638c80` | Frees it (0x80d) |
| `FUN_00638cd0` | Is ItemTypes row a one of b (its Equiv1 / Equiv2 walked; returns 0 when more than 0x7c entries remain after a pop) |
| `FUN_00638d80` | Loads itemtypes (0xe4) and builds the is-a bit matrix `0x96c834` (rows of `0x96c830` dwords, line 0x893) that `FUN_00629b50` reads |
| `FUN_006393e0` | Frees it (0x8a7) |
| `FUN_00639420` | Does `DATA\GLOBAL\EXCEL\<name><ext>` open (`FUN_00516e46`). Caller `FUN_006394a0` |
| `FUN_00639c90` / `FUN_00639cb0` | Free runes / its count address |
| `FUN_00639cc0` | Writes runes row n (0x120 bytes), growing the array (line 0x95b) |

## MonsterTbls.cpp

| Address | What |
|---|---|
| `FUN_00653d60` | Sets MonStats row's word `+0x32` (fatal 0x2ef). No caller |
| `FUN_00653db0` | Loads unique name table n (0..3: title, prefix, suffix, appellation; 2-byte rows, `Name`). No caller: the shipped loader makes the four calls itself |
| `FUN_00653e20` | Frees all four |
| `FUN_00653ed0` / `FUN_00653f10` / `FUN_00653f50` | A random unique prefix / suffix / appellation string from the seed (`d2d_seed_rand_below`), else the empty string `0x96d4b4` |
| `FUN_00653f90` | Adds a treasure class named arg (0x2c bytes, grown 16 at a time, line 0x3a2) |
| `FUN_00654010` | Grows a treasure class's item list (0x1c each, `+0x28`; line 0x3ba) |
| `FUN_00654080` | Appends an item with the class's running totals (`+8`, `+0xc`) |
| `FUN_006540c0` | Changes an item's chance and moves the totals after it |
| `FUN_00654410` | Splits off the next number at character AL (`FUN_00687740`), `_atol` |
| `FUN_00654d50` | Frees the treasure classes (lines 0x643, 0x647) |
| `FUN_00654dd0` | A treasure class by name. No caller |
| `FUN_00654e80` | (difficulty, act, chest): the parsed treasure class at `0x96c5f4[15 d + 3 a + c]`, each clamped (d 0..2, a 0..4, c 0..2). Caller `FUN_00585b90` (objects.md "Chest treasure class") |
| `FUN_00654f20` | Loads monitempercent (4: HeartPercent, BodyPartPercent, TreasureClassPercent, ComponentPercent) |
| `FUN_00654fc0` / `FUN_00654fe0` | Free / row (fatal 0x6dc, 0x6dd). The row has no caller |
| `FUN_00655030` | Loads monumod (0x20) |
| `FUN_006552e0` | Loads superuniques (0x34): the first row of each hcIdx below 0x42 into `0x96c710`; a hcIdx with none is fatal (0x782) |
| `FUN_006556e0` | Superunique row n (0x34 bytes at `0x96c704`), 0 for n >= count (signed) |
| `FUN_00655710` | The superunique count |
| `FUN_00655720` | Loads hireling (0x118): NameFirst / NameLast strings at `+0x114` / `+0x116` (checked in the expansion: lines 0x816..0x818), the first row per (version 0 / 100, `+4`) into `0x96bdd8` |
| `FUN_006562b0` | Frees it and clears that index |
| `FUN_00656390` | (expansion, name string, after): the next hireling row of that version whose name range holds the string |
| `FUN_00656440` | (expansion, id, after): the next row of that version with `+8` == id |
| `FUN_006564d0` | (expansion, seller, difficulty, after): the next hireling row whose `+0x14` is seller, `+0x10` is difficulty + 1 and version word is 0 / 100 |
| `FUN_00656620` | Loads npc (0x4c) |
| `FUN_00656900` | The npc row of a monster class (0x4c rows, `+0`) |
| `FUN_00656930` | Loads monsounds (0x94) |
| `FUN_00656f90` | A monsounds row |
| `FUN_00656fc0` | The monsounds row of MonStats row n (`+0x14` word, signed, below the sounds count); 0 without either table or for n >= the MonStats count |
| `FUN_00657010` | Parses a MonStats2 component list (`"a,b,..."`, up to 12 codes of at most 4 characters, through the compcode link) for layer n into `+0x15 + n` (count) and `+0x26 + 12n`; layer 15 also sums the extra choices into `+0x25` (at most 0xfe). Caller `FUN_006571a0` |
| `FUN_00658460` | Is MonType a one of b (its three equivs walked) |
| `FUN_00658520` | Loads montype (0xc) and its is-a bit matrix (line 0xafc) |
| `FUN_006586d0` | Frees it (0xb16) |
| `FUN_00659500` | Loads monlvl (0x78) |
| `FUN_00659a10` | Loads monpreset (4: act, Place) and the first row and count per act at `0x96c6dc` / `0x96c6f0` |
| `FUN_00659af0` | Frees it |
| `FUN_00659b40` | (act, &count): the act's first monpreset row (act < 5) |
| `FUN_00659b80` | (kind, n): n + the superunique count (kind 0), + the MonStats count (kind 2), else n |
| `FUN_00659bb0` | With the compile flag, loads sounds (2: `Sound`, link line 0xc99) |
| `FUN_00659c30` | Frees it |
| `FUN_00659c60` | Loads monseq (6) and the per-sequence index (first row, two counts; line 0xccc) |
| `FUN_00659de0` | Frees it (0xcea) |
| `FUN_00659e60` | Loads monequip (0x1c; more than 0x7ffe is fatal, 0xd1c): MonStats `+0x2a` = its first row (-1 none); a slot with a bad location (1..10) or an unknown code is cleared |
| `FUN_0065a190` | Frees the monster tables (monequip, npc, hireling, monpreset, superuniques, monumod, monstats, monstats2, monprop, monsounds, monseq, the treasure classes, monlvl, the unique names) |
| `FUN_0065a390` | Loads the treasure classes (link line 0x60e; `FUN_006541c0`, `FUN_006547d0`, `FUN_0065a2c0`); more than 0xfffe is fatal (0x616) |

## TokTbls.cpp, MissileTbls.cpp, OverlayTbls.cpp

| Address | What |
|---|---|
| `FUN_0065adc0` | Loads plrtype and plrmode (0x34, `token`) into one array (line 0x37) |
| `FUN_0065aef0` | Loads monmode (0x34, the `*_Dir` columns) |
| `FUN_0065b1b0` | Loads objtype and objmode into one array (line 0x94) |
| `FUN_0065b2e0` | Loads composit (0x34) |
| `FUN_0065b350` | Loads armtype (0x34) |
| `FUN_0065b3c0` | Frees the token tables (lines 0xd6, 0xe2) |
| `FUN_00663210` | Frees missiles and its side array (line 0x19e) |
| `FUN_00664150` | Loads overlay (0x84, link line 0x38) |

## FieldTbls.cpp and AnimTbls.cpp

| Address | What |
|---|---|
| `FUN_0066a2b0` | The field: width at `+2`, height at `+6`, bytes from `+10` copied (line 0x3e); the half sizes at `0x96d664` / `0x96d668` and ten row offsets at `0x96d630..0x96d650` |
| `FUN_0066a420` | Frees it (0x5b) |
| `FUN_0066a470` | The half width and height. No caller |
| `FUN_0066a4b0` / `FUN_0066a500` / `FUN_0066a540` | Allocate (8 bytes, line 0x87) / free (0x94) / set an origin pair |
| `FUN_0066a580` | The field byte at (x, y) from an origin. No caller |
| `FUN_0066a700` | Reads `DATA\GLOBAL\expfield.d2` (line 0x28) into `FUN_0066a2b0` |
| `FUN_0066a770` | Reads `DATA\GLOBAL\AnimData.d2` (line 0x8a): 256 hash buckets, each a count then 0xa0-byte records, into a 0x4a4-byte header (`+0x40c` = 0x800, `+0x410` = 0x100) |
| `FUN_0066a830` | Frees it (lines 0xbe, 0xbf) |

## Verified in the emulator

Three oracles in `tools/emu/` run these 54 helpers from stock game.exe 1.14d
(SHA-256 `631066c1...adaaf`) on random tables, with the data-tables globals
written directly (no MPQ is read) and the callees hooked and logged. Each
compares the return value, the table bytes after the call, the callee log and
the bytes `ret n` popped against a hand-written model, and `--mutation-check`
breaks the model 26 / 14 / 19 ways and needs each to fail. Re-check:
`cd tools/emu && uv run python monster_tbls.py` (4,930 cases), `item_tbls.py`
(2,078), `lvl_tbls.py` (3,220), each with `--mutation-check`.

The hooked callees are the growing allocator `0x40b4b0` (modelled as a realloc
that copies the old block, which is **assumed**, not traced: `FUN_0040a1f0`),
the excel link `0x6bd500` / `0x6bd3c0`, the table free `0x6125c0`, the link
free `0x6bd0b0`, the pool and client releases, the seeded random `0x45c3e0`,
the string lookup `0x524a30`, the DS1 release `0x668300` and `wsprintfA`. The
C runtime's `memset`, `strchr` and `atol` run for real.

**What the traces settled**

- The row getters differ in what they let through. `FUN_00654fe0`,
  `FUN_00636b20` and `FUN_006375c0` return 0 for `n >= count` **and for -1
  only**: any other negative `n` is below the count and gives
  `table + n * size`, an address before the table. With no table they halt on
  the line in the rows above (0x6dc / 0x4f2 / 0x5d0); an address that wraps to
  zero halts on the second line (0x6dd / 0x4f3 / 0x5d1). `FUN_006335a0` (items index)
  returns -1 for any `n < 0` or `n >= count`. `FUN_00656f90` and
  `FUN_0061e470` / `FUN_0061f0b0` return 0 outside `0 <= n < count`.
  `FUN_0061eaa0` checks neither bound (n * 0x788 + table for any n, drawn from
  -8 to the row count + 8) and `FUN_0061fa90` only n < 0 (it has no count; the
  index is read at n, drawn up to 15).
- `FUN_00658460` (MonType) and `FUN_00638cd0` (ItemTypes) walk equivalents with
  a stack (a 128-int local) and **no visited set**: the depth guard (`0x7c <`
  entries left after a pop) returns 0 only for a walk that grows. A cycle of
  single links (A names B, B names A, the wanted class elsewhere) never
  returns; the oracle sees the instruction limit run out (bugs.md 69). The two
  differ at the edge: `b < 1` is 0 for MonType and **1** for ItemTypes; MonType
  reads three equivalents (`+2`, `+4`, `+6`), ItemTypes two (`+4`, `+6`). The
  first equivalent is pushed only when positive; the later ones when non-zero,
  so a negative one is pushed and indexes the table below row 0. The oracles
  fill the rows before row 0 with real equivalents and build walks that reach a
  target only through such a row; skipping the negative equivalent is one of
  the mutations and is caught.
- `FUN_00653f90` grows the treasure class array by 16 records (0x2c bytes) only
  when the count is a multiple of 16 (the pool call is `0x40b4b0` with the old
  block in EDX, line 0x3a2), then zeroes the new record; `FUN_00654010` /
  `FUN_00654080` do the same per class for the 0x1c-byte items (line 0x3ba).
  `FUN_00654010` does not handle a smaller count: it would hand `memset` a
  negative length (not run; the callers traced here only grow).
- `FUN_006540c0` (change a treasure item's chance) keeps two running columns in
  step: for the chance column (`+4` of each item, class total `+0xc`) always,
  for the other (`+0` of each item, class total `+8`) only when the stack flag
  is zero. The span is measured to the next item's value, or to the class
  total for the last item; every later item and the total move by the
  difference. The item index is a signed division of the pointer difference
  by 0x1c.
- `FUN_00654410` splits a number off a cursor (`*ESI`) at the character in AL:
  the separator is overwritten by 0, the cursor moves past it, a missing
  separator sets the cursor to null, and the return value is `atol` of the
  text before it (white space and a sign allowed; values past 32 bits stop the emulated C runtime and were not tested).
- `FUN_00653ed0` / `00653f10` / `00653f50` (unique title / prefix / suffix):
  nothing is drawn unless the flag in ECX is non-zero **and** the count is
  positive. The seed pointer is flag + 0x20, the draw is below the count, a
  zero 16-bit entry or a missing string gives the empty string `0x96d4b4`.
- `FUN_00656390` (hireling by name string) and `FUN_00656440` (by id) start
  after the row passed in (index + 1), or at row 0 for null, and match the
  version word (`+0`: 100 in the expansion, else 0). The string range is
  inclusive at both ends (`+0x114` <= s <= `+0x116`).
- `FUN_00656900` returns the **first** npc row with the id; `FUN_0061f0e0` the
  first lvlprest row with `+4` (LevelId) == key; `FUN_0061f490` the first lvlmaze row;
  `FUN_0061f310` halts unless a lvlwarp row has the id and a letter that is
  `b`, equals the request or the request is `b`.
- `FUN_0061fc10` and `FUN_0061fc80` are string-to-index lookups in two static
  tables of game.exe: 36 level-type names at `0x6e7d50` (16 bytes each:
  `None`, `1 Town`, `1 Wilderness`, `1 Cave`, `1 Crypt`, `1 Monestary`,
  `1 Courtyard`, `1 Barracks`, `1 Jail`, `1 Cathedral`, `1 Catacombs`,
  `1 Tristram`, `2 Town`, `2 Sewer`, `2 Harem`, `2 Basement`, `2 Desert`,
  `2 Tomb`, `2 Lair`, `2 Arcane`, `3 Town`, `3 Jungle`, `3 Kurast`,
  `3 Spider`, `3 Dungeon`, `3 Sewer`, `4 Town`, `4 Mesa`, `4 Lava`, `5 Town`,
  `5 Siege`, `5 Barricade`, `5 Temple`, `5 Ice`, `5 Baal`, `5 Lava`; the
  spelling `Monestary` is the binary's) and 20 DT1 piece names at `0x6e7f90`
  (8 bytes: `fl wl wr wtlr wtll wtr wbl wbr wld wrd wle wre co sh tr rf ld rd fd
  fi`). The EDI argument is compared as a whole string, case-sensitively; a
  name starting with `0` returns 0 without a search; a miss halts (0x4a5 /
  0x4d0).
- `FUN_0061e4a0` turns `/` into `\` in the path and, for two or more
  characters, rewrites it as `prefix\path` through `wsprintfA` into a 60-byte
  local, then copies the result back over the caller's path (read from the
  decompile; a longer result is unchecked: bugs.md 70).
  `FUN_0061ddf0` returns `0x96c9f4` and stores `0x96c9f8` through ECX.
  `FUN_0061eb40` stores a 0 byte at the destination, then copies string `k` of
  a lvltypes row only when `k < 0x20` (signed, so a negative `k` reads before
  the row: not run); the oracle shows the 0 byte for `k >= 0x20` and the copy
  for `k < 0x20`.

**Verified versus assumed.** Verified: the rows above for the inputs the
oracles draw (random tables, edge counts, null tables, the wrapped pointer, the
depth edge of the walks). Assumed: the realloc copy semantics of `0x40b4b0`;
the callees' own behaviour (each is traced elsewhere or not at all); the
negative `memset` length of `FUN_00654010`; atol past 32 bits; negative `k` in
`FUN_0061eb40`; the 60-byte buffer overflow of `FUN_0061e4a0` (a longer `prefix\path` overruns
the local and the copy back into the caller's buffer; the /GS cookie check ends
in `__security_check_cookie`).

| Oracle | Functions |
|---|---|
| `monster_tbls.py` | `FUN_00653d60`, `00653ed0`, `00653f10`, `00653f50`, `00653f90`, `00654010`, `00654080`, `006540c0`, `00654410`, `00654dd0`, `00654fc0`, `00654fe0`, `00655710`, `00656390`, `00656440`, `00656900`, `00656f90`, `00658460`, `00659b40`, `00659b80`, `00659c30` |
| `item_tbls.py` | `FUN_00633590`, `006335a0`, `00634250`, `00636b10`, `00636b20`, `006374a0`, `00637490`, `006375b0`, `006375c0`, `006379d0`, `00638c80`, `00638cd0`, `00639c90`, `00639cb0` |
| `lvl_tbls.py` | `FUN_0061ddf0`, `0061e440`, `0061e470`, `0061e4a0`, `0061ea90`, `0061eaa0`, `0061eae0`, `0061eb40`, `0061f030`, `0061f0b0`, `0061f0e0`, `0061f300`, `0061f310`, `0061f480`, `0061f490`, `0061fa90`, `0061fc10`, `0061fc80`, `006200d0` |

### Loaders and getters (`monster_loaders.py`)

`tools/emu/monster_loaders.py` runs 19 more MonsterTbls.cpp functions from stock
game.exe 1.14d: the table loaders, the freers and four getters (4,050 cases,
`--mutation-check` breaks the model 36 ways). Each loader builds a column list
(5 dwords an entry: name, type, size, offset, linker) and calls CompileTxt
`0x6122f0`; the oracle hooks CompileTxt, records the list, the table name, the
record size and the row-count address, then writes a scripted row count and
returns a scripted table. The link creator `0x6bd040`, the table and link
frees, the pool free, the fatal path, `D2CheckExpansionGame` `0x408f20` and
the string lookup `0x524d30` are hooked and logged; `strchr` runs for real. Each
loader and freer case fills the data page `0x96b000..0x96e000` with random
dwords and requires the dwords that changed to be exactly the ones the model writes. Re-check:
`cd tools/emu && uv run python monster_loaders.py` (needs `D2_MPQ_DIR`; no MPQ
is read) and with `--mutation-check`.

- **Schemas.** Compared column by column (name, type, size, offset, linker) with
  the model: `FUN_00653db0` (one `Name` KEY_WORD column, linker `0x6117b0`,
  record 2, into `0x96d488 + 4n`, count `0x96d498 + 4n`; `n >= 4` unsigned,
  so -1 too, does nothing), `FUN_00654f20` (4 CHAR columns), `FUN_00655030` (16 columns, link
  `0x96c87c`, `exclude1/2` linked to the MonType link `0x96c868`),
  `FUN_00655720` (70 columns: `namefirst` / `namelast` ASCII 32 at `+0xd3` /
  `+0xf3`, six skill groups linked to the skills link `0x96c7cc`, `hiredesc`
  CODE_CHAR at `+0xd2` linked `0x96bce8`), `FUN_00656620` (20 columns, `npc`
  linked to the MonStats link `0x96c6ac`), `FUN_00659a10` (`Act` CHAR, `Place`
  LINKER_BIT with the function `0x659990`) and `FUN_00659bb0` (`Sound`
  NAME_I_WORD, link at line 0xc99, only with the compile flag `0x96c8b4`).
- `FUN_00655030` caps the monumod row count at 0x100 with a **signed** compare
  (a negative count is kept). `FUN_0065a390` is fatal (0x616) when the parsed
  treasure class count is 0xffff or more, also signed; its three callees
  (`FUN_006541c0`, `FUN_006547d0(pool)`, `FUN_0065a2c0`) run in that order after
  the link (line 0x60e) and are hooked, so their own work is not covered.
- `FUN_00655720` after the compile: all 512 dwords of the two id-line tables
  `0x96bdd8..0x96c5d8` are set to -1; per row the string ids of NameFirst and
  NameLast go to `+0x114` / `+0x116` (low 16 bits of the lookup result); with
  the expansion flag the loader is fatal on a zero first id (0x816), a last id
  not above the first (0x817, tested second) and a row id of 0x100 or more
  (0x818). The first row of each (version >= 100, id) is stored as
  `table[0x100 * (version >= 100) + id]`, only while the entry is still
  negative, and only for a signed id below 0x100 (a classic-mode id of 0x100 is
  skipped). Below version 100 an id of -2 addresses the hireling table pointer
  and overwrites it when that pointer is signed-negative (bugs.md 73). A halt
  leaves the earlier rows' ids written.
- The free functions leave pointers behind (bugs.md 71): `FUN_006562b0` unloads
  hireling and zeroes the count and id lines but not `0x96bdd0`;
  `FUN_00659af0` unloads monpreset and clears the per-act tables but not
  `0x96c6d8`; `FUN_00659de0` clears the MonSeq count `0x96c7b0` (not the table,
  link or extra block pointers) and, when an extra block was freed, its count
  `0x96c7b8`; `FUN_006586d0` clears MonType's table and link but not the extra
  block pointer `0x96c874` (left dangling; its body is guarded on `0x96c86c`, so
  a second call does nothing, and only the other three free twice); `FUN_00653e20` and `FUN_00654d50` do clear what they free.
  `FUN_006562b0` calls the unload unconditionally, the others test the pointer.
- `FUN_00659a10` builds the per-act first-row / count tables from the Act byte
  of each row: act 1's table starts at the first row, every act step records
  the row where the next act starts and the count of the finished one. The two
  tables have five slots (`0x96c6dc..0x96c6ec`, `0x96c6f0..0x96c700`) and are
  adjacent; an Act byte above 5 writes `perAct[5]` over `perActId[0]` and so on,
  and an Act-6 row's final `perActId[5]` store lands on `0x96c704`, the superunique
  table pointer (bugs.md 72; the shipped monpreset uses acts 1..5).
- `FUN_006564d0` is not a lookup by class: it matches `+0x14` (the `seller`
  column, the NPC class) with its second argument, `+0x10` (difficulty) with the
  third plus one, and the version word with 0 / 100. It starts after the
  previous row (pointer difference divided by 0x118, truncating toward zero,
  plus one; an unaligned or lower pointer is not rejected), and returns 0 past
  the table.
- `FUN_00656fc0` and `FUN_006556e0` reject only `n >= count` (signed): a negative
  `n` indexes below the table (bugs.md 73), as for the row getters above.
- `FUN_00654e80` clamps each of difficulty, act and chest (`<= 0` gives 0) and
  indexes `0x96c5f4 + 4 * (15 d + 3 a + c)`; act has five values (0..4), the
  others three.
- `FUN_00657010` (component list for MonStats2 layer `slot`, `slot < 0x10`
  else nothing): clears the slot's count byte (`+0x15 + slot`) and its 12 code
  bytes (`+0x26 + 12 slot`) to 0xff; a null text stops there. A leading `"` is
  skipped, any other `"` is overwritten with NUL (the scan continues past it, so
  `ab"cd` reads as `ab`). The text is then split on commas; an empty token or one
  of more than four characters ends the list without storing it (the codes
  before it are kept); a token is padded with spaces to four characters,
  resolved with `GetClassIdFromName(compcode link, code, 1)` (hooked) and its
  low byte stored; at most 12 codes. The count is stored to `+0x15 + slot`. On
  slot 15 only, the byte at `+0x25` is the sum over the 16 count bytes
  `+0x15..+0x24` of (n - 1) for each n above 1 (the sum includes slot 15's own
  count), capped at 0xfe.

**Assumed here.** The string lookup, `GetClassIdFromName` and the three
treasure class builders are hooked (call order and arguments checked, not their
work; for the string lookup only the first argument, the name pointer, is
logged and checked); `FUN_006541c0`'s register inputs are unknown and are not logged.
`CompileTxt`'s own behaviour is verified by `txt_loaders.py`. The row bytes the
parser would write are not produced (CompileTxt returns a scripted table), so
what a real `.txt` yields for these columns is not claimed.

| Oracle | Functions |
|---|---|
| `monster_loaders.py` | `FUN_00653db0`, `00653e20`, `00654d50`, `00654e80`, `00654f20`, `00655030`, `006556e0`, `00655720`, `006562b0`, `006564d0`, `00656620`, `00656fc0`, `00657010`, `006586d0`, `00659a10`, `00659af0`, `00659bb0`, `00659de0`, `0065a390` |

### Loaders and getters, second batch (`txt_schemas.py`)

`tools/emu/txt_schemas.py` runs ten CompileTxt loaders and one getter from stock
game.exe 1.14d (1,200 cases, `--mutation-check` breaks the model 25 ways). The
harness is `monster_loaders.py`'s rig: CompileTxt 0x6122f0 is hooked and logs the
column list, the table name, the count address and the record size; the string
lookup 0x524d30 is hooked and logs the name pointer it was given.

* **FUN_00617d90** (difficultylevels, stdcall pool): 22 INT columns in the
  array's order (`ResistPenalty` at 0, ..., `GambleUltra` at 0x54), record 0x58,
  table `0x96c8ac`, count `0x96c8b0`. Any count other than 3 halts (line 0x61f) after the store.
* **FUN_00637620** (itemratio, fastcall pool): `Version` SHORT at 0x40, `Uber` and
  `Class Specific` CHAR at 0x42 / 0x43, then 16 INT columns; record 0x44, table
  `0x96cc4c`, count `0x96cc48`.
* **FUN_0061f130** (lvlwarp): 11 INT columns and `Direction` as a one-byte ASCII
  column at 0x2c; record 0x30. The table goes to `0x96ca00`; the count is written
  to `0x96ca04` from a stack slot (the rig logs the slot's address, which is not checked).
* **FUN_0061f380** (lvlmaze): 7 INT columns, record 0x1c; table `0x96ca08`, count `0x96ca0c`.
* **FUN_00659500** (monlvl): 30 INT columns (AC, TH, HP, DM, XP with their (N) and
  (H) variants, then the L- per-level variants; `XP(N)` is at 100), record 0x78,
  table `0x96c7a0`, count `0x96c7a4`.
* **FUN_00637500** (lowqualityitems): one ASCII column of 0x1f bytes, record 0x22;
  table `0x96cc5c`, count `0x96cc58`. Then for each row, in order, the name goes to
  STRTABLE_LookupString and the id is stored as a short at row +0x20.
* **FUN_00636770** (qualityitems): 22 columns (`nummods` CHAR at 0x0a; `mod1code`
  and `mod2code` NAME_INT at 0x0c / 0x1c linked to `0x96bcd8`; the three
  parameter triples at 0x10, 0x20; `effect1` / `effect2` ASCII 0x1f at 0x2c /
  0x4c; the ten equipment flags at 0..9 with `wand` at 4 and `scepter` at 3), record
  0x70, table `0x96cc54`, count `0x96cc50`. After the compile, each row's `effect1`
  is looked up when its first byte is non-zero and its id stored as a short at
  +0x6c; `effect2` likewise to +0x6e. The first store is written through the
  `effect2` field's address (`effect2 + row + 0x20`), so it lands at +0x6c, the last
  two bytes before the record's end; the disassembly agrees with the decompile.
* **FUN_00618d90** (pettype, fastcall pool): creates the link at `0x96c81c`
  (`.\\DATATBLS\\DataTbls.cpp`, line 0x729), 21 columns (`pet type` NAME_I_INT on
  that link, `group` / `basemax` SHORT, six BIT columns in the dword at +4 with bit
  numbers 0..5, the icon and class strings, `name` KEY_WORD linked to
  DATATBLS_LookupStringId 0x6117b0), record 0xe0, table `0x96c818`, count
  `0x96c820`. A count above 0xff (signed) halts at line 0x72b; after the compile the
  count is stored as 0 when below 1 (`size & ((size < 1) - 1)`); the decompile's
  later 0x100 clamp is then unreachable.
* **FUN_00637320** (books, fastcall pool): 9 columns, `name` KEY_WORD linked to
  0x6117b0, two RAW4 spell codes at +0x18 / +0x1c, `pSpell` INT, two NAME_INT skill
  columns linked to the skills link `0x96c7cc`, record 0x20, table `0x96cc64`,
  count `0x96cc60`.
* **FUN_00613e60** (TXT_Experience_GetExpRatio, stdcall level): the table at
  `0x96c8a8`. Level <= 0 gives row 0's ExpRatio (+0x1c, not 0 as the decompile's
  first branch suggests when the table is set); a level above the first dword
  (signed) gives 0; otherwise row level + 1 (`+0x20 * level + 0x3c`). A null table
  gives 0. The disassembly (`00613e60`..`00613e8d`) agrees with the decompile.

The oracle's table pointers, column lists, record sizes and count addresses are
the model's; the data page `0x96b000..0x96e000` is checked dword by dword, and the
two post-process loaders also compare their row tables byte by byte after the
lookups. Re-check: `cd tools/emu && uv run python txt_schemas.py` (needs
`D2_MPQ_DIR`; no MPQ is read), and with `--mutation-check`.

**Not covered here:** the other cited DataTbls.cpp, ItemTbls.cpp, LvlTbls.cpp, MonsterTbls.cpp, TokTbls.cpp,
MissileTbls.cpp, OverlayTbls.cpp, FieldTbls.cpp and AnimTbls.cpp functions that
are not in the tables above.

| Oracle | Functions |
|---|---|
| `txt_schemas.py` | `FUN_00617d90`, `00618d90`, `00637620`, `00637320`, `0061f130`, `0061f380`, `00659500`, `00637500`, `00636770`, `00613e60` |

### FieldTbls.cpp (`fieldtbls.py`)

`tools/emu/fieldtbls.py` runs eight FieldTbls.cpp functions from stock game.exe
1.14d (600 cases, `--mutation-check` breaks the model 16 ways). The archive
reader is hooked (D2FILE `0x517079`, logged with the path it was given); the
fatal path, the pool and client releases run as hooked logs; the memmove
`0x6829c0` runs for real.

* **FUN_0066a2b0** (InitFieldData, stdcall archive): the archive's width is the
  dword at +2, its height the dword at +6, the bytes from +10. The copy is
  `w * h` bytes. **The stride is the height**: the nine row offsets at
  `0x96d630 + 4k` are `Y[k] * h + X[k]` (X at `0x749780`, Y at `0x7497a4`, the
  table of game.exe's step deltas), though the Ghidra output multiplies by
  `nWidth`. Half height goes to `0x96d664` and half width to `0x96d668`; the
  table pointer to `0x96d654`, the size to `0x96d660`, width `0x96d65c` and
  height `0x96d658`. The archive is freed as client memory (line 0x50).
* **FUN_0066a420** (FreeFieldData): frees the table if set (pool, line 0x5b),
  clears the table, the size, the dimensions and the half sizes, returns 1.
* **FUN_0066a470** (GetFieldCenterOffsets, stdcall two pointers): the half
  height at `0x96d664` goes through the **first** pointer and the half width at
  `0x96d668` through the second (the decompile's names are the other way round).
* **FUN_0066a4b0 / 0066a500** (AllocPoint / FreePoint): an 8-byte pool block
  (line 0x87) with x = y = 0; the free checks null (line 0x93) and frees (line 0x94).
* **FUN_0066a540** (SetXY, stdcall point, x, y): x at +0, y at +4; null halts (line 0xa1).
* **FUN_0066a580** (field byte, stdcall origin, x, y): null halts (line 0xb0);
  otherwise the byte at `table + x + ((y - origin[1]) << 8) - origin[0]`. The row
  stride here is 0x100, not the width.
* **FUN_0066a700** (LoadExpFieldFile, fastcall pool): wsprintfA gives
  `DATA\GLOBAL\expfield.d2`, then D2FILE with line 0x28, then InitFieldData.

Not reached here: the null allocation halts of InitFieldData (line 0x40) and
AllocPoint (line 0x88): the allocator hook returns fresh memory in this oracle, so the null branch is not run.

| Oracle | Functions |
|---|---|
| `fieldtbls.py` | `FUN_0066a2b0`, `0066a420`, `0066a470`, `0066a4b0`, `0066a500`, `0066a540`, `0066a580`, `0066a700` |

### Token, missile and overlay tables (`tok_overlay.py`)

`tools/emu/tok_overlay.py` runs six functions from stock game.exe 1.14d (540
cases, `--mutation-check` breaks the model 17 ways), on `monster_loaders.py`'s rig.

* **FUN_0065aef0** (monmode, fastcall pool): name ASCII 0x1f, token RAW4 at 0x20,
  fourteen BYTE direction counts `DT_Dir`..`KB_Dir` at 0x24..0x31, then `SQ_Dir`
  and `RN_Dir` at 0x32 / 0x33; record 0x34. The table goes to `0x96d4bc` and also
  `0x96d4c0` / `0x96d4c4`; the count (copied from a stack slot) to `0x96d4b8`.
* **FUN_0065b2e0** (composit) and **FUN_0065b350** (armtype): name ASCII 0x1f,
  token RAW4 at 0x20, record 0x34, no count (null). Tables to `0x96d4ec` / `0x96d4e8`.
* **FUN_0065b3c0** (free the token tables): frees the merged plr block `0x96d4dc`
  (TokTbls.cpp line 0xd6) and the merged obj block `0x96d4cc` (line 0xe2) through
  the pool; unloads monmode `0x96d4bc`, composit `0x96d4ec` and armtype `0x96d4e8`
  through the table free `0x6125c0`, and clears all five. The calls run in the
  order plr free, monmode, obj free, composit, armtype.
* **FUN_00663210** (free the missiles, MissileTbls.cpp line 0x19e): pool free of
  `0x96bc90` (cleared with the side counts `0x96bc94` / `0x96bc98`), unload of the
  list `0x96c794` and link free of `0x96c798` (both **left set**), then `0x96c79c`
  cleared.
* **FUN_00664150** (overlay, fastcall pool): link `0x96c7e8` (OverlayTbls.cpp line
  0x38), 26 columns (overlay NAME_I_WORD on the link, Filename ASCII 0x40 at +2, and
  the frame, offset, height, colour and flag fields), record 0x84, table `0x96c7ec`,
  count `0x96c7f0`.

Not covered here: FUN_0065adc0 (plrtype/plrmode) and FUN_0065b1b0 (objtype/objmode)
merge two compiled tables with AllocServerMemory, memcpy and two unloads; their
decompile needs the copy and unload helpers modelled, and is left for a later pass.

| Oracle | Functions |
|---|---|
| `tok_overlay.py` | `FUN_0065aef0`, `0065b2e0`, `0065b350`, `0065b3c0`, `00663210`, `00664150` |

### AnimTbls.cpp (`anim_tbls.py`)

`tools/emu/anim_tbls.py` runs the AnimData loader and the record free from stock
game.exe 1.14d (80 cases, `--mutation-check` 6 ways), on the field oracle's rig.

* **FUN_0066a770** (TXT_AllocTxt_AnimTbls, fastcall pool): a `0x4a4`-byte block
  (line 0x83), zeroed by memset; wsprintfA gives `DATA\GLOBAL\AnimData.d2`, which
  D2FILE loads with no size pointer (line 0x8a). The archive's address is stored at
  +0, then 256 slots from +4: slot k is the address of bucket k, the next bucket
  starts `4 + count * 0xa0` bytes after the one before. The defaults are
  `0x800` at +0x40c and `0x100` at +0x410. (The Ghidra output's bucket loop is
  garbled; the disassembly is the model.)
* **FUN_0066a830** (ANIMDATA_FreeAnimDataRecord, fastcall record): a null record
  does nothing; otherwise the client release of its records (+0, line 0xbe) and the
  pool free of the record (line 0xbf), in that order.

| Oracle | Functions |
|---|---|
| `anim_tbls.py` | `FUN_0066a770`, `0066a830` |

### TokTbls.cpp merges (`tok_merge.py`)

`tools/emu/tok_merge.py` runs the two token merges from stock game.exe 1.14d (160
cases, `--mutation-check` 8 ways). The compile hook returns one scripted table per
call, and the source tables are real blocks, since the merge copies from them.

* **FUN_0065adc0** (plrmode_plrtype, fastcall pool): compiles `plrtype`, then
  `plrmode` (record 0x34); allocates `(t + m) * 0x34` (line 0x37); copies plrtype
  then plrmode behind it; unloads both sources. Stores: Mode1 `0x96d4dc` = the
  block, Mode2 `0x96d4e0` = the block, Mode3 `0x96d4e4` = block + t * 0x34, count
  `0x96d4d8` = t + m.
* **FUN_0065b1b0** (objtype_objmode): the same with `objtype` and `objmode` (line
  0x94): ObjType `0x96d4cc`, ObjTypeMode `0x96d4d0`, ObjMode `0x96d4d4` = block +
  t * 0x34, count `0x96d4c8`.

| Oracle | Functions |
|---|---|
| `tok_merge.py` | `FUN_0065adc0`, `0065b1b0` |

### LvlTbls.cpp: expansion free, lvlsub free and portal array (`lvl_small.py`)

`tools/emu/lvl_small.py` runs three LvlTbls.cpp functions from stock game.exe 1.14d
(440 cases, `--mutation-check` 15 ways).

* **FUN_0061f9c0** (TXT_FreeTxt_LvlSub): N = `0x96ca14`. The DS1 file array at
  `0x96ca24`: each non-null slot released (`0x668300`, given the slot's address),
  then client-freed (line 0x3d8) and cleared. The sub-preset rows at `0x96ca10`
  (0x15c bytes each): room data freed (`0x670630`) for each of the N rows, then
  unloaded (`0x6125c0`) and cleared. The index at `0x96ca18` is pool-freed (line
  0x3e8) and cleared. The three parts are independent: the row loop runs even when
  the file array is null.

* **FUN_0061fbf0** (TXT_FreeExpansionLvlTbls): if `0x96ca1c` is set, pool-free it
  (line 0x43a); the pointer is **not** cleared (the Ghidra output agrees).
* **FUN_0061dd00** (LVLTBLS_BuildPortalLevelArray, cdecl, the LevelDefs row table
  in EAX): counts the rows whose dword at +0x8c is non-zero (rows of 0x9c bytes,
  the level count at +0xc5c of the Items table at `0x744304`), stores the count at
  `0x96c9f8`; with a non-zero count, allocates `count * 4` (line 0x16c) into
  `0x96c9f4` and fills it with the flagged row indices in order. A zero count
  leaves `0x96c9f4` alone. The fill's halt (line 0x177) needs a table that changes
  between the two loops, so it is not run.

| Oracle | Functions |
|---|---|
| `lvl_small.py` | `FUN_0061fbf0`, `0061f9c0`, `0061dd00` |

