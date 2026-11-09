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
| `FUN_00638cd0` | Is ItemTypes row a one of b (its Equiv1 / Equiv2 walked, at most 0x7d deep) |
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
| `FUN_00654f20` | Loads monitempercent (4: HeartPercent, BodyPartPercent, TreasureClassPercent, ComponentPercent) |
| `FUN_00654fc0` / `FUN_00654fe0` | Free / row (fatal 0x6dc, 0x6dd). The row has no caller |
| `FUN_00655030` | Loads monumod (0x20) |
| `FUN_006552e0` | Loads superuniques (0x34): the first row of each hcIdx below 0x42 into `0x96c710`; a hcIdx with none is fatal (0x782) |
| `FUN_00655710` | The superunique count |
| `FUN_00655720` | Loads hireling (0x118): NameFirst / NameLast strings at `+0x114` / `+0x116` (checked in the expansion: lines 0x816..0x818), the first row per (version 0 / 100, `+4`) into `0x96bdd8` |
| `FUN_006562b0` | Frees it and clears that index |
| `FUN_00656390` | (expansion, name string, after): the next hireling row of that version whose name range holds the string |
| `FUN_00656440` | (expansion, id, after): the next row of that version with `+8` == id |
| `FUN_00656620` | Loads npc (0x4c) |
| `FUN_00656900` | The npc row of a monster class (0x4c rows, `+0`) |
| `FUN_00656930` | Loads monsounds (0x94) |
| `FUN_00656f90` | A monsounds row |
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
