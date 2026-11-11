# Drops — treasure classes and item quality (1.14d game.exe)

Our port: `components/rules/drops.hpp` (`tc_upgrade`, `roll_drops`,
`roll_quality`, `made_quality`, `stand_quality`, `chest_round`,
`gold_amount`, `add_auto_treasure`), `components/rules/shrines.hpp`
(`open_container`, `stand_item`), loaded in
`components/game/gamedata/gamedata_load.cpp`, used by `components/game/loot.hpp`
(kills) and `world/` (chests). Checked against game.exe by
`tools/emu/drops.py` (see the end) and `tests/game/test_game_tables.cpp`.

## Tables

- Runtime classes: `DAT_0096c5ec`, stride 0x2c, count `DAT_0096c5f0`
  (1013). 0 is empty; 1..160 the auto classes; 161 + row the
  TreasureClassEx rows (852; the loader FUN_006547d0 stops at the first
  empty name).
- Record: `+0` group (s16, offset by the auto groups `DAT_0096d4b0`), `+2`
  level, `+4` entry count, `+8` classic total, `+0xc` expansion total,
  `+0x10` picks, `+0x14` NoDrop, `+0x1a` six u16 modifiers (unique, set,
  rare, magic, and two flag chances always 0 from the txt), `+0x28`
  entries.
- Entry (0x1c): `+4` expansion running start, `+8` u16 item / class index
  (0xffff: none), `+0xa` mul (gold) or unique / set index, `+0xc` flags (1
  unique item, 2 set item, 4 a class), `+0xe..` classic modifiers.
  Entries under Prob 1 aren't added (FUN_00654440).
- Auto classes (FUN_006541c0): for each ItemTypes row with TreasureClass
  (bow, weap, mele, armo, abow in row order), `<code>N`, N = 3..96 step 3,
  level N-3: the spawnable non-quest Weapons / Armor / Misc rows (in that
  order) of the type (type or type2 inheriting it) with qlvl N-2..N,
  weighted by their type1's Rarity; tpot bases only in a tpot class.
  Empty classes stay (bow15: nothing drops, no draw).

## Choosing the class — FUN_005a6600, FUN_0055afa0

- ilvl: FUN_00558200 — a monster's stat 0xc (its level), a player's base
  stat 0xc, at least 1; any other unit (or a null unit) takes the area's
  monster level (FUN_0061dca0).
  Read whole (cited, decompile only; ECX unit, EDX an area used when the
  unit is null): unit null and EDX 0 → 1. A monster (type 1): stat 0xc,
  `FUN_00625480`. A player (type 0): the **base** stat 0xc,
  `FUN_006253b0`. Any other unit: its room's area (`FUN_00620bb0`,
  `FUN_0061a1b0`), as EDX is for a null unit. Those two last take the
  area's monster level for the game's difficulty (+0x6d) and expansion
  (+0x70), `FUN_00554010`, and the result under 2 is 1. **No +4 anywhere
  in it**: the "+4 past level 5" belongs to Charsi's imbue caller
  (`FUN_00579d60`, which adds 4 to the result when it is above 5), not to
  `FUN_00558200`. `FUN_005466b0` (a quest item given with ilvl 0) uses it
  plain. d2d: `rules::player_ilvl` (rules.hpp, "FUN_00558200") adds the 4,
  and `quests/act1.cpp:144` gives Akara's deciphered scroll that ilvl:
  DISAGREES for a player past level 5 (the scroll is a quest item, so the
  effect is its stored ilvl only; the imbue use is right). d2d has no
  monster / area branches of this function as one routine.
- Class: superunique SuperUniques TC (`+0x2c + diff*2`), unique
  TreasureClass3, champion TreasureClass2, else TreasureClass1 (MonStats
  `+0x86 + diff*8`, one u16 each).
- TreasureClass4 (`+0x8c`) instead while MonStats TCQuestId (`+0x9e`) is
  set and the killer's quest has none of flags 15, 1, TCQuestCP (`+0x9f`)
  (FUN_0065c310). Andariel, Duriel, Mephisto, Diablo.
- FUN_0055afa0 moves it on by the monster's level only in expansion
  (`game+0x70`), past normal (`game+0x6d`), for a monster (unit type 1)
  whose MonStats `+0xc` has neither 4 (noRatio, mask `0x6ce270`) nor 0x40
  (boss, `0x6ce280`).
- FUN_00654e00(index, lvl): while the next row has the same nonzero group
  and lvl >= its level, move to it. Levels needn't rise: group 18 runs all
  90 "Super" rows, 0..96 twice, so lvl 96 ends on "Act 5 (H) Super Cx".

## Rolling — FUN_0055a6d0

`(ECX game, EDX dropper; killer, class, forced quality, ilvl, noNoDrop,
out array, count ptr, max = 6)`. A frame stack (64 deep) of
{class, picks left, modifiers}; pushing a sub-class when the frame has no
picks left replaces it, so it is plain recursion. All draws come off the
dropper's seed (unit `+0x20`, the 0x6ac690c5 LCG; rand(n): n < 1 → 0 with
no draw, a power of two masks, else mod).

1. picks = |picks|, at least 1. The modifiers of a pushed class are the
   max of its own and its parent's.
