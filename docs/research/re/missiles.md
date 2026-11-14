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
  d2d keeps both when the missile has no target point: `missile_fly` aims
  with FUN_0064fc60 at the start plus (1, 1), and the room follows only
  when that point lies outside the first room.
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

## Client missile callbacks — `UNIT/Missile.cpp`

The flight and hit path above does not cover the callbacks reached through
the missile data's client-function pointers. In stock `Game.exe` 1.14d
(SHA-256 `631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`),
the callback-related code includes the following routines. These are static
traces of the shipped x86 binary; they have not been replayed as end-to-end
client scenarios.

- **`FUN_004d33e0`** checks whether the missile is locally active, initializes
  its animation state when needed, and compares its current subtile against
  the client view bounds. In bounds it sets path flag `0x80` and advances the
  normal client missile step (`FUN_004d30c0`); out of bounds it dispatches
  the generic client handler (`FUN_004d2d70`, no hit, `ended` 0). The address
  `0x72a3a0` contains a pointer table beginning with this routine and the
  entry at `0x004d3460`; this is indirect dispatch, not a direct call site.
- **`FUN_004d55c0`** validates the missile class and its data row, then
  compares the missile's stored skill value with the skill on a related
  unit. When the values agree and a path exists, it creates the row's
  follow-up missile and redirects that missile toward the current path
  target; otherwise it removes the current missile. The routine is present
  in the callback pointer tables at `0x72a420` and `0x72a448`.
- **`FUN_004d6fa0`** is a state-driven path callback. It obtains the
  callback state and path, handles missing paths by returning to the ordinary
  client step, and branches on states 0 through 4. Those branches test the
  associated unit and its state, advance the missile seed, change path flags
  (`0x80`, `0x200`, or `0x300`), and in some branches move the path endpoint
  or create a missile. The function is referenced from the pointer table at
  `0x72a420`.
- **`FUN_004d7e00`** is another state-driven path callback. State 0 uses the
  missile seed for a periodic random transition; state 1 derives an endpoint
  from the associated unit; state 2 redirects toward the fixed subtile
  `(0x170c, 0x3b1f)`, sometimes offset by one cell. It sets path flags
  `0xf00` for those redirects and then resumes the ordinary client step.
  The pointer table at `0x72a49c` begins with this routine.
- **`FUN_004d2760`** builds a set of missiles around a unit/path origin. Its
  offset list includes the origin and neighboring cells; it calls the client
  missile constructor for each selected offset and adjusts the resulting
  missile's direction/seed state. No direct code caller was recovered, so
  its runtime entry point remains unresolved even though its body is in the
  stock `UNIT/Missile.cpp` function range.

The tables establish indirect reachability, but their consumers and exact
Missiles.txt callback indices are not fully resolved here. In particular,
the state names and intent of `FUN_004d6fa0` and `FUN_004d7e00` remain
unknown; the behaviors above describe their observed branches rather than
assigning gameplay names. The generic per-frame movement, collision and hit
handling remain the separately documented `FUN_004d30c0` and
`FUN_004d2d70` paths.

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
  `rand(n) - radius` on each axis, x first, FUN_0045c390 on the missile's
  own seed (+0x20) that FUN_00650e40 sets to `{x, 666}` just before
  (0x5ace5f: `lea ecx, [esi + 0x20]`, EDX the subtile x; n = 2 × radius + 1,
  0x5ace8f). `hit_spawn` in ai.hpp.
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
on the radius clamp, the
sub missile, the start and the flags-0x20 aim, and keeps its own rng
(ponytail already noted). When both draws land on the missile's own subtile
and the sub missile moves, both aim (+1, +1) subtiles away (0x59fbbe..0x59fc11:
velocity [ebp-0x10] nonzero, no target unit, target equal to the start;
tests/game/test_missile_seed_spots.cpp). The gates do not match: game.exe's also needs an owner
(FUN_00552fd0) and a class inside the table; `hit_spawn` itself checks only the
sub missile and hit function number (58 / 59), and its caller checks expiry
(`now_ms >= dies`) before calling it. No owner lookup in d2d.

## Server hit function 59 — FUN_005acf20 (ECX game, EDX the missile)

Slot 59 of the pSrvHitFunc table 0x73c840 (`0x73c92c`). Baal Taunt's poison
control ends here. Cited from the decompile (no oracle run); the return
paths I read in the decompile return 1.

- Gate: the missile's class (unit +4) is in `0 <= class < Missiles.txt count`
  (half-open; `PTR_DAT_00744304 + 0xb6c`), its record (+ 0xb64, 0x1a4 each) has
  HitSubMissile1 (short, +0x24) ≥ 0, and FUN_00552fd0 finds an owner;
  otherwise nothing is laid. The decompile drops the register arguments, so
  that the owner reaches FUN_005a9370 in EDX is read from that callee's use
  of its second parameter.
- Radius/step: sHitPar1 (+0x4c), 1 when 0 or less.
- Calls FUN_005a9370 with the missile, HitSubMissile1, the missile's skill
  (FUN_0064a280), its level (FUN_0064a210), sHitPar1 as the odd step, **2**
  as the even step, and 0 for the life (param 9).
- FUN_005a9370 zeroes a 0x5c params block: flags **0x17** (1 start given, 2
  relative aim, 4 + 0x10 velocity as given, then FUN_0059fa30's × 75 / 100),
  or 0x1f with a life in [0xd] when the life argument is > 0 (not here).
  Start [5] / [6] = the missile's subtile (FUN_0045adf0 / FUN_0045ae20),
  [0xb] skill, [0xc] level, velocity [10] = record Param1 (+0x38) << 7.
  Even ring: entries 0, 2 ... 14 of the 16-entry pair 0x6e2510 (x) /
  0x6e24d0 (y), 8 ways (byte offset += step × 4 while < 0x40). Then, when the
  odd step is non-zero (FUN_005acf20 clamps sHitPar1 to at least 1 before
  the call, so here it is; FUN_005a9370 itself does not clamp it), velocity Param2 (+0x3c) << 7 and
  entries 1, 1 + s ... while the offset < 0x3c (≤ entry 15) of 0x6e2514 /
  0x6e24d4. Each is made by FUN_0059fa30 (named d2d_ai_andariel_missile in
  the decompile). FUN_005a9370 returns 0 for a class out of range or a null unit.

d2d: `hit_spawn` (ai.hpp) reads the same, cited from the decompile, no oracle run,
on the 8 even ways at Param1 << 7, the
odd ones at a stride of sHitPar1 entries (at least 1) at Param2 << 7 and the × 75 / 100.
Each spoke's target is that ring offset added to the start subtile (flag 2), so
the ways do not share the no-target (+1, +1) aim. Two
notes: its comment says "flags 0x14", game.exe's is 0x17 (the velocity is as
given in both, so the numbers agree as cited from the decompile, no oracle run), and d2d does not check that an owner
exists as the gate does (not examined further).

## Server hit function 23 — FUN_005acfc0 (ECX game, EDX the missile, stack unit)

Slot 23 of the table (`0x73c89c`). Cited from the decompile, no oracle run; the return
paths I read in the decompile return 1.

- Gate: class in range with a record, the missile's skill (FUN_0064a280) and
  level (FUN_0064a210) both > 0, and FUN_00552fd0 finds an owner.
- With the stack unit non-zero, FUN_00620c10(missile, unit) stores it as the
  missile path's target (FUN_00648b90: path +0x58 the unit, +0x5c its type,
  +0x60 its id). Only for missile types 0, 1, 3; any other type is a fatal
  error (strings 0x8ac / 0x8ae).
- Then FUN_0056d810(sHitPar1, skill, level): the srvdofunc `0x7322b0`
  table, index sHitPar1 (< 0xbf, a null entry does nothing). So hit 23 runs
  server skill function number sHitPar1 at the missile's skill and level.

d2d: not ported as a hit function (`git grep` for hit_func 23 finds no
case). The general FUN_0056d810 table dispatch is not ported either. d2d
handles one case only: `srv_do == 14` with Param2 = the curse function
(fight/missiles.cpp:535); no other table index is dispatched.

## Server damage functions 1, 2, 4 and 12 — FUN_005ad050, FUN_005ad1b0, FUN_005ad290, FUN_005ad0f0

These four sit in the **pSrvDmgFunc** table 0x73c960 (`+ slot × 4`: 0x73c964
= 1, 0x73c968 = 2, 0x73c970 = 4, 0x73c990 = 12), not the hit table. They run
with ECX game, EDX the missile, then a unit and the damage record. Cited from
the decompile, no oracle run; the decompile drops ECX / EDX, so which operand FUN_00483360
(a × b / c, here c = 100) got is a reading, not proof.

- **1, FUN_005ad050**: class gate, then FUN_0064b7c0(missile, owner, record
  +0x90 calc, -1, -1) > 0. Then the physical (damage +8) loses
  `min(phys, phys × calc / 100)` and FUN_005a9270 adds that to the damage
  field of the missile's element (FUN_0064b0c0: record byte +0xe4; 0 phys
  +8, 1 fire +0x10, 2 lightning +0x1c, 3 magic +0x20, 4 cold +0x24, 5 poison
  +0x28, 6 +0x38, 7 +0x3c, 8 +0x40, 9 +0x44, 0xb +0x14; 0xc sets +0x24).
- **12, FUN_005ad0f0**: same gate and calc; takes `min(phys, phys × calc /
  100)`, rolls the missile's own element with FUN_005a8c70, clears the other
  fields with FUN_005a91c0, then adds the taken part and the roll to the
  element field.
- **2, FUN_005ad1b0**: class gate only; sets the damage record +0x34 to
  `FUN_00483360(100)` (a × b / 100 on its hidden operands) and +0x30 to 0,
  after FUN_00625480(missile, 0x38, 0) (stat 0x38 of the missile).
- **4, FUN_005ad290**: no gate: damage +0x34 = +0x30, then +0x30 = 0.

d2d: pSrvDmgFunc is read only for 5 (`dmg_func` in gamedata.hpp); 1, 2, 4
and 12 are **not ported**. Which Missiles.txt rows use them needs the game's
table, not read here.

## Server damage function 3 — FUN_005ad220 (flag 0x4000 roll)

Slot 3 of pSrvDmgFunc (`0x73c96c`, data xref only). Cited from the decompile
(the decompile drops ECX / EDX, so the operands are a reading); no oracle run.

- Gate: the missile (second operand) non-null, its class (+4) in
  `0 <= class < *(game + 0xb6c)` (half-open) with a record at `*(game + 0xb64) + class × 0x1a4`.
- Then the missile's 64-bit seed (+0x20 low, +0x24 high) steps
  `seed = low × 0x6ac690c5 + high` (the D2 seed step, stored back), and when
  `(uint32)seed & 0x7f` < the record's **dParam1 (+0x78)**, the damage
  record's word at +4 gets **0x4000** OR-ed in. So a percentage in 128ths
  (immolationfire 19 / 128, see skills.md); it changes no damage itself.
- The stepped seed is stored even when the roll fails.

