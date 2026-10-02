# Drops — treasure classes and item quality (1.14d game.exe)

Our port: `components/rules/drops.hpp` (`tc_upgrade`, `roll_drops`,
`roll_quality`, `made_quality`, `stand_quality`, `chest_round`,
`gold_amount`, `add_auto_treasure`), `components/rules/shrines.hpp`
(`open_container`, `stand_item`), loaded in
`components/game/gamedata_load.cpp`, used by `components/game/loot.hpp`
(kills) and `world.cpp` (chests). Checked against game.exe by
`tools/emu/drops.py` (see the end) and `tests/test_game.cpp`.

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

- ilvl: FUN_00558200 — a monster's stat 0xc (its level), at least 1;
  otherwise the area's monster level (FUN_0061dca0).
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
   `DAT_00883d70`). Capped by the dropper's stat 100 only if
   FUN_0044be50 == 1. For n > 1: p = nd / (nd + total), q = 1 - p^n,
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
- `drops.py act1` prints the Act 1 jobs `tests/test_game.cpp` hashes.
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

- Unique / set item entries (flags 1 / 2; ROP only in 1.14d) and forced
  picks (struct +0x40).
- `Loot::drop_at` reads the live walk grid (& 0x01) and the items lying
  there, not the rooms' full flags: units (0x1000 / 0x2000) don't block,
  and its walk stays on one level.
- An assassin opens a locked chest without a key (player +4 == 6) isn't
  modeled; the barrel's opening step for a player and the events aren't
  either.
- One player in `loot.hpp`. Magic / gold find count gear only (no
  skills, no minion's owner). The picture (VarInvGfx) draw and affixes
  past the pick aren't game.exe's.
- Func 18 (by time) is dropped.
