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

1. AI control flags (+0x08) & 0x40: no. The flag is cleared on the spot. It's a one-shot set by FUN_005deb60 (a skill use with flag 1, through FUN_005dd230).
2. Else the monster's room (FUN_00620bb0) is checked: FUN_0061aa40 → FUN_0066ba70 is true for a room2 of type 1 (a plain outdoor room), or of type 2 (a preset) with flag 0x80000. FUN_00667ed0 gives a preset's rooms 0x80000 when its LvlPrest Outdoors (+0x10) is set. True: no sight needed.
   - In Act 1, Outdoors is set on every preset that sits in an outdoor level, plus the Monastery front (26), the Courtyards (27, 32) and Tristram (38). It's clear on the caves, crypts, the Tower, Barracks, Jail, Cathedral and Catacombs presets.
3. Else sight is needed while flag 8 is clear (the monster has never found a target). Even then it isn't needed when the monster's area (monster data +0x50) has its +0x24 set.

### The candidates

- A skill-set target first (FUN_005dd610: monster data +0x34/+0x38, written by FUN_00573090 from skill code).
- A hostile monster (FUN_006259b0 == 0) walks the 8 player lists at game +0x10f8. A player counts in the same act, in a room, not in a town room (FUN_0061ab00: levels 1, 0x28, 0x4b, 0x67, 0x6d).
  - `nearest` tracks the least distance (FUN_005dc530: larger delta + half the smaller) over all of them.
  - A player under 0x37 and not dead (FUN_005541b0) is taken when its distance is under the best so far, which starts at MonStats aidist (+0x52 + difficulty; 0 → 0x23 = 35), and sight passes or isn't needed.
  - Then list 9 (pets) the same way, then list 10 through FUN_005dd510.
- Allied monsters search through FUN_005dd0b0 mode 5 (callback FUN_005dca70) instead.

### Found one

- Unless FUN_006259b0 == 2, AI flag 8 is set. Nothing clears it (no `AND [+8], ~8` anywhere in the AI code), so a monster needs sight only until its first target.
- The area's +0x24 becomes `old == 0`, where `old` is the flag as read above. It reads 0 when the monster was already aware or saw for itself, so the flag gets set; a monster that got in on the flag clears it. While an aware monster in the area keeps finding its target, the flag keeps coming back.
- In-melee is FUN_00622c40 (unit_distance ≤ MeleeRng + 1, with a path test, mask 0x804). The distance out is the best, or `nearest` when none was found.

### The area — monster data +0x50

- Set at placement: FUN_005b2a00 passes the placement context's area, or FUN_0061ad30(room, x, y) → FUN_0066ceb0, into FUN_00552d60.
- FUN_0066ceb0 returns the preset's area record holding the spot: room2 +100's single record (+0x30) when its flag 1 is set, else a row lookup.
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

- A return to neutral schedules the think aidel frames on (MonStats +0x4f + difficulty; 0 → 15; FUN_005a73e0, from the mode setter FUN_005a7c20). That's skipped if an event is already due later.
- FUN_005de080(n) (stand n) schedules it n frames on.
- A move runs to its path's end, and that mode end thinks again.

## Which rooms think

- Units update in the active rooms around each player. The near list FUN_0066c370 takes rooms less than 6 tiles apart.
- d2d updates every monster within 30 cells of the player (Fight::world). That covers more than game.exe's active rooms, and a monster can't take a target past 0x37 subtiles (11 tiles) anyway.

## In d2d

- `ai.cpp search_target` is the search for both the traced thinks and the AIs not traced (those used to notice within 8 cells and chase within 16, by eye).
- Line of sight is tested in a preset room whose LvlPrest isn't Outdoors (`GameData::prest_outdoors`), until `Monster::sighted`.
- The area flags are `Fight::area_seen`, keyed by level, spawn room and area index.

Approximated (`ponytail:` in ai.cpp):

- Foes count alike. game.exe takes players, then pets, then list 10.
- Every foe is size 2.
- The town-room refusal is left out.
- So are the skill-set target and the 0x40 skip.
- The area is the first of the spawn room's `room_areas` holding the spawn tile.
- AIs not traced search every frame.
- A move re-thinks every aidel instead of at its path's end.
