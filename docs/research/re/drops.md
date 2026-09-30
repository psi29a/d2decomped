# Drops — treasure classes and item quality (1.14d game.exe)

Our port: `components/rules/drops.hpp` (`tc_upgrade`, `roll_drops`,
`roll_quality`, `gold_amount`, `add_auto_treasure`), loaded in
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
   roll stops. FUN_005589a0 then adds the killer's gold find
   (stat 0x4f %, plus its owner's) to gold.

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

## Chests — FUN_00585b90

The act's chest class by tier (area level against the act's two marker
levels: A / B / C = 0 / 1 / 2, `chest_tc`), rolled off the chest's unit
seed with ilvl = the tier: qualities roll at item level 0..2, while
FUN_0055a550 makes the items at the area level.

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
  stack, durability, defence, the pick, the game seed after. 40000 /
  40000 match (17339 defence rolls, 1660 stacks, 8094 picks, 9502 failed
  unique picks, the Cow King's set with and without a bovine).

## Not ours yet

- Unique / set item entries (flags 1 / 2; ROP only in 1.14d) and forced
  picks (struct +0x40).
- Chests and other objects roll off the shared rng, not their unit seed.
- One player, no magic or gold find in `loot.hpp`; the picture
  (VarInvGfx) draw and affixes past the pick aren't game.exe's.
