# The merc's AI (MonAI 61 "Hireable")

Every hireling row in MonStats (roguehire 0x10f, act2hire 0x152, act3hire
0x167, act5hire1 / 2 0x230 / 0x231) has AI `Hireable`, MonAI row 61: AI
table `0x73ca20 + 61*16` = `{think 0x5e52d0, init 0x5e5280, 1, 0}`. The init
clears state 0xc, sets the AI mode and stands 1 frame.

Port: `components/rules/merc.hpp` (`hireable_think`), run by
`Fight::merc_turn`. Checked: `tools/emu/merc.py` runs FUN_005e52d0 natively
on random setups against the port (30,000 cases: every move tried, stand,
teleport, attack hand-off and the merc's seed after, all equal);
`tests/test_monsters.cpp` replays 16 of them.

## The think (FUN_005e52d0)

Calls: `FUN_005e52d0(ECX game, EDX merc, [8] AI params)`. The owner is
`FUN_0058f0d0` (monster data +0x28 → AI control, +0x2c the owner's id); none,
or not a player: it stands 10.

1. In a walk or run (mode 2 / 0xf) it does nothing.
2. Its distance to the owner, `FUN_005dc380`: per axis the gap less the
   merc's size (`FUN_00620510`, not under 0), then (short + 2 × long) / 2.
   The thresholds are 16 and 24 (and a wander of 5), unless the AI control's
   +0x14 is 0x11..0x13, when they're v and 2v (and v / 2). +0x14 is also the
   attack think's skill chance (0, +10 per miss), so only a command could put
   it there.
3. Over 100: `FUN_005e3930` mode 3, a teleport. Over 24: mode 1, run, pace
   60. Over 16 with the owner walking (2) or walking in town (6): mode 0;
   running (3): mode 0 at pace 60.
4. Not standing (mode 1): it drops its events and stands 5.
5. On collision bit 0x40 (`FUN_0064d910` at its own spot, mask 0x40): 6 in
   128 (`FUN_00472210`, 12 for all but 0x152 / 0x230 / 0x231) it wanders 5
   (`FUN_005de200`).
6. Outside town (`FUN_0061ab00`), a target from `FUN_005ddc30` under 25 off:
   the attack think `FUN_005e5050`.
7. On bit 0x40: wander 5. On another level than the owner (`FUN_0061b130`
   at both spots): mode 0, pace 60.
8. Within 1 of the owner: path type 7 for the next move (`FUN_005de190`
   with EDX 7), to a spot 4 about the owner (`FUN_005df530`); else 4 away on
   each axis (`FUN_005defe0`); else steps 0x28 to the owner's path end
   (`FUN_005ded90`).
9. Else 5 in 100 (`FUN_0045c390`) a spot 16 about the owner; else it stands 5.

All rolls are on the merc's seed (unit +0x20). A spot "n about" a unit
(`FUN_005df530` about the owner; `FUN_005de200`, `FUN_005df400` about
itself): one step of the seed picks the axis n off (low bit 0: y), the other
is rand(n) (`FUN_0045c3e0`); two more steps flip x, then y.

## Following (FUN_005e3930)

`FUN_005e3930(ECX game, EDX owner, [8] merc, [c] mode, [10] run, [14]
pace, [18] reach)`. A move's ctx is set by `FUN_005de190(unit, EDX type,
pace, steps)` → `FUN_005a6260`: path type, pace % (velocitypercent for the
next mode) and steps (cap 0x4d), each only when not 0; the next
`FUN_005a7c20` uses and clears them.

- **Mode 0.** The owner's heading: `direction64` from him to his path end
  (path +0x18 / +0x1a), into 8 (`DAT_00745600`). Round the compass from
  there, a spot 8 off his path end (`DAT_006e34f0` → `DAT_006ea998` /
  `DAT_006ea978`: S, SW, W, NW, N, NE, E, SE in level subtiles) on the level
  he stands on, steps 0x28 at the pace: the first that finds a path. None:
  wander 4.
- **Mode 1.** With the owner walking (mode 2): steps 100 to his path end.
  Else his footsteps, newest first (player data +0xa0 the cursor, +0xa8 20
  spots of 8 bytes): the first over 5 off (`FUN_005dc5c0`), walking at the
  pace, steps 100; then as the mode asks with path type 0xf; then, once,
  with type 1. Twenty tried: a wander of a quarter of the gap (4 at least),
  pace 15.
- **Mode 3.** A spot in the owner's room (`FUN_0054dc40`: up to 20 tries
  inside the room's rect less a subtile each side, `FUN_0054dac0`, on the
  room's seed +0x6c, where MonStats class 0x16b fits, `FUN_005b2f20`), the
  merc put there (`FUN_00554ea0`), standing 5.
- Modes 2 (wander), 4 and 5 aren't reached from the think.

## The footsteps

A player's data +0xa0 (cursor) / +0xa8 (20 spots). `FUN_00580c20`, the walk
and run update: more than 25 ms (GetTickCount) after the last, when the
player is over 45 (squared subtiles) from the newest spot, it's written and
the cursor moves on. `FUN_00554ea0`, a unit put somewhere (a warp), writes
the arrival.

## Not traced yet

- The attack think `FUN_005e5050`: the melee mercs (0x152, 0x230, 0x231)
  skill chance 98, the others +0x14 + 40 + 2 × level (95 at most), +10 per
  miss; aip1 0 (the shooters) backs off 4 half the time under 4 off; the
  rest walk into melee (MonStats2 +0xe). A roll under the chance uses a
  skill (`FUN_005e4d30`, not read); else it stands 10. d2d strikes or walks
  at the foe instead.
- `FUN_005ddc30`'s choice (threat order, sight); d2d takes the nearest live
  monster under 0x31 by `merc_gap`.