What 0x4000 does: FUN_0057cee0 tests the get-hit only with result flag 4,
and a burner's record has 1 alone (FUN_005ad730: no GetHit / SoftHit, the
Missiles.txt +4 bits 0x40 / 0x80 by FUN_00661b20's columns). With 0x4000
and no 4 it's a soft hit: a player gets FUN_0064c040 and unit flag 0x8000
(S->C 0x0c: the struck sound with the row's HitClass); a monster those, its
last command 0x13 (FUN_005734c0; FUN_005dd2b0 reads 3 or 0x13 as got hit,
and FUN_005a68e0 makes 0x13 3 at the next mode) and its mods' event 4
(FUN_005a43a0: Lightning Enchanted's bolts). A blocked record (0x10) with
0x4000 plays no block. The six rows with function 3 are the flames (blaze,
firewall, immolationfire, meteorfire, the vampires' two), dParam1 19.

d2d: the burner on a foe (`missiles_update`: `Foe::soft_hits`, the blow's
`landed` off) and on a monster (`Fight::burn`'s `soft`: `Monster::got_hit`,
`soft_hit`); `test_burn_fraction`. Rolled on d2d's rng: its missiles keep no
seed.

## Server damage function 10 — FUN_005ad2b0

Slot 10 of pSrvDmgFunc (`0x73c988`). Cited from the decompile, no oracle run.

- Same class / record gate as slot 2.
- Then damage record +0x34 = +0x30. Unlike slot 4 (FUN_005ad290) it does
  **not** clear +0x30, and unlike slot 2 it calls nothing.

d2d: **not ported**.

## Server damage function 5 — FUN_005ad2f0 (Blessed Hammer)

Slot 5 (`0x73c974`); also called directly by FUN_005ad5c0
(`0x73c994`, slot 13), which first scales the record's +8 by the owner's stat 0x19 percent
(`+8 += +8 × stat / 100`) when FUN_00552fd0 finds an owner. Read from the
decompile: class gate; FUN_005a8c70 rolls the missile's element; then for dParam1 (+0x78) > 0 and
FUN_0063e990(unit) (undead) and for dParam2 (+0x7c) > 0 and FUN_0063e940(unit)
(demon) FUN_005a9270 adds FUN_00483360(100) (roll × dParam / 100) more.

d2d: reads the same, cited from the decompile, no oracle run in this pass
(ported at fight/missiles.cpp:718 and components/rules/combat.hpp:699; an existing
tools/emu/hammer_hit.py and tests/game/test_hammer_hit.cpp are not part of
this change). The
slot-13 wrapper (FUN_005ad5c0) is not ported.

## Server damage function 6 — FUN_005ad3a0

Slot 6 (`0x73c978`). Cited from the decompile. FUN_00552fd0 finds the
missile's owner; when there is one and bit 9 (0x200) of its flags (+0xc4) is
clear, and the stack unit is non-null, the unit's flags (+0xc4) get 0x20000
OR-ed in. Which flag 0x20000 / 0x200 are is not read here; which operand is
the unit (a __thiscall in the decompile) is a reading.

d2d: **not ported**.

## Server hit function 56 — FUN_005acc50 (Armageddon, Diablogeddon)

Slot 56 of the table (`0x73c920`). Cited from the decompile; the return
paths I read in the decompile return 1.

- Gate: class in range with a record, and FUN_00552fd0 finds an owner;
  otherwise nothing happens. Skill = FUN_0064a280, level = FUN_0064a210 of
  the missile.
- Spot: the missile's subtile x / y (FUN_0045adf0 / FUN_0045ae20 inlined:
  types 2 and 4..5 read data +0xc / +0x10, else the path through
  FUN_006488c0 / FUN_00648900, 0 with none).
- Roll: FUN_005a89a0(missile, 0, &rec), then the record's +0xa8 (dword) and
  +0xac (word) are OR-ed into its flags (the first dword and word).
- Radius: sHitPar1 (+0x4c) when ≥ 1; else FUN_00646ca0(game, skill record
  +0x64, skill, level) (a skill calc, +0x64 being the aurarangecalc of
  skills.md), at least 1; a skill out of range (`+0xba0`, 0x23c each)
  or null ends it.
- Burst: FUN_0056bad0(x, y, radius, &rec, 0), which walks the units in the
  radius with callback FUN_0056ba90 (0x8583 default, Skills.cpp); which of
  them it hits is not read here.
- Then, when HitSubMissile1 (+0x24) ≥ 0: FUN_0056ede0(skill, level, class,
  x, y), one missile of that class made by FUN_0059fa30 flags 1 at the spot.

d2d: fight/missiles.cpp case 56 reads the same, cited from the decompile, no oracle run,
on the burst at the missile's spot with sHitPar1 else the skill's aurarange, and the one sub missile on
that spot. Not examined: the OR of record +0xa8 / +0xac into the roll's flags.

## Standing missiles: the wrappers and the path getters

Re-check each with `python3 tools/ghidra/fn.py <addr>`. Cited, not run in
the emulator.

- **`FUN_006486c0`** (path): returns path +0x7c, the velocity set at make
  time (the 75 / 100 scaled one above). Unlike its neighbours `FUN_006488c0`
  / `FUN_00648900` it has no null assert. `FUN_005ae1f0` calls it on the
  missile's path first: non-zero runs `FUN_00554ca0` (path over: ends it).
  `FUN_005ae480` tests it too: non-zero goes straight to `FUN_005ae1f0`.
- **`FUN_00648eb0`** (already ported) recomputes path +0x54 only when the
  velocity is 0, via `FUN_0064d9b0` at the unit's own subtile (path +2 / +6,
  type +0x44, mask 0x7fff). So `FUN_005ae1f0` does reach its unit search for
  a missile that stands still; the line "srvdofunc 1 never strikes at
  velocity 0" in "d2d" below is only true of the moving branch (step 1).
- **`FUN_00648f40`** (path, &list): returns the entered-subtile count
  (path +0x1d4) and sets list to path +0x1d8, an array of {short x, short y}
  (4 bytes each). If path flag +0x34 & 0x20000 is clear it raises the
  "Unrecoverable internal error" (`FUN_00408a60`, `DAT_006cc837`, line
  0x8f4, then `FUN_00681e09(-1)`), the same shape as the null asserts of
  `FUN_006488c0` (line 0x692) and `FUN_00648900` (0x69d). Callers
  `FUN_005ae1f0`, `FUN_004d30c0`.
- **`FUN_0064a640`** (unit): missile data (unit +0x14) +8 as a short, 0 if
  the unit isn't type 3 or has no data. That is the "Range left > Range -
  Activate" test above: `FUN_005ae1f0` returns 1 (nothing yet) while
  `FUN_0064a640` < `FUN_0064a380` (range left, data +0x10).
- **`FUN_005ae480`** (ECX game, EDX missile): path velocity non-zero ->
  `FUN_005ae1f0`. Else ORs 0x40 into the room word of the missile's
  subtile (`FUN_0064cb90`: room of `FUN_00620bb0`, x = path +2, y = path
  +6; a unit of type 2 / 4 / 5 has its pointer at +0x2c read as +0xc / +0x10)
  and then `FUN_005ae1f0`. `FUN_005b04a0` tail-calls it. The 0x40 is taken
  off again by `FUN_005adf10` (`FUN_0064eba0`, shape from `FUN_00620510`,
  bits 0x40) when the missile ends. tools/emu/sk_ground_hit.py runs it.
- **`FUN_005ae520`** (missile do 5, table 0x73c768 + 5 x 4): the same stamp
  but by shape: `FUN_0064ea00(room, x, y, size, 0x40)` (1 point, 2 plus,
  3 box), on each call, then the animation frame (unit +0x44 >> 8) rule by
  the row byte +0x181: on the last frame (+0x181 - 1) the position becomes
  (`FUN_0045c3e0()` + frame) x 256 (its ECX / EDX aren't in the decompile:
  not checked); when range left == +0x181 it becomes (+0x181 - 3) x 256;
  when range left < +0x181 the frame steps back 2, floored at 0. Then
  `FUN_005ae1f0`. Any missing row/path goes straight to `FUN_005ae1f0`'s end
  (`FUN_005adf10(0, 1)`).
- **`FUN_005ae400`** (ECX game, EDX missile): with a valid row whose +0x18
  (a short) is >= 0, evaluates the row's calc `FUN_0064b7c0` (missile, owner
  `FUN_00552fd0`, row +0x80, row index, level `FUN_0064a210`) and passes it
  to `FUN_005a9720(row +0x18, 0, result)`, then `FUN_005ae1f0`. Neither the
  call graph nor the data refs show who uses it: not placed in a table here.
- `FUN_00641cb0` (missile_touches) and `FUN_005ae1f0` read the same as
  d2d (`rules::missile_touches`, `standing_struck`) for missile sizes 1-3.
  Outside 1-3 `FUN_00641cb0` returns 0 (strikes no unit), while
  `standing_struck` keeps a 0.5-cell fallback: a known difference.
  `FUN_0064a380`,
  `FUN_00648eb0`, `FUN_0064d9b0` too: cited from the decompile, no oracle
  run, nothing new to say.

