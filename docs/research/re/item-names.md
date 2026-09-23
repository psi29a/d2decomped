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
