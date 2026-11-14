# Combat — damage, poison, regeneration (1.14d game.exe)

Our port: `components/rules/monsters.hpp` (`Fighter`, `make_fighter`,
`player_blow`, `monster_blow`, `attack_ticks`, `open_wounds_per_sec`),
applied in `apps/d2d/town.hpp` (`land`, `strike`, `monster_dots`,
`apply_regen`) and `components/game/ai.hpp` (`monster_update`, `missiles_update`).
Tests: `tests/monsters/test_monsters.cpp`.

## Applying damage — FUN_0057c6c0 (SUnitDmg.cpp)

The server applies one damage record (an int array) to a unit. Fields seen
in this function, by int index:

| index | use here | call |
|---|---|---|
| 5, 6 | burning damage per tick, length | FUN_0057add0 |
| 10, 11 | poison damage per tick, length | FUN_0057ac50 |
| 0xc | cold length | FUN_0057af80 |
| 0xd | freeze length | FUN_0057b230 |
| 0x11 | stun length | FUN_0057aae0 |
| 0x12 | (life-related, FUN_0057a980) | |
| 0x13 | total damage (8.8 fixed) | subtracted from stat 6 (life) |

Life (stat 6) and this total are both in 256ths, and the unit is dead when its
life is under 0x100 after the hit (LAB_0057c865: set to 0). The elements get
there through FUN_0057c1e0: each record field of the table at 0x732980
(lightning +0x1c with resist stat 0x29, pierce 0x14e, absorb 0x90 / 0x91) goes
through FUN_0057bf80: the resistance (FUN_0057be00: the attacker's pierce
taken off, -100 at the least; a plain monster gets no difficulty penalty and no
75 % cap, LAB_0057c242), 100 % or more is nothing, else MulDiv
(FUN_00483360) of the fine value, then absorb (FUN_0057bf10). d2d keeps whole
points in `Monster::hit_points` plus `life_frac` (256ths already taken off it);
only a missile's element roll (FUN_005a89a0) carries a fraction (`Blow::frac`,
`rules::resisted_fine`), a heal (FUN_0057a980, `heal`) is whole points capped
at max life, and dead is life 0. Physical, poison, burning, thorns, pets and
mercs still deal whole points (ponytail: not traced one by one).
Oracle: `tools/emu/life_apply.py` against `test_game --life-apply`.

Life after the hit below 0x100 (under 1 point) is set to 0: the unit is
dead, and result flag 2 is set. FUN_0057aa60 / FUN_0057aaa0 do the same
for mana (stat 8) and stamina (stat 10).