d2d: `FUN_00648f40`, `FUN_006486c0`, `FUN_0064a640`, `FUN_005ae400`,
`FUN_005ae480`: not ported as such (`missile_fly` keeps its own entered
list). `FUN_005ae520`: ported without the 0x40 stamp (collision.md "By shape"
says `FUN_0064ea00` isn't ported) and without the frame stepping, which only
changes the picture.

## d2d

- `rules::MissileFlight` (launch, step, walk, retarget), `missile_aim`,
  `missile_velocity`, `missile_range`, `wiggle_points`,
  `missile_touches`, `kMissileAim` (test_exe_tables).
- `game::missile_fly` (ai.hpp) flies every moving missile (the player's,
  the merc's, pets', monsters', traps') in act subtiles from its first
  frame: at `Missile::to` when the site knows the target point, else
  FUN_0064fc60 toward the start plus (1, 1); speed from `missile_speed` (path velocity / 4096 subtiles
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

## One unit a frame, and the last unit skipped — FUN_005ae1f0, FUN_005a8730

A missile of a srvdofunc 1 row (its handler at 0x73c768 entry 1 is FUN_005ae1f0)
with LastCollide and neither NextHit nor Pierce is handed ONE unit a frame:
FUN_005ae1f0 stops at the first unit FUN_00641cb0 finds in the subtiles the
frame entered that FUN_005a8730 passes (every unit but the one the missile
struck last, FUN_0064a470), and FUN_005adf10 makes that unit the last one
(FUN_0064a400, 0x5adfae). So two monsters in one frame are struck on two frames
at most, and a monster the missile left for another may be struck again. The
player skills with such a row are Lightning, Lightning Fury, Inferno, Bone Spear
and Arctic Blast. `Fight::skill_missile_hits` keeps `last_hit` for these rows as
it does for NextHit rows (no `struck` list). The Pierce rows (arrows, javelins,
Blessed Hammer) and the srvdofunc 3 rows (Blizzard shards, Poison Explosion and
the javelin clouds, which stand at velocity 0) keep their `struck` list: the
first need ToHit and the hit and damage functions in the oracle, the second take
FUN_005ae480's standing path (FUN_0064cb90), not run. tools/emu/light_pierce.py
and test_light_pierce check 1,192 casts a seed. Which unit of a frame goes first
is d2d's monster order here; game.exe walks the subtiles in path order (not read).

## Pierce — FUN_005ada80 (ESI the missile; called from FUN_005adf10)

Cited. Returns the hit-result mask local of `FUN_005adf10`: 3 (run
the hit function, then end the missile) unless the missile's
Missiles.txt row (0x1a4 each at `PTR_DAT_00744304 + 0xb64`) has the
**Pierce** flag (byte +4, mask 0x04: FUN_00661b20's column list gives
LastCollide mask 0x01, Explosion 0x02, Pierce 0x04, CanSlow 0x08) **and** the
missile's own stat 0x148 (`FUN_00625480(missile, 0x148, 0)`) is nonzero.
Then the stat is lowered by one (`FUN_00627260(missile, 0x148, n - 1, 0)`)
and 2 is returned: the hit function runs and the missile flies on (bit 1
clear, so FUN_005adf10's end test fails). No random roll is drawn.
FUN_005adf10 only asks when the missile has an owner (`FUN_00552fd0`)
that has stat 0xa6 or `FUN_00625500(owner, 0x9c)` nonzero, and a struck
unit. Stats 0xa6 and 0x9c are ItemStatCost's skill_pierce (166, Pierce's
passivestat1) and item_pierce (156).

Stat 0x148 is a per-missile counter. The server seeds it in
**FUN_0059f940** (EAX the owner, the missile on the stack), which
FUN_0059fa30 calls for every missile it makes (0x59fe72, EAX = params[1],
the owner). It needs an owner of type 0 or 1, a missile (type 3) whose
row has the Pierce flag (byte +4 & the mask at 0x6ce270, 4), and a chance
= skill_pierce (`FUN_00625480`) + item_pierce (`FUN_00625500`) that is not
0; otherwise it sets nothing. It seeds {the owner's own stat 0x148
(pierce_idx, `FUN_006253b0`), 666} (`FUN_00650e40`), draws `% 100` while
the draw is under the chance (signed compare, 0x59f9fb), at most 4 times,
and sets the missile's stat 0x148 to the count. So a volley's missiles
from one command share their count. The owner's pierce_idx (unsaved) is
bumped by the skill commands C->S 0x05 / 0x06 / 0x0c / 0x0d
(`FUN_00549d00` and its siblings, network.md); `FUN_00597e20` bumps it too
(its default branch, not read further). The client's copy is
`FUN_004cd420`, the packet handler writes it from a packet byte, and the
client hit adds 1.

Re-check: `python3 tools/ghidra/fn.py 005ada80 005adf10 0059f940`,
`cd tools/emu && uv run python disasm.py 59f940`; the oracle
`cd tools/emu && uv run python pierce_charges.py 2000 1 --dump ../../tests/pierce_charges_cases.inc`
then `git diff --exit-code tests/pierce_charges_cases.inc`.

d2d: `rules::pierce_charges` matches FUN_0059f940 on 2000 cases
(tests/test_pierce_charges.cpp). `Fight::launch` gives a Pierce row's
missile its charges from the owner's skill_pierce + item_pierce and its
stat 0x148: the player's `psum` and `pierce_idx` (bumped by each
`cmd::UseSkill`), or the merc's gear and `merc_pierce_idx` on a skill
missile. A struck monster spends one while
that owner still has either stat, with no draw (`skill_missile_hits`,
tests/game/test_merc_pierce.cpp). ponytail: nothing bumps a merc's stat
0x148 (`FUN_00597e20`'s default branch isn't read), a merc's plain
arrow still ends on the first unit (its hit is not `skill_missile_hits`),
and `pierce_idx`
counts d2d's commands, not game.exe's packets.

## Server hit function 20 helper — FUN_005ab2a0 (area callback)

Cited. The per-unit callback `FUN_005ab370` (hit 20) hands to the area
walker `FUN_0056b7e0`. ECX is the walker's state (+8 the units handled so
far, +0xc a context block of six words), EDX the struck unit. Context:
[0] owner unit, [1] the missile unit, [2] skill id, [3] skill level,
[4] the most units to serve, [5] the Missiles.txt row to make.

It returns 0 (stop) once `ctx[4] <= state[+8]`. Otherwise it fills a
`FUN_0059fa30` param block (0x5c bytes, zeroed): flags **0x20** (aim at a
subtile, start from unit [2]), [1] owner, [2] the missile unit, [4] the
row, [7] / [8] the unit's subtile x / y (`FUN_006488c0` / `FUN_00648900`
for monsters and players and missiles; the data block's +0xc / +0x10 for
objects, items and tiles: types 2, 4, 5), [0xb] skill, [0xc] level. It
makes the missile and returns 1. The result is not tested, and it does
not skip the unit that the missile struck (proven: `tools/emu/hit_fury.py`).

Re-check: `python3 tools/ghidra/fn.py 005ab2a0`,
`python3 tools/ghidra/fn.py 0059fa30` (param [2] is the start unit when
flag 1 is clear: `FUN_0045adf0` / `FUN_0045ae20` of it).

d2d: not ported on its own; `Fight::burst` case 20 (fight/missiles.cpp)
inlines it (see the next section).

## Server hit function 20 — FUN_005ab370 (Lightning Fury)

Cited. Slot 20 of the pSrvHitFunc table 0x73c840 (`0x73c890`, the only
reference to it that I found). ECX game, EDX the missile unit.

- The missile's row (Missiles.txt, 0x1a4 each) must have a sub-missile id
  (short at +0x24) >= 0, the skill (`FUN_0064a280` id, `FUN_0064a210`
  level, from the missile's data block) must be in the skills table, and
  the owner (`FUN_00552fd0`) must exist. Otherwise it returns 1.
  `FUN_00552fd0` (fastcall: ECX game, EDX unit) returns 0 for a null unit
  or one whose `+0xc8` bit 0x400 (shr 0xa, and 1) is clear; else
  `FUN_00552f60(game, type = unit +0x94, id = unit +0x98)`, the unit of
  that type and id: the head at `game + [0x6e10e0 + type x 4] + (id & 0x7f)
  x 4` (type 5 at `game + 0x1b20`, above 5 none), then `+0xe4` links until
  `+0xc` is the id, 0 when none (`ret 4`).
- Radius: the missile row's sHitPar1 (+0x4c) when >= 1, else
  `FUN_00646ca0(owner, skill +0x64 (aurarange), id, level)`; at least 1 only
  on the calc path (a result below 1 becomes 1).
- Count: sHitPar2 (+0x50) when >= 1, else calc1 (skill +0x138); at least 1.
- Filter: the skill's +0x50 (aurafilter), 0xa783 when that is 0.
- `FUN_0056b7e0(x, y of the missile, radius, filter, FUN_005ab2a0, &ctx, 1,
  ".\MISSILES\MissMode.cpp", 0xec3)` walks the units in range and makes one
  sub-missile each (FUN_005ab2a0) until the count is served. Returns **3**
  (damage, end).
- FUN_005ab370 hands the walk the owner in EDX (0x5ab4cf), and the walk
  uses that unit three ways: its rooms are the owner's room
  (`FUN_00620bb0` at 0x56b815) and that room's near list
  (`FUN_00619790`); it is `FUN_0056b3e0`'s caster (ECX at 0x56b95c), so
  the filter's line (0x200, in the default 0xa783) runs from the owner
  while the reach is measured from the missile's x, y; and it is skipped.
- The walker skips only the unit handed to it in EDX, which here is the
  owner (a pointer comparison at 0x56b8e0); the callback has no test for
  the unit the missile struck. FUN_005ab370's stack argument (the unit
  struck, `ret 4`) is never read. The walk's flag 1 also skips a monster
  whose MonStats flag 0x1b is set (`FUN_00457490`, called at 0x56b97e with
  EDX 0x1b): the `noaura` column (the loader `FUN_00651210` gives it bit
  0x1b of record +0xc at 0x651994).

Proven: `tools/emu/hit_fury.py` runs FUN_005ab370, the owner's lookup
FUN_00552fd0 / FUN_00552f60, the walk FUN_0056b7e0 and the callback
FUN_005ab2a0 for real over a room of the owner, the unit struck, another
monster and one out of reach (row 206, sHitPar1 0 / 4, sHitPar2 0..3,
aurafilter 0 / 0xa583, four calc pairs): the struck unit gets the first
sub-missile, the owner none, the filter handed on is the aurafilter, else
0xa783, and the walk's room and the filter's caster are the owner on every
case. With the missile's bit 0x400 clear, or set with an id no unit has, it
returns 1 and walks nothing.

Re-check (in tools/emu): `uv run python hit_fury.py`, `uv run python disasm.py 56b7e0`.

d2d: matches. `Fight::burst` case 20 no longer skips `missile.target`
and tests each monster with the aurafilter, else 0xa783 (`filter_takes`,
aura_filter.hpp), its line from the owner (the hero, or its merc for a
merc's missile); with no owner (`hero_of` finds none) nothing is sent.
`test_lightning_fury` checks it against the oracle's cases, and a wall
between the owner and a monster the missile sees stops the bolt. ponytail:
the walk's rooms (the owner's and its near list) aren't modelled: d2d
walks every monster in reach.

## Server hit function 21 — FUN_005ab500 (Battle Cry)

Cited. Slot 21 (`0x73c894`). ECX game, EDX the missile, stack the struck
unit. Returns 1 (nothing, go on) when there is no owner (`FUN_00552fd0`)
or no struck unit; otherwise, with the skill (`FUN_0064a280` / `a210`) in
range and its auratargetstate (skill +0x82) a valid state (>= 0 and <= the
states count at `PTR_DAT_00744304 + 0xc4`):

1. the target filter test `FUN_0056b3e0` on the struck unit (the decompile
   shows the flags as 0xa783 when skill +0x50 is non-zero, else 0; the
   register arguments are hidden, so the exact flag is not settled);
