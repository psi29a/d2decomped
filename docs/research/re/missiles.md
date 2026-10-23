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
5. FUN_00641cb0, missile size m (Missiles.txt Size), unit size u
   (FUN_00620510, past 3 taken as 3, under 1 none), by u − 1 + (m − 1) × 3:
   same subtile (1,1); |dx|+|dy| ≤ 1 (1,2 / 2,1); 3×3 (1,3 / 3,1);
   |dx|+|dy| ≤ 2 (2,2); |dx| < 3 and |dy| < 2, or |dy| < 3 and |dx| ≤ 1
   (2,3 / 3,2); 5×5 (3,3). A missile sized outside 1..3 (a blank Size: the
   explosions, trails, lights) strikes no unit. `rules::missile_touches`.

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

## The client's hit and explosion — FUN_004d2d70 (ECX missile, EDX unit hit or 0, stack `ended`)

Missiles.txt record (0x1a4 bytes, table at `PTR_DAT_00744304 + 0xb64`,
fields from FUN_00661b20's column list): pCltHitFunc +0xa, HitSound +0x14,
ExplosionMissile +0x16, CollideKill +0x186, CollideFriend +0x187, NextHit
+0x188, NextDelay +0x189, ToHit +0x18b, AlwaysExplode +0x18c, Trans +0x18d.

The client's missile step (`FUN_004d30c0`) calls it three ways:

- its range runs out: no unit, `ended` 0;
- a wall or a blocked path (`FUN_00648eb0` & 5, or the move fails): `ended` 1;
- a unit in a subtile it entered (ClientCol +0x185): that unit, `ended` 0.

With no unit and `ended` 0 it only goes off when AlwaysExplode. A unit hit
skips one NextHit already struck (state 0x56) or one not hittable. Going
off: the pCltHitFunc (table 0x72a508), HitSound, then when
ExplosionMissile ≥ 0 that missile is made at the missile's spot
(`FUN_004cdba0`), the same owner and level. With a unit hit, CollideKill 0
leaves the missile flying.

`fireexplode` (a fire bolt's, the firebolt trap's): 12 frames (Range,
AnimLen), RandStart 5, Light 13, additive, one direction.

## Its frame direction — FUN_00620100 → FUN_006487f0, FUN_00600c90

A missile faces the 64-step direction its path holds at +0x64 (FUN_0059fa30
aims it at the target; 0 straight down, counting clockwise on screen). For a
missile (unit type 3; objects and items read path +0x1c instead),
FUN_00620100 (stdcall, the unit) returns it through FUN_006487f0 (the byte
in AL, the rest of EAX left as it was). The draw takes the frame of an
n-direction sprite from FUN_00600c90 (stdcall: dir64, n), the dword table
at **0x6e45a0**, `[log2(n) + 1][dir64]` (FUN_00517abb: the lowest set
bit's index + 1, 0 for 0).

The table rounds the 64 steps to the nearest of the sprite's own, clockwise
from straight down: 8 directions take 8 steps each (0..3 and 60..63 south),
16 four, 32 two. Each row is in D2's DCC order for its count (8: 4 0 5 1 6 2
7 3 round the compass, S SW W NW N NE E SE, the 16 and 32 rows with the
half steps after), 4 is plain angle order, and 2's row is all 0.
fireball.dcc and firebolt.dcc have 16 directions.

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
- `missiles_update` (ai.hpp): a missile gone into a unit or a wall, or run
  out with AlwaysExplode, leaves its ExplosionMissile as a visual-only
  missile there (`MissileInfo::explosion`).
- `Missile::dir` is the 64-step facing: `missile_facing` (`rules::direction64`
  between the start's and the target's floored subtiles, as `missile_fly`
  flies it) where the site aims at a point, `missile_heading` (the angle) for
  a velocity or a ring's or burst's spoke; `missile_direction(dir64, n)` is the table's
  row, drawn by world_view.cpp. tools/emu/missile_dirs.py fires Fire Bolts
  and prints game.exe's frame for 8, 16 and 32 directions;
  tools/missile-dir-dump prints d2d's: 0 of 4000 differ (seeds 1, 7), and
  the table matches row for row (`table`). Before, the 32-direction index
  modulo the sprite's count drew 684 of 2000 16-direction frames wrong.
  tests/test_game.cpp checks the 8 and 16 rows and eight fired bolts.
- tests/test_monsters.cpp `missile_flights`: 18 of game.exe's flights
  replayed (frame, end, position, a hash of every frame).
- Not yet: stat 87's velocity %, Size 2/3 missiles, flag 0x400's distance
  (FUN_006417f0), the nova tables (0x6e1288), Charged Strike's and
  boss bolts' wiggle (their callback index), standing missiles
  (srvdofunc 1 never strikes at velocity 0; d2d's keep a 0.4-cell test),
  rooms not yet up (game.exe skips them).
