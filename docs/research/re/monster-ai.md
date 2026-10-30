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

- FUN_0046c140(ECX class, EDX 2) is the MonStats2 mode bit (record +0x18 → MonStats2 row, 0x134 bytes, byte +0xf0 + bit / 8 & DAT_006ce268[bit & 7] = 1 << bit): bit 2 is mWL (checked in the emulator against MonStats2.txt). So only a monster with a walk wanders.
- FUN_0064d910(room, x, y, the path's collision pattern FUN_00649180, mask 0x40) tests the grid under the unit.
- There's no other idle wander: Levels.txt MonWndr is the wanderer spawn (objgroups.cpp), not this. d2d's old home wander (2–5 s, 3 cells) is gone; `think` does the above on the unit seed (`rules::think_wander`).

## The attack's target

FUN_005ddf90 / FUN_005dead0 / FUN_005deb60 build the mode ctx with FUN_005a7e60 ([0] mode, [1] unit, [2] the target) and FUN_005a7c20 puts the target on the path (+0x58). A hit, a shot or a skill goes at that unit, not at whoever is nearest. In d2d `Monster::chase` holds it for every mode a think starts (`use`, a walk), cleared when the search finds nobody; `monster_update`'s foe is that one while it lives, else the nearest of the player's side.

## The Gargoyle Trap's shot — FUN_005cc050 (srvdofunc 93)

FUN_00553540 gives the trap's target. On the axis the trap is nearer the target, the trap's coordinate moves up to 4 toward the target's; on the other axis the point takes the target's. With offset (dx, dy) = point − trap, the missile (FUN_0059fa30, flags 3) starts at trap + dx / 6 − 1, trap + dy / 6 − 1 (C division, −1 on both axes whatever the sign; bugs.md #14) and flies along (dx, dy). `rules::gargoyle_shot`.

## Missile velocity — FUN_0059fa30

Every missile made here (Andariel's, the Shaman's, a MissA shot, the player's skills through FUN_0056ecb0 / FUN_0056ee90, flags 0x21 / 0x420, no flag 4) gets path velocity ((Vel + VelLev × lvl / 8) << 8) × 75 / 100 (FUN_00648690: path +0x7c, the walk's field). The flight is traced in missiles.md and built as `rules::MissileFlight` / `game::missile_fly` for every missile.

## Boss mods in the hit

- Cursed (FUN_005a2530, unique flag set): the monster seed's next draw, `& 3` not 0, then Amplify Damage (66) at level mlvl / 5 + 1 through FUN_0056dbc0(ECX game, EDX 3; monster, level, radius, FUN_005a23d0): radius aurarangecalc (Skills +0x64, ln12: 3 + (lvl − 1)) clamped 1..40, round FUN_0056d2c0's point (the monster's target unit, else its path target). FUN_005a23d0 curses each enemy there (FUN_00554200). d2d: the player's side within the radius of the one struck.
- Mana burn (FUN_005a1f90): MonLvl DM (L-DM in an expansion game) for the difficulty × MonUMod constants FUN_005a00f0(0x10 + d) / (0x13 + d), or (0x1c + d) / (0x1f + d) with the unique flag, / 100, into stats 62 / 63. Champions never get the mod.

## Confuse and Attract

- Confuse (FUN_005c3f20, each unit FUN_005c3de0; the test FUN_005c3d50): a monster that's evil (FUN_006259b0 0), an enemy of the caster (FUN_00554200), alive and passes FUN_0056e2f0 (not a boss, unique or superunique; swaps_ai) gets the curse state, alignment 1, list 9, kind 3 (FUN_00573090) and event 10 at the end. Its remove callback (FUN_005c3db0) puts alignment 0 back (not on +0xc4 & 0x80000000), drops the state and takes it off the list.
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
  - Type 0xf is FUN_0067c2d0, the wall pather (collision.md "Path types"): a line of up to `steps` (0x28 at least at a unit), two tracers round each block.
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

## Thinks: Fetish, Arach, Vampire, Griswold, the Smith

Act 1's last untraced MonAIs (MonStats AI column, levels 2..39): fetish1 (Catacombs, MonAI 30), arach1 (26), vampire5 (28), and the superuniques Griswold (griswold row, 90) and the Smith (smith, 98). All are search type 1. Think params block: [0] AI control (+0x14 / +0x18 / +0x1c the scratch words), [2] target, [5] distance, [6] in melee, [7] MonStats row. aip1..5 are MonStats +0x56 / +0x5c / +0x62 / +0x68 / +0x6e + difficulty × 2; Skill1..4 +0x170..+0x176, Sk1..4mode bytes +0x180..+0x183.

| AI | think | in `mon_think` |
|---|---|---|
| Arach | FUN_005f4510 | life % own (FUN_00621f20); Spider Lay unless in state 0x16 (FUN_00639df0) |
| Vampire | FUN_005f4a70 | aip5 bits 0 / 1 / 2: shots (Skill1 / Skill4) / Skill2 / Skill3; second target FUN_005ddc30 |
| Fetish | FUN_005f53e0 | the target's life % against aip4; leader commands through FUN_0058ee80 / FUN_0058ed10 |
| Griswold | FUN_005e5ac0 | |
| Smith | FUN_005e3890 | pace (100 − life %) / 2 |

- **rand(100).** FUN_0045c390 on the unit seed is one step, the same as the inline `seed % 100`.
- **Pace.** FUN_005de190(pct −126..126, steps) → FUN_005a6260 writes the monster's path ctx (monster data +0x2c): +0x18 type, +0x1c pct, +0x20 steps (cap 0x4d), each only when not 0. FUN_005a63f0, in FUN_005a7c20 for any mode but GH, takes them and clears all three; the pct goes to ctx +0x10 and FUN_005a6380 puts it on as stat 0x43 (velocitypercent). So a pace speeds the very next mode only. The Fetish's is 50, the Vampire's Run × 100 / Velocity − 100 clamped to 0..120.
- **Back-off flags.** FUN_005defe0(target, n, f) sets steps n when n > 5 and calls FUN_005deb60 with flags 4 when f. A failed mode start schedules the think aidel on (FUN_005a73e0); flags 4 then remove the pending type-2 events (FUN_00540e60(2, 0)). Flags 2 is the walk's 70 % wander 4, else stand 10.
- **Second target (FUN_005ddc30).** The skill-set target (FUN_005dd610), else FUN_005dd0b0 mode 6 (callback FUN_005dcbd0: the near rooms' units, enemies by FUN_005dc970, FUN_005dc380 distance under 0x31, sight always (mask 4), threat 2 or more primary, else secondary), then FUN_005dd510.
- **Skills.** Spider Lay is srvdofunc 23 (FUN_005c9c10; aurastate spiderlay = state 0x16, auralen 300, aurastat velocitypercent −100, anim A2). VampireFireball and VampireMissile are srvmissile vampirefireball / firehead (SC). VampireFirewall is srvdofunc 24 (FUN_005c9ea0, the Countess's), VampireMeteor 28 (FUN_005ca3e0). The srvdofunc table is at 0x7322b0. vampire5's aip5 is 1, so in Act 1 it only shoots.

## Act 2's thinks

Act 2's MonAIs (MonStats AI on levels 40..74, MonAI table 0x73ca18 + ai × 16: `{search type, init, think, other think}`). Params block as above: [0] AI control (+0x14 / +0x18 / +0x1c), [2] target, [5] AI distance, [6] in melee, [7] MonStats row. Search types 1 and 4 skip the think when none is found (FUN_005b1650), so every think below has a target.

| AI | # | think | in `mon_think` |
|---|---|---|---|
| SandRaider | 8 | FUN_005f0700 | thinks counted (state): at aip5 its overlay (aip6 1: 0x96, else 0x2e), stands aidel + 1; past it state 0x5a / 0x5b on, Skill1 (Fire Hit) next in melee; hurt it walks to its nearest other monster (FUN_005f06b0), seven times |
| Baboon | 11 | FUN_005f0cd0 | hit under aip1 % life, 50 %: flees rand(5) + 2 thinks, regen (stat 0x4a) up aip5 / 8, pace Run / Velocity |
| SandMaggot | 15 | FUN_005f1800 | Skill1..3 (MagottUp / Down / Lay) on frame timers (state2), FUN_005de130 events, never stands |
| ClawViper | 16 | FUN_005f1b60 | the charge (Skill1, FUN_005fd470 usable) within aip2, aip6's state 0x5a / 0x5b while it charges |
| SandLeaper | 17 | FUN_005f20a0 | the leap within 5; past 10 bounds about the target (FUN_005df530, pace 75) |
| PantherWoman | 18 | FUN_005f22b0 | runs in (pace 75), or regroups with its nearest kin past aip3 (FUN_005b0d00, squared) |
| Swarm | 19 | FUN_005f2460 | walks in / A1 / stands aip2 |
| Scarab | 20 | FUN_005f2540 | its leader (FUN_0058f0d0) sends the group charging (FUN_0058f730); charging: Skill1 (Jab) in melee |
| Vulture | 23 | FUN_005f3170 | aloft (state: thinks left) it flies (S1, FUN_005de440) to a point rand(2r) about the target pushed clamp(2r, 12, 36) off itself (state2 / 3, re-placed on a free spot, FUN_0064e7b0); dives at a weak one (FUN_005f30f0: within 11, at most aip3 % life); lands (FUN_005f2fc0, S2) and fights; takes off (FUN_005f2eb0) for rand(8) + 24 thinks |
| Mummy | 21 | FUN_005f2850 | hit: walks in; within aip1 swings or walks in; else wanders 3 |
| GreaterMummy | 22 | FUN_005f2b10 | Skill2 (Bestow) on a hurt one, Skill1's sequence (FUN_005de000, Resurrect2) on a corpse, Skill3 (UnHolyBolt) at the second target (FUN_005f2a00 searches aip5, squared: a monster's lUndead, MonStats +0xd & 8, not a unique, FUN_005a0180 EDX 8; Radament, class 0xe5, aip5 + 10, uniques too, and on Normal any undead, FUN_0063e990; then FUN_00599420, his quest's) |
| BatDemon | 29 | FUN_005f5040 | states 0..4: rises (S3), drops (S4), lands (S2), flees; regen as the Baboon's |
| PinHead | 39 | FUN_005f6340 | Skill1 (Smite) / Skill2 after its first swing |
| MaggotLarva | 38 | FUN_005f6220 | out of melee aip3 % walks in (flags 1), else stands aip4; in melee, rested (state 0), aip1 % A1 (state 1), else stands aip2 |
| MaggotEgg | 40 | FUN_005f6530 | aip2 % hatches (Skill1, wait aip1; state 1); hatched it dies (FUN_0057ccb0); else stands aip1 |
| Duriel | 44 | FUN_005f67b0 | Holy Freeze (Skill4) on when off (FUN_006201d0 → FUN_005701b0); Charge out of melee, Smite / Jab / A2 / A1 in it |
| SandMaggotQueen | 66 | FUN_005f9cf0 | Coldworm; search type 0 (no target looked for, the think runs anyway): rested (+0x1c) stands aip2 × 25; under aip1 young (+0x14) S1 where she stands (FUN_005de440 mode 8 at her own spot, +0x18 1), then FUN_0063efa0 (BaseId 0x11c: her MonStats spawn 8 subtiles east, mode 8 S1) and FUN_005b2f20 within 2 (flags 0x42, the young flag 0x4000000), counted if made; each a wait of aip2, rested after; done, nothing. `mon_ais.py SandMaggotQueen` (50 000 runs, the make answered "not made"), `tests/mon_ais_coldworm_cases.inc`; ai.cpp `MonAct::spawn` |
| NpcStationary | 54 | FUN_005e73a0 | Tyrael (class 0xfb) in the Lair: once his portal is open (FUN_0059df50: the Seven Tombs' record +0xf; +0x3d once gone) and no player is within 12 (FUN_006416d0, the sweep at 0x59df30), not in a talk (FUN_00572dc0), he leaves (FUN_005ddfc0 mode 0), the takers (quest 14 bit 13) getting unit event 0x36 (LAB_0059c710); class 0x196 the same on FUN_005b43f0; else it faces its talk partner (FUN_005ddf20) and stands 20. `World` hides him, for good |
| Summoner | 53 | FUN_005f85c0 | first think opens his quest (FUN_0059c330); the element the target resists less (stats 0x27 / 0x2b) |
| PantherJavelin | 95 | FUN_005e1080 | throws at its second target (FUN_005ddc30; none: 0x7fffffff off) |

Act 3's MonAIs (levels 75..102, same table). Search type in brackets where it isn't 1; types 2 and 5 think with no target found.

| AI | # | think | in `mon_think` |
|---|---|---|---|
| Mosquito | 24 | FUN_005f3730 | in melee aip3 % (or before its first swing) aip4 % Skill1, else A1; else 80 % stands 15, 20 % flies off: back off 10 (state 2), drift (wander 4) aip5 thinks (state 1), walk in (state 0) |
| WillOWisp | 25 | FUN_005f39b0 | the dance: with a target within 32 past its cooldown (+0x1c, a frame), rand(1000) <= difficulty + 2: state 4, the wisps within 32 (FUN_0065a950, class 0x76, itself too): four (word 1 6) or five (5) each get word 2 0, word 3 its place + 1 (FUN_0058ec00), all standing to the next 337th frame. A step (states 6..12 by word 3 + state x 4; 5 the fives' tables) walks to the target + its point (FUN_005de490), 34 % a wander 4, while past 3 off (FUN_005dc5c0); there each 67th frame up to 3 SC casts at the target + its other point (FUN_005ddfc0 mode 7), then the next step; done: state 2, 1800 frames' cooldown. A player it's at gets stat 0x50 (magic find) (difficulty + 1) x 50 for 0x1a5e00 frames each step. Otherwise S1 (state 1), SC out of melee / A1 in it, walk in or wander. The tables live on its stack (read off game.exe in tools/emu) |
| ThornHulk | 27 | FUN_005f4850 | out of melee runs in (7); frenzy (Skill1) for aip6 casts, then a 3-think cooldown (state2); aip1 % fights (aip4 % frenzy, aip2 % A1, else A2), else aip3 % circles 4, else stands 15. A cast waits aip5 past its next think event (FUN_005415a0 type 1) |
| FrogDemon | 52 [5] | FUN_005f8260 | state3 0 / 1 under / 2 up: Submerge (Skill1) past 12 off; Emerge (Skill2, at itself) when it can land (FUN_005f2fc0) within aip8, or within 19 after 65 dives; up: aip2 % A2, aip1 % A1, aip3 % circles 3, else waits aip7 in melee; out of it past aip6 approaches 4 / circles 3, else aip5 % A2, aip4 % circles 4, else waits aip7. Its spot's 0xc01 (FUN_0064cb30) lets it up at first. Other think FUN_005f81d0: up with a Skill1, dives |
| Tentacle / TentacleHead | 56 / 57 [2] | FUN_005f8f80 / FUN_005f9270 | Skill1 (Submerge) and Skill2 (Emerge, at itself) on frame timers (state2; state3 1 up, 2 under; aip3 / aip4 × 25 frames); a tentacle dies with its head (its group leader, FUN_0058f0d0) or 40 % once it's DD, Submerges 50 % when it's in S4; in melee aip1 % A1 (the head's at its second target) |
| FetishShaman | 65 | FUN_005f9a80 | Inferno (Skill1) when the target's AI distance is under Skill1's *level* (FUN_006442a0) and state 0xc is off, its group told to attack (FUN_0058f730 command 1); a dead fetish (class 0x8d / 0x18c) within aip5 (squared): aip1 % its group sent there (command 14) and Resurrect's sequence (Skill3) when the squared distance is at most aip3 (unsquared in game.exe), else about it 10; else aip4 % circles 4. Other think FUN_005f9950: state 0xc off, toggled back |
| ZakarumZealot | 48 | FUN_005f6e60 | spares (runs off 8, else wanders 6) a player, or a pet whose owner is one (FUN_0058f090), in act 3 (FUN_006427f0) with quest 0x15 bit 0 (FUN_0065c310); hit under aip3 % life runs off 8 once (state2 5); on 0x40 circles 4 / walks in; out of melee aip4 % runs in (mode RN, FUN_005ded20), else walks; in melee aip2 % A2 else A1, after a swing aip1 % on, else 80 % stands 10, 20 % circles 4. Pace Run / Velocity |
| ZakarumPriest | 49 | FUN_005f72d0 | hit or in melee under 33 % life, Skill3 (teleport) every aip5 × 4 frames to a point past the target (×2, ×4 in melee); in melee aip1 % A1; Skill1 (heal) 25 % at the weakest zakarum (0xeb / 0xee) under 61 % within aip6 (FUN_005f7240); no line to the target (FUN_005dd290, mask 4): aip1 % then aip2 % Skill4 (cooldown aip5); a line: aip4 %, Skill4, then Skill2 aip3 % (cooldown 20); else 30 % circles 4, stands 20 |
| Mephisto | 50 | FUN_005f78b0 | hurt = (100 - life %) / 5; a case kept in state3 (jump table 0x5f7ef0): 1 runs off 8 (in melee Skill3), 2 casts by his chooser (FUN_005f77c0; bugs.md #21) over the skills set in a row, past Normal Skill6 at a unit with no line or past 30, Skill5 within 15, else by thirds Skill4 / Skill2 / Skill1, every other one circling 3; 3 A1 / Skill3 / circles 3; 4 about the target 6 |
| HighPriest | 85 | FUN_005e0490 | the Council: state 0 picks Skill2 at the weakest ally under 76 % within 50 (FUN_005e0430), Skill1 at a corner 5 off the target (FUN_00472210, 0x6e337c), S1 within MissS1's Missiles.txt Range - 2 (FUN_0046ace0 +0x96), else circles 3 / approaches 6; state 1 shoots, stands, wanders 12 or walks in |
| Hydra | 86 [2] | FUN_005e9e60 | past its end frame (AI control +0x14) gone (FUN_005ddfc0); within 25, 60 % Skill1; else stands 10 |
| FetishBlowgun | 96 | FUN_005e1250 | its group's command (1 A1 at the unit, 14 a walk to it, FUN_00552f60); past aip1 about the target 6; else aip2 % breaks off (state 2: backs off 14, or 20 % circles 4); shoots its second target rand(3) + 3 thinks (none: circles 4), then circles 6 |

Checked: `tools/emu/mon_ais.py` (ACT3_AIS), 5000 runs each, seed 7: `tests/mon_ais3_cases.inc`, `test_monsters`. Their inputs: FUN_005415a0 (next think event), the group leader's mode, FUN_00552f60, Skill1's level (FUN_006442a0), state 0xc, FUN_0064cb30, the zealots' spare (player / pet, act, quest bit), FUN_005dd290 / FUN_00622aa0 (line, mask 4), MissS1's range, Skill6..8, Mephisto's chooser's search; a circle (FUN_005df7d0) says whether it set off.
Submerge (srvstfunc 51, FUN_005cc1d0) clears unit flags 0xe; Emerge (52, FUN_005cc220) sets them: d2d `Monster::buried`.
d2d (`ai.cpp`): a think always has a target; no pending think event is seen; command 14 isn't kept. The breath (95, FUN_005cc240 / FUN_005cc4e0): state 0xc and a flame every calc3 frames for calc2 (the fetish shaman's Inferno: 5 over 15). MonFrenzy (109): a hit puts its state on (FUN_005d8c70: auralen frames, charges up to its level as the aurastats' level: velocity, attack and anim rate, `Monster::anim_pct`). The wisps' search: its room or its near list (FUN_0065a6b0, bugs.md #23). Plain casts run at any mode's action frame (FUN_0056f7f0): scattered bolts (17), missile at target (28), breath (95), Bestow / ZakarumHeal (96), MonTeleport (98, FUN_005ccc80: the free spot nearest the point), the poison nova (99, FUN_005ccd10), Hydra (144: three hydra1, Param1 frames); A wisp's SC fires MissC with its SC element. The wisps' blessing: `Fight::wisp_blessing` into the loot's magic find.
- **Idle** (MonAI row 1, `{0, 0, FUN_005b0cd0, 0}`: search type 0, no init): the cow, chickens, rats, birds, the window / door / barricade units and the like. The think stands 200 frames (FUN_005de080(0xc8)) when the unit has a room (FUN_00620bb0); nothing else. The driver's first step still runs (stun 3, doors, FUN_005b0ff0's follow of the unit at AI control +0x0c). d2d: `monster_update`'s Idle branch.
- **Curse thinks and other thinks.** FUN_005b0e00(ctrl, special) is the AI toggle. It looks up the row (FUN_005b15d0: the special table 0x73d358's for special != 0 (10..12 only for a monster FUN_00623470 passes), else the MonStats AI's MonAI row). If the unit's think (ctrl[1]) is that row's and the row has a fourth word (an other think: rows 15 SandMaggot, 29 BatDemon, 51, 52, 61, 65, 128, 135, 140, 145, 147, 152), it swaps to that, ctrl[0] = special; else it clears ctrl +0x14..+0x1c, runs the row's init (+4) and sets its think. The AI-swapping states: do 30's curses whose state FUN_005c3400 names (23 dimvision, 56 terror) set the curse struct's +8. FUN_005c35c0 then takes only a monster not neutral (FUN_006259b0) that FUN_0056e2f0 passes (FUN_005dd480: not state 0x36, FUN_00623470 (a WL mode, not MonStats boss +0xc & 0x40, SwitchAI +0xe & 1), unit +0xc4 & 4 or dead, not unique / superunique (monster data +0x16 & 0xa)); others get no state at all. It puts the state on with callback FUN_005c3370, then FUN_005c34b0: special 10 for dimvision, 11 for terror (12 for state 27 is never reached). Cloak of Shadows (FUN_005d6520) puts its state on every live unit in range with the same callback and special 10 on one FUN_0056e2f0 passes. Howl (FUN_005ddd00, same tests) puts Terror's state (skill 77 level 1) with no callback, then special 11, thinking next frame. The callback runs whenever the state's stat list comes off (FUN_006269f0 calls +0x38 with ECX unit, EDX state): on expiry, and when FUN_0056e970 replaces it (a curse takes any curse's place; the same skill at the same level only runs longer, a higher level stays and the new one fails). It toggles 0, so the monster is back on its own think. Special 10 (FUN_005e8020): in melee with an A1 mode (FUN_0046c140 bit 4) and not interact (MonStats flag 9) A1, else 20 % wander 3, else stand 10. Special 11 (FUN_005e8140; init FUN_005e80e0 sets a skill-set target to the unit it was at): state 0x38 gone, toggle 0 and think next frame; past ctrl +0x14 (30 when 0) stand 10; pace Run / Velocity (0..120 % on); the first time (+0x1c) FUN_00573120 and back off 30 (FUN_005df140 with an RN mode, else FUN_005defe0); after, in melee A1, else back off 30 (flags 4), wander 6 if it can't. So an other think runs when a monster carrying two AI-swapping states loses one (back on its own think) and then the other (toggle 0 on its own think): Cloak with Dim Vision or Terror, a Necromancer's curse replaced while Cloak holds. A new or restored unit (FUN_00574250, FUN_005424f0, FUN_0056d940, FUN_005c07a0) starts with ctrl[1] 0; FUN_00570530 has no caller. BatDemon's other (FUN_005f4fd0): in mode 11 (S4) +0x14 = 1, wait 1; +0x14 4: +0x14 = 1, mode 10 (S3) at no unit; else toggle ctrl[0], stand 1. SandMaggot's (FUN_005f1750): AI command 14 (FUN_0058efa0, made when missing) +0x18 at 1 with a Skill1: cast it at the unit it's at, event 30, the word 0; else toggle ctrl[0] keeping +0x1c, event 1 (nothing found sets the word). Checked: `mon_ais.py` DimVision / Terror / BatDemonOther / SandMaggotOther, 3 × 50 000 runs (seeds 7, 13, 21), all 90 paths match. d2d: `swaps_ai`, `ai_toggle` (`rules::ai_toggle`), `rules::dim_vision_think`, `terror_think`, `other_think`; `Fight` puts states through `put_state` / `end_state` (one curse slot, one other). Not ported: the other thinks of Acts 3..5, state 0x36, the init's skill-set target (the search finds the same unit).
- **Checked.** `tools/emu/mon_ais.py` runs each think natively on random scenes, every helper hooked at its entry (the seed draws of FUN_005df7d0 / FUN_005de200 / FUN_005df530 made as they make them), and dumps the distinct paths as `tests/mon_ais_cases.inc`; `test_monsters` runs `rules::mon_think` on them: the act, its unit, wait and event, the pace, the effects, the AI control and seed. 3 × 150 000 runs (seeds 7, 13, 21), ~800 paths each, all match; the Vulture 2 × 20 000 more.
- **Sequences.** An SQ skill (mode 14) plays its MonSeq.txt rows one a frame (FUN_00621260 sets the speed 0x100; FUN_00663410 reads a row's mode, frame, direction and event). Mode 14's event function FUN_005a8670 runs the skill (FUN_0056fc50 → FUN_0056f7f0: its srvstfunc once, then srvdofunc[skill] at its level) on a row with event 4, or event 1 / 2 unless unit flag 0x40 (unit +0xc4) is set; a missile do-func such as 85 (FUN_005cb0c0: srvmissilea's row + the monster's TransLvl) sets it, so a sequence fires one shot. `ai.cpp` plays `GameData::mon_seqs` so (the hand-copied Act 1 table gone; its five rows are the data's), a melee row a hit, 85 generic (ShamanFire, UnHolyBolt), Resurrect2 as Resurrect (both 97). A plain SC curse (do 30, FUN_005c37c0: the Summoner's Weaken) curses the player's side within its aurarangecalc of the target's spot, as a Cursed boss's Amplify Damage does: `Fight::curses` (the player's and the merc's, one at a time, the newest, auralen frames; dimvision / terror over AiCurseDivisor), the player's aurastats joining its self states' (Weaken's damagepercent -par5, Amplify's damageresist), the merc's damageresist only; its auratargetstate's overlay drawn. A joined game draws the states the host sets on our player (S->C 0xa8 / 0xa9). A monster's hit takes its mode's stats (FUN_005a4f50: flag 0x08 A1, 0x10 A2, 0x20 S1 damage, elements and to-hit; FUN_006538a0's to-hit is A1TH / A2TH / S1TH by flag, 2109/2109 rows): an A2 swing A2's, Fire Hit and Jab (FUN_005db2d0, EDX 8) S1's, Charge the mode it swings in, Smite (do 150, FUN_005ce9f0's monster branch: EDX 5) A2's with a normal hit roll and calc2 frames of stun on the player (state 21, FUN_0057aae0: a new one's length, 250 at most). On a player that's all the stun does besides the overlay: FUN_0057cee0's player branch, on a hit with result flag 4, plays GH (FUN_005809d0 mode 4) when state 21 is on even if FUN_0057cb00 calls the hit too small (flag 4 is the hit landing on a target without state 54 uninterruptable, set by the hit roll FUN_0057ec10 / FUN_0056b9c0 before any damage, so a hit absorbed to nothing counts) (damage under 1, or under the life share for its hit class). game.exe has no other state-21 test on a player path: the only direct ones (FUN_005a73e0, FUN_005a8520) are the monster think's, and the client tests none, so a stunned player still moves, attacks and casts, hit-locked. The maggots' burrow: MagottDown's start (srvstfunc 45, FUN_005cb270) clears unit flags 0xe and lifts its 0x1000 plus (FUN_0064ec10, pattern 5); MagottUp's (44, FUN_005cb170) moves it to the free spot nearest (FUN_00554ea0 -> FUN_0064e7b0, mask 0x1c09; none: the skill fails), sets flags 0xe and stamps a 0x1000 plus. d2d: `Monster::buried` / `targetable()` (the hit and target scans, `hurt`, `land`, the crowd), drawn on S1 backwards then held (seq_maggotdown's rows; the client's own look unverified). Left: the sequence's own modes row by row (its first row's mode plays).
- **Fire Hit** (srvdofunc 83 FUN_005caf30 → its srvstfunc 42 FUN_005cae40): the hit roll (FUN_0057ec10), then the monster's own damage built in mode 8 (FUN_005a4f50, EDX 8): S1MinD / S1MaxD and the S1 elements (the Sand Raider's fire), the skill's ResultFlags / HitFlags / HitClass OR-ed in. `MonStats::s1_min` / `s1_max`; ai.cpp's hit swaps them in.
- **Charge** (srvstfunc 31 FUN_005cf6b0, srvdofunc 67 FUN_005cf900; Duriel, the vipers' SerpentCharge): in melee it swings at once (mode 5, A2, for class 0xd3 Duriel and 0x49, else A1); else it runs at the target at MonStats Run (<< 8) on a charge path (type 0x14) under state 0x12, the sequence's event rows trying the hit each frame till it lands (flag 0x40). The hit: sure (flags 9 for a monster), calc1 % more damage, the skill's element (FUN_0056e0c0), knockback overlay 0x93. Jab (do 7, FUN_005db2d0) is the same hit rolled, not sure. `ai.cpp`: `Monster::charging`, a run at the target and `charge_swing` at its end; the path type, state 0x12 and the overlay left out.
- **The maggots and the mummies' heal.** MagottLay (87, FUN_005cb3c0): its MonStats spawn (FUN_0056e620 → FUN_0063ea70: maggotegg) two subtiles toward the target: FUN_00621dc0's 64-way direction folded into 8 (DAT_00745600), DAT_006e3138's ring-2 entry of the step tables DAT_006ea998 (x) / DAT_006ea978 (y), flag 0x4000000. MagottDown (86, FUN_005cb300): calc1 % of its life back (FUN_00483360 is a mul-div), flag 0x40. The egg's hatch (84, FUN_005cafa0; its sequence plays in DT): calc1 of its spawn (maggotbaby), the first on its spot, the rest on rings 1..4 (FUN_005b23c0, 2 × calc1 tries), then it dies. Bestow (96, FUN_005cc840): the one it's at gets lo + rand(hi − lo) % of its life (hi: calc2 within 0..100, lo: calc1 within 0..hi; the caster's seed), kept within 1..max. In d2d the larvae stand at the nearest free spots, a live DT sequence runs its events.
- **Leap** (srvstfunc 40 FUN_005d9d50 at the sequence's first event, srvdofunc 77 FUN_005da370 after): a monster first hits its target (FUN_005d9c80: its attack damage, FUN_0057d4f0), then lands at twice the target's spot less its own (FUN_0045adf0 / FUN_0045ae20 of each), the free spot by mask 0x3c01 (FUN_0064e7b0); the rows after fly it (FUN_005d9f70) and land it (FUN_005da120); the knockback blast at the landing is a player's only. `ai.cpp`: `Monster::leaping`, the first event row a hit (then flag 0x40), a straight glide over the sequence to the nearest free spot; struck mid-leap it stays put.
- **Plain casts.** A skill whose Sk mode is a plain SC (the Summoner's) runs FUN_0056f7f0 at the mode's action frame: its do-func, then its srvmissile (+0x46) if it has one. `ai.cpp`: a srvmissile at the target (Fire Ball, Glacial Spike) carrying the skill's element at its level (the missile row names the Skill: FUN_0064b860); a nova (22, FUN_005c9b50 → FUN_0056d400: 64 srvmissilea out to range + LevRange × level + calc1; Frost Nova); a fire wall (24, the Countess's, its maker's SubMissile1).
- **In d2d.** `ai.cpp think`: the searches over the monsters within 80 subtiles; Duriel's aura through Aura Enchanted's (`Monster::aura`); a walk at a kin; S3 / S4. A skill act plays its sequence (above); a charge or an SC skill without one stands. Left out: states 0x5a / 0x5b, overlays, regen (stat 0x4a), FUN_005fd470 (usable always, but Diablo's Prison: `fits_on`), Radament's wider reach, a wait counted from the mode's end; one free-spot search, its landing move onto itself a stand. The Vulture's take-off (FUN_005f2eb0: flags 0xe off, its 0x100 / 0x1000 lifted, pattern 5, path mask 0) and landing (FUN_005f2fc0: fails when down already (pattern 1) or no free spot (FUN_00554ea0), else flags 0xe, a 0x100 plus, pattern 1, mask 0x3c01): `Monster::aloft` (untargetable, out of the crowd, flies over anything). A vulture landed by a pillar with its target still and in another room keeps trying "keep 9 off" and stands: checked 2026-10-12, game.exe's toward (0xd) and wall (0xf) pathers on the same grid find no point either (the keep's spot lies past the pillar), so it's game.exe's behaviour too. "In a group" (FUN_0058f0d0) is an owner (AI control +0x28 / +0x2c, FUN_0058f030: a boss's minions, a flagged MonStats party), not a pack: d2d a minion.

Act 4's MonAIs (levels 103..108, same table). Traced 2026-10-14; all eleven
thinks proven 2026-10-16: `mon_ais.py` (5000 runs each, `--dump
tests/mon_ais4_cases.inc`) against `rules::mon_think` / `other_think` in
test_monsters, wired in `ai.cpp`.
aipN below is MonStats aipN for the difficulty (+0x56 + 6(N−1) + 2·diff).

| AI | # | think | does |
|---|---|---|---|
| VileMother | 68 | FUN_005fa010 | spawns (+0x14 count) while under aip1: not a player's pet (FUN_006259b0), aip3 %: counts its young within 25 (FUN_005dd0b0 search type 0xc, class FUN_0054da60) and under aip2 lays one (FUN_005f9e30: from the direction to the target (FUN_00621dc0 → FUN_0063e7a0), up to eight tries, d = that direction then d + 1 …: the step of {6, 4, 2, 2, 2, 0, 6, 6}[d & 7] (FUN_0063e7e0) × 3 subtiles out, where Skill1 (Nest) fits (FUN_005fd350): Skill1 at that point). In melee aip4 % A1, else stands aip7. Out of it (count < aip1) aip5 % walks in, else aip6 % circles 4, else stands 15 |
| VileDog | 69 | FUN_005fa280 | first think stands 5; in melee aip1 % A1 else stands aip2; out aip3 % walks in, else stands 10 |
| FingerMage | 70 | FUN_005fa380 | flee state (+0x14 1, +0x18 tries): hit (FUN_005dd2b0) resets it; in melee → A1, else Skill1 (Spider) within aip5. Fleeing: life over aip3 %, 25 %, or tries over aip6 ends it; else within 14 backs off 14 (pace (Run·100/Velocity − 100) ≤ 120), else stands 15. No group (FUN_0058f0d0) and life under aip4 %: flees (back off 9). Out of melee: past aip5, within aip7 walks in, else stands 15; else aip2 % Skill1, else state 0x54 on → walk in, off → stands aip8. In melee aip1 % A1, else aip2 % Skill1, else circles 5 |
| Regurgitator | 71 | FUN_005fa710 | a corpse state machine (+0x14): 0/1 look for a corpse within aip6 (squared, ctx +4; FUN_005fa690: a dead monster, not 0x200-flagged, has a corpse, not a pet, in a room); 2 walk to it (approach, up to 5 tries, then wander 8); 3 Skill1 (Eat, srvdo 108) at it → 4; 4 out of melee S2 (mode 5) → state 5, in melee back off 8; 5 back off 16. Fighting: a corpse in reach (≤ 9 squared and aip2 %, or aip4 %) → eat; in melee aip1 % A1, else stands 15; out aip3 % walks in (flags 0); a corpse and aip5 % → eat; else stands 8 |
| DoomKnight | 72 | FUN_005faa90 | in melee aip1 % A1 else stands aip2; out aip3 % runs in (7) else stands aip4 |
| AbyssKnight | 73 | FUN_005fab80 | Skill2 (Bone Armor) when its state is off and life under aip1 %, aip2 %; in melee aip3 % A1 else stands aip4; within aip5 the fire timer (+0x14) set to aip6; Skill1 (Missile) when the timer's 0 and the target isn't a player's corpse-lit (unit +0x14 +0xe < 4), timer = aip6; else counts down; aip7 % runs in (pace rand & 1 + 6); within aip8 circles 3; else stands 15 |
| OblivionKnight | 74 | FUN_005faf00 | its search (FUN_005fae40, within 2500 squared: class 0x136 monsters it sees, FUN_00650d70, not pets; nearest, and nearest under 40 % life); within aip1: Skill4's curse (Decrepify; `0 < id`) when the target's state (skill +0x82) is off, else a walk (4) to the knight found when it's farther from the target (squared, FUN_005b0bd0(knight, target)) than the target's distance squared, else back off 10 (pace 50), else Skill3 (Bone Spirit). The second target (FUN_005ddc30) within aip2: Skill6 (MonCurseCast) past its cooldown (+0x14 < frame, aip4 %), cooldown aip3; else aip5 %: Skill3 (aip6 %) or Skill1 at it. Past aip8, aip7 % about the target 6; else 70 % stands 10, 30 % circles 3 |
| Megademon | 89 | FUN_005e0c80 | Venom Lords and Infector: Skill1 (Inferno) while its state 0xc is off and the target within Skill1's level (FUN_006442a0, at least 1), out of melee: aip1 % when the cooldown (+0x14 frame) passed, cooldown aip6 frames; in melee: state 0xc cleared, aip2 % Inferno (cooldown), else aip3 % A1, else aip4 % circles 3, else stands 5. Out: aip3 % walks in, else stands 10 |
| Izual | 55 | FUN_005f89b0 | first think (+0x14 0 → 1) FUN_005b4390 (a4q1: his greeting sent to every player, `FUN_00544300(FUN_005b3940)`); a pending stand (+0x18) first. In melee: swings (+0x1c) left or aip1 %: A1; else Skill1 (Frost Nova) aip4 %, stand aip5, swings aip6; else stands aidel. Out: within 10 Frost Nova aip3 %; aip2 % walks in; within 10 stands aidel; else keeps 6..9 (FUN_005de6d0 → FUN_005de4e0 2) |
| Diablo | 51 | FUN_005e9170 | his kept point (FUN_0058eef0 kind 10) set to his own spot when he has none (FUN_0058ef40); target by FUN_005e8f20 (players in its room's area, scored FUN_005e8530); action by FUN_005e8810 (below); a Skill7 (Prison) first when the target's out of state (skill +0x80) and usable. Actions: 1 wander about (pace 20), 2 A1, 3 A2, 4 S4, 5 Skill1 (Lightning; state 0xc on → off), 6 Skill3 (Fire), 7 Skill2 (Cold), 8 Skill4 (Wall), 9 Skill7 (Prison), 10 Skill5 (Run), 12 circles 4, 13 Skill6 (Prime Firewall), 14 back off from the target (pace 50, ½ its size, two tries, then stand 2), 15 Skill7 at the player by its leader word, 16 wander 5 (stuck: FUN_0064d910 mask 0x40), else stands 12 / 8 / 4 by difficulty |
| Diablo (other) | 51 | FUN_005e8480 | state 0xc off, back to its think (FUN_005b0e00), the target's spot kept as its leader word, stands 1 |

Diablo's chooser (FUN_005e8810): +0x14 set wins. No target: stuck (FUN_0064d910
at its spot, size 2, mask 0x40) → 16, else `rand(1000) > 0` → 4 (S4) /
11 (stand). With a target: 17 weights, by case (base rows, then nudges):
in melee and a line (FUN_00622c40): {0,0,40,70,0,40,24,40,15,…}; under 20 %
life weight 2 = 50; state 0xb on zeroes 7 and 8; cold res (stat 0x29) above
fire (0x27): 6 −10, else +10; no line (FUN_005dd290) zeroes 5 and 6; his
leader in the Chaos Sanctuary within 85 (LAB_006417d0) → 15 = 10. Line
but not melee: {0,0,0,0,0,25,25,0,15,20,10,0,20,…}; the target's distance
(FUN_005dc380) past 25: 8 −5, 10 = 20, 5 = 0; res nudges 6 and 8; under 2
players around (`param_5`) 6 −10, past 3 +5; past 60 frames (`param_4`) 9
+10; few players looked at and the MonsterSkillBonus (FUN_00573930 +8) under 2 zero 9; casting state (Decrepify 0x3b /
0x38, Amplify 0x33 level > 3, 0x1b level > 7 on the target's left or right
skill: `local_c`) and the target 85+ off (`local_8`): 1 = 0, 12 = 10, 10 =
0, 13 = 15, 9 ≥ 10; 105+ off: 9 = 20, 14 = 60; leader 15 = 15. Neither:
{0,0,0,0,5,0,25,0,25,40,0,0,25,…}, under 2 players 8 −5, 6 = 0, 1 = 25, 9 = 0;
the same casting nudges (13 = 15, 13+1 = 25, 9 ≥ 20); 105+ off 14 = 60;
leader 15 = 20. Then a usable Prison (FUN_005fd470 skill 199: `FUN_005b34c0`
on the target in test mode, flag 1, each of 0x73d4f0's four spots able to take
its bone, `FUN_005b2a00`: request +0x24 bit 0 a dry run, radius −1 the
exact subtile, which must lie in the target's room's rect (both callers,
FUN_005fd470 and the cast FUN_005cd5d0, pass FUN_00620bb0(target) as
FUN_005b3270's EDX: the param block's [1], PtInRect on FUN_00619730's
rect), pass `FUN_005fd350` (it only refuses BaseIds 0xce, 0xe4, 0x12a, 0x14e
and 0x210, so never a bone) and `FUN_0064d9b0` (the class's MonStats2 size
and kind mask); each try draws three times on the room's seed; the cast places
them the same way, (1, 1) first or none) zeroes 9. So he picks Prison only
when the target can't be fully caged (d2d: `fits_on`, 2026-10-16); the
leader's Prison not usable zeroes 15. Pick: `rand(sum)` on the unit seed
(power of two: mask), the first index whose running sum passes it; none → 11.
His MonAI row is `{1, 0, FUN_005e9170, FUN_005e8480}`: search type 1
(FUN_005de890), so with nothing within 0x23 (aidist is blank) the think
doesn't run and he stands; FUN_005e8f20's whole-act player pick (filter
FUN_005e8eb0: same act, under 0x3fc) only runs once that search found
someone. Run (act 10) is in reach only inside that 0x23, with a clear line
and the target not far from his point (2026-10-16: at 6 cells d2d picks it and
sets off; at 15 cells neither thinks).
The port (`rules::mon_think` "Diablo") holds the exact index map, proven
on the dumped runs. Its distances: the target to his point
(FUN_005dc480, less the target's size / 2 + 1: "far" past 0x55, "very
far" past 0x69), him to the target (FUN_005dc380, less his size: past 25),
the target's leader to his point (FUN_006417f0: under 0x55 with the leader
in the Chaos Sanctuary), each (max · 2 + min) / 2. FUN_00573930 (two stack
words) fills {?, ?, DifficultyLevels MonsterSkillBonus (0 / 3 / 7), the
difficulty, ?}; Diablo reads +8 (d2d once read it as the players about him,
set to 1), Baal +0xc.

His target (FUN_005e8f20, 2026-10-16; lists 2026-10-17): over the unit
lists at game +0x10f8, the best scored (FUN_005e8530, above 0, the first
on a tie). Lists 0..7: a player passing FUN_005e8eb0 (same act byte +0x18,
FUN_005dc530 under 0x3fc) scores (0 in mode 0 / 0x11) and so does every pet
after it; list 8 (good monsters) of his act joins the same best. List 9
(neutral: Confuse, Attract) of his act keeps its own best, the fallback:
when it's within 5 of him (FUN_005dc530), the best isn't in reach
(FUN_00622c40) and a path to it (FUN_00648b90, type 2, FUN_00649970)
fails (FUN_00648780), the fallback is the target. It returns how many it
looked at (the chooser's players_looked) and the best score. d2d: ai.cpp's
Diablo block. FUN_00625480 reads the same stats off a pet or monster: its
resists raw (a merc's base + gear, no penalty), and its aura is its right
skill (FUN_005701b0 → FUN_00643c50: a merc's FUN_005e4d30, a summon's
FUN_005c4470, Aura Enchanted's FUN_005a1650, Duriel's Holy Freeze), the
auras' attackrank 1..9; a monster's max damage is its last swing's mode
stats (FUN_005a4f50 into list 1), which d2d leaves 0 (`unit_pick`).
The score of a player, C division throughout:
`(300 if life % (FUN_00621f20) < 20) + ((damageresist 0x24 + magicresist
0x25 x 2) x 4 + lightning 0x29 + fire 0x27 + cold 0x2b only in reach) / 15
+ (reach 100 (FUN_00622c40), else a clear line 75 (FUN_005dd290:
FUN_00622aa0 mask 4)) + 100 when he is past 0x55 from his point and the
player inside it (FUN_005dc480)) x 5 + ((left level x attackrank + right
level x attackrank) / 4 + 200 with state 0xb + (maxdamage 0x16 + fire /
lightning / magic / cold max 0x31 / 0x33 / 0x35 / 0x37 + poison max 0x3a
>> 8) / 2) x 2`, all / 22, 0 → 1; a monster whose MonStats +0x4e is under 2
scores 0. attackrank is Skills.txt's (bin +0x18e, matched on every row).
The levels are each hand's own (FUN_006442a0(unit, skill, 1)), as the
chooser's casting test (Fire Wall over 3, 0x1b over 7) reads them.
`rules::diablo_score`, proven on 400 cases (tools/emu/diablo_score.py,
tests/diablo_score_cases.inc).

## The imps' ride and skills (traced 2026-10-24)

imp1..imp8 (BaseId imp1, class 0x1ec) carry Imp Teleport (Skill1, do
129), ImpBolt, Imp Fireball (Skill3, srvmissile impfireball), Imp Fire
Missile (Skill4, do 132) and Imp Inferno (Skill5, srvst 59 / do 126). The
Imp think (MonAI 122, FUN_005e2ff0) casts only Skill1 and Skill4; Skill3
and Skill5 come from the ride.

- **The claim.** A siege beast (MonAI 115, FUN_005e1900; within its aip1)
  or a barricade tower (MonAI 113, FUN_005e1860: unridden or its rider
  dead, with a target, within 20, then stands aip1) searches
  (FUN_005dd0b0 with FUN_005e1720) for an imp: a monster, alive
  (FUN_0063ea40), BaseId imp1, seen (FUN_00650d70), not a pet
  (FUN_006259b0), unbound (FUN_00552fd0), its ride word (AI control
  +0x14) -1, within the radius (squared, `<=`). FUN_005e17e0 writes the
  mount's id into that word.
- **Getting on.** The Imp think walks to it past b1, else casts Skill1
  at it. Do 129 (FUN_005d1ab0) at a unit that's unbound and alive moves
  the imp onto its spot (FUN_00554ea0 flag 1) and mounts (FUN_005d18e0:
  imp BaseId 0x1ec, mount BaseId 0x1b3 barricadetower or 0x1b9
  siegebeast1; state 0x8f on, both bound (FUN_00621ce0, unit +0x98),
  events 5, the imp's flags 0xe off, special AI 16).
- **The ride think** (special 16, table 0x73d358: init FUN_005e2cd0,
  think FUN_005e2d80). The init draws once on the imp's seed and picks
  slot 4 (Inferno) on an even draw, else slot 2 (Fireball) (table
  0x6e34e0 {2, 4}, flags 0), the other when it lacks one (FUN_006439b0).
  The think: state 0x8f off or no mount → toggle back, stand 1. AI
  distance past 24, the mount dead (FUN_005541b0: mode DT / DD), the
  mount under FUN_006259b0, or no pick → teleport off (FUN_005df850 with
  imp1's Skill1 and Sk1mode, radius imp1's aip2), the ride word -1.
  Within imp2's aip2, imp2's aip3 %: the pick (imp1's Skill / Sk mode at
  that slot) at the target, a stand 20 when that slot is empty; else
  50 % S1 at it, else stand 20.
- **Getting off.** Do 129 while bound: the free spot nearest the point
  (FUN_00554ea0), then FUN_005d19d0: state 0x8f off, unbound; under 10 %
  life (FUN_00621f20) it dies (FUN_0057ccb0, no killer), else flags 0xe
  back, event 5, its own think. With neither a mount nor a unit, do 129
  is MonTeleport (FUN_005ccc80).
- **Imp Fire Missile** (do 132, FUN_005d2090): caster flag 0x40 (once),
  then FUN_0056ecb0 with Missiles row srvmissilea + MonStats +0x4b
  (TransLvl: imp1..5 → impmiss21..25) at the target. impmiss2x carry no
  damage of their own, SrcDamage 128: the imp's A1 damage.
- **Imp Inferno** (srvst 59, FUN_005d1280: the skill record's end frame
  = now + calc1 (rand(par3, par4), 100..120), at least 1; state 0xc on;
  do 126, FUN_005d1350): a flame (srvmissilea impinfernoflame1, velocity
  Vel + VelLev x lvl / 8) at the target's spot (path set to it, facing
  it); its range calc2 + the row's Param2 (16) (FUN_00648e70 path +0x90,
  FUN_0064a2b0 / FUN_0064a330 missile +0xe / +0x10). While the end is
  ahead and state 0xc on: the next think event 3 frames on, else event 2
  13 frames on (the end).

Checked: `mon_ais.py ImpRide,SiegeTower 3000 7 --dump
tests/mon_ais_imp_ride_cases.inc`, all paths match `rules::mon_think`
("ImpRide", "SiegeTower"). d2d: `Monster::bound` / `riding` (untargetable,
out of the crowd, sits on its mount), the claim in `think`, mount and
dismount in do 129, `rules::imp_ride_pick`. Nihlathak's Skill1 is Imp
Teleport too (MonTeleport for him). imp6..8's Inferno is seq_impinferno
(SC rows, an event-4 row 12 running do 126), and probably never plays:
only a riding imp casts Skill5, and imp6..8's levels have nothing to ride
(stubs.md). Not in d2d: the claim's
sight and pet tests.

## Act 5's monster skills (traced 2026-10-24)

The skill record (0x23c bytes) as these read it: +0x48 srvmissilea, +0x4e
srvoverlay, +0x60 auralencalc, +0x80 aurastate, +0x82 auratargetstate,
+0xbc summon, +0xbf summode, +0xe4 sumumod, +0x138 / +0x13c calc1 /
calc2 (-1: none). `FUN_0056e970` puts a state on a unit from a block
{caster, target, skill, level, frames, stat (-1 none), _, state, end
callback}; the callback runs as the state comes off (its end, or the
unit's death).

- **Cry Help** (do 128, `FUN_005d1800`; the overseer's Skill1 when hit,
  S1): with a target, AI command kind 1 at it till frame + calc1 (ln12:
  100 + 20 a level, at least 1) for its group (`FUN_0058f730`: the
  leader's member list, `FUN_0058ec90` a command each); srvoverlay
  curseattract on the target. A commanded Minion (its think, command kind
  1) goes at that unit.
- **Overseer Whip** (do 131, `FUN_005d1f70`; Skill3 at a free minion, A2):
  at a live monster. A minion (BaseId 0x1c5, `FUN_00463900`) not under
  bloodlust, with `FUN_0045c390`'s roll at least calc1 (65), turns
  (`FUN_005d1e10`): its summon (suicideminion1) in summode (S1) through
  `FUN_00574370`, special AI 15 (`FUN_005e1d30`, the SuicideMinion
  think), state 0x8e with stat 0x163, sumumod 33. Else Bloodlust
  (`FUN_005d1d70`): state 141 for auralen (250) frames and its aurastats
  (velocity +80, attack rate +80, armor +50, damage +100 %).
- **Impregnate** (do 133, `FUN_005d2250`; PutridDefiler, S1 at its find):
  `FUN_005d2140`: a monster, alive, not 0x222 / 0x227 (the defilers and
  painworms), not in state 0x6e, not a player's pet (`FUN_006259b0` 2).
  State 0x6e, no end, callback `FUN_005d21b0`: dead, one summon (painworm1,
  else 0x227) in summode (NU) at it (`FUN_005b2490`). The think's find
  (`FUN_005efa90`): `FUN_005dd0b0` mode 1 within 25 with that test
  (`FUN_005efa50`); in reach (`FUN_00622c40`) skill 300 at it, else
  straight at it (`FUN_005deca0`).
- **Siege Beast Stomp** (do 134, `FUN_005d2320`, A2): it returns 0 unless
  the record's +0x48 (srvmissilea) is at least 1. Row 301 has none
  (checked in tools/emu: -1), so the stomp is its A2 and the client's
  shake (cltdofunc 75) alone (bugs.md #28). With one it would be a radius
  blast (`FUN_0056bad0`) of the skill's damage.
- **DeathMaul** (do 136, `FUN_005d25b0`; seq_deathmaulerdig's one event):
  srvmissilea (death mauler, 502) made at the target's spot
  (`FUN_0056ede0`: flag 1, start given, at most 100 subtiles off), calc1
  (96) its anim rate (`FUN_00621780`), range by the distance d
  (`FUN_006417f0`, at least 1): d x Range / 12 past 9, else d x Range /
  24 + Range / 2 (`FUN_0064a2b0` / `FUN_0064a330`). The missile stands
  (Vel 0), Activate 30, NextHit 10.
- **Shout** (Madawc, do 68 `FUN_005d83e0`, SC): the nova of srvmissilea
  (`FUN_0056d400`), its hit 18 putting the state on allies, and the
  aurastate on itself for auralen frames (`FUN_005d8290`).
- **Whirlwind** (Talic: srvstfunc 38 `FUN_005d8f50`, do 76 `FUN_005d9580`):
  the target in reach, a plain attack (`FUN_0056e230`); else a path to the
  point (`FUN_00649970`, `FUN_0064ea90`) at the walk velocity
  (`FUN_0056e5b0`), state 0x12. Each event row: for a monster
  `FUN_005d9320` steps its seed and gives one hit when the low bit is
  clear; the hit goes at `FUN_0056bd10(.., 5, 3, last id)`, the next unit
  within 5 after the last one hit, rolled to hit (`FUN_0057ec10`), calc1 %
  more damage, the element (`FUN_0056e0c0`). The path done, it ends
  (`FUN_005d9460`).
- **Leap Attack** (Korlic: srvstfunc 41 `FUN_005da540`, do 78
  `FUN_005da7e0`): a landing spot by the target (`FUN_00645ca0`); none, in
  reach a plain attack. Flags 0x1080, state 0x12, the target's state 0x36
  cleared (`FUN_00570420`). Events by flags: 0x100 take off
  (`FUN_005da120` / `FUN_005da490`), 0x80 fly (`FUN_005d9f70`), 0x200
  strike (`FUN_005da660`: to-hit, knockback, calc1 % more damage, the
  element).
- **Umod 33** (suicideminion: every BaseId 0x1cd by `FUN_005b21b0`, a
  whipped minion by its sumumod): hook 1 `FUN_005a3e70` (a mode change):
  into GH it's put straight into death (`FUN_005a7e60` mode 0), and into
  death (0 / 12) its event 7 is queued 4 frames on; hook 2 `FUN_005a3ef0`:
  its A1 damage (`FUN_006538a0` kind 8: MonLvl-scaled A1MinD / A1MaxD at
  out[5] / [6]) rolled on its seed, x 256 as physical (+8), and by its
  aip1 (MonStats +0x56): 1 fire (+0x10), 2 up cold (+0x24) for aip1 frames
  (+0x30); missile 426 suicidecorpseexplode on it (`FUN_0056ecb0`); the
  blast within aip4 (+0x68) through `FUN_0057e090` (flags 0x581).

d2d: `ai.cpp` (`castable`, the action-frame dispatcher, the whirl block,
`skill_strike`, `impregnable`, the defiler's find, Cry Help's command in
`think`), `Fight::boss_events` / `suicide_blast`, the painworm in
`Fight::update`. A think's raw skill id (the defiler's 300) finds its slot;
`MonAct::direct` at its kin walks to it. Seen live (2026-10-24, headless
probes): Cry Help, Bloodlust and the suicide turn, a suicide minion's blast,
DeathMaul, Impregnate and its painworm, Madawc's Shout, Talic's whirl,
Korlic's Leap Attack.
Not in d2d: the overlays and anim rates, Bloodlust's and the turn's
states beyond the cry slot, `FUN_0056bd10`'s and `FUN_00645ca0`'s own
searches (the nearest foe by id, the nearest free spot), Leap Attack's
takeoff and landing flags (it flies by row 11, strikes on the next event).

## Baal — FUN_005fcfe0, his clone FUN_005fd210

MonAI 135 `{FUN_005fcfe0, FUN_005fcf30, 1}`, 140 (baalclone) `{FUN_005fd210,
FUN_005fcf30, 1}`. Proven on game.exe: `tools/emu/mon_ais.py
BaalCrab,BaalCrabClone --dump tests/mon_ais_baal_cases.inc` (2026-10-24).

- Pick: FUN_005fbdd0 is Diablo's FUN_005e8f20 (filter FUN_005fc000 = same
  act, under 0x3fc), the score FUN_005fbb00 Diablo's with the point bonus at
  0x4b instead of 0x55.
- Kept point: his own spot when he has none (FUN_0058ef40 kind 10).
- Chooser FUN_005fc630(ECX him, EAX target; game, AI control, score, looked,
  point): AI control +0x14 non-zero is the act. No target: stuck
  (FUN_0064d910 mask 0x40) 4, else low % 100 < 8 → 9, else 1. Far / very
  far: the target past 0x4b / 100 off the point (FUN_005dc480, less its
  size / 2 + 1). The rest by weights rolled on his seed (first index past
  the roll; none → 1), `rules::mon_think` "BaalCrab" holds them:
  in reach FUN_005fc1c0, a clear line FUN_005fc450 (index 15, the clone:
  (2 − AI control +0x18) × 10), neither FUN_005fc300. Index 1's weight is
  (normal 75 / 125, else 50 / 100) + 100 − his life %.
  FUN_005fc0c0 is Diablo's casting test (right hand, then left).
- Acts FUN_005fcb60, then wait 25: 1 stand 35 / 15 / 5; 2, 6 keep 12 off
  (mode 2); 3 circle 6; 4 wander 16; 5 walk to the point; 8 Defense Curse
  (Skill6) when the target's max mana (stat 9, FUN_00625d60) is under its
  max life (stat 7, FUN_00625d10) or it's no player, else Blood Mana
  (Skill7) at a player; 9 Tentacles (0x13b, S2); 10 A2; 11 Nova (0x13c,
  S3); 12 Inferno (0x13d, S1); 13 Cold Missiles (0x13e, A1); 14 Teleport
  (Skill5) to him + (him − target) × 25 / FUN_006416d0 (uber Baal 0x2c5:
  the target's spot); 15 a clone (FUN_005fc860), then stand 5; else stand 5.
- Clone (FUN_005fc860): none while one of his lives (FUN_0058f380 /
  FUN_005fc830); baalclone (0x23a) at his spot + low % 24 − 12 per axis
  (his seed), a third of his max life and life, regen 0, flags 0x4020000,
  +0x18 counted.
- The clone's think: its master gone or busy (FUN_00552fd0, FUN_005541b0)
  → FUN_0057ccb0 (dies); else Baal's chooser, acts 7 / 9 / 14 / 15 → 2,
  no target stands 15.
- Skills: 139 (FUN_005d2940) srvmissilea `baal cold maker` at the target's
  spot, a maker (missile do 31) of `baal cold trail`; 140 (FUN_005d2c20)
  low % 3 + 2 + difficulty tentacles, each at the target + low % 18 − 9 per
  axis, a step down the baaltentacle class chain (FUN_0063efa0 case 0x220),
  made in A1.

d2d: ai.cpp's Diablo blocks take Baal; the clone, the tentacles, the cold
maker (ai.hpp do 31). ponytail: the tentacle class is rand(5) on his seed;
FUN_0054dc40's spot test is the nearest free spot.

## AncientStatue and the ubers' MonAIs

MonAI rows (0x73ca18 + ai × 16: search type, init, think, other):

| ai | name | think | other |
|---|---|---|---|
| 132 | AncientStatue | `FUN_005eeaa0` | — |
| 144 | UberIzual | `FUN_005f8c80` | — |
| 145 | UberBaal | `0x5fd200` (`ret 4`) | `FUN_005fcf30` (BaalCrab's) |
| 146 | UberMephisto | `0x5f81c0` (`ret 4`) | — |
| 147 | UberDiablo | `0x5e9df0` (`ret 4`) | `FUN_005e8480` (Diablo's) |

- AncientStatue (ancientstatue1..3, hcIdx 537..539): with the Rite's record
  (0x23, `FUN_00543640`) +0x11 awake, +0xc (deaths on the Summit) 0 and the
  unit without state 0x92 (invis): skill 302 (MinionSpawner) in mode 4 at
  its own spot (`FUN_005dead0`); else stand 25. Unreachable: MonPreset lists
  them for Act 5, but no Act 5 level places them (tools/emu monsters.py,
  levels 109..132) and nothing else makes them; the Rite's statues are
  objects 474..476.
- The uber Mephisto, Diablo and Baal thinks are bare `ret 4`, the only
  such rows but row 0. Nothing patches the table (no code addresses it but
  `FUN_005b15d0`) and the special table (0x73d358, 18 rows) has no uber
  row. Their only other path is the other think, the swap a curse
  toggle makes as it does for Baal and Diablo, which toggles straight back. Orphans with no caller
  or pointer: `FUN_005e9dd0` (makes 0x2c7 demonhole at a unit, mode 1,
  flags 0x40) and `FUN_005f7f10` (a make next to Mephisto's think), and
  `FUN_005dd140` (counts uber Mephisto / Diablo / Baal, 0x2c7, 0x2c8 and
  731..733 in a radius): the 1.13 uber thinks' helpers, their callers gone.
  The Cube's Pandemonium portals return 0 too (cube.md), so 1.14d never
  takes a player to the uber levels; the event was realm-only. bugs.md #26.
- The uber levels' presets (`FUN_005559a0`): Uber Tristram (136) makes no
  preset monster and skips objects 26 (Cain's gibbet), 268 (Wirt's body),
  269 (the gold placeholder); Matron's Den and the Furnace of Pain (133,
  135) skip object 397 (the sparkly chest); the Forgotten Sands (134) its
  waypoint (402) and the object whose count (game +0x1de4, every other
  object of the level) reaches map seed % 3 + 3. d2d: objgroups.cpp
  `uber_skips`; `diff_drlg.py 100-300 133..136 monsters objgroups` match.
- d2d: `rules::mon_think` UberIzual and the three stubs, checked by
  `mon_ais.py UberIzual,UberMephisto,UberDiablo,UberBaal 3000 7`
  (`tests/mon_ais_uber_cases.inc`); ai.cpp's line for UberIzual takes mask
  6 and its Chilling Armor state lasts the skill's auralen
  (`Monster::self_state_until`). ponytail: the state's armor % and its
  ReturnFire aren't put on the monster.
- UberIzual (`FUN_005f8c80`): Izual's shape (aip by difficulty at MonStats
  +0x56.., +0x18 / +0x1c counters) with three skill slots; not traced
  further yet.
- The Tristram ubers' death (mod 22, `FUN_005e0070`, via FUN_005a30e0):
  sets game +0x1df0 / +0x1dec / +0x1de8 for 0x2c0 / 0x2c1 / 0x2c5; all three
  set: drops `cm2` (the Hellfire Torch) through `FUN_00559a30(7)`, then a
  `std` (Standard of Heroes) per +0x8c; each four-byte code ends in a space.

## Turrets, traps and the rest (traced 2026-10-28)

The MonAIs a level places that were left on the generic melee think. A
sweep of every level (`drlg-dump ... monsters`, map seeds 1..4 and 7)
places only these seven untraced classes; town NPCs, pets and quest NPCs
think elsewhere. All are search type 1 (no target found: no think).
`mon_ais.py` runs them (`LATE_AIS`: Act 2's scene and the trap kind),
`tests/mon_ais_late_cases.inc`.

| ai | name | think | where |
|---|---|---|---|
| 33 | HellMeteor | `FUN_005f56d0` | the River of Flame |
| 45 | Sarcophagus | `FUN_005f6a10` (init `FUN_005f6630`) | Act 2's tombs |
| 78 / 79 | Trap-RightArrow / Trap-LeftArrow | `FUN_005fb6c0` / `FUN_005fb7e0` | the arrow traps (327, 328) of Acts 2-3 |
| 93 | ArcaneTower | `FUN_005e0f60` | the lightning spire (Arcane Sanctuary) |
| 94 | DesertTurret | `FUN_005e0980` | the fire towers (372) |
| 97 | Spirit | `FUN_005e3840` | Mephisto's spirit (the Durance) |

- **HellMeteor**: with Skill1, `rand(100) < aip1` on its seed: Skill1 at
  its own spot plus `rand(2·aip3) − aip3` on x, then y, no unit; else
  stands aip2. Skill1 is do 28 (`FUN_005ca3e0`): its srvmissilea toward
  the path's point (`FUN_0056d2c0`) when the missile's Size square there
  is clear of mask 5 (`FUN_0056e450` → `FUN_0064d800`).
- **Sarcophagus**: the FoulCrowNest's think but past 25 (not 20) it
  stands 25, it's gone (mode 0, flag 0x20000) once it's laid **more than**
  aip3, and the frame is stamped only when it lays.
- **The arrow traps**: out of aip1..aip2 they stand 40. Within 2 of the
  target's column (Right, x) or row (Left, y) and past their frame
  (+0x14): the frame is aip3 on, then `FUN_005fb650`'s kind: 1 casts
  Skill1 (+0x1c aip4 on), else A1 (MissA1, the spike). Else stand 30.
  `FUN_005fb650` keeps one kind a level (the game's level record
  +0x2d4, -1 at first): levels below 40 (Act 1) 0, else `rand(3)` on the
  trap's seed (`FUN_004bc500(seed, 0, 3)`), drawn when the first one
  fires. bugs.md #36.
- **ArcaneTower**: its count (+0x18) starts at aip1. Before its frame
  (+0x1c) it stands 10. Casting (+0x14 0, Skill1 set): Skill1 at the
  target, the count down one; done, it swaps to striking (+0x14 1, count
  aip4, frame aip3 on), else frame aip2 on. Striking: A1 (MissA1
  arcanelightningbolt), count down one; done, back to casting (count
  aip1, frame aip5 on), else frame aip6 on. Skill1 (do 106,
  `FUN_005cd870`), with a target (`FUN_00553540`): `FUN_0056d400`, the
  64-missile ring of srvmissilea at its level's velocity
  (`FUN_00663270`: Vel + VelLev × lvl / 8).
- **DesertTurret**: with Skill1 and +0x14 0, Skill1 where it stands
  (+0x1c 0, +0x14 the frame). Before +0x14 it stands 10. Within aip4 of
  the target: its step +0x1c turns by 0x6e33e0[eighth × 8 + step] (the
  eighth: `DAT_00745600` of `FUN_0064fdc0`), and with both lines clear
  (`FUN_005fd470` skill 0xcb: `FUN_00645910` mask 0x805, to the point and
  to the target) it casts Skill1 at its spot + 0x6e33a0[step] × aip5, no
  unit, and sets its path's point there (`FUN_00648ad0`); the run +0x18
  counts up, and past aip2 +0x14 is aip3 on (the run 0), else aip1 on.
  Otherwise the run counts down and it stands 10 (15 out of aip4).
  Skill1 is do 105 (`FUN_005cd6a0`): calc1 desertfireballs across the
  way to the path's point, 2 subtiles apart on the axis the offset's signs
  pick, each from the turret + offset / 6 (both axes the x offset:
  bugs.md #35) flying the offset (flags 3). `tools/emu/turret_fan.py`,
  `rules::turret_fan`.
- **Spirit**: once it's struck (+0x14) it stands 50; in melee A1 (+0x14
  1); else stands 10.

## Skills made in play: spawns and the rest (traced 2026-10-28)

- **A monster made in play** (`FUN_005b2f20` → `FUN_005b2a00`, request
  `{game, room, 0, class, mode, 0, x, y, radius, flags}`): rings of 3
  subtiles out to 3 × radius round (x, y) (radius −1 the spot alone,
  three draws on the room's seed), the first spot in the room's rect where
  the class fits (`FUN_005fd350`, `FUN_0064d9b0`). Flags: 1 a dry run, 0x20
  unit flags 3, 2 no family mods (`FUN_005b21b0`), 0x40 no party
  (`FUN_005b2830`). MonStats byte +0x4c 1 adds unit flag 0x20000 (only
  rows with no treasure class). The party only matters for an evil hole's
  fallen1, which no level places. d2d: `spawn_spot`, on the room's seed
  as play left it (`Spawning::LevelState`).
- **Unit flags +0xc4**: 0x4000000 no experience for the kill
  (`FUN_005a4ef0` skips `FUN_0057e990`), 0x20000 no drop (`FUN_005a6830`
  returns at once). Nest's young (do 91, `FUN_005cbe00`: radius 1) and
  MinionSpawner's get both; MagottLay's egg (radius −1) and Coldworm's young
  (radius 2, flags 0x42) 0x4000000. d2d: `Monster::no_exp` / `no_drop`.
- **The nests' spot** (`FUN_005fd350`, ECX the caster's class): crownest
  (0xce) 3 below it, sarcophagus (0xe4) 2 below, vile mother (0x12a) the
  spot itself (a flag: always free), 0x14e 2 up-left (mask 0x1c0), 0x210 2
  right 4 below: in a room, a plus clear of 0x3c01. Any other family: free.
- **The vile mother's lay** (`FUN_005f9e30`): eight tries from the
  target's eighth on, each 3 × `DAT_006ea998` / `DAT_006ea978`[{6, 4, 2,
  2, 2, 0, 6, 6}[eighth]]; the first free (`FUN_005fd350`) takes Skill1
  (Nest, seq_vileres) there.
- **MinionSpawner** (start 62, `FUN_005d2420` → `FUN_0056e620` →
  `FUN_0063ea70`): the caster's MonStats spawn at its spot + spawnx /
  spawny in spawnmode (outside 0..15: NU). With no spawn row (Nihlathak),
  the class in AI control +0x3c, which his think sets to
  `FUN_0063ec70(room, 0x1c5)`, the level's own minion. Do 135
  (`FUN_005d2490`): made at radius −1, else 4.
- **The level's own monster** `FUN_0063ec70`: monsters.md; the chain's
  bound is the family's count (MonStats +0x4a, `FUN_006510c0`), which the
  chain never outruns. d2d: `level_monster`.
- **Corpse Explosion from a monster** (Nihlathak, do 55 `FUN_005c4df0`, the
  Necromancer's own): at the corpse his think found, used (state 0x68);
  each foe within aurarange / 2 of it takes calc1..calc2 % of its life,
  half physical, half fire.
- **Smite** (do 150, the Baal minions' A2) and **Power Strike** (start 6,
  MonPowerStrike / MonIceSpear, A1) play their Sk mode; the strike lands
  as the swing's (Power Strike: no to-hit bonus, calc1 %, the element).

## Doors — FUN_005b0f50

Driver step 1. With MonStats flags byte +0xc & 8 (opendoors, DAT_006ce274):

1. FUN_00648eb0(path): the collision word under it (FUN_0064d9b0, mask 0x7fff), cached at path +0x54. Bit 0x800 must be set.
2. FUN_005dd0b0 mode 8 (FUN_005dce60 over the near rooms, callback FUN_005dcd50): an object (type 2) with Objects.txt +0x13a (IsDoor) set, in mode 0 (closed), the nearest by squared subtile distance (FUN_005b0bd0) under 9.
3. Its Objects.txt row (FUN_00640e90, 0x1c0 bytes each) +0x16d (MonsterOK) set: FUN_00584540(2, id) operates it, stand 5 (FUN_005de080), and the driver stops. The stand comes whether or not it was operated.
4. FUN_00584540 operates only in reach (FUN_00623660): FUN_00641530 (`unit_distance`, the object's SizeX) 0, else the monster within 2 of the object's rect (its spot less half SizeX / SizeY), the corners (both axes 2 out) not for a monster under size 3; an object without a size, within 1 of its spot. Then FUN_00584420 runs its OperateFn (FUN_00581d40 for a door: 500 ms since it last changed).

In Act 1 the IsDoor objects are OperateFn 8 (MonsterOK on all but 47 and 75) and 229 / 230 (OperateFn 29, not MonsterOK). Most of Act 1's walkers have opendoors: the Fallen, Shamans, skeletons, Corrupt Rogues, Griswold, the Smith, Andariel.

Ported: `ai.cpp think` runs it first at each think (an untraced AI, at its search), through `Fight::open_door` → `World::monster_door` (`rules::door_pick`, `rules::object_reach`, `operate_door`). The 0x800 test is a BlocksVis door's bit under its test shape (`on_door`; collision.md), read when it thinks, not what a moving path caches at +0x54. Ties go by the level's object order, not the near rooms' unit lists.

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
- Conversion (do 79, FUN_005d0350; its end FUN_005d01a0): an evil monster no one owns that passes FUN_0056e2f0 turns at calc1 % (FUN_005d02b0) for auralen frames: alignment 2 (FUN_005543b0 DL 2) in its caster's list (FUN_005b1900 with the caster's +0xd0), so evil monsters take it as one of the player's pets and it hunts them (mode 5 as a good unit); its target and flag-8 states dropped; one above the caster's level comes down to it with its life in proportion (state 0x6d keeps its own in stats 0xb0 / 0xb1, put back at the end). d2d: `Monster::align` 2, `own_level` / `own_max_life`; the player's side's blows pass over it (FUN_00554200). The level scaling reads FUN_00483360's dropped operands as life × clvl / mlvl.
- Movement is floats in cells, blocked as `monster_step` has it, and the target's +0x68 offset is taken as 0.
- A dead pet is skipped (game.exe takes it off the list), and a dead monster leaves list 9.
- An untraced AI (none in Acts 1 and 2) goes at what it found from the next frame on.
- A pace (`ThinkIn::pace` → `Monster::move_pct`) adds to the move's speed %; Spider Lay's −100 % leaves it creeping at the 10 % floor, and its slowed state on those about is left out. A foe's life % (`Foe::life_pct`) is the player's only.
- The Vampire's second target (FUN_005ddc30) is its target. VampireFirewall / VampireMeteor stand (no Act 1 vampire casts them). The Fetish's leader commands never come, and the pathers 2 / 0xd and FUN_005defe0's steps are `path_to`'s.
