# Missile flight — game.exe 1.14d

How a missile is made and moved on the server. `.\MISSILES\Missiles.cpp`
(make), `.\MISSILES\MissMode.cpp` (per frame), Path.cpp (the move).
Proven by `tools/emu/missiles.py`: game.exe's own FUN_0059fa30 and
srvdofunc 1 in a Blood Moor game, frame by frame, against
`components/rules/missiles.hpp` (thousands of flights: arrow, spike1/5,
shafire1, firebolt, icebolt, andypoisonbolt, chargedbolt, random
Vel/MaxVel/Accel rows; walls, rooms and foes of size 1..3).

## Making it — FUN_0059fa30 (ECX game, EDX params)

Params (u32 array): [0] flags, [1] owner, [3] target unit, [4] Missiles.txt
row, [5] / [6] start subtile (flag 1, else the owner's), [7] / [8] target
(flag 0x20; flag 2: relative to the start), [10] velocity (flag 4; flag
0x10: no << 8), [0xb] skill, [0xc] level, [0x15] / [0x16] a callback and
its EDX, run after the path is set.

- Row record (0x1a4 bytes): +0x96 Range, +0x98 LevRange (shorts), +0x9a
  Vel, +0x9b VelLev, +0x9c MaxVel (bytes), +0x9e Accel (short), +0x136
  Activate, +0x183 CollideType, +0x18a Size.
- Velocity: ((Vel + VelLev × lvl / 8, C division) << 8), × stat-87 % with
  that state (not built), then **× 75 / 100** for every missile (+0x7c).
  MaxVel << 8 (+0x84, **not** scaled) and Accel (+0x88) come after.
- No target unit and target = start: target + (1, 1). Over 99 subtiles
  off on an axis: no missile.
- The unit (FUN_00555230, type 3) stands at the start subtile; mode =
  CollideType. Range + LevRange × lvl frames (+0x10 of the missile data,
  a short) and Range − Activate (+8).
- Path: type 4 with flag 0x40000 (FUN_00649760 → FUN_006492f0): one point,
  the target; position (16.16) the start subtile's centre; unit vector
  FUN_0064fc60 toward the target's centre. If the target isn't in the
  missile's room (path +0x1c), path flag 1: the room follows the missile.
- Flag 0x400 (FUN_0056ee90): range = (FUN_006417f0 distance << 16) /
  (velocity << 4), the frames to arrive.
- Collision mask (path +0x50) = DAT_0073c724[CollideType]; the unit
  filter DAT_0073c720[CollideType] (3: 0x184 and FUN_005a8850 →
  FUN_005a8730: targetable, not the last unit hit, hostile unless
  CollideFriend).

## Unit vectors — FUN_0064fc60

From 16.16 (x, y) to (tx, ty): major / minor of the deltas (steep when
|dx| ≤ |dy|, signed compare), row = int32(minor × 127) / int32(major)
into 0x6eb7e0 (128 × 12 bytes: {small, big, eighth}); small goes on the
minor axis, signs by the target's side. Length 4096.

## Each frame — srvdofunc 1 = FUN_005ae1f0 (ECX game, EDX missile)

1. Velocity ≠ 0: FUN_00554ca0 → FUN_00650840(unit, 0x400). Returns 2
   (path over) → FUN_005adf10(0, 1): the missile ends (a wall's explosion).
2. Range − 1; < 1 → ends, no unit test.
3. CollideType 6: no unit test. Range left > Range − Activate: none yet.
4. Path +0x54 (this frame's collision bits, masked) 0: none. Else each
   subtile the move entered (path +0x1d4 count, +0x1d8 list):
   FUN_0064d9b0 there; a unit by FUN_00641cb0 → FUN_005adf10(unit, 0)
   (the hit); else bit 4 → ends.
5. FUN_00641cb0, missile size m, unit size u (FUN_00620510): same subtile
   (1,1); |dx|+|dy| ≤ 1 (1,2 / 2,1); 3×3 (1,3 / 3,1); |dx|+|dy| ≤ 2 (2,2).

### The move — FUN_00650840 → FUN_006502d0, FUN_00650660

- Accel ≠ 0: a counter (+0x8c); past 4, velocity += Accel, clamp to
  MaxVel (then Accel = 0) or 0; counter 0. So every fifth frame.
- step = velocity × 0x400 >> 6; delta = unit vector × step >> 12 (signed).
  Zero delta: stop (FUN_006507b0 snaps to the subtile centre).
- Path types other than 4 snap onto the next point when within
  max(|dx|, |dy|) on both axes (FUN_00650090), then aim at the next point
  (FUN_0064fe40); past the last point: stop.
- FUN_00650150: the delta halved (FUN_00678f00, arithmetic shifts) until
  each part is within ±0x10000, then walked from the position until the
  delta's end subtile; each new subtile tested (FUN_0064ff90 →
  FUN_0064ed20 → FUN_0064d9b0 with the mask; bits & 5 block) and listed
  (ten at most). A blocked one: the position goes to the centre of the
  last free walked point and the path ends. Else position += the delta.
- FUN_0064d450 reads 0x27 for a subtile in no room of the path's room and
  its near list (FUN_00463740 over room1 +0 / +0x24): blocked.
- Then FUN_0064fb90 → FUN_0064fad0 (only with path flag 1): left the
  room → the near room holding it becomes the path's room.

So a missile aimed at a point inside its own room keeps that room and
dies at the edge of the room's near rooms (bugs.md #16).

## Charged Bolt — FUN_005c9300 → FUN_005c9290

Each of calc1 bolts: params flags 0x21 at FUN_0056d2c0's point, callback
FUN_005c9290 with its index. The callback caps range at 77 (0x4d), seeds
the missile's seed with {target x + index, 666} (FUN_00650e40), sets path
type 10 and steps = range (FUN_00648e70), and computes it
(FUN_0067a240): from the start, range / 2 hops of two subtiles, each the
8-way direction toward the target (FUN_00678c10 → DAT_006f1518's first)
turned by low & 31 of a seed draw into {−1, 0, 1, −1, …, 31: +1}. The
type-4 aim stays for the first frame, which snaps onto point 0 (the
start) without moving.

## d2d

- `rules::MissileFlight` (launch, step, walk, retarget), `missile_aim`,
  `missile_velocity`, `missile_range`, `wiggle_points`,
  `missile_touches`, `kMissileAim` (test_exe_tables).
- `game::missile_fly` (ai.hpp) flies every moving missile (the player's,
  the merc's, pets', monsters', traps') in act subtiles from its first
  frame: at `Missile::to` when the site knows the target point, else along
  its velocity; speed from `missile_speed` (path velocity / 4096 subtiles
  a frame); its rooms from `missile_rooms` (gamedata.cpp near lists).
  Hits: the subtiles entered against each foe's footprint by size.
- Charged Bolt (do 17) sends `bolt` = its index: the wiggle path.
- tests/test_monsters.cpp `missile_flights`: 18 of game.exe's flights
  replayed (frame, end, position, a hash of every frame).
- Not yet: stat 87's velocity %, Size 2/3 missiles, flag 0x400's distance
  (FUN_006417f0), the nova tables (0x6e1288), Charged Strike's and
  boss bolts' wiggle (their callback index), standing missiles
  (srvdofunc 1 never strikes at velocity 0; d2d's keep a 0.4-cell test),
  rooms not yet up (game.exe skips them).
