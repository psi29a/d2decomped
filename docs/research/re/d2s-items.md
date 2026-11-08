# A save's item bitstream (`FUN_0062cbe0`, `FUN_0062a970`)

`ITEMS/Items.cpp`. The item reader checks the "JM" when its fourth
argument is set (a save), reads the 32-bit flag word, then hands compact
items (flag 21) to `FUN_0062a970` and the rest to `FUN_0062cbe0`. Both take
the **save file's version** as their last argument: the player load passes
the header's (+4), the client's item packets pass `0x60`
(1.14d). Every version check below compares that number, never the
item's own 10-bit version.

Read from game.exe's code in call order; d2d's reader is
`components/d2s/d2s_items.hpp` `detail::item`, checked on 21 real 1.14d
saves (`test_d2s`, every list ends on the next "JM").

## The extended item (`FUN_0062cbe0`), in read order

| Bits | Field |
|---|---|
| 10 | item version |
| 3 | mode; 3 or 5 (ground): 16 x, 16 y; else 4 body slot, 4 x, 4 y, 3 page |
| 32 | code (`TXT_Items_ConvertItemCodeToItemClassId`) |
| 3 | filled sockets |
| 32 | the item's seed, only in a save: the unit's init seed, and its rng is seeded from it |
| 7 | item level (below 1 becomes 1) |
| 4 | quality |
| 1 (+3) | picture variant |
| 1 (+11) | class-specific auto affix (shifted by the save version: ≤ 0x58 none, 0x59 +1) |
| by quality | low / superior 3; normal: below; magic 11 + 11; set 12; unique 12; rare / crafted 8 + 8 and six 1 (+11) affixes; tempered 8 + 8 |
| 16 | runeword (flag 26), stored as the first magic prefix |
| 3 + 7 + 7-bit name | ear (flag 16): class, level, the name to a 0 |
| 7-bit name | personalized (flag 24), to a 0 |
| 1 (+32 +32 (+32)) | realm data, only in a save past version 0x56; the third word past 0x5d |
| ItemStatCost row 31 | armour: defence, Save Bits less Save Add (11, 10) |
| row 73 (+ row 72) | armour and weapons: max durability; non-zero: durability (row 72's Save Bits, 9; 8 before version 0x60) |
| 9 | stackable: quantity (8 up to version 0x50) |
| row 194 | socketed (flag 11): the socket count (4) |
| | a packet stops here for an unidentified item |
| 5 | set items past version 0x54: which bonus lists follow |
| lists | the property list, then the set lists, then the runeword's, each to 0x1ff |

Normal quality: a charm (identified, or in a save) has 1 bit, then an
11-bit prefix (set) or suffix (clear); a body part that isn't a player's
has 10 bits; a scroll or a book (ItemTypes, not the code) has 5 bits.

Properties: 9-bit stat, then Save Param Bits and Save Bits less Save Add
from ItemStatCost. Stats 17 (+18), 48 (+49), 50 (+51), 52 (+53), 54
(+55, +56) and 57 (+58, +59, and stat 326 set to 1) read their partners
too. Saves before 0x5d had their own widths for a few stats
(item_addclassskills and its neighbours, item_singleskill's 14 bits);
d2d reads only 1.14d's.

## What this changed in d2d

- The bit d2d read after the name and kept as `bit_after` is the realm
  flag. When it's set, 3 words follow (1.14d), so d2d's reader would have
  lost its place in the bits. It now reads and writes them as the compact
  path already did (`detail::realm`, `write_realm`).
- The compact path and the realm data compared the item's own version;
  they compare the save's now (`Bits::file_version`). For 1.14d saves
  both numbers pass every check.
- A tome's 5 bits belong to normal quality and come before the runeword.
  Normal tomes are never runewords or personalized, so the bits on disk
  are the same.
- Durability is one 9-bit value from save version 0x60 (8 bits before);
  d2d keeps it as 8 + 1 (`dur_extra`), the extra bit only from 0x60.
- Saves before 0x5d take a second set of Save Bits (ItemStatCost's
  older column) for defence, durability, sockets and properties; d2d
  reads 1.14d's only (`ponytail:` in `detail::item`).
- Not ported (`ponytail:` in `detail::item`): a normal charm's affix and a
  monster body part's 10 bits. d2d has no ItemTypes hierarchy in the
  parser, and none of the 21 saves has one.
