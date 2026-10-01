# Monster AI — noticing the player, alerting, thinking (1.14d game.exe)

How a monster finds a target and when it thinks. Placement and population
are in monsters.md; the per-AI thinks are in `components/rules/monsters.hpp`
(`mon_think`).

## The driver — FUN_005b1740

Run whenever a monster's think event comes due (PTR 0x6e2498, called from
the mode-end and timer handlers FUN_005a8030 / FUN_005a8330 / FUN_005a83e0).
It fills a params block (AI control at monster data +0x28, MonStats row)
and goes, stopping at the first step that acts:

1. FUN_005b10e0: state 0x15 stands 3; FUN_005b0f50; FUN_005b0ff0 (flee from the unit at AI control +0x0c).
2. FUN_005b1650: the target search the MonAI table entry names (0x73ca18 + ai × 16; a special AI's 0x73d358, FUN_005b15d0):
   - type 1: FUN_005de890 (search; none found: stand or wander),
   - type 2: FUN_005dd7f0 (search only),
   - types 4 and 5: FUN_005de9d0.
3. FUN_005b13e0: first-sight speech (FUN_005b1140, sets AI flag 0x10), the teleport mod (FUN_005b11f0), the leash. If the target is farther than the level's byte +0x2f (FUN_0061db70), it searches again (FUN_005dc640 / FUN_005dd0b0 mode 0xb), else it wanders 4.
4. The AI's think proper (MonAI entry +4).

## The target search — FUN_005dd7f0

fastcall ECX game, EDX monster; stack: AI control, out distance, out in-melee.

### Is line of sight needed?

1. AI control flags (+0x08) & 0x40: yes, whatever the room or flag 8. The flag is cleared on the spot, and the area isn't read or written. It's a one-shot set by FUN_005deb60 when a move can't set off and its flags have bit 1 (through FUN_005dd230). Of the move helpers only the walk (FUN_005dec80 / FUN_005ded00 / FUN_005ded40) passes the AI's own flags, so it's a "walk n" with n & 1 (the walk 7 of Skeletons and Andariel) that failed.
2. Else the monster's room (FUN_00620bb0) is checked: FUN_0061aa40 → FUN_0066ba70 is true for a room2 of type 1 (a plain outdoor room), or of type 2 (a preset) with flag 0x80000. FUN_00667ed0 gives a preset's rooms 0x80000 when its LvlPrest Outdoors (+0x10) is set. True: no sight needed.
   - In Act 1, Outdoors is set on every preset that sits in an outdoor level, plus the Monastery front (26), the Courtyards (27, 32) and Tristram (38). It's clear on the caves, crypts, the Tower, Barracks, Jail, Cathedral and Catacombs presets.
3. Else sight is needed while flag 8 is clear (the monster has never found a target). Even then it isn't needed when the monster's area (monster data +0x50) has its +0x24 set.

### The candidates

The unit lists at game +0x10f8 are 10 heads of {unit, ?, next +8, prev +0xc} nodes; a unit's +0xd0 holds its list (0xb: none).

- Lists 0..7 are one per player: the player at the head (FUN_005b1880), its pets after it (FUN_005b1900, from the summons FUN_00573270 / FUN_005c4b00 and the merc).
- List 8 holds good monsters (alignment 2): FUN_0054ef50's random spawns, AIs FUN_005edc50 / FUN_005ee3c0, superuniques with flag 0x400 and alignment 2 (FUN_005424f0). FUN_005b1990 adds them, FUN_005b1a90 takes them off.
- List 9 holds neutral monsters (alignment 1): Confuse (FUN_005c3de0), Attract (FUN_005c3b90), FUN_005aeda0, FUN_005c58b0 / FUN_005c5bc0, superuniques with alignment 1.
- Alignment is stat 0xac (FUN_006259b0; FUN_005543b0 sets it, ECX unit, DL value, and skips 0 on a unit with +0xc4 & 0x80000000): 0 evil, 1 neutral, 2 good. Players are 2; units not players or monsters read 2.
- Friends (FUN_00650d70): two evil ones, or two good ones. Enemies (FUN_00554200) first trade a monster for its owner (FUN_0058f0d0: AI control +0x28 set, owner id +0x2c); a unit isn't its own enemy, two players go to the PvP flag, else enemies are those not friends. So a neutral monster is everyone's enemy, the other neutrals' too.

The search, in order:

1. A skill-set target (FUN_005dd610). Monster data +0x38 is a kind and +0x34 a value, written by FUN_00573090 only for kind < 5 when the MonStats row's byte +0xe & DAT_006ce268 (FUN_00573040: the SwitchAI column; 585 rows, not Andariel, Blood Raven or Duriel). Its only callers are Attract's callback (005c3b64: kind 1 for a player, else 2, the attracting unit's id) and Confuse (005c3ef6: kind 3). FUN_00573120 clears both; event 10 (0x5a7f70) runs it at the curse's end.
   - Kinds 1, 2, 4 look the unit up by id (FUN_00552f60: the type's hash at game + DAT_006e10e0[type] + (id & 0x7f) × 4, chained at +0xe4). From FUN_005dd7f0 there's no sight test. The distance is FUN_005dc380(it, monster): the deltas less its size (FUN_00620510), floored at 0, then (min + 2 max) / 2.
   - Kind 3 draws `seed() & 1` (FUN_00472210, unit +0x20), sets its own alignment for the search (neutral: 2 on a draw, else 0; else a draw swaps 0 and 2), runs mode 5 (below) with sight as needed and takes the primary only, then puts the alignment back.
   - A live player or monster (or an object) is returned and the rest skipped; else FUN_00573120 clears it.
2. An evil monster (FUN_006259b0 == 0), best from MonStats aidist (+0x52 + difficulty; 0 → 0x23 = 35):
   - Lists 0..7. A player counts in the same act (+0x18), in a room (FUN_00620bb0), not in a town room (FUN_0061ab00 → FUN_0066bab0 → FUN_006426a0: levels 1, 0x28, 0x4b, 0x67, 0x6d). Else the player and its pets are skipped.
   - `nearest` takes the player's distance (FUN_005dc530: (min + 2 max) / 2 of the deltas, from FUN_006488c0 / FUN_00648900 on the path).
   - At 0x37 or more the player and its pets are skipped. A dead player (FUN_005541b0: player mode 0 or 0x11, monster mode 0 or 0xc, or +0xc6 bit 1) counts 0x7fffffff.
   - Then the player and each pet after it is taken when its distance is under the best and sight passes or isn't needed. Pets have no act, room, town, 0x37 or death test, and don't count for `nearest`.
   - List 8: same act, under the best, sight. No 0x37 cap.
   - List 9: the nearest one in the same act with sight, from its own 0x7fffffff. FUN_005dd510 decides: with no best it's taken. If it's under 6 away and there's no path to the best (FUN_00648* / FUN_00649970), a FUN_005dd0b0 mode 7 search runs; the list 9 unit is taken when that finds nothing or something over 0x13 away, else the best becomes what it found. Otherwise the best stays.
3. Neutral and good monsters search through FUN_005dd0b0 mode 5 instead: the primary, else the secondary (FUN_005dd510).

Mode 5 (FUN_005dcf70, callback FUN_005dca70), block {primary, best 0x7fffffff, need sight, 0x23, area FUN_0061b130(room, x, y), secondary, best 0x7fffffff}:

- The searcher's room's near rooms (FUN_00619790: room +0 the array, +0x24 the count), skipping town rooms and those with +0x78 clear; each room's units from +0x74, next at +0xe8.
- A player or monster, alive, +0xc4 & 4, an enemy: at most 0x23 away by FUN_005dc380, under its slot's best, and seen if sight is needed. Threat (FUN_005dc920: a player 14, a monster MonStats +0x4e) 2 or more fills the primary, else the secondary.
- Else, a monster seen by an evil searcher that needs sight: when it isn't in mode 0 / 0xc, has AI flag 8 and stands in the searcher's area, sight isn't needed for the rest of the walk.

`tools/emu/search.py` runs FUN_005dd7f0 on random players with pets, good and neutral monsters in lists 8 / 9 and evil ones besides, in shuffled near, town and unsearched rooms (acts, deaths, sizes, threats, walls, flags 0x40 and 8, outdoors, the area flag), with an evil or neutral searcher carrying no skill-set target, Attract's (a monster by id, maybe gone) or Confuse's. game.exe's FUN_005dd610 and mode 5 run natively. Against `rules::search_pick` / `search_near` and ai.cpp's flow: 20 000 cases, 16 706 with a target, all match. `--break` (list 9 ignores sight, threat 2 counts low) gives 1 259 mismatches.

### Found one

- Unless FUN_006259b0 == 2, AI flag 8 is set. Nothing clears it (no `AND [+8], ~8` anywhere in the AI code), so a monster needs sight only until its first target.
- The area's +0x24 becomes `old == 0`, where `old` is the flag as read above. It reads 0 when the monster was already aware or saw for itself, so the flag gets set; a monster that got in on the flag clears it. While an aware monster in the area keeps finding its target, the flag keeps coming back.
- In-melee is FUN_00622c40 (unit_distance ≤ MeleeRng + 1, with a path test, mask 0x804). The distance out is the best, or `nearest` when none was found.

### The area — monster data +0x50

- Set at placement: FUN_005b2a00 passes the placement context's area, or FUN_0061ad30(room, x, y) → FUN_0066ceb0, into FUN_00552d60.
- FUN_0066ceb0 (ECX room2, EDX x, stack y; subtiles) reads room2 +0x64. With flag 1 it returns the single record at +0x30 (FUN_0066ccb0: the whole room). Else it looks up FUN_0067c570(+0x1c, x / 5 − room2 +0x34, y / 5 − room2 +0x38): a grid of tiles where FUN_0066ca50 wrote each record into every cell its rect covers (FUN_0067c4f0). FUN_0066ca50 builds the rects over disjoint, unclaimed cells of one label, so the grid's record is the one rect holding the tile. d2d's `room_areas` are those rects; the first holding the spawn tile is the same one.
- FUN_00554670, run when a unit changes room, zeroes it. A monster that leaves its spawn room has no area from then on.

### Line of sight — FUN_00622aa0 → FUN_00622920 → FUN_0064e260

- FUN_00622920 takes ECX x1, EDX size2, EAX x2; stack y1, size1, y2, room, mask. It returns 0 when the line is clear.
- The sizes come from FUN_00620510: a player is 2, a monster its MonStats2 +8, an object its +0x18a, a missile 0. Each is clamped to 2.
- If |dx| + |dy| < size1 + size2, the units see each other.
- Else the ends step their sizes toward each other: along x when |dy| ≤ |dx|, along y when |dx| ≤ |dy| (a diagonal moves both).
- FUN_0064e260 walks the line from the first end to the second over the rooms' collision words (room +0x4c/+0x50 origin, +0x54/+0x58 size, +0x20 → {+8 width, +0x20 words}), both ends included. Step k of max(|dx|, |dy|) is the long axis + k and the short axis + floor(k × short / long). Any word & mask, or leaving every room, blocks.
- `rules::sight_blocked` ports it. `tools/emu/sight.py` checks it against game.exe on random walls: 20 000 cases, 1 606 of them blocked, all match.

### FUN_005de890 (search type 1) with no target

1. Got hit (FUN_005dd2b0: the last mode was 3 or 0x13) and FUN_0046c140: wander 5.
2. Else, a door near (FUN_0064d910 mask 0x40) and FUN_0046c140: wander 5.
3. Else stand: 10 frames when `nearest` < 25, `nearest` − 10 under 35, else 25.

## Confuse and Attract

- Confuse (FUN_005c3f20, each unit FUN_005c3de0): a monster that's evil, an enemy of the caster, alive and passes FUN_0056e2f0 gets the curse state, alignment 1, list 9, kind 3 (FUN_00573090) and event 10 at the end. Its remove callback (FUN_005c3db0) puts alignment 0 back (not on +0xc4 & 0x80000000), drops the state and takes it off the list.
- Attract (FUN_005c3b90): the caster's target, evil, alive, not in town and hostile, gets +0xc4 | 0x40, alignment 1, list 9 and the state. FUN_0056e780 then runs callback 0x5c3b30 over the area round it (FUN_0056d2c0): each hostile unit gets kind 2 (kind 1 if the target's a player) with the target's id, and event 10.
- So the cursed monster hunts whoever is near (Confuse: anyone in mode 5, evil or good by the draw), and Attract's crowd hunts the lured one until it dies or the curse ends.

### Monsters hitting monsters

- A death drops whoever the killer (FUN_0057ccb0 → FUN_005a4ef0 → FUN_0053f720). The quest record (FUN_0066a220) is a player killer's only.
- Experience (FUN_0057e7b0) goes to the player only when the killer or the dead one carries the player's stat list (the curse, flag 0x800).

## Alerting

There's no call for help. A monster wakes in only these ways:

- **Range.** Outdoors (plain rooms and Outdoors presets) no sight is needed. Every monster whose think comes due with a player under its aidist (35 subtiles, about a screen) takes them.
- **Its area.** In caves and the like, one that sees you sets its area's flag, and the next unaware one in that area to think takes you without sight.
- **Its leader.** The Fallen's leader within 15 (aip1 %) and the Fallen Shaman (aip1 %) set their group charging. FUN_0058f730 hands command 1 to each minion, and FUN_0058ef40 to itself.
- **Being hit doesn't make it aware.** Flag 8 is set only by a found target. A monster that got hit and found nobody wanders 5 (above).

So when an Act 1 Fallen stands still at 7 tiles, it isn't because it can't see you. Its think only walks in from aip2 (10 subtiles) unless it got hit or was rallied.

## When it thinks

- A mode end (FUN_005a8030) thinks at once when DAT_0073c6d0[mode] is set: WL (2) and RN (15). Other modes go to NU through FUN_005a7c20.
- A mode that can't start, or NU, schedules the think aidel frames on (FUN_005a73e0: MonStats +0x4f + difficulty; 0 → 15; 0x2d in state 0x15). That's skipped if an event is already due later.
- FUN_005de080(n) (stand n) schedules it n frames on.
- A move gets a re-path budget of 0x14 (FUN_005a7c20 → FUN_006490e0, path +0x94). See the next section.

## A move — path, chase, end

The path is D2DynamicPath (unit +0x2c):

| Offset | Holds |
|---|---|
| +0 / +4 | 16.16 position |
| +0x10 | SP1, the dest |
| +0x14 | SP2, the dest when last pathed |
| +0x18 | SP3, where the path was computed to |
| +0x24 / +0x28 | point index / count |
| +0x3c | type |
| +0x58 | target unit |
| +0x91 | steps |
| +0x93 | stop distance |
| +0x94 | budget |
| +0x9c | points (subtiles) |

- **Set off.** The walk FUN_005dec80 is FUN_005deb60(target, mode 2, stop 1), and FUN_00649070 makes +0x93 = 0.
  - FUN_005a7c20 sets the target, budget 0x14, then FUN_005a63f0: a move mode's ctx byte 0x65 gives type 0xd and steps 5. FUN_005a6290 computes the path (FUN_00649970) and retries type 0xf when that comes out empty.
  - With no points, the mode start (FUN_005a7520 / FUN_005a7550) fails and FUN_005a73e0 schedules the think aidel on.
- **Compute (FUN_00649970).** No path when dest = position, or the dest is over 100 subtiles off. SP3 = SP2 = the dest (a target's spot through FUN_006498a0, whose 1 for a player or monster is `near`). The type's pather is taken from 0x6eb6d8. Leading points whose centre the unit stands on are skipped (FUN_0064fe40).
- **Toward pather (FUN_00679c80; types 2, 5, 6, 0xd).**
  - A Bresenham line (FUN_00679720) to the dest. Clear: that's the path, whatever its length.
  - Else it stops at the last free subtile. Within `near` of the dest (FUN_00679380 = unit_distance at size 1), that subtile is the path.
  - Else from there up to `steps` 8-way steps. Each is the first free of three directions (DAT_006f1518 by FUN_00678c10's 5x5 index), and the walk stops at the dest, a wall, or a step back.
  - Points go at each turn and at the end, unless the last step turned. The cut-short subtile comes twice when the first step leaves from it.
  - Type 0xf is FUN_0067c2d0, a search of up to 0x28 steps.
- **Each frame (FUN_00650840, from FUN_00554ca0 in the WL / RN updates).** FUN_006503f0 runs first:
  - With a target unit: stop when unit_distance ≤ +0x93. Re-path (FUN_00650350) when the target, a player or monster, stands over 5 subtiles from SP2 on either axis, or when idx ≥ count short of SP3.
  - With none: re-path only on the last test.
  - FUN_00650350 is refused when a monster's budget is 0. Otherwise the points reached (+0x24) come off the budget, floor 0.
  - Then it steps one velocity toward points[idx]. It snaps onto the point when within the step (FUN_00650090), so one point a frame.
  - A blocked subtile crossed (FUN_00650150) ends the path (idx = count). A path with flag 0x10 re-paths instead. FUN_00649d00 doesn't set the flag.
  - At idx ≥ count after the step, FUN_006507b0 stops: snap to the subtile centre, clear flag 0x20. So the idx ≥ count re-path in FUN_006503f0 doesn't come up from here, and the budget is spent only by a target that moves.
- **End.** The update sees the stop and calls FUN_005a8030 the same frame. WL and RN think at once. A chase ends at its target, at its path's end (5 steps on, or the clear line's end), when blocked, or when a re-path is refused.
- `tools/emu/moves.py` runs FUN_00679c80 on random walls and FUN_006503f0 on random paths: 20 000 cases each, all match `rules::toward_path` / `rules::chase_check`. 10 259 paths end short of the dest, and the chase cases split 7 657 stop / 6 175 go on / 6 168 re-path. `--break` (re-path at 4, no step-back stop) gives 196 and 667 mismatches.

## Which rooms think

- Units update in the active rooms around each player. The near list FUN_0066c370 takes rooms less than 6 tiles apart.
- d2d updates every monster within 30 cells of the player (Fight::world). That covers more than game.exe's active rooms, and a monster can't take a target past 0x37 subtiles (11 tiles) anyway.

## In d2d

- `ai.cpp search_target` is the search for both the traced thinks and the AIs not traced (those used to notice within 8 cells and chase within 16, by eye).
- Line of sight is tested in a preset room whose LvlPrest isn't Outdoors (`GameData::prest_outdoors`), until `Monster::sighted`.
- The area flags are `Fight::area_seen`, keyed by level, spawn room and area index.

- Foes are the player, then the merc and pets (`Foe::pet`), each with its size (`Foe::size`: the merc's MonStats class, a pet's type).
- A walk n with n & 1 that can't set off sets `Monster::force_sight` (flag 0x40).
- A move is `ai.cpp path_to` / `move_frame` over `Monster::steps` and `budget`: the toward path, the chase check, one point a frame, the snap at the end. It thinks at its end. AIs not traced set off at their thinks and search at the same points.

- `Monster::align` (0 evil, 1 neutral while Confuse or Attract holds it) and `set_kind` / `set_id` / `set_until` (the skill-set target, SwitchAI monsters only; `MonType::switch_ai`, `threat`). With either about, every monster joins the foes after the player's side and is hit as the player is; experience only through the curse (`Fight::killed`'s credit).

Approximated (`ponytail:` in ai.cpp, fight.cpp, monsters.hpp):

- FUN_005dd510's path test always finds a path, so a list 9 monster is taken only when nothing else was.
- Mode 5's candidates are every foe in foes order, not the near rooms' units room by room; its area is the room_areas rect at the unit. List 8 is empty (no good monsters) and kind 1 (a player by id) and 4 aren't set.
- A monster's missiles, poison and Mana Burn reach only the player's side; monsters block as the player does.
- Confuse's and Attract's duration is auralen's, not FUN_005c37a0's; FUN_0056e2f0's test is unread.
- The search pather (type 0xf, FUN_0067c2d0) is `rules::find_path`'s turns over its first 0x28 subtiles. Movement is floats in cells, blocked as `monster_step` has it, and the target's +0x68 offset is taken as 0.
- A dead pet is skipped (game.exe takes it off the list), and a dead monster leaves list 9.
- An untraced AI chases the nearest foe, whichever it found.