2. NoDrop with players: n = near + (players - near) / 2, near = 1
   (the killer's nearby party, FUN_005408e0, 2..8, when there is one),
   players = FUN_00535790 (max of players in game and `/players`,
   `DAT_00883d70`). Capped by the dropper's stat 100
   (monster_playercount, set at spawn by FUN_00573cb0, at least 1) when
   it's a monster (FUN_0044be50: the unit type, 1). d2d: `Monster::players`. For n > 1: p = nd / (nd + total), q = 1 - p^n,
   nd = q == 0 ? 0 : trunc((1 - q) * total / q) (x87 at 53 bits, so
   doubles match).
3. Each pick (picks >= 0): r = rand(nd + total); r < nd drops nothing;
   else the entry whose running start holds r - nd (binary search).
   Negative picks: no draw, the k-th pick (k = 0..) is the entry holding
   k, stopping once k reaches the total (the Countess).
4. A class entry is pushed. An item: quality = forced, or 7 / 5 for a
   unique / set entry, else FUN_00558640. Two more draws only for nonzero
   flag modifiers (never). FUN_0055a550 makes it; gold with a mul has its
   coins (stat 0xe) set to coins * mul >> 8. A made item counts (one
   FUN_00555da0 finds no floor for isn't made and doesn't); at `max` the
   roll stops. The count goes up first (0x55af4c): only a drop that
   leaves it under `max` reaches FUN_005589a0 (0x55af59), which for gold
   (type index 4) sets coins = coins * (100 + gf) / 100, gf the killer's
   stat 0x4f (FUN_00625500) plus its owner's (FUN_0058f0d0). `Loot::drop`
   passes the gear's 0x4f, 0 for the drop that fills 6.

## Quality — FUN_00558640

`(ECX ilvl, EAX item; game, dropper, killer, modifiers)`, draws
FUN_0045c3e0 off the dropper.

- type1 Normal (`+0x16`) → 2 normal. Items `+0x129` (only unique) → 7.
  Type Magic (`+0x14`) and a quest item → 7.
- ItemRatio row (FUN_00637910): Class (the type has a Class,
  FUN_00629f70) and Uber (FUN_0062b4d0: a weap / armo whose code is its
  ubercode or ultracode, not tpot, not quest) must match; the highest
  Version <= 100 wins (later rows on ties).
- mf = FUN_005585d0 (killer's stat 0x50 plus its owner's). mf < -99
  skips to superior.
- d = ilvl - qlvl (`+0xfd`). For unique, set, rare (Rare types only):
  odds = (base - d / divisor) * 128; with mf, odds = odds * 100 / eff,
  eff = FUN_00558610(k = 250 / 500 / 600): x = mf + 100, x < 111 ? x :
  (x - 100) * k / (x - 100 + k) + 100. Magic uses x undiminished. Then
  odds = max(odds, min), odds -= modifier * odds / 1024; odds <= 0 or
  rand(odds) < 128 wins: 7, 5, 6, 4. A Magic type is 4 after set / rare.
- Superior: hq = (base - d / divisor) * 128, hq <= 0 or rand(hq) < 128 →
  3. Normal: nm < 1 or rand(nm) < 128 → 2, else 1 low.

## The dropper's seed at death

A kill (FUN_0057ccb0) sets mode DT (FUN_005a7c20); DT's start
(FUN_005a6ff0, table `0x6e2260`) → FUN_005a6520 → FUN_005a6830 →
FUN_005a6600 → FUN_0055afa0 → FUN_0055a6d0. Nothing on that path draws on
the monster's unit seed (a call-graph scan 7 deep; FUN_005a4f50's
elemental draw needs El mode == DT, and El mode 0 is none), so the roll
starts from the seed as the monster's life left it. Find Item
(FUN_005a8000 → FUN_005a6600, noNoDrop 1) rolls on from where the kill
left the corpse's seed. A monster's unit seed is a step of the game seed
(`game+0xd0`) when it is made (FUN_00555230 → FUN_00552df0: {low, 666}).

## Making the item — FUN_0055a550, FUN_00558d90, FUN_00557ab0

