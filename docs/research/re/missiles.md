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
dies at the edge of the room's near rooms (bugs.md #34, #16 before 2026-10-28).

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

## The roll — FUN_005a89a0 → FUN_005a8910

Each element of the hit record (fire +0x10, lightning +0x1c, cold +0x24,
poison +0x28, magic +0x20) is 0 unless both the missile's min and max stats
are over 0; else min + rand(max − min) on the missile's seed (+0x20,
FUN_0045c3e0), the ends swapped when min > max: the max itself is never
rolled. `rules::missile_blow` follows this for the elements, and
tools/emu/missile_hit.py checks it with the skill's min / max and the hit
(Sorceress elements only).

The physical part (+0x08, inline) reads as the same rule in the decompile,
but `rules::missile_blow` does not follow it: it rolls the physical with the
inclusive `rng.range(phys_lo, phys_hi)` whenever `phys_hi > 0`, even with
`phys_lo` 0. That difference is not examined here and the differential does
not cover a physical missile, so this change does not validate the physical
range.

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
AnimLen), RandStart 5, Light 13, additive, one direction. RandStart
(record +0x17c, a word) is read only by the client's grid maker
`FUN_004ced60` (from `FUN_004cef50` / `FUN_004e1320`: create flags 0x201):
start frame rand(RandStart) on the unit's seed (`& (n − 1)` for a power of
two, else `% n`), which `FUN_004cd540` (flag 0x200) takes off the missile's
life and sets as its frame (+0x44 = start << 8). An ExplosionMissile
(`FUN_004cdba0`, flags 0x20) and a server missile (`FUN_0059fa30`) never
read it, so an explosion starts at frame 0.

## A nova's 64 — FUN_0056d400

Nova, Frost Nova, war cries, charge releases (FUN_005d4db0), Arcane Tower
and coldunique aim their 64 at
{0x6e1288[k], 0x6e1388[k]} subtiles from the caster (int32 x 64 each): a
circle of 30 rounded to whole subtiles, so the angles step 3.9, 5.9, 6.1 ...
degrees, not 5.625. Frozen Orb's burst (FUN_005abb00: 0x6e2738 / 0x6e2638)
and spin (0x6e2b78 / 0x6e2a78) hold the same rows. d2d: `rules::kNovaRing`.
PrimePoisonNova (do 99, FUN_005ccd10) has its own 16 (0x6e31e8 / 0x6e31a8,
`rules::kPoisonNova`): the even 8 at Param1 << 6, then every calc2th from
the second at Param2 << 6. Re-check: `cd tools/emu && uv run python -c
"import emu,struct; e=emu.Emu(); print(struct.unpack('<64i', e.read(0x6e1288, 256)))"`.

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

## Server hit function 58 — FUN_005acdf0 (ECX game, EDX the missile)

Slot 58 of the pSrvHitFunc table 0x73c840 (`0x73c840 + 58 * 4 = 0x73c928`;
slot 59 is `0x73c92c` = FUN_005acf20). Baal Taunt's lightning control
(quests-act5.md "Eve of Destruction") ends here. Cited from the decompile
(no oracle run); the return paths I read in the decompile return 1.

- Gate: the missile's class (unit +4) is in `0 <= class < Missiles.txt count`
  (half-open; `PTR_DAT_00744304 + 0xb6c`), its record (`+ 0xb64`, 0x1a4 each) has
  HitSubMissile1 (short, +0x24) ≥ 0, and its owner is found by
  FUN_00552fd0 (unit flag +0xc8 bit 10, then FUN_00552f60 on unit +0x98);
  none: nothing is laid.
- Radius: the record's sHitPar1 (+0x4c), or 1 when that is 0 or less.
- Spot: the missile's subtile (FUN_0045adf0 / FUN_0045ae20: x, y) plus
  `rand(n) - radius` on each axis, FUN_0045c390 on a stack seed that
  FUN_00650e40 sets to `{x, 666}` just before (the argument is read off the
  call order; the decompile drops the register args, so n = 2 × radius + 1
  is the same kind of reading, matching FUN_0045c390's range 0 .. n-1).
  The missile's own seed is not used.
- Made by FUN_0059fa30 with a zeroed 0x5c-byte params block: flags **0x20**
  (target at [7] / [8] absolute; no flag 1, so the start is the missile's own
  spot via [2]; no flag 4, so the velocity is the record's Vel + VelLev ×
  level / 8, << 8, then × 75 / 100), [1] the owner, [2] the missile, [4]
  HitSubMissile1, [0xb] the missile's skill (FUN_0064a280), [0xc] its level
  (FUN_0064a210), [7] / [8] the spot.
  FUN_0064a280 / 0064a210 read the shorts at +10 / +0xc of the missile data
  (unit +0x14), none for a non-missile.
- FUN_0059fa30's rule applies: a spot in the same subtile as the start
  (no target unit) moves to (+1, +1), so the sub missile flies diagonally
  instead of being laid with no direction.
- Callers' side: FUN_0045adf0 / FUN_0045ae20 (unit x / y): types 2 and 4..5
  read the path-less unit's data (+0x2c)+0xc / +0x10, else the path
  (+0x2c) through FUN_006488c0 / FUN_00648900, 0 with no path.
  FUN_0045c390 (rand): `n ≤ 0` gives 0; a power of two `& (n-1)` of the
  step `lo * 0x6ac690c5 + hi`, else that step's low word `% n`.
  FUN_00650e40 stores `{arg, 0x29a}`.

d2d: `Missiles` `hit_spawn` (ai.hpp) reads the same, cited from the decompile, no oracle run,
on the gate, the radius clamp, the
sub missile, the start and the flags-0x20 aim, and keeps its own rng
(ponytail already noted). It differs in one case: when both draws land on the
missile's own subtile, game.exe aims (+1, +1) subtiles away, d2d at that
subtile's centre.

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
  tests/game/test_game_rules.cpp checks the 8 and 16 rows and eight fired bolts.
- tests/monsters/test_monsters.cpp `missile_flights`: 18 of game.exe's flights
  replayed (frame, end, position, a hash of every frame).
- Not yet: stat 87's velocity %, Size 2/3 missiles, flag 0x400's distance
  (FUN_006417f0), standing missiles
  (srvdofunc 1 never strikes at velocity 0; d2d's keep a 0.4-cell test),
  rooms not yet up (game.exe skips them).
