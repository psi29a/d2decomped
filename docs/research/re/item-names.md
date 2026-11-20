# Item names — save IDs to 1.14d tables

How a .d2s item's quality data (components/d2s/d2s_items.hpp) turns into
its displayed name. Verified by matching every item in 19 real 1.14d saves
against the 1.14d tables (installer-patched, see mpq-archives.md): each
mapping below is the only one of the candidates tried where every item's
row is allowed on that item (affix `itype` columns vs the item's
ItemTypes ancestry; unique/set `code`/`item`; runeword rune sequence vs
the socketed runes).

| quality        | ID bits            | row                                                        |
|----------------|--------------------|------------------------------------------------------------|
| 4 magic        | 11 prefix, 11 suffix | MagicPrefix / MagicSuffix **raw** data row — the blank "none" row is 0 and "Expansion" separators count (252/252, 223/223) |
| 5 set          | 12                 | SetItems row, separators dropped (36/36)                    |
| 7 unique       | 12                 | UniqueItems row, separators dropped (91/101; the other 10 are cube-upgraded bases, e.g. Shaftstop on an elite `uhn`) |
| 6 rare, 8 crafted | 8 + 8           | RarePrefix row `id - 156`, RareSuffix row `id - 1` (88/88)   |
| runeword       | 12                 | Runes.txt `RunewordN` rows sorted by N (80 and 96 don't exist): rank `id - 27` (11/11; 2718 is Delirium's special ID) |

Name strings are the row's key column (`index`, `Name`, `name`) looked
up in patchstring → expansionstring → string; base names are
armor/weapons/misc `namestr`. Two-line names (set, unique, rare,
runeword) show the base item on the second line.

## Property lines

The rest of the hover text comes from the item's stat list, described by
ItemStatCost's `descfunc`/`descval`/`descstrpos`/`descstrneg`/`descstr2`,
highest `descpriority` first (d2d `prop_lines`). Repeats of a
(stat, param) sum first, like the unit's stat list. `dgrp` groups
(all resistances, all attributes) print once when every member is
present with the same value. Min/max damage pairs are hard-coded to the
`strMod*DamageRange` strings ("Adds %d-%d fire damage"), poison scaled
by length/256 over length/25 seconds; 17+18 with equal values become
"+X% Enhanced Damage". Per-level stats (op 2..5) show `value * clvl >>
op param`.