What the decompile of FUN_0055a550 shows by itself (cited, decompile only;
most of the record's fields are written by instructions it hides, so the
list below is the doc's, not re-read): ECX is the item index and it returns
at once for one under 0; the dropper (ESI) gives its spot
(`FUN_00620870`) and room (`FUN_00620bb0`) to `FUN_00555da0` (1, 1), and a
zero result returns (nothing is made, no draw); the 0x84-byte record is
zeroed (`_memset`); the level it takes is the same three-way read as
`FUN_00558200` (below, "Choosing the class"): a monster dropper's stat
0xc, a player's **base** stat 0xc (`FUN_006253b0`), any other unit's area
monster level for the game's difficulty and expansion; then
`FUN_00558d90(0)` makes the unit. There is no level floor of 1 in this body
(`FUN_00558200` has one; where this record's +0xc is clamped is not
visible here, `FUN_00558d90` clamps it, see below). d2d: `objects.cpp:81`
uses the area level for a chest, agreeing for a type-2 dropper (not
verified here against `d2d_ai_area_monster_level`'s table); a player
dropper (base stat 0xc) is not ported.

FUN_0055a550 (ECX item index, ESI dropper; game, quality, unique / set
index + 1, flags) places it first (FUN_00555da0, no draws; none free →
nothing made), then fills a 0x84-byte struct: +0xc ilvl (the dropper's
level, an object's area level), +0x2a / item data +0x30 version
(`game+0x78`: 101 in expansion), +0x30 quality, +0x40 forced index,
+0x80 flags | 1 for a Hell Bovine (MonStats 0x187) dropper.

FUN_00555230 makes the unit: two steps of the game seed. FUN_00552df0:
unit +0x20 = {low, 666} (the unit seed), +0x28 = low; FUN_00552e90: item
data +0x10 = low, +4 = {low, 666} (its own seed, FUN_00627d90).

FUN_00557ab0 (ECX game, EDX &item; struct, do quality) off the unit seed:

- Gold (type 4): coins = ilvl + rand(5 ilvl), at least 1 (ilvl at least
  1); struct +0x54 > 0 forces it. The only draw.
- Arrows / bolts (ItemTypes Quiver, `+0xe`): quantity min + rand(max -
  min) (maxstack + stat 0xfe), at least 1.
- Armor (`armo`): durability rand(dur / 2) + dur / 2 (at most 255), max
  dur (Items `+0x112`); defence (stat 0x1f, FUN_00556360) minac +
  rand(maxac - minac + 1).
- Weapons (`weap`): a stackable's quantity min + rand(max - min), then
  durability as armor's; FUN_005563d0 draws nothing.
- Misc stackables: min + rand(bound - min), bound = spawnstack (Items
  `+0xec`), or max(min, max) when it's 0 or under min.
- Then rand(VarInvGfx) (ItemTypes `+0x23`) for its picture, then quality
  (FUN_00557450) when asked.

FUN_00557450: a drop's quality is forced (struct +0x30; FUN_00556f60
draws nothing at version != 0). The unique (FUN_005566b0) and set
(FUN_005c2940 → FUN_005c25c0) picks are the first draws on the item's own
seed (FUN_00650e50 before them only reads it):

- Unique: UniqueItems rows (`+0xc24`, stride 0x14c) with version < 100
  (or an expansion item), enabled (flag 1), not ladder (flag 8) outside a
  ladder game (`game+0x6a` / `+0x74`), the item's code and lvl <= ilvl;
  weight rarity, at least 1. A forced index among them wins with no draw;
  else rand(total). One found already (bit set in `game+0x1b24`) fails
  but for a quest item (Items `+0x12a`); FUN_00556530 sets the bit unless
  nolimit (flag 2).
- Set: SetItems rows (`+0xc18`, stride 0x1b8), version as above, the
  code, lvl <= ilvl, set 29 (Cow King's Leathers) only with flag 1;
  weight rarity, 0 counts 1; rand(total), then subtract.
- A failed unique goes rare with durability x3, a failed set x2.

### The quality roll, expansion and classic — FUN_00556f60

(cited, decompile only, no disassembly or oracle run.) `FUN_00556f60(item,
record)`; `v` is the item's version (`FUN_0062a670`, item data +0x30: 0 in a
classic game, non-zero in an expansion one, see above).

1. `v != 0` and record +0x30 non-zero: that is the quality, no draw.
2. The ItemRatio row is `FUN_00637910(item class, game+0x6d difficulty,
   v == 0 ? 0 : 100)` (the highest Version row not above the argument; any
   other version argument is the fatal id 0x61d). No row: fatal id 0x486; no
   Items row for the code: fatal id 0x489; no game: id 0x482. An Items row
   with +0x12a (quest) set returns 2 at once.
3. The qualities are tried in turn from the table of six words at 0x6e1138
   (its words are not read here; the order unique, rare, set, magic,
   superior, normal is the one above, as case 7, 6, 5, 4, 3, 2). Each has a base `b` and divisor `d` in the row (case 7
   ints 0 / 1, case 6 ints 3 / 4, case 5 ints 6 / 7, case 4 ints 9 / 10,
   case 3 ints 0xc / 0xd, case 2 ints 0xe / 0xf). A try wins when `rand(b -
   past / d) == 0` (the item's own seed, `FUN_00627d90`, via
   `FUN_0045c3e0`).
4. **Expansion (`v != 0`)**: `past` = record +0xc (the ilvl) minus Items
   +0xfd (qlvl), and 1 when that is under 1 **or when the item is of
   ItemTypes row 0x34** (`FUN_00629bb0(item, 0x34)`, an is-a test on the
   type's equivalence bits). Each of the six qualities in the order table (7 to 2) divides. When the
   computed odds `b - past / d` are under 1 (after the level adjustment, not the base `b`), the try wins
   **without a draw**.
5. **Classic (`v == 0`)**: `past` is the raw ilvl (no qlvl taken off). Only
   normal, superior and magic (cases 2, 3, 4) divide; unique, rare and set
   (cases 7, 6, 5) are `b - ilvl`, no divisor. Computed odds (`b - ilvl` or `b - past / d`)
   under 1 become 1 and the try **still draws** (`rand(1)`, a step of the item's seed), then wins.
6. No quality won: `((record +0x80 & 0x40) | 0x20) >> 5`, that is 3
   (superior) with the bit and 1 (low quality) without.

d2d: `stand_quality` (components/rules/drops.hpp) ports the expansion path
only: it agrees on the try order, `past / d`, the no-draw win under 1 and the
superior fall-through. It differs from game.exe in: no classic path (no
version-0 ItemRatio row, qlvl is always taken off, divisors always, computed
odds `b - past / d` under 1 never draw, where classic sets them to 1 and draws); no ItemTypes row 0x34 override; and a failed round
ends in superior whatever the 0x40 bit (d2d's stands set it, so the common
case agrees; low quality, 1, is the other result in game.exe). The loader
(`gamedata_load.cpp:1034`) keeps the highest Version <= 100 row per class /
uber pair, as the expansion call does; it has no lookup for the version-0 row (it
only lands on one if a pair has no row above 0).
Re-check: `python3 tools/ghidra/fn.py 00556f60 00637910 00629bb0`.

## A new character's items — FUN_00534f10, FUN_00534d60, FUN_00534c70

FUN_00532590 (PlrSave.cpp, a new character) calls FUN_00534f10, which
walks the class's CharStats row (`[0x744304] +0xbc4`, stride 0xc4) over
its ten item slots at `+0x5c`, 8 bytes each: the item code, loc (a body
location, 0 none) at `+4` and count at `+5`. FUN_00534d60 makes each of
the count:

- FUN_00534c70 makes it (FUN_00559ce0: normal, ilvl from FUN_00558200).
  The first slot's item also gets stat 0x6b (item_singleskill) set to the
  row's StartSkill (`+0xac`, when it's a skill). Then a stackable item
  (Items `+0x132`, FUN_006289f0) gets stat 0x46 (quantity) set to
  FUN_006295b0: maxstack (Items `+0xe8`) + stat 0xfe, at most 511. A drop
  rolls its stack (above); a new character's comes full, so the Amazon's
  javelins (Weapons.txt maxstack 60) come as 60.
- A belt item (ItemTypes Beltable, `+0x19`, FUN_0062bad0) goes to the belt
  (FUN_0055e9b0).
- Else one with a loc is put there (FUN_00534bb0), or in the inventory
  when that fails (FUN_00534b30). One with no loc goes to the inventory.
- Then an arrow quiver (ItemTypes row 5, `bowq`, FUN_00629bb0) gets
  quantity 250 (`0x534e6b`), or 100 when it has no loc (250 at `0x534e98`,
  then 100 at `0x534ebe`). Any other item gets stat 0x48 (durability) set
  to FUN_00625e00, its max durability. Bolts (`xboq`, row 6) aren't
  `bowq`: they keep the full stack. No class's row in 1.14d has a quiver.

d2d: `new_character` (components/game/character/character_store.hpp) gives
a stackable start item its maxstack and a `bowq` 250 / 100, after
`generate_item`'s roll, and each item of the first slot item_singleskill
at 1 with the start skill as its param: FUN_00534c70 calls FUN_00627150
(the item's stat list, 0x6b, 1, the skill) once per item it makes, so a
first slot with a count of 2 gives both. In 1.14d's data only two classes
get it: Fire Bolt (36) on the Sorceress's staff, Raise Skeleton (70) on
the Necromancer's wand.

Checked by `tools/emu/new_character_stacks.py` (`jav 60`, `stat 0x6b 36 =
1`, `stat 0x6b 70 = 1`; with `quivers`: `aqv 250, aqv 100, cqv 250`),
`tests/game/test_new_character_stacks.cpp` and
`tests/game/test_new_character_start_skill.cpp`.

## Item mods — FUN_0065fec0, FUN_0065fd70

FUN_0065fec0 (item, row, kind: 1 superior, 3 unique, 4 set, 0 / 2 / 5
affixes, through FUN_0065fe10) runs each mod's Properties.txt row through
FUN_0065fd70: up to 7 (func, stat, set, val) entries, each func off the
table at 0x7462f8, called with ECX = kind and (item, mod, set, stat, val,
first, ...). `first` is what the first func returned; later funcs reuse
it. The value draw (FUN_0065e9e0) is min + rand(max - min + 1), none
when min == max; each func draws only when it needs one:

- 1 / 2 / 13 / 21 / 22 always; 12 / 36 draw the param. 3 / 4 / 5 / 6 / 7
  / 8 / 9 / 10 / 14 / 24 reuse `first` (0: a draw). 17 is the param (0: a
  draw). 15 / 16 are min / max. 11 / 18 / 20 / 23 don't draw; 19 draws
  the charges.
- FUN_0065ea50 stores (stat, param, value) on the item's list and
  returns the value; nothing (and 0) for no stat or a 0. Stat 58 (poison
  max) adds 326 = 1.
- FUN_0065ccc0 (ac rule): on func 2 / 4, and 1 / 3 / 13 when ECX is 1
  (superior), stat 16 / 31 on an armor with maxac sets the defence to
  max(def + 1, maxac + 1). It also handles the base damage stats 0x11 /
  0x16 and 0x12 / 0x15.
- 11 (FUN_0065f470), chance to cast: param skill * 64 + (level & 63),
  value min (5 if < 1). Level = max when > 0. With max == 0 it is
  (ilvl - req) / 4 + 1, at least 1, at most maxlvl (FUN_004aa8b0, Skills
  +300; 20 without). With max < 0 it is (ilvl - req) / max(-(max(99 -
  req, 1) / max), 1), at least 1. req = Skills reqlevel (FUN_00644710,
  +0x174; 0x7fffffff unknown).
- 19 (FUN_0065f6a0), charges: level as 11. c = 5 for min 0, -min + -min
  * level / 8 for min < 0, else min; clamped to 1..255. now = (rand(c - c
  / 8) + c / 8 + 1) & 0xff; value now + c * 256, param skill << 6 | level.
- 14, sockets: min(grid at most 6, FUN_0062bc20), the value `first` or a
  draw, else the param. Sets flag 0x800 and stat 0xc2.
- 23, ethereal (FUN_0065e4d0): flag 0x400000, damage stats and armor
  defence (31) x 3/2. Durability is untouched (FUN_00556ca0 halves it).
- A set (kind 4): 9 props, then aprop1a..aprop5b, slot k in pair (k >>
  1) + 2. With add func 0, every slot goes to the main list (state 0,
  flags 0x40). Otherwise state 0x6edb40[pair] (0xa5..0xa9), flags
  0x6edb5c[pair] (0x2040): a list of its own, the d2s set list k >> 1, on
  with (k >> 1) + 2 pieces worn. Those still draw and still take the
  func-2 ac rule (Milabrega's Robe's ac% in list 0 sets its defence to
  234).

`apply_mod` / `generate_item` (rules.hpp) follow this. The set lists go
to `Item::set_props` / `set_lists` / `set_list_sizes`, which
`set_bonus_props` (character.hpp) reads back.

## Magic — FUN_005565e0, FUN_005c1560 (2026-10-11)

FUN_00557450 case 4 (after the type flags: FUN_0062e9e0 always magic,
FUN_0062ea30 normal-only and the rest, `made_quality`): FUN_005565e0
picks a prefix (FUN_005c18e0 → FUN_005c1560 for an expansion item,
FUN_005c12f0 classic), then a suffix, forced when no prefix came. Each pick
on the item's own seed:

- one step, a coin: even and not forced → none;
- the affix level: max(ilvl, qlvl), then + MagicLvl, or without one − qlvl
  / 2 (2·level − 99 from 99 − qlvl / 2 up), clamped 1..99;
- the rows (0x90 bytes) of its section: spawnable, version < 100 or an
  expansion item, level ≤ alvl ≤ maxlevel (0: none), rare-capable or the
  item not rare / normal / superior, the type test (FUN_0065e620: an
  expansion item skips the stack / throw test; a first mod setting stat
  0xc2 (sockets) needs an item with an inventory and a socket count; no
  etype, an itype), frequency > 0, classspecific none / the item type's
  Class (7: any) / equal, no group the item's prefixes or suffixes have
  (FUN_005c1500);
- weight frequency (× level with MagicLvl); FUN_00472280 rand(total + 1),
  subtracted in row order, the last when it runs past; its mods at once
  (FUN_0065fec0).
- Type codes are compiled as four characters, space padded: "ring "
  (MagicSuffix "of Nova Shield") is a ring and "staff" (MagicPrefix
  Burning / Blazing / Sparking / Charged / Chilling / Freezing) a staf.

Then FUN_005c1260 (staff mods). Neither fitting: FUN_00557320 clears the
affixes, puts the own seed back to its start (item data +0x10), remakes it
superior (FUN_005c2ad0) and normal when that fails (FUN_005572a0). Func 14
(sockets, FUN_0065f590): the value (`first`, else a draw, else the param),
at least 1, at most the grid (6) and FUN_0062bc20; flag 0x800, stat 0xc2.

d2d: `generate_item` (quality 4 with a unit seed); the affix loader
trims type codes. `drops.py items 1-10000` (magic now through
FUN_00557450 too): 9994 / 9995 (the `elx` code, which `drops.py` skips: the row 0xd (Charm)
magic path, FUN_00556a60, not ported; whether `elx` is of that row is not traced).

## Chests — FUN_00585b90

The act's chest class by tier (area level against the act's two marker
levels: A / B / C = 0 / 1 / 2, `chest_tc`), rolled off the chest's unit
seed with ilvl = the tier: qualities roll at item level 0..2, while
FUN_0055a550 makes the items at the area level. At most 6 items; a forced
quality (EDX: a sparkling chest's 4 / 6, chest 397's) stands for every
pick, gold's too. It returns the first item made; FUN_0062a0f0 asks if
that is magic or better as FUN_00557450 made it (`made_quality`).

The unit seed: FUN_00555230 → FUN_00552df0, {a game-seed step, 666} when
the object is made (`objgroups.cpp` make: `Npc::seed`); a chest's InitFn
(3 / 57) re-seeds it to {object seed % 0xfffe + 1, 666} (`roll_chest`).
The containers' own draws (how many rounds, the undead, the sparkle) are
on the object seed (game +0x10f0, `Spawning::objects`, one a game).
A preset 580 (FUN_0054f370) is a sparkling chest; a gold placeholder
(InitFn 28) is ON once made, so its OperateFn does nothing. `open_container`:

- Chest (OperateFn 4, FUN_00585f60): a sparkling one (FUN_005540d0, unit
  +0x78) draws rand(100) < 5 ? 6 : 4 as its forced quality (object 397
  draws it too, then drops it); then rand(100) >= 25, or sparkling or
  locked, opens 1 round (2 locked); a sparkling one with no magic item
  tries up to 10 more until one is.
- Chest 397: rand(10000): < 1200 one unique / set / rare round (200 /
  600 / 1200), a second when the first isn't magic; < 3200 up to 10
  magic rounds, 3 magic items enough; < 6200 up to 10, 2 magic enough,
  then 7 - plain gold piles (FUN_00585970); else, or when those find
  nothing, 10 tries for magic, at least 4 rounds, 5 gold, 2 hp3, 2 mp3.
- "Magic" in both is FUN_0062a0f0 on the round's first made item (the
  item FUN_00585b90 returns): its quality (item data +0) 4..9. The
  quality is the one the item was made at (FUN_00557450): a unique with
  no row (FUN_005566b0 fails) is made rare (FUN_005573f0, x3 durability
  first), a set with none (FUN_005c2940 fails) magic (FUN_00557380, x2);
  rare falls to magic, magic to superior (FUN_00557320) and normal
  (FUN_005572a0) only when no affix fits (FUN_005c21a0 / FUN_005565e0
  fail). So a unique or set that finds no row still counts as magic,
  unless no affix fits it either (then superior or normal: not magic).
- Casket (1, FUN_00586410): a round; nothing, it stays shut; then the
  undead roll (`(next % 10000) & ~0x1fff`). Urn (3, FUN_005866c0): a round
  at rand(100) < 21. Barrel (5, FUN_005868a0): the undead roll, then the
  same. Corpse / crate (14, FUN_005867a0): a round. Bookshelf (26,
  FUN_00584060): rand(20) < 13 a scroll, else a tome, of town portal or
  identify (next & 1; FUN_00559a30).

## Stands — FUN_005594c0, FUN_00559630

An armor stand (OperateFn 19, FUN_005594c0) or weapon rack (20,
FUN_00559630) picks off its room's seed (room1 +0x6c; `Level::room_seeds`,
as its room's population left them) a spawnable non-quest armor / weapon of qlvl
<= ilvl (area level - 1, past 1) by Rarity (`stand_item`); the rack tries
6 times for a base with bitfield1 & 2 (FUN_00629cc0). FUN_00558d90 makes
it off the same room seed (unit seed, then own). FUN_00556f60 rolls its
quality, nothing asked for, the first draw on its own seed: unique, rare,
set, magic, superior, normal (ItemRatio rows 0, 2, 1, 3, 4, 5), each won
at rand(base - past / divisor) == 0, past = ilvl - qlvl at least 1;
none: superior (args +0x80 & 0x40); a quest base normal (`stand_quality`,
`Loot::put` for quality 0).

## Where a drop lands — FUN_00555da0

From (x + 2, y + 3), or (x, y) when that's off the rooms, the free
subtile nearest (`drop_spot`, FUN_0064e810 → FUN_0064dea0): no 0x3e01 there
and no 0x801 on expfield.d2's walk back to the dropper (FUN_0066a670);
rings 1..49, each side to side, the first nearest by Manhattan distance.
A landed item marks 0x200, so the next lands elsewhere. Object footprints
(FUN_006209d0): 0x400 (| 4 BlockMissile); 0x8000 for a SubClass 4
non-door; a door 0x806 if BlocksVis, else 0x808 with BlockMissile, else
0x400.

## Checks

- `cd tools/emu && uv run python drops.py 1-100000` runs FUN_0055a6d0 per
  job (seed, class, level, ilvl, players, mf; FUN_0055a550 hooked to
  record what it makes, FUN_005585d0 to answer mf) and diffs
  `build/tools/drop-dump` line by line, the dropper's seed after
  included. 100000 / 100000 match, every class but ROP (N) / ROP (H)
  (Annihilus, a unique entry).
- `uv run python drops.py tables`: every class's entries against ours,
  1012 / 1012.
- `drops.py act1` prints the Act 1 jobs `tests/game/test_game_tables.cpp` hashes.
- `uv run python drops.py items 1-40000`: a fake item unit takes its
  seeds off a game seed through FUN_00552df0 / FUN_00552e90, FUN_00557ab0
  rolls it (FUN_00627260 hooked to record the stats), FUN_005566b0 /
  FUN_005c2940 pick on a game whose one-per-game list carries on; diffed
  against `drop-dump items` (`Loot::put`'s path): both seeds, coins,
  stack, durability, defence, the pick, the game seed after. For a
  unique / set that took, it also compares the mods (FUN_00627030 /
  FUN_00627150 hooked per list), each set bonus list, ethereal and the
  own seed after. 39980 / 39980 match (4803 with set lists).

- `uv run python drops.py objects 1-20000`: every container class
  (OperateFn 1 / 3 / 4 / 5 / 14 / 26) in turn at varied levels,
  difficulties, locks and sparkle; the real OperateFn on a fake object,
  diffed against `drop-dump objects`: drops, extras, shut, both seeds
  after. 20000 / 20000 match.
- `uv run python drops.py stands 1-20000`: FUN_00559630 / FUN_005594c0 on
  a room whose seed is the job's, then the item's seeds and FUN_00556f60,
  against `drop-dump stands`. 20000 / 20000 match.
- `uv run python diff_drlg.py 1-50 <level> drops`: three items dropped at
  each group object after place_objects, game.exe's FUN_00555da0 against
  `drlg-dump ... drops` (`drop_spot`).

## Not ours yet

- Unique / set item entries (flags 1 / 2; ROP only in 1.14d) force quality
  7 / 5 in `roll_drops`, with no quality draw. Flag modifiers (bin +0x30 /
  +0x32) are 0, so those draws do not run. Forced picks (struct +0x40, the
  entry's unique / set index) are stored and not applied when the item is made.
- `Loot::drop_at` reads the live walk grid (& 0x01) and the items lying
  there, not the rooms' full flags: units (0x1000 / 0x2000) don't block,
  and its walk stays on one level.
- An assassin opens a locked chest without a key (player +4 == 6) isn't
  modeled; the events aren't either. A player opening a barrel kicks it
  (FUN_005868a0: skill 1, Kick, KK at the barrel; World::operate).
- One player in `loot.hpp`. Magic / gold find count gear only (no
  skills, no minion's owner). The picture (VarInvGfx) draw and affixes
  past the pick aren't game.exe's.
- Func 18 (by time) is dropped.

## The socket roll of a made item (`FUN_00556b60`)

`FUN_00556b60(game)` (EDI the item, EBX the drop record; callers `FUN_00556d80`,
`FUN_00557450`) decides whether a new item is socketed and with how many. In order:

1. Quality `FUN_00627e70(item)` below 2, no inventory (`d2d_item_has_inventory`) or a
   Stackable base (`FUN_006289f0`): nothing. The most `FUN_0062bc20(item)`: 0 nothing.
2. The most is cut by the byte `game +0x6d` (0, 1, 2 = difficulty; the stack argument, not the drop record): above 2 to 3, above 3 to 4,
   above 5 to 6.
3. When the item's version (item data `+0x30`, `FUN_0062a670`) is under 100 and the item is of
   ItemTypes row 3 (`FUN_00629bb0`): nothing. Otherwise `rand(100)` (`FUN_0045c390`) on
   the item's own seed (`FUN_00627d90` = item data `+4`) is drawn, unless the record's `+0x80` has
   bit 3 (8: no sockets), and forced to 0 by its bit 4 (0x10). It sockets when the record's
   `+0x44` has 0x800 or the draw is under 33.
4. Flag 0x800 is set on the item. With a non-zero version the count is the item's
   own seed field (item data `+0x10`, `FUN_00627df0`) `% most + 1`; with version 0 it is
   `min(most, 3)`, and 2 at most for an item of ItemTypes row 0x25. `FUN_0062bcb0` sets
   it (clamped to the grid area and 6; items.md).

Evidence: `python3 tools/ghidra/fn.py 00556b60`, `python3 tools/ghidra/fn.py 00627e70`,
`python3 tools/ghidra/fn.py 0062bc20`, `python3 tools/ghidra/fn.py 0062bcb0`.
d2d: reads the same, cited from the decompile, no oracle run, for version 100+ (`generate_item`'s `finish`: `rng(100) < 33`, `start % capped + 1`,
the 3 / 4 / 6 caps, `!stackable`). Not ported: the version-0 branch, the row 3 / row 0x25
tests, and the record flags (those are the forced picks the `ponytail:` there lists). d2d keeps
quality 2..3 itself; the game's own test here is only `quality >= 2`, the caller's choice
(`FUN_00557450`) was not read for this. Cited from the decompile, no oracle run.

## The per-type fix-ups after an item is made (`FUN_00556e80`)

`FUN_00556e80()` (EAX the item, EDI the drop record; callers `FUN_00556f30`,
`FUN_005572a0`) tests the item against five ItemTypes rows with `FUN_00629bb0`, in this order,
and ends with `FUN_005c1260` (the staff mods above):

| Row | What it does |
|---|---|
| 0xd | `FUN_00556a60(record)`: the charm-affix path (row 0xd is `char`, Charm, items.md; it asserts string 0x372 when neither of its rolls gives a value, and sets item flag 0x10 clear) |
| 0x28 | item data `+0x28` (`FUN_00629df0`, which asserts 0x107c / 0x107d / 0x107f for a null unit, a non-item or no item data) set to `record +0x40`, or to the dword at `*record + 4` when `*record` is non-zero |
| 7 | the same `+0x28` store, then item flag 0x10000 set (`FUN_006280d0`) |
| 0x16, 0x12 | item data word `+0x3e` (`FUN_00627fb0(item, value, 0)`) set to `FUN_005c2540(..)`: the row of the books table (`d2d_books_count_ptr`) whose dword at `+0x18` / `+0x1c` equals the item's code (Items `+0x80`), else the table's count |

`FUN_005c2540` is a fastcall whose ECX the decompile does not show (the item is assumed to
be passed through); the books-table fields were read only by offset. The `+0x28` of rows 0x28 /
7 is the same field `tools/emu/drops.py` prints as `sub` (-1 until a unique / set row is picked).
Evidence: `python3 tools/ghidra/fn.py 00556e80`, `python3 tools/ghidra/fn.py 00629df0`,
`python3 tools/ghidra/fn.py 00627fb0`, `python3 tools/ghidra/fn.py 005c2540`.
d2d: not ported as a function (the charm-affix path, row 0xd, is not ported). Cited from the
decompile, no oracle run.

## The class choice and its helpers (`FUN_005a6600` and what it calls)

The helpers below are read one by one, callee first. Each ends with its evidence and
what d2d does. All are cited from the decompile and `tools/emu/disasm.py`, no oracle run.

### A player's quest flags (`FUN_00543520`)

`FUN_00543520` is a fastcall: ECX the game, EDX a player unit. It pushes EDX to
`FUN_006221a0` (the player data, `d2d_player_data`; asserts for a null or non-player
unit) and returns the dword at player data `+0x10 + difficulty * 4`, the difficulty
being the byte `game +0x6d`. That dword is the pointer `FUN_0065c310(ptr, quest, bit)`
tests (`*ptr` is the flag block, bit `quest * 16 + bit`). Ghidra drops EDX and shows
only two arguments, so the decompile of its callers (`FUN_0055cc90`, `FUN_00577830`)
reads `FUN_00543520(0x14, 0)` with the quest and bit arguments of the next test call;
the real arguments are the game and the player.
Evidence: `uv run --project tools/emu python tools/emu/disasm.py 543520` (8 instructions:
`mov esi, ecx`, `call 0x6221a0` with EDX pushed, `movzx ecx, byte [esi+0x6d]`,
`mov eax, [eax + ecx*4 + 0x10]`); `python3 tools/ghidra/fn.py 00543520`;
the same pointer is already written in quests-act1.md ("The flags pointer is
`*(FUN_006221a0(player)+0x10+diff*4)`").
d2d: reads the same, cited from the decompile, no oracle run; not ported as a function
(`header.quest_flag(diff, quest, bit)` reads the save's block for the active difficulty).

### The class moved on by level, then rolled (`FUN_0055afa0`)

Fastcall (ECX game, EDX the dropper) with eight stack words (`ret 0x20`): killer, class,
forced quality, ilvl, noNoDrop, out array, count pointer, max. It sets `lvl = 0`, and sets
it to the dropper's stat 0xc (`FUN_00625480(unit, 0xc, 0)`) only when all of these hold:
`game +0x70` (expansion) is non-zero, the byte `game +0x6d` (difficulty) is above 0, the
dropper is a monster (type 1) with a MonStats row (`FUN_00451f80(unit +4)`), and that row's
byte `+0xc` has neither bit in `[0x6ce270]` (4, noRatio) nor `[0x6ce280]` (0x40, boss).
`FUN_00654e00(class, lvl)` then picks the row (it returns the class itself for `lvl <= 0`,
and 0 for class 0 or one past `DAT_0096c5f0`); a 0 ends it with nothing rolled; otherwise
it runs `FUN_0055a6d0(game, unit; killer, row, forced, ilvl, noNoDrop, out, count, max)`
with the five other words passed on unchanged.
Evidence: `uv run --project tools/emu python tools/emu/disasm.py 55afa0` (the two tests at
0x55afaa / 0x55afb1, the masks at 0x55afcf / 0x55afd7, `ret 0x20`),
`python3 tools/ghidra/fn.py 00654e00`.
d2d: reads the same, cited from the decompile, no oracle run. `Loot::drop` passes `tc_upgrade(class, diff > 0 && !tc_fixed ? level : 0)`
(`tc_fixed` = noRatio or boss), whose loop starts only for `lvl > 0` like `FUN_00654e00`. d2d
has no expansion test (it only plays 1.14d expansion), and no empty-class case (a class
index of 0 or past the table drops nothing in the game).

### A monster's owner (`FUN_0058f0d0`)

ECX a unit. It returns 0 unless the unit is a monster (type 1) with data (`+0x14`) whose AI
control (data `+0x28`) is set and whose control `+0x28` is non-zero; then it returns
`FUN_00552f60(game = ctl +0x28, type = ctl +0x30, id = ctl +0x2c)`, the owner's unit (0 when
no unit of that id is in the game's list). The three words are the ones `FUN_0058f030` stores
(`ECX` to `+0x28`, the two stack words to `+0x2c` and `+0x30`), so control `+0x28` is the game
pointer given there, which is also the "set" test (monster-ai.md "Friends" says "AI control
+0x28 set, owner id +0x2c"; the type at `+0x30` is what that line leaves out). The decompile
shows only `FUN_00552f60(*(ctl + 0x2c))` because Ghidra drops ECX and EDX.
Evidence: `uv run --project tools/emu python tools/emu/disasm.py 58f0d0` (loads at 0x58f0ec,
0x58f0f3, 0x58f0f7), `uv run --project tools/emu python tools/emu/disasm.py 58f030`,
`python3 tools/ghidra/fn.py 0058f0d0`.
`FUN_005a6600` calls it for a killer that is not a player, to try the killer's owner for the
quest test (below). d2d: reads the same where it uses it, cited from the decompile, no oracle run: the "in a group" test in `ai/act2.cpp` is
this owner test (its comment cites `FUN_0058f0d0`, AI control `+0x28` / `+0x2c`). `Loot::drop`
has no owner step: it reads the one local player's quest bits and gear (drops.md "Not ours
yet": one player, no minion's owner).

### A monster flag test (`FUN_005a0180`)

Fastcall (ECX unit, DX a mask): true when the unit is a monster (type 1) with data (`+0x14`)
and `(data +0x16 & mask) != 0`; false for anything else (`and ax, dx; neg ax; sbb; neg`). The
matching setter is `FUN_005a01b0` (ECX unit, DX mask, stack set / clear); it has no direct
caller in the decompile, so who sets bits 2 and 8 was not traced. What the readers show:

| Mask | Reader | Reading |
|---|---|---|
| 2 | `FUN_005a03a0` reads the SuperUniques index at data `+0x26` only when the bit is set; `FUN_005bffc0` (Crushing Blow's divisor) passes 2 (`lea edx, [eax+2]` at 0x5c0063, eax 0) | superunique |
| 4 | `FUN_005a6600` after the superunique test | champion: TreasureClass2 (`+0x88`) |
| 8 | `FUN_005a6600` next: TreasureClass3 (`+0x8a`); the stun test `FUN_0057aae0` passes 8 (0x57ab04) | unique |

Evidence: `uv run --project tools/emu python tools/emu/disasm.py 5a0180`,
`uv run --project tools/emu python tools/emu/disasm.py 5bffc0` (the `call 0x5a0180` at
0x5c0068), `uv run --project tools/emu python tools/emu/disasm.py 5a6600` (0x5a66c0..0x5a66dd),
`python3 tools/ghidra/fn.py 005a03a0`.
d2d: reads the same for the class pick, cited from the decompile, no oracle run (`Loot::drop`: superunique, then champion, then unique).
**Possible disagreement, not established:** `crush_divisor` (`ai.hpp`) and monsters.md call
mask 2 "a unique" and give 8 to `Boss::unique` as well as `Boss::superunique`, while the
game tests bit 2 only, the superunique bit by `FUN_005a03a0`; `strike.cpp`'s stun test
(mask 8) takes champions and superuniques too. Whether a plain unique carries bit 2, or a
superunique bit 8, needs the writers of `FUN_005a01b0`'s bits; they were not found, so no
issue is filed.

### A room's level id (`FUN_0061a1b0`)

`FUN_0061a1b0(room)` (stdcall, `ret 4`): 0 for a null room; otherwise it loads the room's
`+0x10` into ECX and returns `FUN_0066bab0`, which is `*(*(ECX + 0x58) + 0x1d0)`: the level
id (drlg.md "FUN_0066bab0 the room's level id"). `FUN_005a6600` feeds it the room of the
dropper (`FUN_00620bb0`) and gives the id to `FUN_006427f0`, and to `FUN_00558200` as the
area for an item level (above).
Evidence: `uv run --project tools/emu python tools/emu/disasm.py 61a1b0` (the `test`, the
load at 0x61a1be, the call at 0x61a1c1), `python3 tools/ghidra/fn.py 0066bab0`.
d2d: reads the same, cited from the decompile, no oracle run (not a function: `room_of(level, x, y)` and the level's own id; the Chaos
Sanctuary check in `monsters.hpp` cites it for the target's level id 108).

### The act of a level id (`FUN_006427f0`)

`FUN_006427f0(level id)` (stdcall, `ret 4`): walks `i = 1..5` over the dwords at
`0x6eb2f0 + i * 4` and returns `i - 1` at the first one the id is below (`cmp ecx, [eax*4 +
0x6eb2f0]; jl`); when the id is not below any of the five it returns 0 (`xor al, al`; the
decompile's `uVar1 & 0xffffff00` is the same 0). So the result is the act, 0 for Act 1 up
to 4 for Act 5 (callers: `FUN_005a6600` here, quests-act1.md "`FUN_006427f0` == 0",
quests.md "low byte 4" for Act 5), and an id past the last threshold reads as Act 1 again.
The table's values were not read in this pass; skills.md gives the starts 1, 40, 75, 103, 109,
which does not say what `[1..5]` holds, so the id at which the fall-through starts is open.
`FUN_005a6600` calls it with the level id and drops the result (`FUN_006427f0(uVar8)` has no
use in the decompile or the asm after 0x5a6649): the call has no effect there.
Evidence: `uv run --project tools/emu python tools/emu/disasm.py 6427f0`,
`uv run --project tools/emu python tools/emu/disasm.py 5a6600` (0x5a6649, EAX unused until
0x5a664e overwrites it).
d2d: `rules::level_ids::act_of` (`upper_bound` over the act towns, minus 1, at least 0)
gives 4 for any id past Harrogath's; the game gives 0 past its last threshold. Whether a
level id exists in that range was not checked, so this is not filed as a disagreement.

### A unit's stat sub-list by flag (`FUN_00625760`)

`FUN_00625760(unit, flags)` (stdcall, `ret 8`): reads the unit's stat list (`unit +0x5c`);
0 for a null list or one without flag `0x80000000` in its `+0x10`; otherwise it returns
`FUN_006256e0(list, flags)`, the first sub-list whose flags share a bit of `flags & ~0x2000`
(stats.md "FUN_006256e0": the `+0x3c` chain, or `+0x40` when `flags` has `0x2000`, walked
by `+0x2c`). It does not test the unit for null: the first load is `[unit + 0x5c]`
(`mov eax, [eax + 0x5c]` at 0x625766), so a null unit faults; `FUN_005a6600` tests its
killer for null first (0x5a672f), the other callers were not read. In `FUN_005a6600` it is asked for flag `0x800` on a killer that is not a
player, and the sub-list's owner type (`FUN_00625250`, list `+8`) and id (`FUN_006252b0`,
list `+0xc`) name the unit whose quest the kill counts for (the owner step below).
d2d callers use other masks: 0x20 for a curse state on a target, flag 2 for the picked-up
item in the inventory tint.
Evidence: `uv run --project tools/emu python tools/emu/disasm.py 625760`,
`python3 tools/ghidra/fn.py 006256e0`, `python3 tools/ghidra/fn.py 00625760`.
d2d: reads the same on how it is used, cited from the decompile, no oracle run (`ai.cpp`'s curse test, `panels.cpp`'s `ponytail:` note, the
`on_foe_states` curse/mark lists); the 0x800 sub-list (an owner link, e.g. a minion's) has
no d2d counterpart: nothing in d2d makes a stat list that names an owner.

### Does a unit have a state of one set (`FUN_0063a690`)

`FUN_0063a690(unit)` (stdcall, `ret 4`) puts the pointer at `[0x744304] +0x144` in EBX and the
unit in EAX and runs `FUN_0063a130` (a register-convention helper, so the decompile shows no
arguments). That reads the unit's state bit words (`FUN_00625bb0`: stat list `+0x58`, 0 when the
unit has no list or the list lacks flag `0x80000000`; 0 returns 0 here), and over
`(count + 31) / 32` words, `count` being the dword at `[0x744304] +0xc4`, returns 1 when any
word shares a bit with the same word of the set, else 0. `FUN_0063a6b0(state)` tests one
state of the same set (`+0x144`, bounds-checked by `+0xc4`) and `FUN_0063a710` one of the set at
`+0x148` (its `FUN_0063a6f0` the "any" form). Which States.txt column fills `+0x144` was not
traced, so the set has no name here. `FUN_005a6600` uses it as the gate before looking for an
owner on the killer's stat list: only a killer that is not a player and has a state of this set.
Evidence: `uv run --project tools/emu python tools/emu/disasm.py 63a690`,
`uv run --project tools/emu python tools/emu/disasm.py 63a130`,
`python3 tools/ghidra/fn.py 00625bb0`.
d2d: not ported (no minion's owner in the loot code, drops.md "Not ours yet").

### The kill's drop roll (`FUN_005a6600`)

`FUN_005a6600(game, dropper, noNoDrop, record)` (stdcall, `ret 0x10`). The record's `+8` is the
killer unit (0 when none). Callers: `FUN_005a6830` (the DT start: dropper `record +4`, noNoDrop
0, the event record as `record`) and `FUN_005a8000` (Find Item: a 0x20-byte local record
holding its own first argument at `+8`, noNoDrop 1). In order:

1. Checks that stop the game with the internal-error path (`FUN_00408090(line)`,
   `FUN_00408a60`, `FUN_00681e09(-1)`): a null dropper (line 0x194), no room
   (`FUN_00620bb0`, 0x196), and a MonStats row the dropper's class `+4` does not give (class
   below 0 or at or past the count at `[0x744304] +0xa80`, rows of 0x1a8 from `+0xa78`; 0x19a).
2. The room's level id (`FUN_0061a1b0`); `FUN_006427f0(id)`, whose result is dropped; the item
   level `FUN_00558200(dropper, id)`.
3. The class: `FUN_005a03a0` (the superunique index, -1 for none). With an index, the row
   `FUN_006556e0(index)` (0 past the count) gives its u16 at `+0x2c + diff * 2`, or, with no row,
   MonStats `+0x8a + diff * 8` (TreasureClass3). Without one: mask 4 on `FUN_005a0180` gives
   `+0x88 + diff * 8` (TreasureClass2), else mask 8 gives `+0x8a + diff * 8` (TreasureClass3),
   else `+0x86 + diff * 8` (TreasureClass1). The difficulty is the byte `game +0x6d`.
4. TreasureClass4 (`+0x8c + diff * 8`) replaces it when MonStats `+0x9e` (TCQuestId) is non-zero,
   that word is non-zero, and the record and its killer exist; a killer that is not a player
   is first replaced by `FUN_0058f0d0(killer)` (its owner), and then, when `FUN_0063a690(killer)`
   and a `FUN_00625760(killer, 0x800)` sub-list exist, by the unit that list names
   (`FUN_00552f60(game, owner type, owner id)`), if that is found. The (possibly replaced)
   unit must be a player with quest flags (`FUN_00543520`), and none of the quest bits 15, 1 and
   `+0x9f` (TCQuestCP) may be set (`FUN_0065c310`, `+0x9e` the quest).
5. `FUN_0055afa0(game, dropper; killer, class, 0, ilvl, noNoDrop, 0, 0, 0)`.

Evidence: `uv run --project tools/emu python tools/emu/disasm.py 5a6600` (asserts 0x5a6610,
0x5a6639, 0x5a6673; class 0x5a66a2..0x5a6705; quest 0x5a6705..0x5a67f4; call 0x5a6817),
`uv run --project tools/emu python tools/emu/disasm.py 5a8000`,
`uv run --project tools/emu python tools/emu/disasm.py 5a6830`.
d2d: reads the same for a player kill in the local game, cited from the decompile, no oracle run (`Loot::drop`: the same order, the same three
bits, `tc_upgrade` for step 5). Differences, none reachable in single player with the stock
tables as far as read: the game's superunique branch takes its row's class even when that is
empty (nothing drops), where d2d falls back to the champion / unique / normal class; the
owner step (a minion's or a pet's kill counting for its owner's quest) has no d2d
counterpart; and the quest class applies only to a killer that resolves to a player, where
d2d always reads the one player's bits. No bug of the original was found in this function.
