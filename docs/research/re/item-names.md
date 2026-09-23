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