2. the length `FUN_00646ca0(owner, skill +0x60 (auralen), id, level)`;
3. `put_state` (`FUN_005c6cc0`'s caller chain, `d2d_states_put_state`)
   and, when it takes, `FUN_005c6cc0(skill, id, level)` (the aurastats).

`FUN_005c6cc0` (ECX the unit, EDX the stat list; stack: skill row or 0, id,
level) does nothing for a null unit or list, or with no row and an id outside
the skills count. For each of the six slots (stat ids as shorts at row
`+0x54`, calc offsets at `+0x68`) whose stat id is `0 <= id <` the stat-table
count (`+0xbd4`) with a non-null stat row (`+0xbcc`, rows of `0x144`) it
evaluates `FUN_00646ca0(unit, calc, id, level)` (the passed id, even with a row
given), and for a nonzero value adds it with `FUN_006270b0(list, stat, value,
0)`; stat 68 (0x44) also sets 69 (0x45) to it through `FUN_00627150`.
It ends with `FUN_00623f50(unit, ".\SKILLS\SkillDruid.cpp", 0xf8)` on every
non-rejected call. Run with those four callees hooked and their arguments
logged, 11 cases (row given and from the table, bad ids, null unit / list, a
zero value, a null stat row): `tools/emu/skills_combat_helpers.py`. The callees
and d2d are not compared.

Returns **0** when the filter passed, else 1.

Re-check: `python3 tools/ghidra/fn.py 005ab500`.

d2d: reads the same in part, cited from the decompile, no oracle run. `Fight::skill_missile_hits` hit_func 21 (fight/missiles.cpp:629)
puts the auratargetstate for auralen frames (at least 1) on the monster
and no damage (returns 0; game.exe's decompile also returns 0 on the filter-passed path, cited, no oracle run). Whether d2d's state carries the
aurastats of `FUN_005c6cc0` was not checked, and game.exe's decompile shows
no floor on the length (d2d takes at least 1). No difference proven; no
oracle run.

## Ring of eight — FUN_005ab700 (helper of hit 25)

Cited. Makes up to eight missiles round a unit. Rejects a row index outside
the table (`PTR_DAT_00744304 + 0xb6c`). Param block (zeroed 0x5c bytes) for
`FUN_0059fa30`: flags **0x1f** (start subtile, relative target, velocity
given), [1] owner (EDX), [2] the start unit, [4] the row, [5] / [6] the
start unit's subtile (as in `FUN_005ab2a0`), [7] / [8] the offset from the
tables `0x6e2618` / `0x6e25f8`, [10] the row's field `+0x38` << 7 as the
velocity, skill and level in [0xb] / [0xc] and the level - 1 in [0xd]. The
offset index steps `param_7 * 4` bytes over 0x20, so a step of 1 makes 8
(the 8 table entries), 2 makes 4. The return paths I read in the decompile return 1. The order of the register and stack arguments is
not settled from the decompile, so the skill / level / step roles are the
most likely reading, not proven.

Re-check: `python3 tools/ghidra/fn.py 005ab700`,
`python3 tools/ghidra/fn.py 0059fa30`.

d2d: not ported (no mention of the tables or of 0x6e2618).

## Server hit function 25 — FUN_005ab820

Cited. Slot 25 (`0x73c8a4`). ECX game, EDX the missile, stack an optional
unit. Returns 1 if the missile has no data, its row's sub-missile id
(short +0x24) is negative, or there is no owner (`FUN_00552fd0`). The
start unit is the stack unit, or the missile itself when 0. The step is
the row's sHitPar1 (+0x4c), at least 1. Calls `FUN_005ab700` (the ring of
eight, step 1 = 8 missiles, 2 = 4) with the missile's skill and level
(`FUN_0064a280` / `a210`) and returns **3** (damage, end).

Re-check: `python3 tools/ghidra/fn.py 005ab820`.

d2d: not ported. `Fight::burst` has no case 25.

## Server hit function 26 — FUN_005ab8d0 (Grim Ward's start)

Cited. Slot 26 (`0x73c8a8`). ECX the missile, EDX unit. With a missile
data block, its row's sub-missile id (short +0x24) >= 0, an owner
(`FUN_00552fd0`) and a skill in range, it makes one missile with
`FUN_0059fa30`: flags **0x8001** (start subtile given, range given), [1]
owner, [4] the sub-missile row, [5] / [6] the missile's own subtile,
[0xb] / [0xc] skill and level, and [0x13] the **range in frames**:
sHitPar1 (row +0x4c) when >= 1, else calc1 (skill +0x138) raised to **at
least 5** (the floor applies only on the calc path). The return paths I read in the decompile return 1.

Re-check: `python3 tools/ghidra/fn.py 005ab8d0`; the flag 0x8000 branch is
in `python3 tools/ghidra/fn.py 0059fa30` (range = param [0x13]).

Proven: `tools/emu/hit_grim_ward.py` runs FUN_005ab8d0 for each row with
pSrvHitFunc 26 (Missiles.txt rows 249, 252, 255), sHitPar1 -1, 0, 1, 3, 9
and calc1 -3 .. 8: flags 0x8001 and that range each time.

d2d: matches. `Fight::burst` case 26 (fight/missiles.cpp) sets the
sub-missile's range to sHitPar1, else calc1 at least 5;
`test_grim_ward_start` checks it against the oracle's cases.

## Server hit function 28 — FUN_005aba10

Cited. Slot 28 (`0x73c8b0`). ECX game, EDX the missile, stack the struck
unit. The returns I read in the decompile are 1. It acts only with a skill in range, an owner
(`FUN_00552fd0`) and a struck **monster** (type 1). The unit it measures
from is the one whose id sits in the missile data's `+0x28` (`FUN_0064a730`,
the slot `FUN_0064a710` writes) looked up by `FUN_00552f60`. If that unit is
found and the squared distance (`FUN_006492a0`) of two positions is below
`FUN_004e6ca0(skill, level)^2`, with `FUN_004e6ca0` = `(level - 1) *
skill[+0x14c] + skill[+0x148]`, it runs `FUN_005ddd00` (the Howl / Terror
run, missiles.cpp:623) on the struck monster with the skill's `+0x158`
and `+0x15c` values (`FUN_004e6c70`, `FUN_004f4110`). Which two positions
are compared, and which skill uses slot 28, are not settled from the
decompile (hidden register arguments).

Re-check: `python3 tools/ghidra/fn.py 005aba10`.

d2d: not ported. `Fight::burst` / `strike` have no case 28.

## Server hit function 29 — FUN_005abb00 (Frozen Orb's burst)

Cited. Slot 29 (`0x73c8b4`). ECX game, EDX the missile.

- No data block, a row sub-missile id (short +0x24) < 0 or no row: returns 1.
- The missile's **remaining frames** (`FUN_0064a380`, data +0x10) non-zero:
  returns **2** (hit handled, the missile flies on); nothing is made.
- No owner (`FUN_00552fd0`): returns 1.
- Else `ceil(64 / step)` missiles (step 3 gives 22): `FUN_0059fa30` flags **2** (relative target),
  [1] owner, [2] the missile as start unit, [4] the sub-missile row, [7] / [8]
  = `0x6e2738[k]` / `0x6e2638[k]`, [0xb] / [0xc] skill and level. `k`
  steps `sHitPar1 * 4` bytes (at least 1 entry) over 0x100 bytes. Each new
  missile gets that same vector stored in its data block at `+0x28` /
  `+0x2c` (`FUN_0064a710` / `FUN_0064a760`: the spin direction). Returns **3**.

Re-check: `python3 tools/ghidra/fn.py 005abb00`,
`python3 tools/ghidra/fn.py 0064a380`.

d2d: reads the same on the burst, cited from the decompile, no oracle run: `Fight::burst` case 29 launches
`kNovaRing[k]` for `k += max(hit_par1, 1)` over 64. Not modelled: the
return 2 while the orb still has frames (d2d returns false for hit 29 in
`skill_missile_hits`, missiles.cpp:609, which passes over units too), and the stored
vector at +0x28 / +0x2c. No oracle run.

## MissMode.cpp, the 44 functions — `tools/emu/missmode.py`

`MISSILES/MissMode.cpp` is FUN_005ab630 to FUN_005adcc0. Its 44 functions
run for real in the emulator on a synthetic Missiles.txt / Skills.txt table
(sgptDataTable at `0x744304`: missiles base +0xb64, count +0xb6c, row 0x1a4;
skills base +0xb98, count +0xba0, row 0x23c). The calls it makes out of the
module (unit accessors, stat reads and writes, `FUN_0059fa30`, skill calcs,
rooms, spawns) are hooked and logged, with `FUN_0059fa30`'s whole 0x5c-byte
argument block, and a model written from the disassembly predicts the return
value, the log and the memory written. The oracle does not play a game: it
shows what each function asks of its neighbours, not what they then do.
Mutating the model (an off-by-one in a clamp, a threshold, a count) fails it.

