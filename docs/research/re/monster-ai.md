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
- List 8 holds neutral monsters (alignment 2): FUN_0054ef50's random spawns, AIs FUN_005edc50 / FUN_005ee3c0, superuniques with flag 0x400 and alignment 2 (FUN_005424f0). FUN_005b1990 adds them.
- List 9 holds allied monsters (alignment 1): Confuse (FUN_005c3de0), Attract (FUN_005c3b90), FUN_005aeda0, FUN_005c58b0 / FUN_005c5bc0, superuniques with alignment 1.
- Alignment is stat 0xac (FUN_006259b0): 0 hostile, 1 ally, 2 neutral; units not players or monsters read 2.

The search, in order:

1. A skill-set target (FUN_005dd610). Monster data +0x38 is a kind and +0x34 a value, written by FUN_00573090 only when the MonStats row's byte +0xe & DAT_006ce268 (FUN_00573040). Its only callers are Attract's callback (005c3b64: kind 1 or 2, the attracting unit's id) and Confuse (005c3ef6: kind 3). Kinds 1, 2, 4 look the unit up by id (FUN_00552f60) and test sight; kind 3 runs a FUN_005dd0b0 mode 5 / 6 search among monsters. A valid live one is returned and the rest skipped; else FUN_00573120 clears it.
2. A hostile monster (FUN_006259b0 == 0), best from MonStats aidist (+0x52 + difficulty; 0 → 0x23 = 35):
   - Lists 0..7. A player counts in the same act (+0x18), in a room (FUN_00620bb0), not in a town room (FUN_0061ab00 → FUN_0066bab0 → FUN_006426a0: levels 1, 0x28, 0x4b, 0x67, 0x6d). Else the player and its pets are skipped.
   - `nearest` takes the player's distance (FUN_005dc530: (min + 2 max) / 2 of the deltas, from FUN_006488c0 / FUN_00648900 on the path).
   - At 0x37 or more the player and its pets are skipped. A dead player (FUN_005541b0: player mode 0 or 0x11, monster mode 0 or 0xc, or +0xc6 bit 1) counts 0x7fffffff.
   - Then the player and each pet after it is taken when its distance is under the best and sight passes or isn't needed. Pets have no act, room, town, 0x37 or death test, and don't count for `nearest`.
   - List 8: same act, under the best, sight. No 0x37 cap.
   - List 9: the nearest one in the same act with sight, from its own 0x7fffffff. FUN_005dd510 decides: with no best it's taken. If it's under 6 away and there's no path to the best (FUN_00648* / FUN_00649970), a FUN_005dd0b0 mode 7 search runs; the list 9 unit is taken when that finds nothing or something over 0x13 away, else the best becomes what it found. Otherwise the best stays.
3. Allied monsters search through FUN_005dd0b0 mode 5 (callback FUN_005dca70) instead.

`tools/emu/search.py` runs FUN_005dd7f0 on random players with pets (acts, rooms, towns, deaths, walls, flags 0x40 and 8, outdoors, the area flag; lists 8 and 9 empty) against `rules::search_pick` and ai.cpp's flags: 20 000 cases, 12 935 with a target, 7 805 of them a pet, all match.

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
- A move gets a re-path budget of 0x14 (FUN_005a7c20 → FUN_006490e0, path +0x94). FUN_00650350 re-paths a chase whose path ended short of a moved target, spending the points walked; at 0 it can't, and the move ends.

## Which rooms think

- Units update in the active rooms around each player. The near list FUN_0066c370 takes rooms less than 6 tiles apart.
- d2d updates every monster within 30 cells of the player (Fight::world). That covers more than game.exe's active rooms, and a monster can't take a target past 0x37 subtiles (11 tiles) anyway.

## In d2d

- `ai.cpp search_target` is the search for both the traced thinks and the AIs not traced (those used to notice within 8 cells and chase within 16, by eye).
- Line of sight is tested in a preset room whose LvlPrest isn't Outdoors (`GameData::prest_outdoors`), until `Monster::sighted`.
- The area flags are `Fight::area_seen`, keyed by level, spawn room and area index.

- Foes are the player, then the merc and pets (`Foe::pet`), each with its size (`Foe::size`: the merc's MonStats class, a pet's type).
- A walk n with n & 1 that can't set off sets `Monster::force_sight` (flag 0x40).
- A move thinks at its end (`Monster::path_left`, `Monster::chase`); AIs not traced search at the same points.

Approximated (`ponytail:` in ai.cpp):

- No skill-set target and no lists 8 / 9: monsters don't fight monsters here. Confuse and Attract are `blind_until` on the cursed one.
- The re-path budget is 4 cells walked, and a chase steps at the foe each frame instead of re-pathing at its path's end.
- A dead pet is skipped (game.exe takes it off the list).
- An untraced AI chases the nearest foe, whichever it found.
