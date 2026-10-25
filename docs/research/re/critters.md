# Critters: the client's own ambient monsters (1.14d)

Bats, rats, chickens, bugs, scorpions, snakes, parrots, minispiders, bunnies:
flavour the host never sends (no S->C 0xac in any 2026-10-25 net log). The
client makes, moves and removes them itself; nothing can fight them.

## Spawning (`FUN_0046c460`)

When a room comes up on the client (`FUN_0044c750`: once per room, room
+0x34 bit 0), before the room's client-only presets (`FUN_00466820`):

- The level's Levels.txt record (`FUN_0061db70`) +0xcc: `cmon1..4` (s16
  MonStats rows, -1 ends the list), +0xd4 `cpct1..4`, +0xdc `camt1..4`.
- For each: one step of the room's seed (room +0x6c, LCG 0x6ac690c5);
  `% 100 < cpct` spawns `rand(MaxGrp - MinGrp) + MinGrp` of them (MonStats
  +0x2f MinGrp, +0x30 MaxGrp, rand = `FUN_0045c3e0` on the room's seed),
  x camt when camt != 0.
- Each (`FUN_0046c1a0`): up to 10 tries of a spot `room.x + rand(w - 1)`,
  `room.y + rand(h - 1)` (subtiles, room's seed) that `FUN_0064d9b0` finds
  free for the critter's size (MonStats2 +8) under mask 0x3f11; then
  `FUN_00466730` makes it (id from the client counter `DAT_00711f30`,
  unit flags 0x600000, the client unit table 0x7a5270).

Levels (cmon / cpct): camp chicken 30; Act 1 / 2 dungeons rat 30 + bat 30
(sewers, Ancient Tunnels: bat 100) + scorpion 20 in Act 2; Act 2 outdoors
scorpion 20; Lut Gholein bug 30; Kurast jungle snake 30 + parrot 30;
spider caves minispider 30; Act 3 dungeons rat 30; Durance bat 30;
Act 5 outdoors bunny 2..3; Duriel's lair larva 100.

## Thinking (`FUN_0046d780`, per client frame, `FUN_00465aa0` -> `FUN_00463cc0`)

- A countdown (`FUN_004ae110`): while > 0, count down and do nothing.
- The nearest live player (`FUN_0046c720`, the 8 player slots at
  0x7a7470, not dead modes 0 / 0xc; distance `FUN_006416d0`).
  None: wait 25 frames, stand (`FUN_0046cb40`). Farther than 30: wait
  `min(d - 15, 200)` frames (`FUN_0046cbd0`), stand.
- MonStats flag 0x16, `zoo` (`FUN_00457490(class, 0x16)`): `FUN_0046d660`
  (unreachable: no cmon is a zoo row). Needs a walk mode (MonStats2 mWL,
  `FUN_0046c140(class, 2)`), else stand 20; rand(100) < 40 stand 20, < 60
  walk to the player then wait 6 (`FUN_0046c770`), < 80 flee 8 wait 4, else
  wander 8 wait 4.
- Else by class (jump table 0x46d8d0 / 0x46d8f4, rows 0x95..0x153):

| Rows | Function | Does |
|---|---|---|
| 149 chicken, 268 bug | `FUN_0046ccd0` | in NU: another critter near (`FUN_0046c570`/`FUN_0046c600`)? under 4 away: flee (`FUN_0046c7d0`, 4, 0xc, 0x78), else wander 4; else 30 %: wander 2; else wait 5 |
| 151 rat, 269 scorpion, 283 larva, 339 minispider | `FUN_0046d110` | on the player's subtile: stand; player under 4: flee 6; past 7, a corpse (below); with one and not resting: walk to it, wait 6, rest (+0x34); not resting: rand(100) > 74 wander 5, else wait 30; resting: rand(100) < 20 stops it, wait 32 |
| 157 / 158 bird, 319 parrot | `FUN_0046ce40` | flown (+0x34) and the player past 25: gone (`FUN_00465f00`); else flies (below) |
| 159 bat | `FUN_0046cdc0` | player under 5: 66 % flee 8, else wander 12; else wander 9 |
| 227 maggot | `FUN_0046d070` | 40 %: wait 20; else wander 4 |
| 318 snake | `FUN_0046d280` | player under 2: flee 4; 75 %: wait 32; else wander 4 |
| 556 bunny | `FUN_0046d310` | as the chicken, flee 6 |
| 320 fish | — | stand |
| 574 Worldstone Effect | `FUN_0046d450` | 5 %: a unit (`FUN_004cdb40`) at a random spot up to 30 subtiles round, its fades set (unreachable) |

The rat's corpse (`FUN_0046c570` over the units of its room's near list,
`FUN_0046c660`): a dead player (mode 0 / 0x11) or monster (0 / 0xc) 3..25
subtiles from the rat and over 7 from the local player; each one a
rand(2) on the rat's seed, the last that hits wins.

A bird's flight: four steps of its seed. The first odd: x 40, y rand(40);
even: x rand(40), y 40; the next two negate x, then y on odd. It flies to
3 x (player + that) - 2 x itself, subtiles, its path's distance capped at
77 (`FUN_00648e70(path, 0x5a)`, kept under 0x4d), no wait after.

`FUN_0046c960(radius, mode, wait)` walks to a random corner `radius`
subtiles off (a sign per axis from the unit's seed); `FUN_0046c7d0(unit,
radius, mode, wait)` walks `radius` straight away from the player when the
next subtile's free; the wait runs once the walk is done. Distances are
`FUN_006416d0`: subtiles less half of each one's size, (smaller + 2 x
larger) / 2.

d2d (2026-10-25): `Town::critter_tick` makes a room's as it comes up
(Spawning's `up`, the rooms the host would add; the camp keeps no rooms,
so its come at once from `camp_room_seeds`) and thinks a 40 ms client
frame at a time; `debug critters` lists them. Seed 3: 23 chickens in the
camp, 35 rats / 7 bats in the Den once its rooms are up, and after `debug
kill` rats rest at the corpses (Arne's 1.14d client: 15 chickens in its own camp). ponytail: a critter's unit seed is the
room's stepped on (game.exe's comes with the unit, not traced); the rat
looks at d2d's monsters then players' bodies, not its rooms' unit lists in
their order; a bird's capped path is cut short along its line.

## The Den's light (`FUN_0046af70`)

Level 8 with `DAT_007a745c` set (the Den cleared?): up to 3 missiles 0x11f
(`denofevillight`) at random free spots of each room as it comes up (25 tries).