Filled sockets add their bonuses: a jewel its own properties, a gem or
rune its gems.txt `weaponMod`/`helmMod`/`shieldMod` row (shield when the
parent's ItemTypes ancestry reaches `shld`), each mod code resolved
through Properties.txt (`func` 1/3 = value, 15/16 = min/max, 17 = param,
5/6/7 = min/max/% damage). Runewords get their rune bonuses this way —
the save only stores the runeword's own list.

## Hover text

game.exe's `FUN_0048dd90` (UI\inv.cpp) builds an item's tooltip as one
string drawn bottom-up: each helper appends its line(s) after the ones
below it, so the call order is bottom to top. Top line first (d2d
`item_lines`):

| Line | Function | String | Shown when / colour |
|---|---|---|---|
| name | `FUN_0048c060` | — | quality colour (as before) |
| 'rune letters' | `FUN_00486670` | 20506 RuneQuote `'` each side, then 3998 newline | an item that takes sockets (items +0x137, `FUN_00629900`), runeword or not: each socketed item of type 0x4a (rune) whose items +0xf0 names a gems.txt row (`FUN_006372c0`, 0xc0-byte rows) adds that row's letter (+0x20, up to 6 chars), in the item's inventory list order (`FUN_0063b2c0` / `FUN_0063dfa0`); no rune, no line. It sets no colour of its own; `FUN_0048dd90` gives it one as it joins it: `FUN_004521c0(buffer, 4)` (EDX 4 at 0x48ebac) puts colour code 4 (gold) in front, where the other helpers' buffers get 0 (white). Its buffer (ebp-0x2434) is the last one `FUN_0048dd90` adds to the bottom-up string before the name's (0x48ebb1; the helpers fill their own buffers, joined at 0x48ea21..0x48ebc4), so it shows just under the name, in gold |
| Defense: N | `FUN_00485ee0` | 3461 | armor, total > 0; N blue when ≠ base |
| Chance to Block: N% | `FUN_00485be0` | 11018, `"%d%%"` 0x6d9bec | shields: block column + stat 20 + class BlockFactor; Holy Shield (state 0x65, skill 0x75) adds dm56 (`FUN_00645c10`, Param5/Param6) before the cap of 75; blue over the column |
| Smite / Kick Damage: a to b | `FUN_00485d40` | 3468 / 21782, "to" 3464 | Paladin + shield (no other class's) / Assassin + boots; armor.txt mindam/maxdam. A shield adds Holy Shield's MinDam..MaxDam (`FUN_00647bc0` / `FUN_00647d00` >> 8) while state 0x65 is on. Boots do not |
| Throw / One-Hand / Two-Hand Damage: a to b | `FUN_00485410`, `FUN_00485240` | 3467 / 3465 / 3466 | weapons: stats 21-24/159-160 (`LAB_0062d300`: low quality ×3/4 floor 1/2, ethereal ×3/2), min ×(100+18)%, max ×(100+17+219·clvl/8)% + 218·clvl/8; throw line for ItemTypes Throwable; Barbarian + 1-or-2-handed gets both; numbers blue when raised |
| Throw Damage: a to b (potions) | `FUN_00485410` | 3467, "to" 3464 | type tpot (`FUN_00629bb0` 0x26), in place of every other damage line: the thrown missile (`FUN_006288a0`) at level 1, element (`FUN_0064b100` / `FUN_0064b1d0`; poison / max(ELen (`FUN_0064b2a0`) / 25, 1)) + physical (`FUN_0064af20` / `FUN_0064aff0`), >> 8, max ≥ min; label white, numbers by EType (table 0x485bc4: fire 1, lightning 4, cold 3, poison 2, else 0); one number when equal. Oracle `tools/emu/throw_potion_text.py` |
| Quantity: N | `FUN_00486100` | 3462 | identified, not socketed, stacks |
| spelldesc | `FUN_00486370` | misc.txt spelldescstr | replaces quantity; mode 2 scales potions by class (`FUN_0062a5d0` life, `FUN_0062a620` mana: ×1.5 Ama/Pal/Asn, ×2 Barb life / Sor/Nec/Dru mana) |
| Keep in Inventory to Gain Bonus | — | 20438 | charms |
| socket filler | `FUN_004865d0`, `FUN_004e6850`, `FUN_004e67d0`, `FUN_004e6410` | 11080; 11075/11076/11073/11074 | type sock; gems/runes add Weapons/Armor/Helms/Shields groups (gems.txt), the label + " " (3995) before each group's top line |
| Durability: a of b | `FUN_00484e90`, `FUN_00629930` | 3457, "of" 3463 | durability column, not nodurability / indestructible / throwable; b blue with stat 75 |
| (Class Only) | — | 10917 + class | class items; red for another class (`FUN_0062eaf0`) |
| Required Dexterity / Strength: N | `FUN_00485170` / `FUN_004850a0` | 3459 / 3458 | weapons/armor: column + column·stat91/100 − 10 ethereal; red when unmet |
| Required Level: N | `FUN_00484ff0`, `FUN_0062b5b0` | 3469 | identified, > 1: affix/set/unique levelreq (crafted +10+3/affix, ≤ 98), base, sockets, + stat 92 |
| Class - Speed | `FUN_004861d0`, `FUN_0062a710` | class table 0x721eb0, " - " 3996, 4088 + band | weapons with a player: frames<<8 / ((100+IAS−WSM)·animspeed/100) of the class's A1 anim, banded by DAT_00721f10 (rows 10..27) and column 0x722078; speed blue with stat 93 |
| Unidentified | — | 3455 | red |
| properties | `FUN_004e6410` | | blue |
| Ethereal (Cannot be Repaired), Socketed (N) | `FUN_00484b10` | 22745, 3453 | blue |

Not yet: charged skill details.

**The name's colour.** `FUN_0048dd90` keeps it in `[ebp-4]` and puts it in
front of the name's buffer (ebp-0x3c34) with `FUN_004521c0` at 0x48ebf7. By
quality (`FUN_00627e70`, jump table 0x48edd8): 4 magic 3 (blue), 5 set 2
(green), 6 rare 9 (yellow), 7 unique 4 (gold), 8 crafted 8 (orange), 9
tempered 10; any other quality 5 (grey) when socketed (flag 0x800, 0x48e87d)
or ethereal (0x400000, 0x48e89c), else 0 (white). Then (0x48e9b0..0x48ea15)
it is 8 (orange) when the items.txt code (+0x80) is `ceh` `bet` `tes` `fed`
(the essences), `toa` (Token of Absolution), `dhn` `bey` `mbr` (the organs),
`pk1` `pk2` `pk3` (the keys), or the item is of itemtype 0x4a (rune,
`FUN_00629bb0`), whatever its quality. Last, item flag 0x100 makes it 1 (red,
0x48ebe7). The floor label (`FUN_004527d0`, UI\ui.cpp) maps quality the same
way, then puts 4 (gold) on a quest item (items.txt +0x12a, not `leg `), then
the same orange list. d2d: `item_lines` gives the orange (test_orange_names,
cited from the asm, no oracle run). Not ported: the red flag, the floor
label's quest gold. Not traced: the flag-0x10 branch at 0x48e8b2 that can
set `[ebp-4]` to 0.

**A set item** (`FUN_0048d1d0`, UI\inv.cpp; `FUN_0048dd90` hands it an
identified quality-5 item, 0x48d1c4): its own lines as above, then, appended
in this order to the bottom-up string (so shown in the reverse, last on top):
the members, each `FUN_0048be80` of 0x2769 "%0" with its SetItems name
(+0x24) and a newline, coloured green (2) when `FUN_00486770` finds the
owner's piece of that row, else red (1), in the set's member order (Sets.txt
record +0x110, count +0xc: SetItems row order; read from game.exe's loaded
tables); the set's name (record +2) in gold (4) and a newline; when the item
is worn (unit +0x10 == 1, 0x48d870) and `FUN_004e6680` finds the owner's
stat list of that set (one of the six list ids at 0x6dbd70 whose stat 0x47
is the set's), a newline and that list in gold; a newline; the item's own
set lists that are on (`FUN_004e6560`, the SetItems add func +0x87: 1 per
member worn, 2 by the count) in green; the properties (`FUN_004e6410` flags
0x400) in blue. `FUN_004e60a0`'s lists end in a newline (`FUN_004e6410` trims
its own last one). `FUN_00486770` walks the owner's item list for an
identified set item of that SetItems row on page 0 (inventory), 3 (cube), 4
(stash) or none (worn), grid kind (`FUN_0063e020`) 1, 3 or 4. The owner is the
hovered unit (`FUN_0048dd90`'s param); a merc's own items for its sheet.
d2d: `item_lines`' `owner_items` (test_set_tooltip). Cited from the asm
(0x48d889..0x48d9fe), no oracle run. Time-of-day stats (268-303,
Properties `*/time`) are on no 1.14d item: no UniqueItems, SetItems, Sets,
Runes, CubeMain, Automagic, Gems or QualityItems row names one, and the
MagicSuffix rows that do (of Dawn, of Sunlight) have spawnable 0.

## Sockets

- Inserting (C→S 0x28, `FUN_0054b650` → `FUN_00562660`): the held item
  must be a socket filler (item type 53 `sock`: gems, runes, jewels) and
  identified; the target stored or worn, identified, socketed (0x800),
  able to hold sockets (Items +0x137) and with a free one.
- Runewords: after the filler goes in, `FUN_0062bed0` looks up the Runes.txt
  row (`FUN_006394a0`, 0x120 bytes: +0x80 complete, +0x81 server, +0x86
  itype1..6, +0x92 etype1..3, +0x98 Rune1..6, +0xb0 T1Code/Param/Min/Max
  x 7): not quality 4..9, not a quest base (Items +0x12a), every socket
  filled (`FUN_006299b0`), the fillers' classes Rune1.. in order, none of
  the etypes and one of the itypes (`FUN_00629bb0`). A `server` row needs a
  ladder game (game +0x74, `FUN_00562660`). `FUN_006600a0` (unless the item
  has stat list 0xab / 0x40) sets item flag 0x4000000 and puts each T1 mod
  through `FUN_0065fd70` kind 6 into that list. d2d: `rules::runeword_row`,
  `make_runeword` (the save id as item_text reads it; the seed is the
  item's fingerprint).
- The hover overlay (`FUN_00483e40`, a hovered identified socketed grid
  item, `Panel\GemSocket` = `0x7bef14`): per socket i a place in half
  cells from `0x6d91e8`, 48-byte records `[sockets-1][width-1][height-1]`
  of six {col, row}; the socket at x = (col - 1) * box_w / 2 + left - 1,
  bottom (row + 1) * box_h / 2 + top, the filler's inventory art over it.
- Worn items (`FUN_0048b540`): the same, from the item's cells centred
  in its body slot (the grid's cell size).