Slots: the server hit table is at `0x73c840` (slot = (address − it) / 4), the
damage table at `0x73c960`, the server do table at `0x73c768`. Row fields used:
+4 / +5 flag bytes (mask 2 / 3 of Fog's bit table at `0x6ce268`), +0xc srvdo,
+0xe srvhit, +0x24 / +0x26 / +0x28 HitSubMissile1..3, +0x36 ProgOverlay, +0x38
Param1, +0x44 Param4, +0x4c / +0x50 sHitPar1 / 2, +0x78 / +0x7c dParam1 / 2,
+0x90 DmgCalc1, +0x186 CollideKill; Skills.txt +0x138 calc1, +0x144 calc4,
+0x14c Param2. The functions that read a row reject a null unit and a class outside
`[0, count)` first (`005ad9d0` and `005ada80` do not check, and the oracle
only gives them valid classes); the columns below leave that out.

| Function | Slot | What it does (ECX game, EDX the missile; target on the stack) |
|---|---|---|
| `005ab630` | area callback | Not a table slot: its address is stored by `Missiles_SrvHitFunc_SpawnSubMissileInvertFrameInArea` (code at `0x5aded3`). ECX a context (game, hit count at +8, argument block at +0xc), EDX the unit found. Stops (0) when the cap (block +0x10) is positive and the hit count reached it. Else one `FUN_0059fa30`, flags 0x20, owner, missile, skill and level from the block, sub-missile (+0x14), start = the unit's position. Returns 1 |
| `005ab700` | helper | ECX game, EDX owner, stack target, row, skill, level, stride. Row outside the table: returns 0. Else flags 0x1f, the target as missile and target position, velocity Param1 << 7, level − 1 as loop count, and one missile per entry of the eight-vector table at `0x6e25f8` / `0x6e2618`, stepping `stride` entries. Returns 1 |
| `005ab820` | 25 | HitSubMissile1 ≥ 0; no owner: returns 1; the target defaults to the missile; stride = sHitPar1 (at least 1); `005ab700`. Returns 3 |
| `005ab8d0` | 26 | HitSubMissile1 ≥ 0, owner, skill in range. One stationary sub-missile (flags 0x8001) at the missile's position; lifetime sHitPar1, or when that is 0 or less the skill's calc1, at least 5 (the decompile's order, calc before the zeroed block, is the compiler's). Returns 1 |
| `005aba10` | 28 | Target is a monster, `FUN_00552f60` finds the minion named by the missile's target X; target within `range(skill, level)` subtiles of it (squared distances): `FUN_005ddd00` applies terror to it with the skill's Param5 and Param4. Returns 1 |
| `005abc40` | 5 | A fan of 64 (steps of sHitPar1 × 4 bytes over 0x100 of the tables `0x6e2838` / `0x6e2938`): flags 2, owner, the missile as start; a new missile also gets that vector as its spin direction (`FUN_0064a710` / `FUN_0064a760`). No owner: 1. Returns 3 |
| `005abd70` | 31 | Life leech: damage from `FUN_005a8c70`, negative taken as 0, added to the owner's life and capped at its max. No owner or target: 1. Returns 2, 3 when CollideKill |
| `005abe50` | 32 | Called with no unit hit and the missile's target X still 0: `FUN_005a9930` opens a portal of the row's Param4 kind. Returns 0 |
| `005abeb0` | 33 | No unit hit: sets the room's portal flag (`FUN_0061aed0`). Returns 0 |
| `005abee0` | 35 | No unit hit: finds the object named by target X, sets its room's flag and, when its mode is 0, flags it for update and sets a timer event at `ObjectTbls line +0xdc >> 8` + the game frame (`FUN_005417d0`). Returns 1 (0 with a unit hit or no object) |
| `005ac020` | 37 | Returns 2 when a unit is hit, else 0 (pierce through units) |
| `005ac0a0` | 38 | A star at the missile's position: flags 3, count = (level − 1) × sHitPar2 + sHitPar1, or when that is 0 or less the skill's calc4, at least 1, path callback `0x5ac040`; `FUN_0056d4e0`. No owner: 1. Returns 1 |
| `005ac1d0` | 39 | `FUN_005b3570` spawns a random monster at the missile's position in its room. Returns 1 |
| `005ac250` | 40 | Same count as 38, then `FUN_005d5bf0` scatter (count, count / 4). Returns 1 |
| `005ac350` | 43 | Heal the target: random between `FUN_00647bc0` and `FUN_00647d00` of the owner's skill, the target's life capped at its max; ProgOverlay when it was not full. A skill outside the table: 1 (no heal). Returns CollideKill |
| `005ac480` | 45 | Star of sHitPar1 missiles at the missile's position (sHitPar1 > 0), path callback only when sHitPar2 ≠ 0. Returns 1 |
| `005ac800` | 50 | With a unit hit: returns 2 when remaining frames < total − sHitPar1, else 0; no unit hit: 0 |
| `005ac870` | 51 | HitSubMissile1, 2, 3 (each ≥ 0): `FUN_0059fa30` with the missile as start. No owner check. Returns 1 |
| `005ac940` | 52 | Eight vectors (`0x6e2a38` / `0x6e2a58`) as 005abc40. **It does not write the missile row**: the argument block's row stays 0 (the decompile agrees: no `HitSubMissile1` store). Returns 1 |
| `005aca50` | 53 | Aura on the unit hit: frames left (target X − game frame) at least 10 and within `FUN_00644f20`: `FUN_005c7db0`. Returns 2, else 1 |
| `005acaf0` | 54 | No unit hit: `FUN_006439f0` finds the owner's skill, `FUN_006444a0` its Param1: `FUN_0054e600` spawns that monster at the missile. Returns 1 |
| `005acb60` | 55 | Only players: mana drained by sHitPar1 % (clamped 1..100, at least 1, at most the mana left). Not a player or no unit: 2; invalid row, sHitPar1 < 1 or no mana: 1; else 2 |
| `005acf20` | 59 | `FUN_005a9370` radial pattern (documented above); no owner: 1. Returns 1 |
| `005acfc0` | 23 | `FUN_0056d810` dispatch (documented above) |
| `005ad8f0`, `005ad910` | event callbacks | Not hit functions. Three stack arguments (a, b, c; c unused): `FUN_005d0c40(a, 0x7c, b, 0)` and `(…, 1)`. Their addresses are pushed as data in the function that holds `0x5b099f` (named SrvDoFn19 in Ghidra; 0x7c is skill 124, which the decompile names Redemption). Ghidra's five-argument prototype is wrong |
| `005ad970` | 57 | No unit hit and target X 0: sets it to 1, and when the quest flag (`FUN_005444b0(game, 0x24)`) is set `FUN_0058e920` spawns Tyrael; sets the room flag. Returns 0 |
| `005ad9d0` | unreferenced | Runs the row's srvhit slot (when `0 < slot < size`) with no unit, then frees the unit (`FUN_00555600`). No reference to it in the 1.14d image |
| `005ada80` | pierce | Documented above (Pierce). Returns 3, or 2 with a pierce charge left |
| `005adbb0` | server do | **EDI the missile, the game on the stack.** srvdo slot `0 < n < size` else nothing. Frees the missile (`FUN_00555600`) when: a block-in-town missile's owner stands in a town room; an owner that is a player stands in town; the missile has no keep-in-town bit and its room (or the nearest one, `FUN_00463740`, when it has none) is town; or the srvdo function returns 2 |
| `005adcc0` | timers | ECX game, EDX missile, one stack argument ignored: `005adbb0`. Called from `EVENT_DispatchMissileTimers` (`0x54129f`, `0x5412f6`) |

Damage functions (ECX game, EDX missile, stack target and a `D2DamageStrc`;
fields +4 flags word, +8 physical, +0x28 / +0x2c poison damage and length,
+0x30 cold length, +0x34 freeze length, +0x44 stun length, +0x60 hit class):

| Function | Slot | What it does |
|---|---|---|
| `005ad050` | 1 | DmgCalc1 (`FUN_0064b7c0`) = a percentage, 1..100: that share of the physical moves to elemental (`FUN_005a9270`) |
| `005ad0f0` | 12 | The same share, but the elemental damage is recalculated: `FUN_005a8c70`, the record cleared (`FUN_005a91c0`), then the sum added back |
| `005ad1b0` | 2 | Freeze length = the missile's cold-length stat (0x38) × dParam1 % (negative as 0); cold length 0 |
| `005ad220` | 3 | One step of the missile's seed (`lo × 0x6ac690c5 + hi`); if its low 7 bits are below dParam1, result flag 0x4000 |
| `005ad290` | 4 | Freeze length = cold length; cold length 0 |
| `005ad3a0` | 6 | The target gets unit flag 0x20000 when the owner's flag bit 9 is clear |
| `005ad3e0` | 7 | Stun length = dParam1, or when that is 0 or less the skill's range; hit class 0x60 |
| `005ad450` | 8 | ProgOverlay (> 0) on the target |
| `005ad4a0` | 9 | Stun length = dParam1, or the skill's Param2 (0 for a skill outside the table); hit class 0x60 |
| `005ad2b0` | 10 | Freeze length = cold length (kept) |
| `005ad530` | 11 | Poison length = frames left (target X − game frame) when at least 10 and within `FUN_00644f20`; else poison damage and length 0 (also with no owner or target) |
| `005ad5c0` | 13 | Monster targets only: physical += physical × owner stat 0x19 / 100 (C division), then `FUN_005ad2f0` |
| `005ad630` | 14 | Knockback roll (result flag 0x8) on a 100-sided roll of the missile's seed; chance dParam2, tripled / doubled / kept by the target's MonStats2 small (bit 10) and large (bit 11) flags and dParam1: players need dParam1 ≥ 1; 0: small monsters only; 1: small ×2, large never, else ×1; 2 or more: small ×3, large ×1, else ×2. Chance ≤ 0: no roll |

Re-check: `cd tools/emu && D2_MPQ_DIR=<mpq folder> uv run python missmode.py`.
Not covered: the callees' own effects, and which Missiles.txt rows select
each slot.
## Server hit function 5 — FUN_005abc40 (a ring like Frozen Orb's burst)

Cited. Slot 5 of the pSrvHitFunc table (`0x73c854`; the only reference to
it is that table entry, `game-calls.tsv` "data"). ECX game, EDX the missile.
It is hit 29 (`FUN_005abb00`) without the "frames left" test:

- No data block, a row sub-missile id (short +0x24) < 0 or no row, or no
  owner (`FUN_00552fd0`): returns 1.
- Else `ceil(64 / step)` missiles with `FUN_0059fa30` (named
  `d2d_ai_andariel_missile` in the decompile): flags **2** (relative
  target), [1] owner, [2] the missile as start unit, [4] the sub-missile row,
  [7] / [8] = `0x6e2938[k]` / `0x6e2838[k]`, [0xb] / [0xc] the missile's
  skill and level (`FUN_0064a280` / `FUN_0064a210`). `k` steps `sHitPar1 *
  4` bytes (at least 1 entry) while below 0x100, as hit 29 does.
- Each new missile gets the same two table words in its data block at
  `+0x28` / `+0x2c` (`FUN_0064a710` / `FUN_0064a760`).
- Returns **3** (damage, end). It never returns 2.

The tables `0x6e2938` / `0x6e2838` are not the ones hit 29 uses
(`0x6e2738` / `0x6e2638`) nor the spin ones (`0x6e2b78` / `0x6e2a78`); I did
not read their values, so whether they hold the same 64 circle points is
not settled. Which Missiles.txt row uses hit function 5 was not checked.

Re-check: `python3 tools/ghidra/fn.py 005abc40`, and compare with
`python3 tools/ghidra/fn.py 005abb00`.

d2d: not ported. `Fight::burst` has no case 5 (fight/missiles.cpp:799).

## Client missile spawn: FUN_0046b680

Cited. No arguments, no callers recorded in `game-calls.tsv`. Three client
missiles with `FUN_004cd540` from a zeroed 0x5c-byte block (flag word [0]
left 0, [0x14] = 1): block word [4] (the row) **0x11c**, then 0x128, then
0x125. Only the first is tagged: `FUN_0064a710(missile, 0x32)`.

Re-check: `python3 tools/ghidra/fn.py 0046b680`.

d2d: not ported (no mention of rows 0x11c / 0x128 / 0x125).

## Client missile spawn: FUN_0046b9a0

Cited. No arguments; called from `0x4ad37a`. Same shape as `FUN_0046b680`:
three client missiles with `FUN_004cd540` from a zeroed block (word [0x14] =
1), rows **299 (0x12b)**, **0x166**, **0x132**. Only the first is tagged,
`0x4b` in both data slots (`FUN_0064a710` and `FUN_0064a760`).

Re-check: `python3 tools/ghidra/fn.py 0046b9a0`.

d2d: not ported (no mention of rows 299 / 0x166 / 0x132).

## Client unit spawn at a unit's spot: FUN_0046b3c0

Cited. ECX the unit; nothing happens for a null unit. It reads the unit's
two position words: for unit types 2, 4 and 5 from the unit's data block
(`unit[0xb]`, +0xc and +0x10), else (when a data block exists) through
`FUN_006488c0` / `FUN_00648900`. It makes a unit with
`FUN_004cdb40(+0xc word, +0x10 word, 0, 1)` and, when that returns one,
tags it `FUN_0064a710(it, 0x19)`. The tag setter only acts on a missile
(type 3), so the result is a missile (the setter ignores anything else).

Re-check: `python3 tools/ghidra/fn.py 0046b3c0`.

d2d: not ported. No caller is recorded in `game-calls.tsv`.

## Client missile spawn on an object: FUN_0046b530

Cited. ECX a unit (an object: its class is read from `unit[1]`). Only three
classes act: **0x21c** looks for class 0x1db, **0x21d** for 0x1dc and
**0x21e** for 0x1da; any other returns. The lookup is
`FUN_0065a620(FUN_00620bb0(unit), 2, class)` (the unit's room, type 2).
When it finds a unit, a block is zeroed (0x5c bytes) with flags **0x420**,
row (word [4]) **0x21d**, the caller as the two unit words ([1], [2]), the
found unit's two position words as the start spot ([0xb], [0xc]) and word
[0x14] = 1; `FUN_004cd540` makes the missile and `FUN_0064a710(it, found[3])`
tags it with the found unit's word +0xc (its id).

Re-check: `python3 tools/ghidra/fn.py 0046b530`.

d2d: not ported. No caller is recorded in `game-calls.tsv`.

## Client missile spawn near an object: FUN_0046b850

Cited. ECX a unit. It looks for a type-2 unit of class **0x182** in the
unit's room (`FUN_0065a620(FUN_00620bb0(unit), 2, 0x182)`).

- Found: it calls `FUN_004dcd40(0x15e, 0, 0, 0, 0)` (a 0x15e id, not
  identified) and takes the found unit's two position words as the spot.
- Not found: the spot is the caller's own two position words, each less
  **0xf**.

Then a block with flags **0x420**, row (word [4]) **0x170**, the caller as
the unit words, the spot as words [0xb] / [0xc] and word [0x14] = 1 goes to
`FUN_004cd540`. When a unit was found the missile is tagged
`FUN_0064a710(it, found[3])`; with none found it is not tagged.

Re-check: `python3 tools/ghidra/fn.py 0046b850`.

d2d: not ported. No caller is recorded in `game-calls.tsv`.

## Client skill missile, short form: FUN_004e3720

Cited. ECX the caster, EDX the skill id, stack the level; called through
the data reference at `0x727c14`. Returns 0 when the skill id is outside
the skills table (count at tables `+0xba0`, rows of 0x23c at `+0xb98`) or
`FUN_004f21b0()` is not a valid missile row index (table `+0xb64`/`+0xb6c`).
Otherwise it ORs **0x40** into the caster's flags (`+0xc4`), computes
`FUN_00646ca0(caster, skill +0x138, skill, level)` (the skill's calc1
value), makes a missile with `FUN_004c5420(skill, level)`, tags it
`FUN_0064a710(it, calc1 value)` and returns 1 (0 if no missile).

Re-check: `python3 tools/ghidra/fn.py 004e3720`.

d2d: not ported (no mention of the address). Decompile arguments of
`FUN_004f21b0` are hidden registers, so what it returns is not settled.

## Client skill missile, long form: FUN_004e1b10

Cited. ECX the caster, EDX the skill id, stack the level; called through
the data reference at `0x727c8c`. Returns 0 when the skill id is out of
the skills table or the skill's missile row (short +0xea) is outside the
missiles table. Otherwise it ORs **0x40** into the caster's flags
(`+0xc4`) and fills a block: word [0] **3**, [1] / [2] the caster,
[4] the row, [5] / [6] the caster's position (`FUN_0045adf0` /
`FUN_0045ae20`), [0xe] the level, [0xf] the skill id, [0xd]
`FUN_00646ca0(caster, skill +0x138, skill, level)`. Words [7] / [8] are the
offset from the caster to a target spot: from `FUN_00648a00` /
`FUN_00648a10` of the caster's data (`+0x2c`) less its position, or, when
`FUN_004648f0()` is non-zero, from the position after `FUN_00649ef0`
adjusted it. `FUN_004cd540` makes the missile, tagged with the level
(`FUN_0064a710(it, level)`); returns 1.

Re-check: `python3 tools/ghidra/fn.py 004e1b10`.

d2d: not ported (no mention of the address). The meaning of
`FUN_004648f0` is not settled.

## Missile data setters — FUN_0064a710, FUN_0064a760

Cited. Both take `(unit, value)` on the stack and do the same thing: when
the unit is non-null, its type (`*unit`) is **3** (a missile) and its data
block (`unit[5]`, +0x14) is non-null, they store the value at data `+0x28`
(`FUN_0064a710`) or `+0x2c` (`FUN_0064a760`); otherwise nothing. 32 bytes
each, no return value. `FUN_0064a710` is the setter of the +0x28 slot
`FUN_0064a730` reads (hit function 28); `FUN_0064a760` writes +0x2c, which
that getter does not read.

Callers tag a missile they just made (the result of `FUN_004cd540` or
`FUN_0059fa30`): `FUN_0046b3c0` stores 0x19 at +0x28, `FUN_0046b680` 0x32,
`FUN_0046b9a0` 0x4b in both slots; `FUN_0046b530`, `FUN_0046b850`,
`FUN_004e1b10` and `FUN_004e3720` store a value they were given (a unit id
or a skill-derived value). Hits 5 and 29 store the ring vector in both. So
+0x28 / +0x2c are two general per-missile words; the meaning depends on
the missile.

Re-check: `python3 tools/ghidra/fn.py 0064a710`,
`python3 tools/ghidra/fn.py 0064a760`,
`python3 tools/ghidra/calls.py 0046b9a0`.

d2d: not ported. d2d keeps no missile data block; the only mention of the
slots is the "not modelled" note under hit 29. Not a disagreement.

## The seed draw — FUN_0045c3e0 (ECX seed, EDX n)

Cited. Read from the decompile: the seed is two dwords {low, high}. With n
under 1 it returns 0 and leaves the seed alone. Otherwise one step,
`(low * 0x6ac690c5 + high)` as 64 bits stored back whole (new low = the low
half, new high = the carry word), and the result is `new low & (n - 1)` when
n is a power of two, else `new low % n` (unsigned). n = 1 therefore still
steps the seed. Five direct callers (`0045e6d0`, `0046c460`, `00470390`,
`004ae8d0`, `004aedd0`) and the roll helper FUN_005a8910 below; the missile
roll draws on the missile's own seed (+0x20).

Re-check: `python3 tools/ghidra/fn.py 0045c3e0`,
`python3 tools/ghidra/calls.py 0045c3e0`.

d2d: reads the same, cited from the decompile, no oracle run. `Rng::operator()` (components/rules/rules.hpp:164) has the
same step, the same power-of-two mask, `%` otherwise, and 0 without a step
for n < 1.

## The roll helper — FUN_005a8910 (stack: a stat id, a bonus stat id)

Cited from the decompile, no oracle run. The decompile drops ECX / EDX, so
which operand each call got is a reading. Three direct callers: FUN_005a89a0,
FUN_005a8c70 and FUN_005a9fb0. Shape:

- Two stat reads on the missile (`FUN_00625480`, layer 0): the first from a
  register operand (the other end of the pair), the second from the stack stat
  id. Either one under 1 returns 0.
- The lower of the two is the base (the ends are swapped when the first is
  lower), as the "min > max" note under "The roll" says.
- A third stack argument other than -1 names a bonus stat (FUN_005a8c70 passes
  0x149 / 0x14a / 0x165 / 0x14b / 0x14c, the fire, lightning, magic, cold and
  poison masteries, skills.md line 344 and 694 for 329 / 330 / 331): when the missile has that stat
  non-zero, `FUN_00483360(100)` (a × b / 100 on hidden operands) is added to
  the base. A second `FUN_00483360(100)` call right after has its result thrown
  away.
- Then `FUN_0045c3e0` (the seed draw, on the missile's seed) is added to the
  base and returned. The draw's range (the hidden EDX) is not visible here; "The
  roll" above reads it as max − min.

So the roll has a mastery term that "The roll" does not mention. `rules::missile_blow`
takes its min / max (`elo`, `ehi`) from the skill's calc and does not read a
mastery stat of the missile at this point.

Re-check: `python3 tools/ghidra/fn.py 005a8910`,
`python3 tools/ghidra/calls.py 005a8910`.

d2d: **not ported** for the bonus term (a gap, not shown to disagree: whether a
missile unit carries the mastery stats, or the skill calc already adds them, is
not settled here). The min / max / zero rules read the same as `missile_blow`
(components/rules/combat.hpp:694), cited from the decompile, no oracle run.

## Monster class test: demon — FUN_0063e940

Cited. One unit pointer. False unless the unit is non-null, its type (`*unit`) is
**1** (monster), its class id (`unit[1]`) is in `0 <= id < *(game + 0xa80)` (upper end exclusive) and the
MonStats record (`class * 0x1a8 + *(game + 0xa78)`) is non-null. Then
`(record[0xd] & 0x20) != 0`. A player (type 0) is never a demon here.
Callers (the work list's, from `game-calls.tsv`): FUN_00454ad0, FUN_004cf3c0,
FUN_0057b630, FUN_0057b7d0, FUN_0057c1e0; FUN_005a8c70 also calls it (its
decompile, below).

Re-check: `python3 tools/ghidra/fn.py 0063e940`,
`python3 tools/ghidra/calls.py 0063e940`.

d2d: reads the same, cited from the decompile, no oracle run. `Target::demon` (components/rules/combat.hpp:531, "MonStats
demon") is the same bit; components/game/fight/missiles.cpp:626 uses it for the
dParam2 bonus.

## Monster class test: undead — FUN_0063e990

Cited. The same gate as FUN_0063e940 (non-null unit, type 1, class in range,
MonStats record non-null), then true when `record[0xd] & 8` (lUndead) or
`record[0xd] & 0x10` (hUndead) is set, else false (returns 0 / 1).
Callers (the work list's): FUN_00454ad0, FUN_004c5960, FUN_004cf3c0,
FUN_0056b3e0, FUN_0057b660; FUN_005a8c70 also calls it.

Re-check: `python3 tools/ghidra/fn.py 0063e990`,
`python3 tools/ghidra/calls.py 0063e990`.

d2d: reads the same, cited from the decompile, no oracle run. `Target::undead` (components/rules/combat.hpp:530) and
`kUndead` (components/rules/aura_filter.hpp:17) are the same two bits; the
dParam1 bonus (components/game/fight/missiles.cpp:626) and the AI check at
components/game/ai.cpp:1041 use it.

## The record fill — FUN_005a8c70 (ECX game, EDX the missile, stack unit, record)

Cited from the decompile, no oracle run; the decompile drops ECX / EDX, so the
operand roles below are a reading. It fills one 0x70-byte damage record
(`param_4`) from the missile's stats and returns the number its caller adds
(0 for a null missile). The switch is the byte FUN_0064b0c0 gives for the
missile's class (the "element" in "Server damage functions 1, 2, 4 and 12"):

| Case | Record field(s) | From |
|---|---|---|
| 0 physical | +0x08 | FUN_005a8910 with stat 0x15, no bonus stat (-1); see below |
| 1 fire | +0x10 | FUN_005a8910 with 0x30, bonus 0x149 |
| 2 lightning | +0x1c | 0x32, bonus 0x14a |
| 3 magic | +0x20 | 0x34, bonus 0x165 |
| 4 cold | +0x24, +0x30 | 0x36, bonus 0x14b; +0x30 = stat 0x38 (length) |
| 5 poison | +0x28, +0x2c | 0x39, bonus 0x14c; +0x2c = stat 0x3b; divided by stat 0x146 when that is over 1; returns +0x28 × 0x19 |
| 6 / 7 / 8 / 9 | +0x38 / +0x3c / +0x40 / +0x44 | stat 0x3c / 0x3e / 0x40 / 0x42, plain; 7, 8, 9 return 0 |
| 0xb | +0x14, +0x18 | 0x13c, bonus 0x149; +0x18 = stat 0x13b; returns +0x14 × 0x19 |
| 0xc | +0x24, +0x34 | as 4 but the length goes to +0x34 |

Case 0, with a unit given (`param_3`): the roll is scaled by
`pct = stat 0x19 (+ stat 0x79 if the unit is a demon, FUN_0063e940) (+ stat 0x7a
if undead, FUN_0063e990)`, floored at −90 (`< −0x59` becomes −0x5a), as
`roll += roll × pct / 100` (C division). Then, whatever the unit, a non-zero
stat 0x8d sets bit 0x2000 of the record's word at +4 and doubles +0x08. Both
the scale and the doubling come from stats of the missile itself (the first
argument of FUN_00625480 is the missile).

Callers: FUN_005a9a70, FUN_005aa250, FUN_005aa8b0, FUN_005abd70 and
FUN_005ad0f0 (once per burst or hit), plus the Blessed Hammer slot.

Re-check: `python3 tools/ghidra/fn.py 005a8c70`,
`python3 tools/ghidra/calls.py 005a8c70`.

d2d: **not ported** for the physical case. `rules::missile_blow`
(components/rules/combat.hpp:677) rolls the physical from `phys_lo` / `phys_hi`
and applies neither the 0x19 / 0x79 / 0x7a percent nor the 0x8d doubling; the
undead / demon bonus d2d has is the Blessed Hammer slot's (FUN_005ad2f0). Whether a
missile unit carries those stats (they would have to be copied from the
owner when it is made) is not shown here, so this is a gap to check, not
a proven disagreement. The element rows read the same as `missile_blow`'s
min / max reading (cited from the decompile, no oracle run), apart from the bonus stat discussed under FUN_005a8910.

## Hit function FUN_005abd70 (the owner's life by a missile's roll)

Cited from the decompile, no oracle run; the decompile drops ECX / EDX
(game, the missile) so the operand roles are a reading. **No caller and no table
entry** was found for it in `game-calls.tsv` (the pSrvHitFunc table at 0x73c840
has no entry for 0x73c8b8 .. 0x73c8c8 in that file), so which Missiles.txt row
uses it was not checked.

- Gate (returns **1**): no missile, class out of range or no Missiles.txt
  record (`PTR_DAT_00744304 + 0xb64`, 0x1a4 per row, count at +0xb6c), no owner
  (`FUN_00552fd0`), or the stack unit is 0.
- Else a 0x70-byte zeroed record is filled by FUN_005a8c70 for the missile (the
  unit goes with it), and the returned number (0 when negative) is added to
  the owner's life, stat 6 (`FUN_00625480`), limited to the owner's maximum
  life (`FUN_00625d10`, named `d2d_unit_max_life`), and stored with
  `FUN_00627260(owner, 6, life, 0)`. The number is in the record's units
  (256ths for the elements), the same units as the life stat.
- Returns **3** (damage, end) when the row's CollideKill (+0x186) is set, else
  **2** (damage, the missile flies on).

So it heals the missile's owner by what the missile just rolled against the
struck unit, and does no damage itself.

Re-check: `python3 tools/ghidra/fn.py 005abd70`,
`python3 tools/ghidra/calls.py 005abd70`.

d2d: **not ported**; no d2d code names it (`git grep -n 005abd70` finds only this
note).

## Other `UNIT/Missile.cpp` routines: creation and callback helpers (static trace)

The following additional routines were untouched in the gap map. These notes
are direct static readings of the stock 1.14d decompilation; they do not
establish which Missiles.txt row selects a callback, nor do they claim a
runtime match. The commands below recheck every listed function in its table.
`FUN_004cd540` below is the client missile/unit
constructor; record flags and fields are only assigned meaning where their
uses in these bodies show it.

### Shared creation and data helpers

Recheck:
`python3 tools/ghidra/fn.py 004cc800 004cc870 004cca00 004ccb50 004ccbe0 004ccd20 004cce40 004ccf40 004cd170 004cdd10 004ce050 004ce320 004ce420 004ce4b0 004ced20 004cefe0`.

| Address | Observed operation |
| --- | --- |
| `FUN_004cc800` | Rejects a null room and a radius smaller than the squared x/y offset, then tests the candidate rectangle against collision using `FUN_0064e260`. |
| `FUN_004cc870` | Initializes a spawned missile's vertical position from a seed roll and row field `+0x5c`, copies the source missile's animation, sets its animation mode to 0, and chooses a random starting frame. |
| `FUN_004cca00` | Computes missile range from row shorts `+0x96/+0x98` and the missile's frame; redirects its path either toward a supplied target or to a target point encoded in missile data, with a 25-cell route limit. |
| `FUN_004ccb50` | A conditional callback: returns early for invalid row/state, otherwise uses the missile's target to derive a point, runs `FUN_004c5eb0`, and passes that result to `004cca00`; return is 0 only after this action. |
| `FUN_004ccbe0` | Returns a Boolean eligibility result from the owner, room activity, missile flags, path/target state, and an optional caller-supplied target id. Several early exits return true, so this is not a simple “valid target” predicate. |
| `FUN_004ccd20` | Creates a record of type 3 for row short `+0x24`, current owner and missile coordinates, sets callback `LAB_004cccc0`, then invokes `FUN_004c71f0`; when the computed quantity is below 1 it obtains an additional value from the skill row. |
| `FUN_004cce40` | Creates a type-3 record from row short `+0x2c` and skill data, optionally selecting callback `FUN_004e2ed0` when row field `+0x70` is nonzero, then calls `FUN_004c71f0`. |
| `FUN_004ccf40` | Filters a candidate unit using unit flag bits 2 and 3, an optional state-`0x56` exclusion, the missile's range predicate `FUN_0064a470`, and a final state/owner check. |
| `FUN_004cd170` | Releases the missile stream, missile data, motion and graphics records; conditionally detaches additional state, releases the path, and clears the path pointer. |
| `FUN_004cdd10` | General missile constructor wrapper. It rejects null source units and missing parameters, normalizes the source unit for types other than 0/1, fills a `0x5c`-byte creation record (including source, missile id, coordinates, skill, and flags), then calls `FUN_004cd540`. |
| `FUN_004ce050` | On a path with flag 8, builds a type-5 creation record at the path endpoint, carrying the missile id, target id and owner; positive row parameters set record flag `0x8000` / flag 8. |
| `FUN_004ce320` | On a positive frame period, spawns only when elapsed frames are divisible by that period; chooses two seeded offsets inside the row radius, requires a collision-free room position, and creates the row's missile there. |
| `FUN_004ce420` | Sets a vertical launch profile from row fields `+0x58/+0x5c`: initializes z/velocity, sets duration to the larger value divided by the smaller, and applies that duration as both motion and missile range. Invalid missile data removes the unit. |
| `FUN_004ce4b0` | Variant vertical launch profile using the row's stored height and positive speed; unlike `004ce420`, it does nothing when speed is nonpositive. |
| `FUN_004ced20` | Creates a child missile at the parent's position and skill, then copies the parent's animation and palette/color data to the child. |
| `FUN_004cefe0` | Repeatedly creates missiles from two static offset arrays around the parent position. The first loop steps the 16 offsets by row parameter `param_4`; the optional second loop uses another 15 offsets and a separate step. Row fields `+0x38/+0x3c` scale the two sets. |

### Skill and pattern spawn callbacks

Recheck:
`python3 tools/ghidra/fn.py 004cf9b0 004d0380 004d06f0 004d07e0 004d0a80 004d0b90 004d0e00 004d0eb0 004d1040 004d1110 004d1240 004d1300 004d14a0 004d16c0 004d1890 004d19d0 004d1d70 004d1ec0`.

| Address | Observed operation |
| --- | --- |
| `FUN_004cf9b0` | Builds a type-`0x4001` creation record from the owner, skill, missile id, and current path position. It emits each configured row group at successive offsets from two static delta tables, then emits optional row `+0x2e` and `+0x30/+0x32` groups. |
| `FUN_004d0380` | Creates the row's primary missile at the owner's position; quantity comes from the row or skill calc, with a floor of 5 when the row quantity is zero. It copies the owner's animation and palette to the created unit. |
| `FUN_004d06f0` | Creates the row's primary missile and optional `FUN_004cef50` group, then calls `FUN_00470390` with event code 2 once or twice depending on its input flag and row short `+0x36`. |
| `FUN_004d07e0` | Uses the missile seed for a bounded sequence of random offsets around its current path position and creates a type-`0x102` record for each. The count is the missile's current data value; each child receives a seeded data value. |
| `FUN_004d0a80` | Computes a skill-scaled quantity from row fields / skill calc, calls `FUN_004c7700` with that quantity and its quarter, then optionally creates the row's secondary missile at a seeded offset. |
| `FUN_004d0b90` | Generates a random set of candidate positions within a square around the missile, rejects positions whose collision query is at most 3, creates a type-`0x524` record at accepted points, and then creates children from a fixed 18-offset array when the second count is nonnegative. |
| `FUN_004d0e00` | Optionally requires a target of unit type 1 and a successful helper check. If row short `+0x2c` is enabled, creates a missile toward the current owner/skill point; then delegates its row quantities and missile ids to `004d0b90`. |
| `FUN_004d0eb0` | Builds a type-`0x21` record from row missile id, owner, current path location and target, plus path velocity/target values. After construction it writes the target and throw parameters to the new missile motion. |
| `FUN_004d1040` | If the owner exists, creates one record for each enabled row short at `+0x2c`, `+0x2e`, and `+0x30`, using the row id and current owner/skill. |
| `FUN_004d1110` | Creates the primary row missile at the current unit's position and copies the owner's animation state to it. |
| `FUN_004d1240` | Creates the primary row missile and, when the secondary row field is enabled, creates a second missile and sets its vertical position from row `+0x6c`. |
| `FUN_004d1300` | Uses two seed draws to choose signed x/y offsets within `[-param_1, param_1-1]`, then creates a type-`0x21` missile record at that point with the supplied missile id. |
| `FUN_004d14a0` | Returns 0 for nonzero second argument. Otherwise queries `FUN_0064a3e0`, selects a local marker (`0x158` or `0x159` for two switch cases; default `-1`), obtains path target coordinates for path types 2 and 4–5 or current path coordinates otherwise, then calls `FUN_004639d0` and `FUN_004cdb40` with those coordinates, owner and skill. For a created missile it copies the source animation and sets flag `0x80`. The switch's semantic meaning is not established here. |
| `FUN_004d16c0` | Finds a related client unit, computes the source-to-related-unit x/y delta, creates a missile from the source position, and sets the new missile's initial position, velocity, acceleration and two stored deltas. |
| `FUN_004d1890` | Creates a type-`0x8005` missile with id `0x178` and a row-derived height; on success stores a row quantity and a seeded integer from 10 through 44 on the new missile (LCG result modulo 35, then +10). |
| `FUN_004d19d0` | Runs fourteen creation attempts (`local_c` 0 through `0x34` by four) for type-`0x8102` records, assigns the passed missile id and stores a new seeded value (10–44, LCG result modulo 35 then +10) on each successful child. |
| `FUN_004d1d70` | Only runs for mode 0 and when a related unit is found; creates type-`0xa021` using source and related-unit path coordinates, their missile id, skill and frame data, then stores the related unit's field `+0xc` on the child. |
| `FUN_004d1ec0` | Advances the source seed and creates a child explosion only when the low seed bit is clear; copies animation and palette and sets child animation mode 0. |

### Timed effects and trajectory callbacks

Recheck:
`python3 tools/ghidra/fn.py 004d1fa0 004d2190 004d2400 004d2520 004d2610 004d2990 004d2af0 004d3070 004d3630 004d37f0 004d38d0 004d3c40 004d3d30 004d3e00 004d3f10 004d4040 004d4180 004d43a0 004d44b0 004d4590 004d46d0 004d47f0 004d49f0`.

| Address | Observed operation |
| --- | --- |
| `FUN_004d1fa0` | Resolves a target coordinate, creates a record at a seed-selected offset of roughly 10 cells around the current path point, then validates that the row at `+0x96` equals `0x19`; it sets the new missile's position to height 400, velocity from the target vector, and applies `FUN_004da350`. Mismatch uses an assert at source line `0x1861`. |
| `FUN_004d2190` | Creates a type-3 missile at one of four adjacent cells, choosing its row id from `0x25f`–`0x263` (the default is `0x25f`) and setting a random height modulo `0x7d`; it writes the source path coordinates into the created missile's data fields. |
| `FUN_004d2400` | Creates missile id `0x11d` at two seeded offsets in `[-19,20]`; if both offsets are zero, another seed draw replaces the x offset with a value from 1 through 19. It stores the offsets and sets z to `-10`. |
| `FUN_004d2520` | Checks a row-appropriate collision at the requested location, then creates missile id `0x27b`–`0x27d` with a random selector; initializes z 250, downward velocity 10, duration/range 25, and flag `0x80`. |
| `FUN_004d2610` | Runs its unit-follow and frame-advance path only when the related unit is in a room whose level id is `0x83` or `0x84`. If owner data is zero or a seed's low six bits are zero, it calls `FUN_00476a80(3000,2000)` and sets missile data to 1. On another seed interval it calls `004d2520` at a seeded offset. Otherwise it removes the missile. |
| `FUN_004d2990` | Tries up to four seeded candidate offsets around a supplied point, accepting a clear location according to `FUN_0064cb30`; then creates two missiles (ids `0x267` and `0x268`) at the chosen coordinates. |
| `FUN_004d2af0` | Uses missile row data to choose a seeded point within the configured radius around the current position and emits one type-`0x20` record with the row's missile id, owner and skill. |
| `FUN_004d3070` | Removes the missile if its owner is absent; for owner types below 2, an auxiliary predicate can dispatch the generic no-hit handler. Otherwise it applies the unit/missile update and advances the frame. |
| `FUN_004d3630` | For a valid row and path flag 8, builds a type-`0x21` missile record using row `+0x1e`, target and skill. Optional row fields select the spawned unit id and flags; the new unit inherits precise x/y. Every path then calls the client missile step. |
| `FUN_004d37f0` | If the room is active and the missile's row flags permit, periodically resolves a nearby candidate with `FUN_006416d0`, applies a movement route when the candidate passes range/state gates, and advances the ordinary missile step. Invalid room/row removes the missile. |
| `FUN_004d38d0` | When the row timer byte at `+0x135` has elapsed and the missile has path flag 8, creates a type-1 missile toward the current unit/target coordinates, then runs the ordinary missile step. |
| `FUN_004d3c40` | Selects a row id, optionally adding a seed-bounded value between row shorts `+0x1e` and `+0x20`; derives a spawn period from row speed/lifetime, emits the periodic child through `004ce320`, applies `004ce420` when successful, then advances the missile step. |
| `FUN_004d3d30` | Reads row speed/lifetime. At frame 0 it initializes a vertical motion profile with `FUN_00476a80`; each tick advances the ordinary missile step. Invalid row values remove it. |
| `FUN_004d3e00` | For each positive row threshold at `+0x1e`, `+0x20`, or `+0x22`, checks the corresponding frame at `+0x58`, `+0x5c`, or `+0x60`. At a matching frame it obtains the current path coordinates and calls `FUN_004639d0` and `FUN_004cdb40` with those coordinates, owner and skill; then it advances the ordinary missile step. The created record's semantic id is not identified here. |
| `FUN_004d3f10` | Computes a seeded row id and skill-calc quantities; when both calculations are positive, creates the periodic child via `004ce320`, applies vertical motion via `004ce420`, and advances. Otherwise it removes the missile. |
| `FUN_004d4040` | Periodically checks the target and range, moves the missile path one signed row `+0x68` step toward the target on each axis, and asks the movement system to recalculate the route before the ordinary missile step. |
| `FUN_004d4180` | Continues a moving missile while its row-derived lifetime condition holds. It steers toward a valid target on a configured interval; can also spawn a type-5 child on another interval, then performs the standard step. |
| `FUN_004d43a0` | On an active path, emits a type-`0x21` record for row id `+0x1e`; a seed check selects whether its flags include `0x4000`. Then advances the ordinary missile step. |
| `FUN_004d44b0` | Chooses a missile id from the row or its missile data, checks remaining row lifetime and a positive count, and every third frame spawns the configured `FUN_004cef50` group. Quantity and step depend on whether count exceeds 4. |
| `FUN_004d4590` | After the row timer byte elapses and path flag 8 is set, creates a type-1 missile at the owner's current position, copies path, precise x/y and z, and sets row count as child data; then executes the standard step. |
| `FUN_004d46d0` | At row-period intervals creates a type-2 missile at the current position, selects its x/y offsets from static tables using masked missile data, writes a new masked value back to the missile, then advances the standard step. |
| `FUN_004d47f0` | On its timer and period gates, steers the path toward an intermediate point between missile height/data and stored target, then writes both derived values back and advances the standard step. |
| `FUN_004d49f0` | Creates the row's periodic path callback through `FUN_004cc870` and preserves the missile's z. On its row timer it uses a seed bit to sign-mix missile data and target-y into a new vector, recalculates the path route when the vector is nonzero, and stores the resulting components. |

### Collision, burst and path maintenance callbacks

Recheck:
`python3 tools/ghidra/fn.py 004d4bb0 004d4dc0 004d5090 004d5200 004d5470 004d56b0 004d5710 004d5880 004d5950 004d59e0 004d5ba0 004d5c10 004d5dd0 004d5f80 004d6080 004d6130 004d62b0 004d63e0`.

| Address | Observed operation |
| --- | --- |
| `FUN_004d4bb0` | Uses row frame thresholds to set vertical motion and scans the room's unit list for units with type 2 and class id 17–21. Depending on configured row fields it either creates a child missile at the related unit or stores that unit's id; always resumes the standard step. |
| `FUN_004d4dc0` | On a row-configured frame cadence, draws four independent seed offsets within the row's x/y ranges and creates a type-`0x21` missile from that randomized rectangle. |
| `FUN_004d5090` | Emits a row missile at the current target point when path flag 8 is set, then on its interval redirects the path toward a seeded blend of current height and target data; writes the resulting x/y offsets to missile fields. |
| `FUN_004d5200` | At its configured frame emits four type-`0x2001` children at offsets from the current position using two four-entry static arrays; then advances the ordinary missile step. |
| `FUN_004d5470` | Stores 1 in missile data at the configured frame. If its target-data field is nonzero, periodically emits a type-`0x21` missile at a seeded offset within row range. |
| `FUN_004d56b0` | Calls `FUN_004ce140` with row values at `+0x5c/+0x60` and missile id `+0x1e`, then performs the standard step. |
| `FUN_004d5710` | Requires a live missile and path; during the first two frames stores the difference between the path target and current path coordinates in missile data. When path flag 8 is set it emits two type-`0xb` records with opposite signed x/y offsets, then advances the ordinary missile step. |
| `FUN_004d5880` | When row short `+0x1e` is nonzero and path flag 8 is set, creates a type-9 record at the current position, passing the row missile id and row `+0x58` value. |
| `FUN_004d5950` | Copies motion target-x into missile data when the row's `+0x34` is positive; if current data differs, triggers sound `FUN_004b9a00`, then routes through `004d3630`. |
| `FUN_004d59e0` | At a skill-scaled interval, chooses a seeded collision-valid point near the owner, creates the row's primary missile there, and calls `d2d_unit_set_dead_flag` if creation succeeds; optionally creates a second row missile. The decompiler does not expose which object that no-argument helper mutates. |
| `FUN_004d5ba0` | Calls `FUN_004ced20` on row-period intervals and then performs the ordinary missile step. |
| `FUN_004d5c10` | Uses two skill calculations to choose count and period; between configured frame limits it makes a seeded child-spawn call to `FUN_004cdc30`, passing the owner, skill and row id, then advances the ordinary step. |
| `FUN_004d5dd0` | At configured frames creates a type-`0x2001` row missile, then creates the row count of seeded nearby children. At a second configured frame it sets owner flag `0x40000`, creates another missile, and may play a sound. |
| `FUN_004d5f80` | If path flag 8 is set, creates two type-2 records from missile data and the negated x/y vector, then advances the missile step. |
| `FUN_004d6080` | Creates a row missile when path flag 8 is set, then calls `FUN_004d37f0` to perform target movement / ordinary missile update. |
| `FUN_004d6130` | Counts enabled missile ids from three adjacent row fields, selects one with the missile seed, and at/after row threshold `+0x58` creates that child on a cadence stored beside its id; with no enabled ids it calls the generic end handler. |
| `FUN_004d62b0` | Advances the client update, derives the missile's target vector, caps the stored target-y value to missile height, and creates a type-`0x20` child with a movement route. It then clears the source target-y field. |
| `FUN_004d63e0` | At the positive row period, unpacks the target vector, advances the seed, and uses its low bit to select one of two signed component mixes. It routes to the resulting roughly quarter-scaled vector (replacing zero components with 1), stores a new seed value and packed vector, then advances the ordinary step. |

### Remaining frame callbacks

Recheck:
`python3 tools/ghidra/fn.py 004d6590 004d65e0 004d6660 004d6680 004d66e0 004d6820 004d6c00 004d72c0 004d7400 004d7710 004d7880 004d7930 004d7fc0 004d8000 004d8400`.

| Address | Observed operation |
| --- | --- |
| `FUN_004d6590` | At frame 8 sets vertical acceleration to `0x100` and animation frame to half the missile's signed short at `+0x4c`; at frame `0x23` sets animation frame to twice that short. Both paths run the generic client missile step. |
| `FUN_004d65e0` | When missile target-data is zero, finds the owner, invokes `004d16c0` to spawn the related missile, draws a new seed value and stores a derived value minus one; then runs the standard step. |
| `FUN_004d6660` | If an owner unit resolves, resets the missile's frame field at `+0x44` to 0, then advances the standard missile step. |
| `FUN_004d6680` | Requires a related unit; at frame 10 uses its data field to call `004d19d0` and initializes a vertical effect with `FUN_00476a80`; always advances the standard step. |
| `FUN_004d66e0` | Calls the seeded offset helper `004d1300` with radius 10 and id `0x17c`; during the row's configured frame window creates a type-`0x20` missile id `0x17e` at a seeded offset, sets its vertical acceleration, and applies throw settings. |
| `FUN_004d6820` | Walks a static frame-to-offset table and creates id `0x154` records when the missile frame matches one of its entries. Additional frame ranges emit repeated records; frame `0x96` calls `FUN_00476a80(4000,0)`. |
| `FUN_004d6c00` | Chooses an entry in a static frame-period table, then on that period emits an id-`0x15c` child at the owner's path point and copies two selected row values into its data fields. |
| `FUN_004d72c0` | Every fourth frame creates record id `0x162` at independently seeded offsets around the current path position, then advances the ordinary step. |
| `FUN_004d7400` | Uses a frame-indexed table to emit two kinds of children: id `0x2a` at a seeded offset on one period, and id `0x16c` on another period with height, speed, range and flag `0x80` initialized. |
| `FUN_004d7710` | Spawns two `004d2190` effects each frame, periodically calls `004d2400`, and at frame `0x19` creates two effects at `+2,+2` from the current path coordinate; then advances the ordinary step. |
| `FUN_004d7880` | Computes a path endpoint from the current path coordinate, missile height and target-y data, asks the pathing helper to redirect to that endpoint, and advances the standard step. |
| `FUN_004d7930` | Selects a frame-period row from a static table. On its cadence it creates id `0x272` at seeded offsets and stores its parent id; it also sets missile data based on whether frame is below `0xd2`, emits effects at selected frames `0x145`, `0x177`, `0x1a9`, or `0x1c2`, and may find a room unit of class `0x209` for an additional `004d2520` spawn. |
| `FUN_004d7fc0` | Keeps the missile only while its z lies between negative missile height and target-y; otherwise calls the missile remove helper. |
| `FUN_004d8000` | During the first `0x6e` frames emits type-1 id-`0x255` children at the current point plus `(2,2)`, with count 8 before frame `0x32`, 5 through frame `0x4f`, and 1 thereafter. At frame 5 it calls `FUN_00476a80(4000,3000)`; every seventh frame calls `004d2990` for two additional effects. |
| `FUN_004d8400` | On every other frame, computes a point by combining path coordinates, missile height and target-y, redirects the route to it, then advances the standard step. |
