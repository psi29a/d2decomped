# The Horadric Cube — game.exe 1.14d

The Cube's Transmute: cubemain.txt compiled at load, matched against what
the Cube holds, the outputs made. d2d: `components/rules/cube.hpp`
(`compile_cube_input` / `_output`, `cube_recipe`, `cube_match`,
`cube_make`), `World::transmute`, `tests/test_cube.cpp`.

## The button and the packets

- **The panel** (client state 0xe, `FUN_0048edf0`): `PANEL\supertransmogrifier`
  in four quarters (bottom-anchored at the left panel's x, bottom − 0xe0 /
  − 0x30), its grid, the close button (x + 0x113, bottom − 0x41), and the
  Transmute button `PANEL\miniconvert` (frame 0 up / 1 held) at x + 0x90,
  bottom − 0xbc. Its hit box (`FUN_0048a000`): x + 0x90 .. + 0xb8, bottom
  − 0xdf .. − 0xb7. Let go over it while held (`FUN_0048a190`): C→S
  **0x4f** `{u16 0x18, u16 0, u16 0}` (`FUN_00478600`, 7 bytes).
- **Opening**: right-clicking the Cube is an item use (C→S 0x20); the host
  opens it (the player's interaction 4) and answers S→C **0x77** action
  **0x15** (`FUN_004b8cf0` → `FUN_0048a460`: state 0xe). Shutting it sends
  0x4f `{0x17}`.
- **The host** (C→S 0x4f, `FUN_0054c7c0` → `FUN_00568060`): buttons 0x17 /
  0x18 only while the interaction is 4 → `FUN_00566ae0`: 0x17 ends it
  (`FUN_0055fa40`), 0x18 transmutes (`FUN_005665f0`).

d2d: `cmd::Transmute` (wire 0x4f 0x18); a joined game sends the host's
packets (`d2gs::c2s::button`), opens the panel on 0x77 0x15 and sends 0x17
when it shuts.

## The rows (FUN_00669130)

0x148 bytes a row; `DAT_0096d628`, `FUN_0066a1b0(i)` the row.

| off | column |
|---|---|
| +0 / +1 / +2 | enabled, ladder, min diff |
| +3 | class (ama..ass, −1 any) |
| +4 / +8 / +0xc | op, param (a stat name or number, `FUN_006690e0`), value |
| +0x10 / +0x12 | numinputs, version |
| +0x14 + 8i | input i (7) |
| +0x4c + 0x54k | output k (3: output, output b, output c) |

**An input** (`FUN_00668600`, 8 bytes): u16 flags, u16 the item (Items
index; 0xffff "any") or type, u16 a named unique / set row + 1, u8
quality, u8 qty. The head: "any"; four letters or fewer, an item type
(tried first, flag 2) or an item code (flag 1); else a UniqueItems index
name (flags 0x41, quality 7) or a SetItems one (0x41, quality 5). Then
`qty=N`, `low nor hiq mag set rar uni crf tmp` (quality 1..9), `nos` 4,
`sock` 8, `eth` 0x10, `noe` 0x20, `upg` 0x80, `bas` 0x100, `exc` 0x200,
`eli` 0x400, `nru` 0x800; an unknown word ends the list.

**An output** (`FUN_00668a90`, 0x54 bytes): u16 flags, u16 item / type,
u16 named row + 1, u8 quality, u8 qty (or sockets), u8 kind (1 Cow Portal,
2 Pandemonium Portal, 3 Pandemonium Finale Portal, 0xfc an item code
(tried first), 0xfd a type, 0xfe useitem, 0xff usetype), +9 lvl, +0xa
plvl, +0xb ilvl, +0xc three prefixes, +0x12 three suffixes, +0x18 five
mods of 12 bytes (u32 property, u16 param, s16 min, s16 max, u8 chance).
A unique's name: flags 8, kind 0xfc, quality 7, its lvl into ilvl; a set
item's the same with quality 5, and nothing parsed after it. Words: `qty=`,
`pre=` / `suf=` (up to three each), a quality word, `eth` 4, `sock=N` 2
(N in the qty byte), `mod` 1, `uns` 0x10, `rem` 0x20, `reg` 0x40 (kind
usetype), `exc` 0x80, `eli` 0x100, `rep` 0x200, `rch` 0x400.

1.14d's enabled rows: op only 28 (the quest item's difficulty); outputs 79
codes, 46 usetype, 18 useitem, the three portals; one name ("The Stone of
Jordan", an input).

## Which row (FUN_005665f0)

Rows in order: enabled (or the debug flag), ladder only in a ladder game,
version < 100 or an expansion game, min diff ≤ the game's, class −1 or the
player's, numinputs == the items in the Cube (`FUN_00564fd0`: location 3),
the row op 1..14 (the date: day of month in param..value, day of week;
the player's stats against value >> statshift); then every input
(`FUN_005658c0` → `FUN_00565010`). The first that takes is made
(`FUN_00565ab0`) and the search ends.

**An input** (`FUN_00565010`), over the Cube's items in the player's list
order, each item used once (a 48-entry used list): the item (exact class;
with `upg` its code's tier or one under it: the item normcode takes a
normcode input, ubercode norm / uber, ultracode all three) or the type
(`FUN_00629bb0`); the quality; the named row (`FUN_00629da0`); `nos` /
`sock` (socketed or not), `noe` / `eth`, `bas` / `exc` / `eli` (the item is
its base's normcode / ubercode / ultracode), `nru`; op 28: a questdiffcheck
quest item made in this difficulty or later (stat 0x164); the first input,
when output a is usetype / useitem with `sock`, an item that can hold
sockets (`FUN_0062bc20`); the first input's row op 15..27 (its stats). The
first input also feeds all three outputs, by **output a's** flags: its
ilvl, the item (for `mod`, usetype, useitem), usetype's base (output a's
`exc` → ubercode, `eli` → ultracode, when the version allows). It takes
when the count is exactly qty (1 if none), or at least qty once a
stackable item matched.

## What it makes (FUN_00565ab0)

Each output:

1. **Level**: lvl; else plvl% of the player's level + ilvl% of the first
   input's ilvl (each `x * pct / 100` with overflow care), 1 .. MaxLvl
   (experience.txt's, 99).
2. **Kind**: a portal (1..3) through the table at 0x6e11c8: 1 the Cow
   Portal (`FUN_00594140`: d2d's rules::CainQuest::cow_portal_opens), 2 and
   3 the Pandemonium portals, a bare `return 0` in 1.14d (`FUN_00594270`,
   `FUN_00594280`; the event was realm-only). A portal that returns 0 makes
   nothing and the inputs stay; one made takes them. Without `mod`:
   useitem copies the input (`FUN_0055a2a0`, its socket contents kept
   unless `uns` / `rem`; `tmp` quality gets new names, `FUN_005c1bc0`);
   else a new item (`FUN_00558d90`): usetype's base (with `reg`: the input's
   quality and named row, a unique not yet found reserved), the code, or a
   type's random item (`FUN_00565930`: from a random Items row on the game
   seed, one lap of spawnable, version-allowed rows of the type up to the
   level, ≤ 256, one of them on the game seed); the quality, the level,
   the named row, the forced prefixes / suffixes, `sock` without a count /
   with one (creation flags 0x10 / 8), `eth` (4, else 2). With `mod`: the
   input copied, its class changed to the kind's, re-made (`FUN_00557ab0`).
3. `rem`: the socketed items come out as their own items (`uns` drops them).
4. **Mods** 1..5 (a property ≥ 0): a chance 1..99 rolls rand(100) on the
   item's own seed (applied when the roll ≤ the chance); `FUN_00660240`.
5. `rep`: a stack refilled to min(qty, its max); durability to its max
   (stat 0x48), or the broken flag cleared (`FUN_0055f900`). `rch`: charges
   (`FUN_0055fe80`).
6. Without `sock`: qty= on a stackable item, min(qty, its max). With
   `sock=N`, when it has no sockets yet and isn't socketed: N, at most
   `FUN_0062bc20`; magic / tempered at most 3, set / rare / unique /
   crafted 1.

Then, if anything was made: every item in the Cube goes (`FUN_00564f30`),
the player's event (`FUN_00553380`), and each new item into the Cube
(`FUN_00560200`), else on the ground by the player (`FUN_00555600`). The
Horadric Staff ("hst") calls `FUN_0059e5c0` (record 9 data+0x2a, the part
counts, quest 10 bit 11); Khalim's Will ("qf2") `FUN_005b86e0`.

## Gaps

d2d makes the items through `generate_item` (its rare roll isn't game.exe's
yet), so: crafted (`crf`) is made rare and then called crafted; forced
`pre=` / `suf=` aren't applied; `mod`'s new base doesn't recompute its
stats; `rch` doesn't refill charges; useitem / mod outputs roll their mods
off a fresh seed, not the item's own; op 28's
difficulty stat isn't checked; the auto placement is d2d's first fit. A
valid recipes play `cursor_convert_ready`, then a successful transmute plays
`cursor_convert_item`. Checked live (devctl, 2026-10-11): the Horadric Staff (bit 11) and
three chipped rubies → a flawed ruby.
