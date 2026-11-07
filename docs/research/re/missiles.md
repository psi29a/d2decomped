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
odd ones at a stride of sHitPar1 entries (at least 1) at Param2 << 7 and the × 75 / 100. Two
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
case); FUN_0056d810 itself is ported (fight/missiles.cpp:531).

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

d2d: **not ported** (components/game/fight.hpp:1043 `ponytail:` says so); no `dmg_func == 3`
case exists (`git grep -n "dmg_func" components`).

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
unit. Stat 0x148 is a per-missile counter: the client packet handler
writes it from a packet byte, and the client hit adds 1; who seeds it on
the server is not traced.

Re-check: `python3 tools/ghidra/fn.py 005ada80`, `python3 tools/ghidra/fn.py 005adf10`.

d2d: **DISAGREES.** fight/missiles.cpp:701 treats stat 328 as a percent:
`pierce && rng(100) < psum[kPierceIndex]` on the shooter's summed
passives, drawing one rng per hit. game.exe draws none and counts down a
stat on the missile, gated on the owner's stats 0xa6 / skill 0x9c.

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
not skip the unit that the missile struck.

Re-check: `python3 tools/ghidra/fn.py 005ab2a0`,
`python3 tools/ghidra/fn.py 0059fa30` (param [2] is the start unit when
flag 1 is clear: `FUN_0045adf0` / `FUN_0045ae20` of it).

d2d: not ported on its own; `Fight::burst` case 20 (fight/missiles.cpp)
inlines it (see the next section for what differs).

## Server hit function 20 — FUN_005ab370 (Lightning Fury)

Cited. Slot 20 of the pSrvHitFunc table 0x73c840 (`0x73c890`, the only
reference to it that I found). ECX game, EDX the missile unit.

- The missile's row (Missiles.txt, 0x1a4 each) must have a sub-missile id
  (short at +0x24) >= 0, the skill (`FUN_0064a280` id, `FUN_0064a210`
  level, from the missile's data block) must be in the skills table, and
  the owner (`FUN_00552fd0`) must exist. Otherwise it returns 1.
- Radius: the missile row's sHitPar1 (+0x4c) when >= 1, else
  `FUN_00646ca0(owner, skill +0x64 (aurarange), id, level)`; at least 1 only
  on the calc path (`iVar3 < 1 -> 1`).
- Count: sHitPar2 (+0x50) when >= 1, else calc1 (skill +0x138); at least 1.
- Filter: the skill's +0x50 (aurafilter), 0xa783 when that is 0.
- `FUN_0056b7e0(x, y of the missile, radius, filter, FUN_005ab2a0, &ctx, 1,
  ".\MISSILES\MissMode.cpp", 0xec3)` walks the units in range and makes one
  sub-missile each (FUN_005ab2a0) until the count is served. Returns **3**
  (damage, end).
- The walker skips only the missile unit itself (`piVar5 != local_c`); the
  callback has no test for the unit the missile struck. That is only how
  the code reads, cited from the decompile, no oracle run.

Re-check: `python3 tools/ghidra/fn.py 005ab370`, `python3 tools/ghidra/fn.py 0056b7e0`.

d2d: **DISAGREES** on one point. `Fight::burst` case 20 skips
`missile.target` (`int(j) != missile.target`), the monster the bolt struck;
game.exe's walk and callback have no such exclusion, so the struck unit gets
a sub-missile too when it passes the filter (a difference in d2d; nothing
shows that game.exe meant otherwise, so this is not recorded as a bug). Radius and count (sHitPar1 /
sHitPar2 else aurarange / calc1, count at least 1) read the same, cited from
the decompile, no oracle run. d2d ignores the
skill's aurafilter. Confidence: medium (read from the code, no oracle;
the walker's counter at state +8 is not shown incrementing in the decompile).

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

Returns **0** when the filter passed, else 1.

Re-check: `python3 tools/ghidra/fn.py 005ab500`.

d2d: reads the same in part, cited from the decompile, no oracle run. `Fight::strike` hit_func 21 (fight/missiles.cpp:629)
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

d2d: **DISAGREES.** `Fight::burst` case 26 (fight/missiles.cpp:788) shares
the 2 / 4 / 36 / 51 arm: `sub_at(x, y, 0, 0)` with the sub row's own
`range + lev_range * level` frames. game.exe overrides the range with
sHitPar1, else calc1 (at least 5). Confidence: medium; the d2d side is read
from the code, with no oracle run.