**Mana back: FUN_0057aa00** (cited, decompile only, no disassembly or oracle
run). The leech's mana half; called only from `FUN_0057c420`. One stack
argument, `points`; the unit is a register the decompile hides
(`unaff_EDI`, as in `FUN_0057a980`). `points < 1`: returns 0 and writes
nothing. Else `max` = `FUN_00625d60` (stat 9, maxmana), `cur` = stat 8
(`FUN_00625480`), `new` = `min(cur + points, max)`, written with
`FUN_00627260(unit, 8, new, 0)` (always, even when unchanged). Returns
`max - cur` when `new == max`, else `points`. Two readings follow from
that: a hit that fills mana exactly returns `max - cur` (= `points`), and a
unit already above max has mana pulled down to `max` and gets a negative
return; `FUN_0057c420` drops the return value, so neither is seen. Unlike
the life twin `FUN_0057a980`, there is no dead-unit test and no state test
(life has `FUN_005541b0` and state 0x5c). d2d: no function of its own; the
mana leech of a player's hit is `std::min(kMaxMana, mana + (blow.mana <<
8))` in `components/game/fight/strike.cpp:489`, the same cap (agrees);
d2d does not apply the dead-unit gate to mana because nothing there needs it.
Re-check: `python3 tools/ghidra/fn.py 0057aa00 0057c420`.

**Life back: FUN_0057a980** (cited, decompile only). The twin of
`FUN_0057aa00`, called by `FUN_0057c6c0` with record int 0x12 and by
`FUN_0057c420`. One stack argument `points` (the raw units of stat 6,
which `FUN_0057c6c0` treats as 256ths: dead under 0x100), the unit in
EDI/ECX registers the decompile hides. It returns 0 and writes nothing when:
`points < 1`; or the unit is dead (`FUN_005541b0`: a null unit, a player in
mode 0 or 0x11, a monster in mode 0 or 0xc, a unit of any other type, or
unit +0xc6 bit 0); or the unit has **state 0x5c** (`FUN_00639df0(unit,
0x5c)`). Else `new = min(stat 6 + points, max)` with max =
`FUN_00625d10` (stat 7), written by `FUN_00627260(unit, 6, new, 0)`; the
return is `max - cur` when capped, else `points`. State 0x5c is set by
`FUN_005a6920` (the life regen / drain tick, stat 0x4a) when it takes life
to 0 in a unit that has state 0x36, and cleared by `FUN_005544b0` (state 0x36 off).
It is not preventheal: that is state 0x34 (`FUN_0057d810`), which the
regen `FUN_005a6920` and the teleport mod's heal `FUN_005b11f0` test
themselves and this function does not. d2d: `ai.cpp` `heal` caps at max
life and refuses a dead monster (agrees), keeps `no_heal_until` (state
0x34) as a caller's gate (agrees with where game.exe tests it), and has no
state 0x5c. Its header calls `points` "whole points"; game.exe's `points`
from a hit's record are stat 6 units (256ths), so the comment is true for
d2d's own callers only. Re-check: `python3 tools/ghidra/fn.py 0057a980
005541b0 005a6920 005544b0`.

## The game behind a unit — FUN_00554010

(cited, decompile only.) `__fastcall`, ECX the unit: a null unit fails the
usual fatal check (`FUN_00408090(0xa29)` message, `FUN_00408a60`, then
`FUN_00681e09(-1)`, no return); otherwise it returns the dword at unit
+0x80. The callers cited here (not a full caller list) use the result as the game struct: the
difficulty byte at +0x6d (`FUN_00611d30`, `FUN_00637910`'s second argument,
`FUN_0057c420`'s Drain scaling) and the expansion word at +0x70
(`FUN_00558200`). `tools/emu/end_percent.py:93` replaces it with a hook that returns
the game struct, so that oracle assumes this reading and does not check it
(the decompile above is the evidence). So a **server** unit's +0x80 is
the game pointer. Client units use +0x80 differently (a sound time: the
Neutral timer and the Gibbet's call frame, unit-sounds.md "Monsters" and
"Objects"), so `structs.tsv`'s `unit 0x80 neutral_time` row (and 0x7c / 0x84)
describes the client layout, not this one; the table has no server/client
column yet, and its `components/game/ai.hpp:187` citation no longer shows
those names.
Re-check: `python3 tools/ghidra/fn.py 00554010 0057c420 00558200`.

## Poison and burning — FUN_0057ac50 / FUN_0057add0

Poison is **state 2** (`poison`) and burning **state 0x73**, each holding a
stat list with stat 0x4a (74, `hpregen`) set to **minus the per-tick
damage**, lasting `length` ticks (plus the current frame), ending through
FUN_0056e900. The unit's regen event (event 3) is scheduled for the next
frame.

One of each at a time: when the state is already there, the new one
replaces it (value and length) only if it is at least as strong
(`new == old || new > old`); a weaker one is ignored.

Healing potions are also `hpregen` (misc.txt stat1), in their own state, so
potion and poison add up in the same regen tick.

## Drinking a potion

C→S 0x20 (inventory, FUN_0055e170) and 0x26 (belt, FUN_00562390) both go
through FUN_005bf240, which runs the item's pSpell (Items +0x94) from the
table at 0x741790 (8 bytes an entry, the second the server's): pSpell 3,
FUN_005be3f0, healing / mana; pSpell 5, FUN_005beac0, rejuvenation.
FUN_00562390's shift flag gives a belt potion to the hireling
(FUN_00574ec0(7, 0)), types hpot 0x4c, apot 0x50, wpot 0x51 only.

FUN_005be3f0, for each of the item's stats (Items +0x9e, calc +0xa4):
- `amount = calc`; `hpregen` (0x4a) `<< 8` through FUN_0062a5d0, the
  class's life bonus (amazon, paladin, assassin ×1.5 as `v + (v >> 1)`;
  barbarian ×2; others ×1; a non-player ×2); `manarecovery` (0x1a)
  `<< 8` through FUN_0062a620 (amazon, paladin, assassin ×1.5; sorceress,
  necromancer, druid ×2; others ×1). No HealthPotionPercent in 1.14d.
- if vitality (life) / energy (mana) > 0: `r = rand(stat) >> 1`, and
  `rand(100) < r` doubles it (the unit seed, +0x20).
- `<< ISC ValShift` (0 for both); classic only: FUN_005c6870.
- into the state (Items +0x98) for `len` (calc +0xb0) frames, joining
  what's left of it: `left = end − now`, per frame
  `(old × left + amount) / (left + len)`, lasting `left + len`; it ends
  through FUN_0056e900.

FUN_005beac0: each stat a percent of the max (ISC +0x32's stat) by MulDiv
(FUN_00483360), 100 the max itself, capped; no class bonus, no roll.
d2d: rules::potion_amount / potion_rate, Fight::potion.

## Picking up

C→S 0x16 → FUN_00548b00 (an item: within 5, a clear path); action 0 is
FUN_00563560's auto-place, else to the cursor (FUN_0055cf50).
FUN_00563560:
1. FUN_0055cc90 may it be taken; not: message 0x13 (impossible).
2. gold: FUN_0055c850.
3. stacking, FUN_00560020: a scroll (0x16) into an inventory tome of its
   Books kind with room (FUN_0063c3b0, FUN_0055ffa0 / FUN_0055ef20); a tome
   (0x12) into one (FUN_0055d370): past its maxstack the picked one keeps
   the rest on the ground; a stackable AutoStack type into the inventory's
   matching stacks (FUN_0055d0d0, FUN_0063c200, the match FUN_0062c850:
   class, quality, flags, stats 0x15–0x18, 0x9f, 0xa0; max FUN_006295b0:
   maxstack + stat 254, ≤ 511), the rest placed on.
4. FUN_0055d710 (mode 0): into a free body location. Only an identified
   item the player can use (FUN_0062eaf0), of a type with BodyLoc1/2 and
   Body (FUN_0062fdf0), not broken (0x100) or flagged 0x4000
   (FUN_0062a4e0), not ItemTypes row 0x26 (tpot); ammunition (its type's
   Quiver, FUN_00628480) only when a hand (4 / 5) holds a weapon whose
   Shoots it is. BodyLoc1 == BodyLoc2: that slot if empty. Else BodyLoc1
   if both are empty; one taken: the free one when the pair goes together
   (FUN_0055d560 flags both, FUN_0055d670: two rings, bow / crossbow and
   its ammunition, a one-hander and a shield, two weapons for a Barbarian
   or two claws for an Assassin). d2d: `rules::auto_equip_slot` (pairs by
   `hands_ok`).
5. the belt: FUN_0062bad0 (own type Beltable), FUN_00628ba0 (passes any
   item with a record: its FUN_0063c560 test, a bottom match not isc /
   tsc, returns 0 or 1 and is compared with -1), FUN_0063c790 →
   FUN_0063c600: 1×1; columns 0–3 whose bottom item matches (FUN_00628a40:
   the same code, or both in hp1–5 / mp1–5 / rvs, rvl, 0x744660–0x744698)
   take their lowest free box below the belt's boxes; else, autobelt, the
   first free bottom box; FUN_0063afd0 places it.
6. the inventory: FUN_005600a0 → FUN_0063b950; no room: message 0x17.

Messages voice on the client (FUN_004cb9c0): the class's table off
0x72a008 (amazon 0x727eac), 0x13 → +8, 0x14 → +0x18, 0x15 → +0xc,
0x16 → +0x10, 0x17 → +0x14 (cantcarry, amazon sound 0xb76), 0x18 → +0x1c;
the same sound within 0x4b of FUN_0044db00's clock (DAT_007a0498, +1 each
client game frame in FUN_0044c790: 75 frames, 3 s) is skipped.
Message 0x15 is the client's own: FUN_004610c0 asks FUN_004d9fc0 →
FUN_00647960 whether the skill can go; reason 1 (short of mana:
FUN_00647540's cost `(mana + lvlmana × (lvl − 1)) << manashift` against
stat 8, or FUN_006440f0's startmana × 256, Skills +0x184) voices table 0x711ddc[reason]
(0, 0x15, 0x13, 0x13, 0x13): `<class>_needmana_1` (amazon 0xb8d). A skill
with AttackNoMana (Skills +7 & 0x10) on status 1, 2 or 4 (not only mana:
LAB_00461101) swings Attack instead, silently (skills.md "Can the skill
go now"; `tools/emu/skill_status.py client`). The
server (FUN_0056f640) just drops a skill it can't pay for. d2d:
`Fight::need_mana`.
d2d: rules::pick_up, Loot::take. ponytail: no step 4; stacks match by
code; the voice's repeat guard; the doubling rolls d2d's rng; no
shift-click to the hireling.

## Regeneration — who can die to poison

- **Players**: FUN_00580810 (event 3 callback, re-armed every frame) calls
  FUN_00580610: `life += hpregen`, capped at max life; then
  **`if (life < 0x100) life = 0x100`**. Negative regen (poison, burning) can
  never take a player below 1 life. Then mana (FUN_005806f0) and stamina
  (FUN_00580500; char-panel.md, the HUD).
- **Mana a frame** (FUN_005806f0, 256ths): unless state 0x55 (nomanaregen),
  `max(maxmana / (CharStats ManaRegen (+0x3a) × 25), 1)` (7500 frames when
  ManaRegen is 0; it's 120 for every class), times `(100 + stat 27) / 100`
  through FUN_00483360 (truncating); plus stat 26 flat; capped at what's
  missing. Integer per frame: 20 max mana regains 1/256 a frame, 0.1 a
  second, not 20 / 120. A full globe removes state 0x6a (manapot). d2d:
  `rules::mana_per_frame`, `Fight::apply_regen`.
- **Monsters**: FUN_005a6920: `life += hpregen` (less a state's own
  contribution when FUN_0063a750 says so), capped at max, clamped at **0**.
  At 0 the monster dies; the kill goes to the owner of its state 2 (poison)
  or 0x3e, and the kill runs through FUN_0057ccb0 / FUN_005c0c30.

So poison kills monsters and credits the poisoner, but never kills a player.
d2d matches both, and the one-poison-at-a-time rule.

## To hit — FUN_0057ec10 → FUN_0057d9b0

- Defense: FUN_006223f0 + stat 33 (vs melee) or 32 (vs missiles).
- Player attack rating (FUN_00622560): stat 19 + (dex − 7) × 5 + CharStats
  ToHitFactor; then × (100 + mastery to-hit + stat 119 + the skill's
  bonus) / 100 (plus vs-monster-type bonuses, stat 0xb3).
- Monster attack rating: stat 19 + dex × 5 + the skill's bonus, × (100 +
  stat 119) / 100.
- `c = AR × 100 / (AR + DEF)`; `c = c × 2 × alvl / (alvl + dlvl)`, clamped
  5..95; hit when the attacker's seed % 100 < c. A swing at a player in
  mode 3 (RN) skips the roll and lands (FUN_0057ec10); a missile still rolls.
- A missile's hit (FUN_005adf10) rolls FUN_0057d9b0 only when its row has
  ToHit (+0x18b), with the owner's stat 0x13; ToHit 0 skips the roll, and
  either way FUN_005ad730 then runs the defender's rolls (FUN_0057dfb0:
  block, avoid). A Corpse Explosion's record goes straight to
  FUN_0057c6c0 (FUN_005c4d10): no rolls at all. d2d: `MonStats::no_hit_roll`,
  `monster_blow`'s `sure`.
- A passed roll by a player (unit type 0) with stat 117 (Prevent Monster
  Heal) puts state 0x34 (preventheal) on a monster, not the ubers (hcIdx
  0x2c0..0x2c5): FUN_0057d810, a FUN_0056e970 block of 120 000 frames,
  before the block roll, so a blocked hit still carries it. Read by the
  teleport mod's heal (FUN_005b11f0) and regen (FUN_005a6920). d2d:
  `Blow::prevent_heal`, `Monster::no_heal_until`.
- Then the defender's rolls (FUN_0057dfb0 → FUN_0057dd60), each cancelling
  the hit: **block** (the defender's seed % 100 < chance, the chance a third
  while the player runs: FUN_00622d00's moving, modes 2 / 3 / 6, and not
  mode 2, so walking keeps it whole), then Weapon Block (claws), **dodge** (stat 338,
  melee, standing), **avoid** (339, missiles), **evade** (340, walking or
  running).
- Block chance (FUN_00622720): players need a shield; `(stat 20 +
  CharStats BlockFactor) × (dex − 15) / (clvl × 2)`, clvl at least 1;
  monsters stat 20 when MonStats allows it (or with a shield); at most 75.

## Damage — FUN_0057b7d0, FUN_0057b420

- Physical (FUN_0057b420): `min` / `max` = stats 21 / 22 (23 / 24 for a
  second weapon; 1 / 2 barehanded) + stat 111, in 256ths. The weapon's own
  enhanced damage is already in them (ItemStatCost op 13 applies stats
  17 / 18 to the item's own damage). One percentage `p` = the skill's
  enhanced damage + stat 25 (`damagepercent`) + str × StrBonus / 100 +
  dex × DexBonus / 100 + mastery damage (`madm`), at least −90. Then
  `min' = min + min × (stat 18 + p) / 100`, `max' = max + max × (stat 17 + p)
  / 100`, a roll between them, times SrcDam / 128.
- A monster's blow (`FUN_0057b420` on its mode damage, stats 21 / 22): the
  roll is `(min << 8) + rand((max - min) << 8)` on the attacker's seed, one
  draw, the top not rolled; min is taken at least 1 and max at least min + 1
  (`0..0` rolls 1..2 and draws once, `5..4` rolls 5..6). `FUN_0057c1e0` then
  raises the absorb event on the unreduced physical, reduces the record
  (the flat, stat 34, first, then the percent, stat 36) and the life stat
  takes the total +0x4c in 256ths when it is above 0 (`0057c852`). V:
  `tools/emu/monster_damage.py` runs both on the 8 Act 1 classes (sets `roll`,
  `reduce`, `absorb`, `both`, `negpct`) and d2d's `rules::monster_blow`,
  `reduce_fine` and `absorb_blows` match it in life, pool and seed after. A
  record built without the roll (Corpse Explosion `FUN_005c4d10`, Diablo's
  cold touch `FUN_005cce80`) enters `FUN_0057c1e0` as `min << 8` and draws
  nothing (V: set `exact`; U: which callers build one, read at 005c4dc1 and
  005ccf3f). Energy Shield (`FUN_005ca6c0`) is an absorb event too (0xb): it
  takes calc1 % of each unresisted field, record by record, and the
  resistance works on what is left (`rules::energy_shield_blows`,
  `test_energy_shield`; V run: the game-side order against the real d2d
  build). `FUN_0057bf80` takes the flat / magic damage reduction off a field
  BEFORE the resistance and stores a result under 0, which the record total
  sums (V run, no shield: 100 fire at 50 % with mdr 10 deals 45);
  `rules::monster_blow` and the shield path both do that now (the fields in
  256ths, one field a type, the total clamped once; `test_combat`).
  Cyclone Armor works on the unresisted 256ths fields the same way (skills.md
  row 25, `test_cyclone_armor`). Not covered (U): elements, a missile's
  `FUN_005a89a0` roll.
- **The flat's frame rate.** `FUN_0057c1e0` takes the defender's stat 34
  (normal_damage_reduction) and 35 (magic_damage_reduction) `<< 8`, and
  when the record's +0x54 is above 0 scales each by it / 1024
  (`FUN_00483360(flat << 8, +0x54, 0x400)`, 0x57c26a / 0x57c291) before
  `FUN_0057bf80` takes them off. +0x54 is a missile's: its maker
  (`FUN_0059fa30`) puts Missiles.txt DamageRate (record +0x19c) in the
  missile's stat 327 damage_framerate, and its hit on a unit
  (`FUN_005ad730`) copies that stat into the record (`param_4[0x15]`)
  before `FUN_0057c6c0`. DamageRate is set only on the burners (fire
  walls, Inferno and the other per-frame flames: 41, 82, 205), so their
  per-frame damage loses DR / MDR x rate / 1024, not the whole flat; any
  other hit's +0x54 is 0 and takes the flat whole. d2d:
  `rules::frame_rate_flat` on a monster burner's hit (srv_do 5, the MDR
  first, then the resistance); V: `tools/emu/frame_rate_flat.py`, 2000
  records frozen in `tests/frame_rate_flat_cases.inc` (`test_frame_rate_flat`).
- **The pair percent** (`FUN_0057c060`, EAX the hit context: +4 ?, +8
  attacker, +0xc defender; 100 when either is missing or they are one).
  On a player: 17 (0x11) from a player, a hireling (`FUN_0063ee90`: classes
  0x10f, 0x152, 0x167, 0x230, 0x231) or a player's pet (unit +0xc4 bit 31,
  `FUN_00451f30`), else 100. On anything else: 25 (0x19) hireling on
  hireling; a hireling on a MonStats boss (`FUN_0063e9f0(0, ..)`, +0xc &
  0x40) takes the context's +4 record's +0x38 (100 without one; not
  traced); a primeevil (`FUN_0063edc0`, +0xc & 0x80: Andariel, Duriel,
  Mephisto, Diablo and his clone, Baal and his clone, the ubers) on a
  player's pet: 200 for a hireling, 400 for any other; else 100.
  `FUN_0057c1e0` (0x57c214 on) scales each damage field of the record
  (table 0x732980's 12 rows, those with +0x20 set) above 0 by it
  (`FUN_00483360`, × pct / 100) before the absorb events and the
  reduction.
- Deadly strike (stat 141), critical strike (stat 337) and mastery crit
  (`macr`) are **separate** rolls against rand(100); any one doubles the
  physical damage.
- Item elemental damage: fire 48/49, lightning 50/51, cold 54/55, magic
  52/53, each with its mastery (329 / 330 / 331 / 357), × SrcDam / 128.
  Poison (57/58 over 59, with poison mastery 332) is split by stat 326.
  Cold length += stat 56, stun += stat 66.
- Leech: life stat 60, mana 62, stamina 64 (a monster's scaled by SrcDam).
- Conversion (record byte +0x65, the % at +0x68, last in `FUN_0057b7d0`):
  `FUN_00483360(physical, %, 100)` leaves the physical and goes to fire
  [4] (1), lightning [7] (2), magic [8] (3), cold [9] (4, cold length [0xc]
  at least 50), poison [10] (5: an eighth of it a tick, length [0xb] at
  least 50), 10 a random one of 1..10; 11 fire with burn length [6] ≥ 50,
  12 cold with freeze length [0xd] ≥ 50. d2d: `player_blow`.

The absorb events run newest first: FUN_005c0ad0 (0x5c0ad0, SUnitEvent.cpp)
puts each new event node at the head of the unit's list (+0x90), and a buff's
cast first drops its state's old nodes (FUN_005c0b50(1, state): do 18
FUN_005c9480, do 23 FUN_005c9c10) before FUN_0056e740 registers them again. So
on each record the state cast last takes first: Energy Shield cast after Bone /
Cyclone Armor takes its percent before the armor's pool sees the field; the
armor cast after the shield goes first (as d2d always did).

FUN_005c0b50 (`SUNITEVENT_RemoveEventsByQueueAndOwner`, game, unit,
queueNo, ownerKey) is driven directly over random event lists: a node
whose queue and owner both match is unlinked and freed through
`FreeServerMemory` when it isn't mid-execution (flag bit 0), or left
linked with "free pending" (bit 1) set when it is; a non-matching node
is untouched. 3000 cases plus `--mutation-check` (3 ways: keeping an
executing match linked and unmarked, ignoring the owner key, freeing a
non-matching node) pass against game.exe 1.14d. Oracle
`tools/emu/unit_event_remove.py`.

The same `+0x90` list is a general queued-event list, not only absorb
events (its nodes carry a queue number, an owner key and a skill, read by
the two functions below), and two more server functions (SUnitEvent.cpp)
round it out:

- **UNITEVENT_FreeAllEvents, FUN_005c0a90** (game, unit): walks the list
  freeing every node (`FreeServerMemory`, `.\UNIT\SUnitEvent.cpp` line
  0x3d) and clears the head. Its only caller is `FreeServerUnit`
  (LAB_00555733), so it's the list's teardown when a unit is removed from
  the game.
- **UNITEVENT_FindByCallbackParams, FUN_005c0be0** (game, unit, queueNo,
  ownerKey, eSkill): the first node whose (queueNo, ownerKey, skill) match,
  or none. Callers `STATSLISTEX_FunctionCallback` and
  `SKILLNEC_ApplySummonBuffs` use it (queueNo 2, the summon's GUID as
  ownerKey, the passive/aura stat as skill) to skip re-registering an aura
  event (`D2SetAuraEvent`) a summon already has.

d2d has no literal node list for this queue — its equivalents
(`energy_shield_blows`, `monprop_hit`) compute each effect directly rather
than scheduling and later freeing/searching nodes — so there is nothing to
port for either function; documented for the record only.

## Corrections applied (phase 0)

1. Hit chance rounds as game.exe does: percent first, then × 2 × alvl /
   (alvl + dlvl); negative defense adds to the attack rating.
2. Critical strike (337) and deadly strike (141) are separate rolls, either
   doubling; the mastery crit joins them with skills.
3. Enhanced damage counts only from the weapon and what's socketed in it
   (op 13); stat 25 joins the strength/dexterity bonus, stat 111 adds to
   both ends; barehanded gets no stat bonus.
4. Defense vs melee / missiles (33 / 32) apply to swings / spikes.
5. Dodge (standing, swings), avoid (missiles) and evade (moving) roll after
   block; they're 0 until passive skills give them.
Not needed: monster AR's `dex × 5` (MonStats monsters have no dexterity).

## Hit recovery: the get-hit test (FUN_0057cb00)

`FUN_0057cee0` (a unit takes damage) calls `FUN_0057cb00(unit, record, hit
class)` on result flag 4 (the hit landed) unless the unit has state 21
(stunned: a sure get-hit), for a player at 0x57d37a (mode 4 through
`FUN_005809d0`) and a monster at 0x57d0c8 (mode 3 through `FUN_005a7e60`).
It returns 1 (too small) for a frozen unit (state 1, `FUN_00639df0`), a
record whose poison [10] equals its total [0x13], or a total under 0x100;
else the divisor d comes from the hit class through the jump table at
0x57cbf8 (8 for classes 2, 6, 10, 11; 32 for 4, 8; 64 for 5; 16 for 1, 3,
7, 9, 12 and everything else): under max life / d (`FUN_00625d10`) too
small; under 2 max / d a roll of 2 on the unit's seed (`FUN_00472210`, unit
+0x20) of 0 is too small; then under 4 max / d a roll of 4 of 0 is; else a
get-hit, which a monster also needs MonStats2's GH mode for (`FUN_0046c140`
bit 3, at 0x57cbda: after the rolls, so a monster without it still rolls
its seed). The hit class is the record's +0x60 after `FUN_0057ce30` (run at
0x57cf54 when the record's +0x64 byte is 0 and the class has no 0xf0 bits):
13 for none, OR-ed with the first of cold 0x30 / fire 0x20 / lightning 0x40
/ poison 0x50 whose record field (0x24 / 0x10 / 0x1c / 0x28) is over 0,
else 0x10 for record flag 0x2000 (0x57b929: a mastery crit, critical
strike or deadly strike roll landed in `FUN_0057b7d0`). Magic (+0x20) is not
in that table. Any of those
puts the class past 12: a blow with an element divides by 16 whatever the
weapon. d2d: `rules::get_hit` (`Blow::elemental`, `hurt`'s class, the player's
and the merc's struck frame). Its oracle, `tools/emu/hit_rule.py`, runs
game.exe's `FUN_0057cee0` on each record (`FUN_0057ce30`, `FUN_0057cb00` and
the seed's `FUN_00472210` inside it) against `test_combat --get-hit`, the
decision and the unit's seed after each frame: no difference on 100,000
cases each for seeds 1 and 7. An armor's pool
works record by record too: `FUN_0057c1e0` raises state event 0xb
(`FUN_005c0c30`, 0x57c2fc) on each record before its resistances and its
total +0x4c, and Bone Armor (`FUN_005c6370`) takes from that record's
physical field, Cyclone Armor (`FUN_005c87a0`) from its fire, cold, then
lightning, the pool carrying to the next record; the get-hit then reads what
is left, an element emptied no longer marks. `FUN_0057ce30` reads the
fields in 256ths, so an element with under a point left still marks. d2d:
`rules::absorb_blows`. Its oracle, `tools/emu/hit_absorb.py`, runs
`FUN_0057c1e0` on each record of a frame with either armor's handler, then
`FUN_0057ce30`, against `test_combat --get-hit-absorb` (each record's total
and mark, the pool after the frame): no difference on 100,000 records each
for seeds 1 and 7. It has no reduction and no resistance on the defender
(`monster_damage.py` and `cyclone_armor.py` have those).

## Approximations still in the port (marked `ponytail:` in code)

- Crushing blow: a quarter of current life less physical resistance, with
  no boss or difficulty divisors.
- FBR acts as an animation-rate bonus on the plain frame count, not the
  block branch's (50 + FBR, 100 under state 0x65).
- Damage reduced %: capped at 50, applied before the flat reduction, which
  can reach 0.
- Open wounds: 1.10's per-level table for 8 s. Leech: × MonStats Drain.
- Stat sums (`gear_props`, character.hpp) cover worn slots 1..10, what's
  socketed in them (gems.txt by weapon / shield / other), inventory charms,
  and a set piece's bonus lists that are on (list i with i + 2 pieces of
  its set worn). The Fighter, the char panel, +skills (`skill_level`), the
  equip check (`wearer`) and magic / gold find (stat 0x50 FUN_005585d0 /
  0x4f FUN_005589a0, drops.md) all read them. Sets.txt's bonuses join
  them (`set_props`, FUN_00663b40 → FUN_00660120): with n distinct pieces
  of a set worn (a popcount of its piece mask, 0x6eda40), the partial
  PCode2a, 2b .. na, nb, at most (pieces − 1)'s; with every piece worn the
  full FCode1..8 too (pieces: the Sets record's +0xc). An add func 1 list (keyed to another piece)
  counts pieces.
- Monster life regeneration: FUN_00573cb0 sets stat 0x4a = its rolled
  life (256ths) x MonStats DamageRegen (+0x1a0) >> 12 (past 0x7fffffff /
  DamageRegen the life shifts first); FUN_005a6920 (event 3, every frame)
  adds stat 0x4a, the net of regen and poison, to life up to its max, a
  gain held back under state 0x34. d2d: `MonStats::regen`, `monster_dots`.
- Not in yet: skills (skills.md), the weapon swap, champions/uniques.

## Hit recovery: the length (SUnit.cpp)

- **Rate** (`FUN_00623f50`'s get-hit branch, 0x62409f..0x624385, a player's
  mode 4 or a monster's mode 3): AnimData speed x (50 + `FUN_00621740(unit,
  row 1)`: stat 99 through 120 v / (120 + v) for v != 0, truncated toward zero)
  / 100, 0..0x7fff, at unit +0x4c. The tail's multiply and divide are unsigned
  (0x62435c..0x624368): once 50 + effective FHR is negative, the wrapped product
  normally clamps the rate to 0x7fff. Native cases cross that threshold at FHR
  -35 / -36 (rate 2 / 32767 at speed 256); FHR -200 gives 896. At exactly -120,
  `FUN_00621740`'s IDIV divides by zero, so d2d leaves the stat unchanged as a
  defined fallback (deviations.md #13). The branch reads no other stat: chill
  (stat 69) and an aura's attackrate / other_animrate don't slow a get-hit.
- **End** (`FUN_005539b0`, from the player's mode-4 handler `FUN_0057fe30`
  and the monster's `FUN_005a7c20`): with the start frame 0 (`FUN_00623b10`)
  and the end at frames x 256 (`FUN_005533d0`) it adds the rate a tick while
  the frame stays under the end, n such ticks, and queues event 1 at the game
  frame + max(n, 1) + 1 (`FUN_005417d0`; a rate of 0: the next frame).
  `FUN_00581020` (event 1) plays the player's NU through `FUN_0057f020`; a
  monster's mode table (0x6e2298) ends mode 3 with `FUN_005a8030` -> NU. So a
  Sorceress (8 frames at 256) holds 16 ticks at 0 FHR, 15 / 14 / 13 / 12 / 11
  / 10 / 9 / 8 / 7 / 6 at 5 / 9 / 14 / 20 / 30 / 42 / 60 / 86 / 142 / 280
  (the client's animation, stepped by `FUN_00463390`, shows one frame fewer).
- Ported: `rules::get_hit_rate` / `get_hit_ticks` (the player's `set_pmode`,
  a monster's and the merc's GH), checked by `tools/emu/hit_len.py` (the two
  pieces run natively; `tools/emu/README.md` for the setup) against
  `test_combat --get-hit-length`: `uv run python hit_len.py gen 100000 7 > cases.tsv`,
  `... game cases.tsv > game.tsv`, `<build>/tests/test_combat --get-hit-length < cases.tsv > after.tsv`,
  `... compare game.tsv after.tsv cases.tsv` (0 differ). `tools/emu/hit_len_harness.cpp`
  is the old d2d's lines, for the same `compare` (the before).
- **Drawn frame** (the client): `FUN_004b1280` on the monster mode table's
  get-hit row (0x7252e0 + 3 x 12, [0, 1, 2]; `FUN_004af400`) tests "over"
  (`FUN_006217c0`: +0x44 + rate >= +0x48) and, not over, steps (`FUN_00623e00`:
  +0x44 += rate); the draw shows +0x44 >> 8 (`FUN_00621810`, 0x4715ca). The
  animation plays once at the get-hit rate and stands on its last stepped
  frame; it never loops (a Zombie: 0 0 0 0 1 1 1 2 2 2 2 3 3 3 4 4 4 over its
  17 ticks). A player's `FUN_00463390` (row 4 of 0x711e00, [0, 1, 2]) steps
  the same way and, re-testing after the step, ends the mode a tick before
  the server's event. Ported for monsters and the merc: `rules::get_hit_frame`
  (`Unit::get_hit_rate` in the renderer), checked by the oracle `tools/emu/hit_render.py`
  (game.exe's step and draw run natively on hit_len.py's cases and its rate and ticks) against
  `test_combat --get-hit-frames`: no difference on the 55,089 monster cases of `hit_len.py gen 100000 7`. Not ported: the player's own frames
  (prate, a stretch over the hold); a joined game's merc has no FHR on the wire.

## A player's attack swing: the start frame and the strike tick (FUN_00623b10, FUN_005539b0)

V (`tools/emu/swing_start.py`, game.exe's functions run natively, 11,270 cells of `test_swing_start --swing-start`).

- `FUN_005539b0` files a mode's events from `start = FUN_00623b10(unit) << 8`: it walks the animation at the unit's rate (+0x4c) and
  files the action event (an AnimData flag byte, 1, 2 or 4) on the tick its frame is reached and the end event (type 1) when the
  position reaches frames x 256. In closed form, from the tick the mode starts: strike `max(1, ceil(256 (flag - start) / rate))`, end
  `max(1, ceil(256 (frames - start) / rate))` (`rules::frame_tick`, `attack_ticks`, `attack_action_tick`). The strike is that tick
  itself; it is not the flag frame scaled by the rounded swing length.
- `FUN_00623b10` is 0 for a monster and for a player mode other than A1 and A2 (S3 and S4 only for a Barbarian or Assassin (`FUN_006235a0`, a class test),
  whose table entries are 0 anyway). For A1 and A2 it takes the weapon in hand (`FUN_00623990`: slot 4, else slot 5, a weapon that is not broken and whose requirements are met,
  `FUN_0063c9b0` -> `FUN_0062a4e0`), its class key (`FUN_00629fe0`: weapons.txt `wclass`
  through the 8-entry map at 0x7446a0: bow 1, 1hs 2, 1ht 3, stf 4, 2hs 5, 2ht 6, xbw 7, ht1 12; nothing in hand 0; a key the map lacks also 0) and reads the
  dword at `0x6e8e60 + 4 (row x 7 + player class)`. Only an Amazon and a Sorceress are not 0: 1 barehanded, 2 with a 1hs, 1ht, stf, 2hs
  or 2ht weapon, 0 with a bow, crossbow or claws (`rules::attack_start_frame`, `kAttackStart`; `swing_start.py table` prints it).
- d2d played the whole animation (no start frame) and scaled the action frame by the rounded swing length: an Amazon or Sorceress
  swing was 1 to 3 ticks too long (815 of 840 cells with a start frame), its strike 1 to 4 ticks late; the other classes' swing length
  was right but its strike one tick late in 1,068 of 8,120 cells (1,176 of the 10,430 cells without a start frame).
- d2d applies the start frame to a plain swing and to a skill whose `anim` is A1 (Power Strike, Charged Strike, Lightning Strike, Fend,
  the bow skills; `Fight::set_pmode`), as game.exe does. A skill whose `anim` is TH (a throw: d2d plays the A1 record) gets the strike rule
  only, start 0, as the game's TH mode has; its record is still d2d's A1. Jab and Impale (`anim` SQ, `start_sequence`) keep their length.
- The end event's floor in game.exe is 2 ticks (`frame + max(n, 1) + 1`), not 1: the two differ only when one tick covers the whole rest of the
  animation (rate >= 256 (frames - start)), which no player record reaches; d2d's closed form gives the same for the real records.
- U: which frames the client draws for a swing that starts in; d2d keeps drawing the whole animation over the shorter swing.

## Cold, freeze and poison lengths (SUnitDmg.cpp)

The damage record (ints): [9] cold, [10] / [0xb] poison per tick (256ths
of life) / length, [0xc] cold length, [0xd] freeze length.

- **Building it** (`FUN_0057b7d0`): [0xc] += stat 56 coldlength when cold
  damage > 0; [10] = roll(stat 57, 58), [0xb] += stat 59 (over stat 326 when
  above 1, or stat 101 instead). A monster's El per mode
  (`LAB_005a502b` / `LAB_005a5245`): cold 54 / 55 = min / max, 56 = Dur;
  poison 57 / 58 = min × 10 / max × 10, 59 = Dur × 2; frze (12) has no
  case.
- **Before resists** (`FUN_0057c140`): cold or freeze length: stat 153
  (cannot be frozen) zeroes both, else 118 (half freeze duration) halves
  them. State 0x85 zeroes the poison length, 0x83 the burn's.
- **Resists** (`FUN_0057c1e0`, the table at 0x732980: 12 × 44 bytes {record
  offset, resist, max, attacker pierce, absorb %, absorb flat, ...}, read
  by `FUN_0057bf80` → `FUN_0057be00`): value × (100 − res) / 100, res at
  most 100. Cold and freeze length use cold resist (43 / 44 / pierce 335);
  the poison length uses stat 110 poison length reduction (pierce 336),
  poison resist only the per-tick damage. `FUN_0057be00`: stat − pierce;
  a player adds the difficulty penalty to all but physical / magic, caps
  at 75 + the max stat (≤ 95; 75 with none, so PLR's 75), −100 the least;
  monsters use the same −100 floor, including physical and magic, with no
  upper cap before the immunity check. `tools/emu/sk_resist.py` compares
  the effective resistance and the magic missile's damage with 1.14d.
- **Applied** (`FUN_0057c6c0`): chill `FUN_0057af80` ([0xc]), freeze
  `FUN_0057b230` ([0xd]), poison `FUN_0057ac50` ([10], [0xb]).
  - Chill: state 11, −50 on stats 67 velocitypercent, 68 attackrate, 69
    other_animrate for a player; a monster's MonStats coldeffect
    (+0x168 + difficulty), none at 0, the length over DifficultyLevels
    MonsterColdDivisor (+0x18) when negative, 20 % for state 0x6b. At least
    1 tick, only ever lengthened; then `FUN_00623f50` re-rates.
  - Freeze: a player's is chill. A monster's is state 1, the length over
    MonsterFreezeDivisor (+0x14).
  - Poison: state 2, hpregen −per tick; a new one at least as strong
    replaces the old.
- **Rates** (`FUN_00623f50`): walk % = effective FRW (table 0x6e8e24) +
  stat 67 (100; running run × 100 / walk), at least 25, on the walk
  velocity and on the walk's animation (RN 0x65, WL / TW 0xd5, char-select.md
  "Unit animation timing"); attack % = effective IAS + stat 68, 15..175 (attack_ticks'
  eias, chill as −50 SIAS). Only stat 93 (item_fasterattackrate, by the
  table 0x6e8e24's row: `FUN_00621740`) goes through 120 v / (120 + v);
  stat 68 (attackrate: a weapon's speed as its own stat 68, the auras' and
  skills' SIAS, chill's −50) is added as it is (0x6241e7..). So an aura's
  attackrate (Fanaticism) is SIAS, not IAS (d2d: `Fighter::sias`; V:
  `tools/emu/player_attack_rate.py`, `test_player_attack_rate`). A monster (0x623f50 on): its cast mode (SC, 7:
  `FUN_006216e0`) runs at min(100 + effective FCR, 175) % of its AnimData
  speed, stat 68 / 69 not read; an attack mode (`FUN_00621580`: A1 / A2,
  and S1..S4 / SQ while the unit's current skill has UseAttackRate) at effective IAS + stat 68
  (attackrate, 100 base), clamped 15..175; a moving mode (`FUN_006214a0`)
  at stat 67 + effective FRW, at least 25; any other (not GH / DD, which
  have their own) at stat 69 (other_animrate) clamped 15..175. The speed ×
  pct / 100 is at least 1 when not 0 and at most 0x7fff.
  `FUN_00621580` (ECX mode, EDX unit type, stack unit, class): a monster's
  row is 0x6e8b90 + mode x 0x14 (class < 0x19a, else 0x6e8cd0, the same
  for these columns). +0xc set (A1, A2): an attack mode. +8 set (S1..S4,
  SQ): an attack mode when the unit's current skill (`FUN_00620250` ->
  `FUN_006439a0`, unit skill list +0x10; its id `FUN_00643ce0`) has record
  byte +6 & 8 (`DAT_006ce274`): flags bit 0x13, which the Skills.txt loader
  (0x6169d5: {"UseAttackRate", .., 0x13}) sets from UseAttackRate. 45
  non-class rows have it, among them Fire Hit, MonFrenzy,
  BloodLordFrenzy, Teleport 2, Cry Help, Overseer Whip, Suck Blood, the
  Baal / Imp teleports, Vine Attack and the druid auras' S1 / SQ casts.
- Ported: `chill_length` / `monster_blow` (combat.hpp), `Fight::land`
  (monsters), `Fight::chill_rate`; the merc and the chill's 0x6b roll aren't.

## Cures

- **NPC heal** (`FUN_00578d30`, from `FUN_00578e70` / `FUN_00578ed0`):
  life, mana, stamina to max; states 2 (poison) and 1 (freeze) off, and
  every state flagged at DataTables+0xfc (`FUN_00578c20` →
  `FUN_0063a460`: the curses). Pets: `FUN_00578ca0` (life, curses,
  poison, freeze). Chill stays.
- **Well** (`FUN_00585720`, in OperateFn 22 `FUN_005858a0`): life / mana
  Parm1 / 256, stamina; the same states off, each one counting as a
  drink. Pets: `FUN_005856a0`.
- **Antidote / thawing** (misc.txt pSpell 6): states antidote / thawing
  for len 750 (+50 poison / cold resist, +10 max), clearing cstate1 / 2
  (poison; freeze, cold). Akara stocks both. The state (`FUN_005beda0`):
  a second drink adds len to what's left; its stats set while it lasts.

## Death

- **As the player dies** (the death mode, `FUN_00580ec0` → `FUN_00535ab0`;
  a monster's kill):
  - the save's died bit: `FUN_00538650(client, 8, 1)` at `00580f7c` sets
    0x08 in client `+0xa`, the save's status (`+0x24`), softcore or
    hardcore. `FUN_0057fca0` sets it again when the death has played
    (`0057fd3f`); with a realm (`DAT_00883d50`) and a hardcore character it
    then saves at once (`FUN_00532400`);
  - experience (`FUN_005359f0`): DifficultyLevels DeathExpPenalty (0 / 5 /
    10) % of the level's span, exp_next[level] − exp_next[level − 1] (the
    Experience.txt thresholds, `FUN_00611800`), never below the level's
    start + 1;
  - gold (`FUN_005357d0`): min(level, 20) % of the purse and the stash
    together; in single player (game type 3) no more than what leaves level
    × 500, and only from the purse; the rest of the purse falls where the
    player dies (`FUN_00535510`, split into piles by `FUN_0055a090`); the
    purse goes to 0 and goldlost (stat 175) holds the loss.
- **A player's kill leaves an ear** (`FUN_0055a200`, `FUN_00580ec0`'s death
  mode; cited, decompile only, no disassembly or oracle run). `FUN_00580ec0`
  calls it, after the died bit and the experience and gold losses, when the
  dying unit is a player (type 0) and the killer is given and is not the
  dying unit, and the killer is either a player (type 0) or a monster
  (type 1) whose owner (`FUN_0058f0d0`: the summoner behind a pet or a
  merc) is a player other than the victim. Then, for the victim (EDX; ECX is
  the game, `FUN_00580ec0`'s own ECX, which the decompile hides at the
  call):
  1. victim missing or not a player: nothing;
  2. `FUN_00633640(0x20726165, &index)` ("ear " as a little-endian code)
     finds the item row (stride 0x1a8); no row: nothing;
  3. the victim's x, y (`FUN_00620870`) and room (`FUN_00620bb0`), then
     `FUN_00555da0` finds a free spot (the same call as a dropped item,
     drops.md "Where a drop lands"): the search starts from (x + 2, y + 3),
     or from (x, y) when that offset is off the rooms (`FUN_00463740`), not
     from the victim's own coordinates; none: nothing;
  4. `FUN_00559ce0` makes the item (the create record of drops.md, level =
     the victim's stat 0xc through `FUN_00625480`, so its item level is the
     victim's level; the other literals 4, 2, 1, 1 and the zeros are as the
     decompile shows them and are not resolved here);
  5. `FUN_00558aa0` puts it on the ground at the spot in the room
     `FUN_00555da0` returned (items.md "ground placement").
  What the ear holds is set in `FUN_00558d90`'s unit-type-7 branch (a record
  whose +0x2c is 0): the name is `FUN_006221a0(record unit)` (the player's
  name, `FUN_00628370` copies it to item data +0x4a), the level is that
  unit's stat 0xc (`FUN_006283d0`, data +0x48); the record's unit must be
  a player, else the item is thrown away (LAB_005590e2). Which unit the seed
  puts in the record is not visible in the decompile (the ECX of the call);
  the ear carrying the victim's name and level is the reading that fits
  steps 1 and 4, not a traced fact. d2d: not ported (no ear is ever made;
  `d2s` only reads and writes a saved one, items.md "compact reader").
- **When the death has played** (mode 0x11: `FUN_00581020` →
  `FUN_0057fca0` → `FUN_0057f700`, PlrModes.cpp): a corpse unit, the
  player's class in mode 0x11, at most 16 a player. Everything worn (body
  locations 1..12) and the item in hand go onto it; its experience stat is
  75% of what the death took.
- **Resurrecting** (C → S 0x41, `FUN_0054c0e0`): hardcore ends the game;
  otherwise life, mana and stamina full, and the player goes to the act's
  town at its start.
- **Taking one's corpse** (`FUN_0057fb70`): its experience back to the
  player; each item (`FUN_00562f30`) to its body location when that's free
  and its requirements are met, else to the inventory; the corpse goes
  when nothing's left on it.
  The worn pass (`FUN_00562f30`): body locations 1..12 of the corpse, in
  order, passes repeated while one moved something. An item whose
  requirements pass (`FUN_0062eaf0`) goes to `FUN_0055f2d0(player, item,
  &A, &B, &loc)`, A the player's item at its location, B the one at the
  other side's (`FUN_0055f240`: 4 <-> 5, 6 <-> 7 the rings, 0xb <-> 0xc).
  Both free: it fits. Both taken: no. A taken, B free (0x55f32f..): it
  goes to the other side instead, with A as its pair. For a hand the pair
  is then checked (a bow with what it Shoots, a quiver only with its bow,
  two weapons only for a Barbarian (class 4) or an Assassin with two claws
  (type 0x43), else one weapon; a monster unit (the merc) any two); a ring
  needs nothing more. No: to the inventory now (`FUN_005600a0`); fits:
  worn (event 4, `FUN_00543d80`).
- **Saving:** the d2s keeps a corpse list after the player's items ("JM",
  count, then 12 bytes and an item list each; `FUN_00533850` makes them
  again on joining).
  - Which corpse (`FUN_005697f0`): over the player's corpse list
    (inventory +0x34, oldest first, `FUN_0063d570` / `FUN_0063d610`),
    each corpse with items (`FUN_0063cd60`) scores 1 + the repair cost
    at Malah (`FUN_0062fdc0` -> `FUN_0062efb0` mode 3, npc 0x201, the
    player's quest flags for the game's difficulty, 0x5698a8..0x5698b4) of
    each item that isn't gold (`FUN_00629bb0(item, 4)`); the highest
    score is written, a tie to the later (`local_18 <= score`). None with
    items: count 0. The 12 bytes: a word, then the corpse's x and y
    (`FUN_0045adf0` / `FUN_0045ae20`).
  - Read back (`FUN_00533850`): a corpse unit (`FUN_00555230` type 0,
    the class, at 0, 0, no room, mode 0x11) with the items, on the
    player's corpse list with flag 1 (`FUN_0063d470(.., 0, 1)`); the
    saved x / y aren't read.
  - Laid when the player comes into the game (`FUN_005394a0`, after the
    player stands: `FUN_0053bc10`): each flagged corpse at the player's
    act's town (`FUN_0061ab70`) start, spawn index 0 (`FUN_0061b060`,
    tile x 5 + 3), then `FUN_005352c0`: the nearest free spot by
    `FUN_0064e7e0(room, &xy, 2, mask 0x9c19, 4)` -> `FUN_0064dea0` with
    test 2 (`FUN_0064d9b0` case 2: the plus, `FUN_0064d100`) against
    0x9c19, which holds the player's own 0x1000 footprint, and a step of
    4 (rings of radius 1, 5, 9, ..., spots 4 apart along each edge;
    `FUN_0064e7b0` steps 1); with the player on the start the corpse lands
    at (x - 1, y - 1) (tools/emu/corpse_spot.py). Mode 0x11.
- **The look:** a death COF has only a TR layer and only LIT pieces (no
  BATRHVYDTHTH); a composite piece missing in a mode is drawn as LIT.

d2d: `World::death_penalty` / `make_corpse` / `take_corpse_items`
(server.hpp), the View's `corpses` (drawn lying, clickable as -3000 - k),
d2s `parse_corpse` / `write_save(..., corpse)`.
ponytail: the gold pile isn't split; requirements aren't checked when
items go back; a saved corpse lies by the camp's start (the save's x / y
aren't read); goldlost isn't kept.

## A unit's paired records — unit +0xac (SUnitDmg.cpp)

A unit's +0xac is the head of its own singly-linked list of allocated
records, each one naming a **pair** of units: +4 / +8 a u32 type and id
(the list owner's own, i.e. the unit this list hangs off), +0xc / +0x10 a
second unit's type and id, +0x14.. a payload not traced here, +0x84 the
next node. `FUN_0040b430` / `FUN_0040b480` (file, line, size or pointer,
a register the decompile drops) are the allocator's alloc / free, as in
`path.md`'s `STRUCT_FreePath` and `items.md`'s inventory free; `0057c980`
and `0057c9f0` free through them, so this is a pool-backed list, not a
flag.

- **`FUN_0057d690`** (`__fastcall`, ECX unit A, EDX unit B; no stack args):
  read-only. Null A or B: 0. Else walks A's own +0xac list; a node
  matches when its +4/+8 equal A's own type/id (`FUN_0044be50` /
  `FUN_00451f50`, both already d2d-verified null-safe accessors) **and**
  its +0xc/+0x10 equal B's. Returns the matching node +0x14 (the
  payload), or 0 when A, B or no match. Callers: `FUN_0056f1f0`
  (skills.md's srvdofunc [2], already ported, `components/game/fight.hpp`)
  uses it to test "does A already have a record for (A, B)" before
  granting the skill's target state at Skills.txt +0x82; `FUN_0056f115`
  (not otherwise traced here) the same, before a freeze-length clamp. So
  the pair is (self, the other unit a skill's state concerns), and the
  payload holds at least a tick count the skills.md citation already
  describes as "the calc at +0x60 ticks".
- **`FUN_0057c9f0`** (`__fastcall`, ECX a pool-context pointer whose +0x1c
  is passed to the free call, EDX unit A, one stack arg unit B): frees
  (SUnitDmg.cpp:0x79f) every node in A's own +0xac list whose +4/+8 /
  +0xc/+0x10 match (A, B) exactly (raw unit +0 / +0xc, not through the
  accessors above), unlinking it from the list first. Null A, null B, or
  an empty list: no-op. Callers run from `005caf30`..`005d8e00`
  (monster/skill handling the batch didn't open) and `0057d4f0`: the
  mirror of `FUN_0057d690`'s lookup, dropping the (A, B) record once it's
  no longer wanted.
- **`FUN_0057c980`** (`__fastcall`, ECX the same pool-context, EDX unit A;
  no stack args, no null check on A — a null A faults reading `A[0x2b]`):
  frees (SUnitDmg.cpp:0x769) every node in A's own +0xac list whose +4/+8
  equal **A's own** type/id, regardless of what +0xc/+0x10 say — i.e.
  every record this list's owner still tags as being about itself. Two
  callers, both teardown: `FUN_005349d0` (a unit's death/removal chain,
  passing the room/game's own pool at the caller's own +0x1c) and
  `FUN_00553570` (freeze/resume, oracle `tools/emu/freeze_resume.py:188`,
  which calls it right before `FUN_00624690` clears the freeze state).
  Not traced: why a live node's self pair would ever stop matching its
  own owner — this reads as a guard against stale self-tagged records
  surviving a unit's teardown (and the pool slot's later reuse for a
  different unit), not an everyday no-op.
- **`FUN_0057e240`** (`__fastcall`, ECX / EDX plus two stack args, `RET
  0x8`): forwards the same four arguments, unchanged and in the same
  order, first to the already-ported `FUN_0056e170` (physical/leech
  portion of a skill's damage) then to the already-ported `FUN_0056e0c0`
  (elemental portion). No branch of its own; one caller, `FUN_005d3140`
  (Static Field, skills.md:1129, already ported), which builds a damage
  record from the result with `d2d_objects_take_blast`. d2d: no function
  of its own found for this pairing; not flagged as a difference since
  the two pieces it combines are already ported separately and the
  combiner adds no logic.

`FUN_0056e170(skill, level)` (ECX the caster, EDX the damage record) does
nothing for an id outside `0 <= id < count` or a null row. Otherwise it
takes FUN_00647bc0 (the minimum, by its use below) and FUN_00647d00 (the
maximum), both called as (unit, skill, level, 0); then
FUN_0045c3e0 on the caster's seed (`+0x20`) with bound `max - min` (so
the roll is `0..max-min-1`: the maximum itself never comes up; a bound under 1
gives 0 and leaves the seed alone); and adds `min + roll` to the record's `+8`.
The decompile hides the bound argument (it is EDX at the call; seen in the
disassembly, `0056e1c1`). Run with FUN_00647bc0 / FUN_00647d00 hooked (logged,
scripted results) and the real seed draw, 36 cases:
`tools/emu/skills_combat_helpers.py`. Their formulas and d2d are not compared.

d2d: no function of its own for the +0xac list; nothing in `ai.cpp` or
`fight.hpp` was found keeping a per-(caster, target) record list, so this
is cited, not matched. Not a behavioural difference by itself — the
callers above are already ported and credited to the functions that use
this list's result.
Re-check: `python3 tools/ghidra/fn.py 0057d690 0057c9f0 0057c980 0057e240
0044be50 00451f50 0056f1f0 005d3140`.

## A unit's +0xdc field — FUN_00553980 (SUnit.cpp)

`__fastcall`, ECX the unit: null-safe getter, `unit[0xdc / 4]` (byte
+0xdc) or 0 for a null unit. No caller outside `.\UNIT\SUnit.cpp`'s own
sequence/overlay helpers (`00540df0`, `00540e60`, `00540ea0`, `00540ee0`,
`00540f30`, `005415a0`); the field itself isn't named here, not traced
further. d2d: no function of its own; not flagged as a difference, since
nothing was found reading or writing this field outside that same
cluster.
Re-check: `python3 tools/ghidra/fn.py 00553980`.

