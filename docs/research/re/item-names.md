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
| Defense: N | `FUN_00485ee0` | 3461 | armor, total > 0; N blue when ≠ base |
| Chance to Block: N% | `FUN_00485be0` | 11018, `"%d%%"` 0x6d9bec | shields: block column + stat 20 + class BlockFactor, cap 75; blue over the column |
| Smite / Kick Damage: a to b | `FUN_00485d40` | 3468 / 21782, "to" 3464 | Paladin + shield (no other class's) / Assassin + boots; armor.txt mindam/maxdam |
| Throw / One-Hand / Two-Hand Damage: a to b | `FUN_00485410`, `FUN_00485240` | 3467 / 3465 / 3466 | weapons: stats 21-24/159-160 (`FUN_0062d300`: low quality ×3/4 floor 1/2, ethereal ×3/2), min ×(100+18)%, max ×(100+17+219·clvl/8)% + 218·clvl/8; throw line for ItemTypes Throwable; Barbarian + 1-or-2-handed gets both; numbers blue when raised |
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

Not yet: set item / set bonus lists (`FUN_004e6410` flags), the gold
line (`FUN_00486670`), Holy Shield's block and smite, time-of-day stats
268-273, throwing potions' missile damage, charged skill details.
