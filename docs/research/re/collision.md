# Collision — game.exe 1.14d

`.\COLLISN\Collisn.cpp`, 0x64c000..0x64f800.

## The grid (FUN_0064c900)

Each room owns a u16 per subtile (5x5 per tile), header `{x, y, w, h,
…}` then the cells (`FUN_00619730` gives the rect; allocated at
Collisn.cpp line 0x185). Building it, for every floor, wall and roof tile
of the room (`FUN_00619660/6a0/6e0`), `FUN_0064c790`:

- ORs the tile's 25 DT1 subtile flag bytes in (`FUN_0064c4c0`): subtile
  row r, column c of the tile takes flag **(4 − r)·5 + c** — the DT1
  stores subtile rows bottom-up;
- adds bits from the tile entry's own flags (+0x14 of a 0x30 record):
  0x02 → 0x10, 0x40 → 0x01, 0x80 → 0x04, over the whole tile
  (`FUN_0064c700`): FUN_0066db20's tile flags, each word sharing the
  tile ORs its own in.

The grid lives at room1 +0x20 (`{x, y, w, h}` in subtiles, then the room's
tile rect; u16 cells from +0x24, row-major). It's made with the room1
(`FUN_00619890`), right after the room's tiles (`FUN_0061b190`): the tiles
of the near rooms already up count, each clipped to this room's rect
(`FUN_00619df0`). A room coming up later that re-picks a shared tile
(`FUN_0066e740`) patches the grid of the room it lies in, if that's up
(`FUN_0064c860`: the old tile's flags off, the new one's on). So a grid
depends on the order the rooms came up in, as the tiles do.

Bits: 0x01 blocks walking, 0x08 blocks the player. An object's bits
(`FUN_006209d0`): 0x400, | 0x04 with BlockMissile; 0x8000 for a SubClass
4 non-door; a door 0x806 with BlocksVis, else 0x804 with BlockMissile,
else 0x400 (on in the modes HasCollision says, off as it opens). An item
0x200, a warp tile 1. Outside every room reads 0x27.

`FUN_0064ca10` releases a room's `room1` allocation: with a non-null room
and non-null `room1` at +0x20, it calls the debug allocator free entry
`FUN_0040b480` with the caller's original ECX, the `room1` pointer, and
`Collisn.cpp:0x1c1`, then sets `room + 0x20` to null (`game114d.asm`
0x64ca10..0x64ca47; the file string is at 0x6eb730). A null room or null
`room1` does nothing; a repeated release sees null and does not free again.
Verified by `tools/emu/collisn_release.py` against stock 1.14d, with the
allocator entry captured rather than invoked. d2d has no one-to-one function:
`components/game/gamedata/gamedata.hpp` stores the whole-level grid in
`Level::walk`, and `components/game/gamedata/gamedata.cpp` builds it in
`set_level_tiles`; the vector is released with its owning `Level`.

Units stamp their own (`FUN_00649400`, off with `FUN_00649560`; the mover's
own lifted while it paths, `FUN_00649970`) by pattern (`FUN_0064ea90`):

| pattern | own bits (player 0x80, monster 0x100) | footprint |
|---|---|---|
| 1 / 3 | the plus | 0x1000 / 0x2000 on the subtile |
| 2 / 4 | the 3x3 square round it (`FUN_0064ddf0`: x - 1 .. x + 1, y - 1 .. y + 1; SizeX 3 is the only size with pattern 2) | 0x1000 / 0x2000 on the plus |
| 5 | the plus | none |

The pattern (`FUN_006484e0`): MonStats2 SizeX through `DAT_006eb3dc` (0,
1, 1, 2; over 3: 1); a MonStats npc or inTown (+0xd & 1 / 4) that isn't
interact (+0xd & 2) goes 1 → 3, 2 → 4 — the mercs and the cow — as does a
player's pet (unit +0xc4 bit 31: FUN_0056d8d0 for every summon, Revive's
FUN_005c55c0; FUN_0063e860). Pattern 0 (SizeX 0: the chicken, rats,
hydras, sentries) stamps nothing and tests its own subtile. A mover
tests (`FUN_0064d910`) the plus for 1 / 3 / 5 (`FUN_0064d4e0`), its box for
2 / 4 (`FUN_0064d7c0`), against its mask (path +0x50): a player 0x1c09, a
monster 0x3c01, opendoors (+0xc & 8) 0x3401, flying (+0xd & 0x40) 0x1804.
So a player walks through its merc (0x2000) but not a monster or Akara
(0x1000); an opendoors monster through a BlocksVis door.

In d2d the grid holds tiles and objects (`Level::walk`, u16); units are
the `Crowd` (ai.hpp), each with its `UnitShape` (gamedata.hpp).

## Rects and units

- `FUN_0064d0d0` makes a unit's rect from its position and size:
  `x − size/2 … x − size/2 + size − 1` (same for y), inclusive.
- `FUN_0064cc30` tests a rect against a mask, `FUN_0064cd70` sets bits,
  `FUN_0064cce0` clears them; `FUN_0064ceb0` splits a rect across rooms.
- Point tests by collision pattern (`FUN_0064d870`): 0 one subtile
  (`FUN_0064d450`); 1/3/5 a plus — the subtile and its four neighbours
  (`FUN_0064d100`, edges clipped per room); 2/4 bigger (`FUN_0064d4a0`).
  Players and small units use the plus. **Verified**: `tools/emu/collisn_point.py`
  runs the dispatch for every pattern (0..5, plus the fatal-looking default,
  which answers `COLLISION_ALL` for an out-of-range pattern instead) against
  a fake room and diffs it against calling each pattern's own shape function
  directly — 20000 random (room content, mask) pairs, no disagreement
  (`uv run python collisn_point.py 20000 42`).
- `FUN_0064e7b0` → `FUN_0064dea0`: nearest free spot (spawns, the town
  start).
- A door's doorway (`FUN_00581d40`, modes 2 / 5, 500 ms since it last
  changed: unit +0xd4): `FUN_0064d800(room, x, y, SizeX, SizeY, 0x8180)`
  over the door's rect (`FUN_00620870` / `FUN_00620510` / `FUN_006205a0`).
  0x80 / 0x100 are the units' own bits, stamped by pattern
  (`FUN_0064ea90`): 1 / 3 / 5 the plus (`FUN_0064dc80`), 2 / 4 the 3x3
  square (`FUN_0064ddf0`), 0 nothing. So a unit is in the doorway when its
  plus (or 3x3) meets the rect, and a pattern-0 unit (SizeX 0: rats,
  hydras) never is. Then a second test with 0x8000 alone (an object
  stamped there).
- The Maggot Lair's slime door (objects 229 / 230, OperateFn 29,
  `FUN_005821a0`) has no doorway test: in any mode but 0 (unit +0x10,
  0x5821aa) it does nothing; shut, it goes to mode 1
  (`FUN_00624690(unit, 1)`, 0x5821b8) and its footprint lifts
  (`FUN_00623830`, 0x5821c1); with Mode2 (objects.bin +0x141) event 1 at
  FrameCnt1 + 1 takes it to mode 2 (0x58223c). It never shuts. d2d:
  `World::operate_door` (`test_slime_door`).

## d2d

- `Level::walk` is built like the room grids (flipped rows), one grid
  for the whole DS1, from the picks as each cell's room had them when it
  came up (`Level::Pick::stamp`, `Level::patches`); objects and standing
  NPCs stamp 0x01 over their size rect.
- Order: a level is built in list order; as the player brings rooms up
  (`player_moved`, the arrived room then the near list's), `relevel` lays
  the whole level again in that order (the rest after, list order). The
  oracle: `diff_drlg.py 1-10 <level> collision` brings game.exe's rooms up
  shuffled ($ORDER) and compares every room's grid (levels 2–39, 10/10).
- `Scene::unit_blocked` is the plus test with mask 0x09.
- Pathing: game.exe's path types (below). The player walks type 7
  (`rules::player_path` / `ai.cpp player_walk`); monsters, town NPCs,
  Cain, the merc and pets walk as monsters, 0xd then 0xf
  (`rules::monster_path`; `ai.cpp path_to`, `set_off` / `walk_on`).

## Path types (`.\PATH\Path.cpp`)

A unit's path (+0x2c, 0x200 bytes, FUN_00649d00) has a type (+0x3c, set by
FUN_00648cf0 with flags from `0x6eb690`). FUN_00649970 computes it with the
pather `0x6eb6d8[type]`: 0 / 16 `0x67ad00`, 1 `0x67b850` (search), 2 / 5 /
6 / 0xd `0x679c80` (toward), 3 `0x679e50`, 7 `0x679ed0`, 8 `0x67a000`, 9
`0x679f70`, 0xb `0x679fd0`, 0xc `0x679e60`, 0xf `0x67c2d0`; 4, 0xa and 0xe
(flag 0x40000) go through FUN_00649760 instead.

- A player's path is type 7 (FUN_00649d00 for unit type 0: steps +0x91 =
  0x49, +0x92 = 0x46, mask +0x50 = 0x1c09); FUN_00648dc0 puts it back to 7
  before each walk (FUN_0057f090, from C→S 0x01 / 0x03 → FUN_005809d0 →
  FUN_0057f1f0).
- Type 7 (FUN_00679ed0): the toward pather; taken when its last point is
  within `near` of the end and isn't the start. Else, when the end is under
  18 subtiles off (dx² + dy² < 0x145), the search pather; if that finds
  nothing, the toward path again. So a far click stops at what's in the
  way; a close one goes round it.
- The search (FUN_0067b850): A* in a 200-node pool (FUN_0067b1c0), open and
  closed lists hashed by position (DAT_006f1998 / DAT_006f1b98, 128
  buckets), the open list sorted by cost with ties newest first
  (FUN_0067aed0). Step 2 straight, 3 diagonal; estimate 2 × the long axis +
  the short (FUN_0067adc0). Neighbours (-1,-1) (-1,+1) (+1,-1) (+1,+1)
  (-1,0) (0,-1) (+1,0) (0,+1) (FUN_0067b440). A cheaper way to an open node
  changes its cost in place (not re-sorted); to a closed one it passes down
  the node's up to 8 children (FUN_0067afe0). It keeps the node nearest
  the end (at a tie, one more than 5 further along) and stops at the end, an
  empty open list or a full pool. FUN_0067b690 turns the way into points:
  that node and every turn, start first, the start left out, at most 77.
  Walking to a unit, nothing when all eight subtiles 2 off it are blocked
  (FUN_0067b740).
- A monster's walk (town NPCs too: FUN_005ded90 → FUN_005a7c20 →
  FUN_005a63f0, move mode 0x65 → type 0xd, steps +0x91 = 5): FUN_005a6290
  computes type 0xd (the toward pather, monster-ai.md) and, with no point,
  type 0xf. FUN_005a7c20 gives it a budget of 0x14 points (+0x94); a path
  run out short of its end paths again while that lasts, the points walked
  coming off it (FUN_006503f0 → FUN_00650350).
- The wall pather (FUN_0067c2d0, type 0xf): a line (FUN_0067b9f0,
  Bresenham on the long axis) of at most `steps` − 1 subtiles (`steps` is
  +0x91, 0x28 at least with a unit to walk to), else none; none with two
  subtiles or fewer either. Walking it, a blocked subtile starts two
  tracers from the one before (FUN_0067bdf0), taking turns: one keeps the
  wall on its right (after a step it turns left, blocked it turns right:
  tables 0x6f1f00 / 0x6f1ec0), the other on its left (0x6f1e80 /
  0x6f1e40); they start 45° either side of the line (DAT_006f1e18 + 1 /
  + 7; directions DAT_006f1d98, 0 north, clockwise). A step tries its way
  and up to three turns (FUN_0067bbf0). A tracer back on the line further
  on is spliced in (FUN_0067bd80; the subtile after it isn't tested). One
  stepping onto the other's last-but-one spot ends the path before the
  block; one reaching the other's spot goes on alone until they part.
  Stuck, or past `steps` less the line less the walked, they stop: then
  (unless over 0x50 were left: the path ends at the block) the tracer
  nearer the end ends the path, if nearer than the start. FUN_0067c1e0
  makes the points: each turn and the last (a first step off both axes
  isn't a turn).
- Checked: `tools/emu/moves.py` runs FUN_0067b850, FUN_00679ed0 and
  FUN_0067c2d0 on random walls against the port (6,000 and 9,000 cases,
  all equal).
- FUN_00650660 moves a unit one step a frame. FUN_00650090 tests the next
  point: within the step on both axes, the unit lands on it and the frame
  ends there, with the rest of the step unused. So a repeated point (the
  toward pather's cut-short subtile) costs a frame (`follow_path`).
- ponytail: A target that moves re-paths at
  0.3 cells (the player) or 1 cell (merc, pets: a fresh budget), not
  FUN_006503f0's 5 subtiles off SP2. Town NPCs walking up to the player go
  to a spot, not a unit. The merc's moves carry their own type, pace and
  steps (merc-ai.md).
- ponytail: FUN_0056d8d0 skips bit 31 for a summon record with flag 8;
  no caller seen passes it (the golems and Valkyrie 0, traps 1), so every
  d2d pet takes 3 / 4.

## Is a subtile blocked for a unit: `FUN_0064d910`, and the blocked-end walk back

Read from the decompile only (the register arguments of the helpers below
were not read in the asm); cited, no oracle run.

- **`FUN_0064d910`** `(room, x, y, pattern, mask)`: non-zero means blocked.
  The pattern picks the test: 0 `FUN_0064d450` & 0xffff (the decompile
  shows only the mask as its argument; the rest go in registers, unread);
  1 / 3 / 5 `FUN_0064d4e0(room, x, y)` (the plus); 2 / 4 `FUN_0064d7c0`
  (the decompile shows y and the mask; the others are in registers,
  unread); any other pattern returns 1, blocked. The mask is the path's +0x50 (`FUN_00649970` passes path +0x1c
  room, +0x48 pattern, +0x50 mask). d2d: agrees with "Rects and units"
  above and `UnitShape` (gamedata.hpp:61, 274: 0 own subtile, 1 / 3 plus,
  2 / 4 box); the pattern 5 case (the plus again, no footprint) is the one
  `grid_set_pattern_bits` shows for stamping.
- **`FUN_00648050`** (ESI = the path request `rq`, a record built by
  `FUN_00649970`; its shorts `rq[0..1]` are the start x, y, `rq[2..3]`
  the end x, y, `rq +8` the room, `rq +0x28` the pattern, `rq +0x2c` the
  mask): pushes the end on, by at most 5 - max(|dx|, |dy|) table steps
  (the table is unread, so "away from the start" is the intent, not
  checked). With dx, dy = end - start, it does nothing unless both |dx| and
  |dy| are under 5 and they are not both 0. The step is
  (`0x6eb490[dx + 9 * dy]`, `0x6eb490[dy + 9 * dx]`) as x and y (a table
  of 9 x 9 dwords; its values were not read). From n = max(|dx|, |dy|) up
  to 4 it adds the step to the end, asks `FUN_0064d910` there, and stops
  at the first blocked subtile; each free one is stored in `rq[2..3]`. It
  returns nothing. Its only caller is `FUN_00648120`, for a player (below).
  d2d: not ported.
- **`FUN_00678df0`** `(out, a, b)` (thiscall, ECX = out): the three
  direction tries toward b from a. `i = FUN_00678c10(a, b)` (the 5 x 5
  index, coordinates packed x low, y high) and `out[0..2]` = the bytes at
  `0x6f1648 + 3 * i + 0 / 1 / 2`, widened to dwords. `FUN_00678cd0` reads
  the first of those bytes alone; `FUN_00678d70` fills the same three from
  the dword table `0x6f1518` (12 bytes a row, 25 rows: d2d's `kTry`,
  monsters.hpp:567). So 0x6f1648 is a byte table of the same shape that
  follows it (0x6f1518 + 25 * 12 = 0x6f1644); that its rows equal `kTry`
  was not checked (the bytes were not read). A third entry of 0xff is
  skipped by `FUN_00648120` (below), as `toward_path` skips it. d2d: not
  ported; `kTry` and `FUN_00678d70` are the dword twin.
- **`FUN_00648120`: walk a blocked end back toward the start** (EAX = the
  request `rq` of `FUN_00648050` above; `rq +0x30` is the unit's path).
  `FUN_00649970` calls it only when path flag 0x1000 is set and
  `FUN_0064d910` finds the path's end (+0x10, +0x12) blocked for the
  path's pattern (+0x48) and mask (+0x50); its result replaces the "1,
  go on" that `FUN_00649970` otherwise keeps, so **0 means the pather is
  not run and the path is empty**.
  - A path whose flag 0x40000 is set raises the internal error (`FUN_00408090(199)`;
    runtime-support.md); `FUN_00649970` sends those to `FUN_00649760`
    first, so it is not reached.
  - The unit type (the first dword of the owner at `path +0x30`; 6 when
    that owner pointer is null) is kept; type 0 only
    matters at the end.
  - `cur` = `rq[2..3]` (the end), `start` = `rq[0..1]`. The tries
    `t0 t1 t2` come from `FUN_00678df0(cur, start)` (the direction of the
    start as seen from the end; bytes of table `0x6f1648`). Three points
    are kept: `cur`, and two side points `B` and `C`, all three starting
    at the end. The step of direction d is (`0x6eb398[8 * d]`,
    `0x6eb39c[8 * d]`), shorts of two dwords a row (not read; d2d's
    `kDir`, `DAT_006f1798`, has the same layout).
  - Each round: if `cur == start`, **return 0** (no free subtile on the
    way: the walk found nothing). Else `cur += step(t0)`, and ask
    `FUN_0064d910(room, cur, rq +0x28, rq +0x2c)`. Free: the end becomes
    `cur`. Blocked: `B += step(t1)` and ask there; free: the end becomes
    `B`. Blocked: if `t2` is not 0xff, `C += step(t2)` and ask; free: the
    end becomes `C`. All blocked: the tries are read again for the new `cur`
    toward the start (the decompile shows `t0` taken from the new read; the
    other two are not visible in it) and the next round runs. `cur` moves
    on every round whether it was free or not, but `B` and `C` only add
    their own tries, from the end, never from `cur`.
  - Once a free spot was taken as `rq[2..3]`: if it equals the start,
    **return 0**; else for unit type 0 (the player) `FUN_00648050` pushes
    it on again (above); **return 1**. Monsters keep the free spot as is.
  - d2d: **not ported**. `ai.cpp:348` (`path_to`) leaves it out with
    "its setter isn't found". This read does not find the setter either:
    only `FUN_00649970` tests path flag 0x1000, and the places that set
    +0x34 bits were not searched. So d2d agrees as far as it goes, and no
    disagreement is claimed. `rules::player_path` is a player's walk, so
    the push-on (`FUN_00648050`) matters only once that flag is set.

## A walk's stop

Read from the code; `objdump -d -M intel --start-address=0x64eda0
--stop-address=0x650910 game.exe` shows the first three paragraphs.

- **The stop.** FUN_00650840 calls FUN_00650660, which calls FUN_00650150
  (LAB_0065073c). A blocked subtile: FUN_00650150 writes the centre of the
  last free walked point (LAB_0065025f, `and edi,0xffff0000`, then adding 0x8000) and
  returns 1, and FUN_00650660 sets idx = count (LAB_0065076c). FUN_00650840
  puts the unit there (LAB_006508ca), sees idx ≥ count and calls FUN_006507b0
  (LAB_006508ff), which snaps again (LAB_006507b5), clears flag 0x20 and sets
  idx and count to 0. No path means the same stop, from the same label.
- **Steps don't count.** The walk starts at the unit's own position
  (LAB_0065017c) and no step made earlier decides the snap: path +0x1d4 starts
  at 0 each call, and only its cap of 10 subtiles a call (LAB_00650236) leaves
  without a snap. Blocked on the first step, the unit goes to the centre of
  the subtile it stands on. A velocity of 0 snaps too (LAB_006506a8).
- **Wall or unit, the same.** A walker (no path flag 0x40000) tests a new
  subtile through FUN_0064ff90 → FUN_0064eda0: its own stamp off
  (FUN_0064ec10), then FUN_0064d870 against the mask (path +0x50). Any bit
  blocks (LAB_0065005f, `or` into path +0x54, then LAB_00650070), so a wall,
  an object and the units' 0x1000 take one path. Only a change of subtile is
  tested (LAB_006501c0 on). FUN_0064eda0 is also where the stamp lands: free,
  it's added at the new subtile and the old one stays clear (`COLLIDE_NONE`);
  blocked, it's put back at the old one and the blocking flags are returned,
  so a blocked test never leaves the unit unstamped. **Verified**:
  `tools/emu/collisn_point.py`'s "move" cases do this for real (a cross-pattern
  player stamp, the real 0x1c09 mask), checking the stamp's new position with
  FUN_0064d870 after: 20000 random free/blocked pairs, no disagreement.
- **Who.** Player, monster and merc share this stop; the merc differs only in
  what it stamps (0x2000, "The grid" above). What follows differs: FUN_00554ca0
  turns the stop into 2, a player's FUN_00580c20 then goes idle or takes its
  pending interact, a monster's walk update (LAB_005a84c4) thinks at
  once (FUN_005a8030). Only a client-side monster re-paths from the stop
  (monster-ai.md).
- **d2d.** `follow_path` (ai.cpp), for the player, merc, pets and town NPCs,
  stops for a wall and the crowd alike, as game.exe does, on a step that stops
  short of its next point; one that reaches a point tests nothing. A stop puts the
  unit on the centre of the subtile it stands on and clears its path, as game.exe's
  does (#226). A walk with no route at all doesn't snap, though game.exe's does
  (LAB_006508ff, the same stop): `follow_path`'s loop doesn't run on an empty path. A
  hostile monster walks by `move_frame`, which snaps already.

### What keeps a walk to a unit going: the client

The server re-issues a walk once at most (below). What keeps a walk to an NPC,
object or player going is the client, every game frame, until it stands in reach:

- **Queued at the click.** FUN_00461dc0 starts the walk (LAB_0046213f; an NPC
  over 2 away, LAB_0046204e, LAB_0046212a) and queues the interact: pending
  (player data +0x150) = 1, action +0x154 = 0x13, target +0x158 / +0x15c
  (LAB_00462159). A stop leaves it queued, the mode unchanged (FUN_00463390,
  LAB_004636ae).
- **Queued at a click on a player** in town: FUN_004625b0 (LAB_0046281a) calls
  FUN_004619e0 when both units' rooms are in town (FUN_0061ab00). Within 2 with
  a clear 0x804 line it interacts at once (LAB_00461a11); otherwise it walks
  (LAB_00461a22) and queues the same way (from LAB_00461a3c). It isn't a caller of the frame code:
  FUN_00481600 has one caller, LAB_0044f126.
- **Run each frame** of a game. FUN_0044efa0 calls FUN_00481600 (LAB_0044f126)
  only while 0x7a061c is set: S→C 0x04's handler sets it (LAB_0045c9a3), and
  S→C 0x05's handler (LAB_0045ca3f), FUN_0044e380 (LAB_0044e3df), FUN_00477ee0
  (LAB_00477f2f) and FUN_00453910 (LAB_004539bb) clear it, as FUN_0044e200 does
  at the start. FUN_00481600 clears the queue
  (LAB_00481672, FUN_0045c470) and asks FUN_00480c80 about
  a player, monster or object target (LAB_004816f5, LAB_004816ea,
  LAB_004816d8).
- **Out of reach,** FUN_004812e0 runs the walk command (LAB_0048136b,
  FUN_00480c10 → FUN_00461250 → FUN_00480780: a new path from where the unit is) and, with a
  path, sends C→S 0x02 or 0x04 (LAB_0048137a). **Verified**:
  `tools/emu/path_target_unit.py` runs FUN_00480780 for real with the lookup
  (UNITS_FindClientSideUnit) hooked found and not found: not found, it
  clears the path's target and returns 0 without a path call; found, it
  sets the target then calls PATH_CalculatePath and forwards its answer. Path or no path it queues the
  action again (LAB_004813c3), with no count, no timer and no test of the
  steps made. With no path the unit goes neutral, nothing is sent, and the
  next frame tries again.
- **In reach,** FUN_00481030 (LAB_00481709) sends 0x13 and clears the queue.
- **Abandoned** by any other action (FUN_00481030 clears the queue,
  LAB_00481240), a target gone (LAB_00481697, the queue already cleared), or a
  target dead or not selectable (FUN_00467a70). Besides those, in address order:
  - LAB_0045c9e5: S→C 0x04's handler (0x45c9a0, table 0x7114cc): the game
    coming up.
  - LAB_004615b9: FUN_00461250's command 8, the player dying (table 0x4616a4, index
    0x4616e4; FUN_00480e70 with mode 0, LAB_004615a9), through FUN_0045c470. Only a packet
    can pass 8: S→C 0x0d's handler (0x45ccc0) reads it at +6 (LAB_0045cce7), as net-packets.md's
    recording of a player's death shows. The fixed callers pass 0, 1, 2, 7, 0x15 to 0x19.
  - LAB_004622c4 and LAB_004622db, FUN_004621d0, the first thing FUN_004625b0
    does on a click, held or not: a queued action other than 0x13 goes
    (LAB_004622db); a queued 0x13 goes if FUN_00463990 finds its target
    (LAB_004622c4), and if not FUN_00481600 clears it. So a new click ends a queue.
    FUN_004621d0's skill gate can return first, without clearing (LAB_00462226).
  - LAB_004b3c45: FUN_004b3c20(id), then FUN_00463990(id, 1) and FUN_004b3790,
    from the NPC menu code (calls from 0x4b3d10 to 0x4b70c2) and FUN_00453910.
  - LAB_004b6e1f: FUN_004b6dd0, S→C 0x28's (handler 0x45d370), when the unit
    isn't found (and the packet's kind isn't 6): it also sends C→S 0x30 and
    calls FUN_004b3830.

  Nothing here is a timer or a count: a queue ends on input, a packet or its target.
- **A town portal gone.** A new portal from the same player closes the old
  pair first (FUN_00535430): each end gets a room event 2 with its unit id
  (FUN_0061a270, the room's list +0x18) and is freed (FUN_00555600), so the
  client loses the unit and a walk queued to either end is a target gone
  (LAB_00481697): it stops. Read, not run.

So a blocked walk to an NPC re-plans the frame after it stops, from the
snapped centre, whether or not it moved. This is a reading of the code; no
packet log has been taken of 0x02 going out each frame.

d2d does the same since #227 (`tick.cpp`, "Queued, a walk that stopped tries
again"): the interact stays queued, the walk re-plans the tick after each stop, and the player
stands while no step is possible.

### The server's pending interact: one more walk

FUN_00548a50 walks a player at a target out of FUN_00548b00's reach (mode 3,
FUN_00580a70) and sets pending (+0x150, LAB_00641f37). When the walk ends,
blocked or not, FUN_00580c20 goes idle and calls FUN_00548b00 again
(LAB_00580e73). Out of reach, that starts another walk and sets pending, and
FUN_00580c20 then clears pending (LAB_00580e88). So the second walk ends in
plain idle: one re-issue, then nothing. C→S 0x02 / 0x04 (`549890`, `549920`)
never set it.

An item or a warp is where this shows. FUN_00481600 sends those with no range
test (LAB_004816b1 to LAB_00481709), once, as FUN_00480930's 0x16 or 0x13, so
a blocked walk to one gets the two walks and stops. The 0x16 handler (`54aad0`)
ends in FUN_00548b00 (LAB_0054ab27). An NPC farther than 8 gets no walk at
all from FUN_00548b00 (LAB_00548d7d): 7 and 8 walk, 6 or less talks.
**Partly verified**: `tools/emu/interact_any_object.py` runs FUN_00480930 for
real for a player, an item, a non-interactable object and a monster (both
sides of its 200ms throttle, `GetTickCount`/`dwTickCount`): the item alone
sends 0x16 (with the inventory-open flag), everything else here sends 0x13,
and a target not found sends nothing. Not run: an interactable object's
melee/kick skill cast and the 0x194/0x178 class-specific cases, which need
real weapon and inventory state; they still end in the same 0x13 send,
untested by this oracle.

The sends, as FUN_00481030 reaches them: action 0x13 (LAB_004810f1) → FUN_00480c10
→ FUN_00461250's command 2 (LAB_00461634) → FUN_00480930 (LAB_0046163d). That
switches on the type (table 0x480b20): 0x16 only for an item (`mov cl,0x16`,
LAB_00480b11, through 0x4786d0), 0x13 for the rest, a warp too (LAB_00480a3d, through
0x4786a0). The other 0x16 immediates in these functions aren't sends: LAB_004807db is a state
test, and LAB_00481186 and LAB_004811f6 are FUN_00461250 command numbers.

d2d's item walk (`tick.cpp`, `pick_item`) re-plans without that limit.

A warp's click, FUN_00461dc0's type 5 (LAB_00461fb7): refused until 0x7a04c8, the time
FUN_0044dad0 sets 500 ms ahead (LAB_00462022, LAB_0048168f; FUN_00467000 uses it too).
Within 4 it sends at once (LAB_00462010); further it walks and queues 0x13 (LAB_00461fed,
LAB_00461ffc). FUN_00548b00's case 5 (LAB_00548bdc) answers: over 0x32 nothing, over 4
walk (FUN_00548a50), else FUN_005550b0 (LAB_00548c32). The test runs when a packet comes and
when the walk ends, not on the way. This case has no held test of its own, but FUN_00462930
stops a held frame while the unit walks ("What a held frame sends"). So a held button clicks again
after a stop, at most each 500 ms, and each click is two more walks.

d2d (`World::use_warp`):

- Same reach: `warp_close`, under 5 by `unit_distance`.
- No two-walk limit here: "Short of the target with a step made" re-plans with no
  count, and `take_warp` goes when the walk ends short of reach. Since #226 a block
  ends it, as does a stop with no step, so a blocked walk is one walk where game.exe has two.
- It tests every tick, so it takes the warp on the way at reach 4; a single click in
  game.exe walks on to the path's end first. Up to 4 subtiles early.
- A held button sets `take_warp` only on the press, so after a stop it's gone.
- No 500 ms refusal. No 0x32 limit, which a click can't reach: at 800x600 the
  screen corner is about 31 subtiles from the player, by the iso projection.

All read from the code, no oracle. It takes a crowd or a blocked path on the way to a warp; how
often wasn't measured.

### The per-frame queue step (`FUN_00481600`, 0x481600, 277 bytes)

Read from the decompile and disassembly (cited, no oracle run). The unit
(ECX) must be a player (type 0) with player data (+0x14) and a queued
action (+0x150 nonzero). Then, in this order:

1. read action (+0x154), target type (+0x158) and id (+0x15c);
2. find the target (`FUN_00463990`), and **clear the queue**
   (`FUN_0045c470`) before anything else is tried;
3. action 0x13 on a type 5 target (a tile): `FUN_0044daa0` and
   `FUN_0044dad0` (not traced);
4. no target, or action 0: stop (the queue stays cleared);
5. target type 0: `FUN_00481400(action, 0, id)`; 1: `FUN_004814a0`; 2:
   `FUN_004815a0`; a nonzero answer goes to `FUN_00481030`
   (ECX action, EDX the player, then type, id, 0 on the stack, 0x481701
   to 0x481709), zero ends it. Any other type (3, 4 an item, 5 a
   tile): no range test, only the target's unit flag 2 (`FUN_00451f30`,
   selectable): clear, `FUN_00467a70` (abandon); set, `FUN_00481030`.

That is why an item or a warp is sent once and cannot wait for range, as
"The server's pending interact" says. d2d: `tick.cpp:435-440` cites it
for the NPC walk (target gone, retry through the walk code), no disagreement
found; the type 5 pair `FUN_0044daa0` / `FUN_0044dad0` is not ported.

#### The three target-type gates (`FUN_00481400`, `FUN_004814a0`, `FUN_004815a0`)

Read from the decompile and disassembly (cited, no oracle run). All three
share the shape FUN_00481600 calls them with: the resolved target arrives
in EAX (kept in EDI for the rest of the function — its selectable bit is
read off this register below) and the acting player stays in ESI from the
caller; the stack carries the queue's action, target type and id, in that
order (`[ebp+8]`, `[ebp+0xc]`, `[ebp+0x10]`).

- **`FUN_004815a0`** (object target, type 2, 94 bytes): a null target, or
  one whose `+0xc4` bit 1 (selectable) is clear, is `FUN_00467a70`
  (abandon), return 0. Otherwise `FUN_00480c80(EAX = action, EBX = ESI the
  player, EDI = the target)`: false (not yet in position) returns 1 so
  `FUN_00481600`'s caller tries again; true sends the engage,
  `FUN_004812e0(EAX = ESI, stack = id, action)`, return 0.
- **`FUN_00481400`** (player target, type 0, 89 bytes): first asks
  `FUN_00464820(ECX = the target)`. A **zero** answer takes the same
  selectable-bit-then-`FUN_00480c80` gate as `FUN_004815a0`. A **nonzero**
  answer skips `FUN_00480c80` entirely: `ECX` becomes the target's `+0xc`
  field when ESI (a second, apparently unrelated register read in this
  branch only) is non-null, else -1; `FUN_0047a4f0(ECX, EDX = id)` gates a
  direct send — nonzero sends `FUN_00481030(ECX = action, EDX = the
  target, stack = id, 0, 0)` and returns 0, zero is `FUN_00467a70` and
  returns 0. Which case `FUN_00464820` answers (town vs. elsewhere, a
  player vs. a merc) isn't traced; its own body wasn't read here.
- **`FUN_004814a0`** (monster target, type 1, 227 bytes): the same
  `FUN_00464820(target)` split. A **zero** answer: the same selectable-bit
  + `FUN_00480c80` gate as the other two. A **nonzero** answer instead
  switches on the action (`[ebp+8]`, the jump table at 0x481584, byte
  index 0x48158c): 5-0xb calls `d2d_left_skill(ESI)` (`FUN_00620190`),
  0xc-0x12 `d2d_right_skill(ESI)` (`FUN_006201d0`); any other action is
  `FUN_00467a70`, return 0. A null skill record, or its node id
  (`FUN_00643ce0`, asserting `.\UNIT\UnitMode.cpp` line 0x44f) negative or
  past the Skills.txt row count (`PTR_DAT_00744304+0xba0`), is also
  `FUN_00467a70`; otherwise the row (`0x23c` bytes, `PTR_DAT_00744304
  +0xb98`) byte `+7` is read and masked by `DAT_006ce268` — a clear bit is
  `FUN_00467a70` too. Only a set bit falls through to `FUN_00480c80` (same
  gate, no skill-record check this time) and the same send/retry split as
  `FUN_004815a0`.

So all three only ever reach `FUN_00480c80`'s range-and-line test for a
player target through `FUN_00464820`'s zero branch, and skip it for a
monster target whose cast skill passes the Skills.txt row's `+7` bit;
`FUN_00481030` is reached from inside these three, not after them as the
summary above shortens it to. Not traced: `FUN_00464820`'s own body, the
`+7` bit's name, and the ESI/-1 branch inside `FUN_00481400`'s nonzero
case.

#### The range-and-line gate itself (`FUN_00480c80`, 0x480c80, 160 bytes)

Read from the decompile and disassembly (cited, no oracle run). Not a
normal calling convention: action in EAX, the acting player in EBX, the
target in EDI, no stack arguments, plain `RET` — the three callers above
set these up before the call. In order:

- Action 6 or 0xd (a left or right skill already in flight): `d2d_left_skill(EBX)`
  (`FUN_00620190`) or `d2d_right_skill(EBX)` (`FUN_006201d0`) finds that
  skill's cast node. A null node, or `FUN_00644140(node)` (the node's
  current skill record, already verified at skills.md:1780) null, or that
  record's word at `+0x2e` not 0x13, falls through to the range/line test
  below. Otherwise `FUN_00461ac0(ECX = node, EDX = EDI the target, two
  local out-params)` answers directly, converted to 0/1, and `FUN_00480c80`
  returns that — the skill in flight skips the range/line test entirely.
- Fallthrough (any other action, or the skill check above falling
  through): a null target is true (in range); an object target (`*EDI ==
  2`) is `d2d_monsters_object_reach(EBX, EDI)` (`FUN_00623660`, already
  ported) true unless `FUN_00622b50(EBX, EDI, 0x804)` (the line-of-sight
  test, already verified at collision.md:548) then answers 0; any other
  target type is `FUN_00622c40(EBX, EDI, FUN_00622dc0(EDI))` (also already
  verified, `ai.cpp`'s melee-range/line check) answering nonzero.

So `FUN_00480c80` itself is two gates layered: a skill already queued on
the action's own left/right slot, whose record's `+0x2e` reads 0x13, is
let through by `FUN_00461ac0`'s own range test instead of the normal
reach-and-line test; everything else (including a different action, or no
such skill) takes the reach-and-line path todo.md row 39 already
describes. d2d: not ported as a function (todo.md row 39 covers the
reach/line half only); the skill-in-flight bypass and what skill id 0x13
and the object-type special case mean are not ported, no disagreement
found (no d2d code to compare the bypass against).

**`FUN_00461ac0`** (fastcall `(ECX skill node, EDX target)` plus two stack
out-pointers for range and distance, `RET 8`; callers `FUN_00480c80` and
`FUN_00461b40` at `LAB_00461bac`):
a null local player, node or target answers false. Otherwise
`FUN_00644140(node)` (the node's current skill record) null also answers
false; else `FUN_006442a0(local player, node, 1)` and
`FUN_00646ca0(local player, dword record+0x64, record[0], that)` fill the range
out-param, `FUN_006416d0(local player, target)` fills the distance one,
and the answer is distance <= range. Not traced: `FUN_006442a0`,
`FUN_00646ca0` and `FUN_006416d0`'s own bodies, so what the range and
distance actually measure (subtiles, a skill's stated range, a line
check) is read from the call shape only.

### The action send (`FUN_00481030`, 0x481030, 601 bytes)

Read from the decompile and disassembly (cited, no oracle run). ECX is the
action, EDX the player, then the target type, id and a third value on the
stack. `FUN_00480ba0` must accept it first (0: return, nothing sent).
**Verified**: `tools/emu/unitmode_caninterrupt.py` runs it for real with a
faked `Skills.txt` row: with no current skill, an invalid skill id or a
missing row it always answers 1 (go ahead); with a skill row, its flags
byte (+7) against a fixed mask (0x80, game.exe's own byte at 0x6ce284)
answers 1 regardless of the player when set; clear, it answers 0 unless
the player is null or its mode (+0x10) is 1 or 5 (Neutral, TownNeutral) —
34 cases, no disagreement
(`uv run python unitmode_caninterrupt.py`). The switch is on the action itself (`action - 1`, a byte index at
0x4812b4 into a jump table at 0x48128c; 0x48107d to 0x481084). By action:

| action | does |
|---|---|
| 1, 3 | with a player (type 0, data +0x14) whose queued action (+0x154) is already 0x13: return, nothing sent; else on to the send |
| 2, 4 | on to the send |
| 5, 8 | left skill (`d2d_left_skill`): its node id (`.\UNIT\UnitMode.cpp` line 0x32c) and `FUN_00643ad0` of it ride in the command |
| 6, 7, 9, 10 | the same, line 0x336 |
| 0xc, 0xf | right skill, line 0x33e |
| 0xd, 0xe, 0x10, 0x11 | right skill, line 0x348 |
| 0x13 | type 4 target found by `FUN_00463990` and its unit flag 2 (`FUN_00451f30`) clear: return (the queue is kept); else on |
| any other (0, 0xb, 0x12, 0x14 up) | the assert, string id 0x352, `FUN_00681e09(-1)`: the game exits |

The send is `FUN_00480c10(&command, 0)`; when it answers nonzero, actions 5 to
0x11 (the skill ones) add 1 to the player's stat 0x148 (`FUN_006253b0`,
`FUN_00627260`), and `FUN_00480b40(type, id, third)` goes out. **Verified**:
`tools/emu/unitmode_click_send.py` runs FUN_00480b40 for real over every
`eD2UnitModeAction` (1..20): `UNITMODEACTION_ITEM_INTERACT` (19) sends
nothing; attack (2, 4) and a left/right skill on a unit (6, 7, 9, 10, 13,
14, 16, 17) go out as `FUN_004786a0` (`NET_D2GS_CLIENT_Send_INT_INT`, the
full 32-bit `type`/`id`); every other action (an interact, or a left/right
skill at a coordinate) goes out as `FUN_004785d0`
(`NET_D2GS_CLIENT_Send_SHORT_SHORT`, truncated to 16 bits each) — 60
cases, no disagreement (`uv run python unitmode_click_send.py`). Whatever
happened, a player's queue (+0x150, +0x154, +0x158, +0x15c) is then zeroed
(LAB_00481240, "abandoned" above). An action 1/3 early return, and the 0x13 one,
leave it. What stat 0x148 counts is in missiles.md (the pierce seed), not traced here. d2d: `tick.cpp` and
`travel.cpp` cite the queue (above); the stat bump is ported as `World::apply`'s
`++actor->pierce_idx` for `cmd::UseSkill` (`commands.cpp`; its timing can differ from
the client's); the action 1/3 early return and the assert are not ported.
### The pending interact's fields and its starters (`.\PLAYER\PlrModes.cpp`)

Read in the asm (`tools/emu/disasm.py`, since the decompiler drops the register
arguments), cited, not run. The functions are in the order they are met.

- **`FUN_0057edd0`: may this mode start now?** EAX the unit, ECX the game, EBX the new mode
  (`FUN_00580a70` LAB_00580ad8 to LAB_00580adc). A unit with a cursor item
  (`FUN_0063c1e0` on its +0x60) and a new mode other than 0 and 0x11 gets 0. New mode 0 gets 1
  (the byte table 0x57ee78, case 0). Any other goes by the unit's current mode (+0x10, byte
  table 0x57eea8): 0, 4, 9 and 0x11 refuse (0); 7, 8, 10 and 0xb allow only
  while `FUN_0057ed70` (game frame +0xa8 ≤ `FUN_005415a0(unit, 1)` + 5), or when the new mode
  is 4 or 9; 0xc allows on `FUN_0057ed70` alone; 0xd refuses when unit +4 is 0; 0xf refuses
  when unit +4 is 5; 0x12 asks `FUN_0057ed90` (a skill record byte +0x16 non-zero, else
  `FUN_0057ed70`'s test); the other modes in the table read here allow. d2d's "a skill that comes mid-swing is
  dropped" (net.cpp, net-packets.md) is the 7 / 8 / 10 / 0xb arm. **agrees** as far as that arm
  goes; the other arms are not ported.
- **`FUN_0057eec0(game, unit, mode, flag, skill)`: may the player act?** Two callers
  pass different arguments. It returns 0 for a unit in state 0x36 (`FUN_00639df0`), a null
  unit, and a unit whose current mode (+0x10) is 0 or 0x11 (LAB_0057eed6). Past those, a zero
  requested `mode` argument or a zero `flag` returns 1
  (LAB_0057eff2). **The dispatcher** `FUN_0054d750` calls it as `(1, 0, 0)` (LAB_0054d7b0:
  pushes 0, 0, 1), so for the ids read here, all but 0x14, 0x15, 0x3c, 0x43, 0x66 (no test) and 0x41 (only in
  mode 0x11) the gate is those three tests and nothing else. **Walking is not one of them**,
  so the "a walking player is busy" that network.md, `walk.cpp` and `World::apply`'s comment said is
  not what `FUN_0057eec0` reads (`tools/emu/player_may_act.py` runs it as the dispatcher
  does, every mode 0..0x12); whatever drops a packet from a walking player is in the
  handler, not traced here. **`FUN_00580a70`** calls it as `(mode, 1, skill)` (LAB_00580af6), and
  then it goes on: the unit's skill list (`FUN_00620250`) and the skill record of its head
  (`.\PLAYER\PlrModes.cpp` line 0xb9, `FUN_0045c4b0`); none of either returns 1. The skill
  argument equal to the list head with record +0x2e 0x43 or 0x4c returns 1. With the record's
  +7 flag (`[0x6ce284]`, 0x80 in the decompile) clear: current mode 1 calls `FUN_005809d0`
  and returns 1; a new mode of 7, 8, 10, 0xb, 0xd or 0x12 returns `FUN_0057ed70`; else 0.
  With the flag set: state 0x2a on the unit and its list's stat 0xa4 over a roll
  (`FUN_0045c390(unit seed, 100)`) go to the mode test above; otherwise state 0xf absent returns 1,
  present goes to the same mode test. The roll, the states and `FUN_005809d0` are not ported
  or traced further. d2d's network.md, `walk.cpp` and `World::apply` no longer call a walking player busy (#363).
- **`FUN_006248e0`: the log line.** Does nothing while `0x96ca48` is 0; else
  `FUN_00624780` inside the critical section `0x96ca30` (stubs.md has its callers). Here only
  `FUN_00580a70` uses it, for a target that is gone: `(1, 2, 4, 4, ".\PLAYER\PlrModes.cpp",
  0x83a, "StartModeID: Bad Target")`, then it returns and the mode doesn't start (LAB_00580aaf).
  The pending clear (next item) comes after this return, so it is skipped too. d2d: not ported
  (a log line only).
- **`FUN_00580a70(game, unit, skill, mode, type, id, skip)`: start a player mode at a target**
  (stack [+8] skill, [+0xc] mode, [+0x10] target type, [+0x14] target id, [+0x18] skip;
  LAB_00580a9c on). In order: a null game is a fatal message (0x835, then exit -1); the target
  is found by `FUN_00552f60(game, type, id)` and a miss logs "StartModeID: Bad Target"
  (`FUN_006248e0`) and returns; `FUN_0057edd0(unit, game, mode)` must allow it; and unless `skip`
  is set `FUN_0057eec0(game, unit, mode, 1, skill)` must too (LAB_00580ad8 to LAB_00580afd).
  Then, for a player (`*unit == 0`, data at unit +0x14), **it zeroes the pending interact's
  +0x154 and +0x150** (LAB_00580b10, LAB_00580b21), so any mode start cancels a pending
  interact; a non-zero `skill` is set on the unit (`FUN_00620210`); and the starter at
  `0x6e1744 + mode * 8` is called `(mode, target unit)`. The starter pointer is checked with
  `IsBadCodePtr` (fatal message 0x853) and the mode is not range-checked. d2d:
  `components/game/world/objects.cpp` ports one use of it (the barrel's kick, `FUN_005868a0`);
  the gate and the clear are not ported as such.
- **`FUN_00548a50`: walk at a target and queue the interact.** Registers in: EBX the player
  unit, ECX the game, EDI the target type, ESI its id, and one stack word, the marker (-1, or
  -2 when `FUN_00548b00`'s fourth argument is non-zero: `neg / sbb / add -1`, LAB_00548c12;
  that argument is 0 from C->S 0x13, `FUN_0054aa90`, and the "to cursor" word of 0x16,
  `FUN_0054aad0`, so only an item's walk can carry -2). It calls
  `FUN_00580a70(game, unit, 0, 3, type, id, 0)` (LAB_00548a5d: mode 3, no skill, the gates on),
  then `FUN_00641f20(unit, marker, type, id)` whatever the first returned (LAB_00548a69). That
  one, for a player only, sets **+0x150 = 1** (LAB_00641f37) and `FUN_00460780` stores
  **+0x154 = marker, +0x158 = type, +0x15c = id**. So the pending interact is: flag, marker
  (-1 or -2 as above; 0 or more takes `FUN_00580c20`'s other arm, not traced), the target's
  type and id. `FUN_00580c20` reads them back (LAB_00580e47, LAB_00580e5c: id, then type,
  and `edi == -2` as the fourth argument, LAB_00580e68). A refused first walk still leaves
  the pending set. d2d: `walk.cpp` and `commands.cpp` `walk_to` start the walk, and this
  reads the same, cited from the decompile, no oracle run (the warp's
  `take_warp` is the pending); the marker, the type and the id have no counterpart in d2d's
  fields. **reads the same** on the walk, **DISAGREES** on the limit (below).
- **`FUN_00535060(unit)`: is the player busy?** 0 for a null unit or a non-player. Otherwise 1
  when the interact flag (player data +0x6c, `FUN_00554100`: it also yields +0x68 the type and
  +0x64 the id) is set, or when a cursor item is held (`FUN_0063c1e0` on unit +0x60); else the
  value of player data +0x4c (`FUN_006221a0`). `FUN_00548b00` case 1 starts a talk only while
  it returns 0 (LAB_00548dae), so a player with an open interact, an item on the cursor or +0x4c set
  walks up but doesn't talk. d2d: act5.cpp (the last portal) and `town_npcs.hpp` name it for
  the NPC-visitor and portal tests; d2d's talk start was not checked against the cursor-item and
  +0x4c arms. **not ported** as one gate.
- **`FUN_005678a0`: interacting with a player?** (EDX the unit; the stack word 1 the callers
  push isn't read.) 1 when the unit's interact flag +0x6c is set, its type +0x68 is 0 and the
  unit `FUN_00552f60` finds for +0x64 exists; else 0. `FUN_00548b00` case 0 asks it twice for a
  dead player target (mode 0x11): of the player itself (EDX = the player, LAB_00548e39) and of
  the corpse's owner (EDX = the target, LAB_00548e48). Only when both are 0 does it take
  the corpse (`FUN_0057fb70`); otherwise `FUN_00566e60`.
  `itemmode.md` cites `FUN_005678a0(1)` for the cube too, which this read does not show:
  the test is on the interact's target type 0 (itemmode.md's "cube open" reading wants a
  second look). d2d: the corpse take (`FUN_0057fb70`, `world.hpp`) was not checked for this
  test; **not ported** as one.
- **`FUN_0057fb70(game, taker, corpse)`: take a corpse** (ECX game, EDX the taker, stack the
  corpse unit; `FUN_00548b00` case 0, LAB_00548e56). The corpse must be in state 7
  (`FUN_00639df0`, LAB_0057fb80) and `FUN_0057faf0` must pass: the taker is a player and its
  inventory owner (`FUN_0063d450` on the corpse's +0x60) is the taker, or the owner's unit is found
  and `FUN_0055b300(1)` answers: bit 1 of the owner's relation flags toward the taker, i.e. whether
  the owner has granted the taker "loot" (party.md "The unit relation list (server)"). When the corpse's owner id equals the taker's
  (LAB_0057fbb2), its stat 0xd, if any, is zeroed (`FUN_00627260`) and added to the taker
  (`FUN_0057eab0`): the experience comes back only to the owner. The items pass
  (`FUN_00562f30`, combat.md "Taking one's corpse") follows; when it returns non-zero the
  corpse leaves the taker's corpse list (`FUN_0063d4e0`, inventory.md), gets room event
  `FUN_0061a270`, `FUN_00555600` frees it, and `FUN_00553380(taker)` runs; else
  `FUN_00553380` alone. d2d: `take_corpse_items` reads the same on the experience and the items, cited from the decompile, no oracle run
  (combat.md); the state 7 test and the second-owner arm were not checked.
- **`FUN_00640e90(index)`: an Objects.txt row.** `[0x96d470] + index * 0x1c0` (the table base stored at 0x96d470) for an index from 0
  below the count at `0x96d474`; an index at or over the count shows message 0xdd and one under 0
  shows 0xde (`FUN_00408090`, `FUN_00408a60`), then exits -1 (`FUN_00681e09`). It returns 0 only if the table base at `0x96d470` is itself 0, so
  the `== 0` test after it in `FUN_00548b00` case 2 (message 0x2bf, LAB_00548b3f) can't fire once the table is loaded.
  The stride and base are the ones monster-ai.md already uses. d2d: not ported as a getter.
- **`FUN_00548b00(player, type, id, to_cursor, game)`: interact with a unit, by type.** ECX the
  player, EDX the type (over 5 returns 1; the jump table is at 0x548e8c), stack id, `to_cursor`,
  game. It returns 0 when it acted (or walked), and also when it did nothing (case 1: an NPC 9 or
  more away, or a `FUN_00535060`-busy player within 6), so 0 doesn't show that anything ran; 1 when the target is gone or past the 0x33
  range of `unit_distance`, 3 from cases 2 and 4 only (below). Each walk in the cases below is
  `FUN_00548a50` with the marker above. The cases, from the asm:
  - 0 a player: over 8 walks; within 8 a dead one (mode 0x11) is taken (`FUN_0057fb70`) when
    `FUN_005678a0` is 0 of both; else `FUN_00566e60`.
  - 1 an NPC: first, when its MonStats row +0xd has bits 1 and 2, the talk's start
    (`FUN_0058ec00(0x28)`, town-npcs.md "A talk's start"), before any distance test; over 6 and
    under 9 walks, 9 and over does nothing (return 0, no walk); within 6, a player not
    `FUN_00535060`-busy talks (`FUN_00573020`).
  - 2 an object: its mode (+0x10) over 7 returns 3; within 0x33, a failed reach test
    (`FUN_00622b50(player, unit, 0x804)` blocked, or the object-reach test) walks, else
    `FUN_00584540(2, id)` operates it (it returning 0 gives 3).
  - 4 an item: only in mode 3 (on the ground); under 5 with a clear 0x804 line it is picked up
    (`FUN_00563560` to the bags, or `FUN_0055cf50` to the cursor when `to_cursor`), and a non-zero
    result there returns 3; else it walks.
  - 5 a warp: `FUN_00641530` over 0x32 returns 1, over 4 walks, else `FUN_005550b0`.
  Nothing in it tests that the player is walking. d2d: the cases are ported in `walk.cpp`,
  `net.cpp` and `world/` by the doc rows that cite it (collision.md above, net-packets.md 786,
  drlg.md 1645); the walks of these cases share the two-walk limit above, which d2d lacks. **DISAGREES**
  only there (todo 47).
- **`FUN_00580c20(game, unit)`: a walking or running player's frame** (a function-table entry,
  data refs at 0x732c18, 0x732c1c, 0x732c28, 0x732c5c). In order: `FUN_00553490`; state 0xd on
  gives `FUN_005c9d90`; in mode 3 with no stamina drain `FUN_0057f090` (tick.cpp's stamina);
  `FUN_00554ca0`, which returns 2 once the step update stops (`FUN_00650840` says no more path,
  as "A walk's stop" above) and 1 while it walks; the footstep ring (player data +0xa0 cursor, +0xa4
  time, 20 pairs at +0xa8): a point 25 ms on from the last and over 0x2d squared from the
  previous one is stored (tick.cpp's `footstep`, which reads the same: cited from the decompile, no oracle run). **Only at 2** does the pending
  interact matter, and nothing runs between steps. Unit and its player data present and
  +0x150 set (the arms from LAB_00580e28):
  - marker +0x154 below 0: `FUN_0057f020(1, 0, 0)` (idle), then `FUN_00548b00(unit, type
    +0x158, id +0x15c, marker == -2, game)` (LAB_00580e73). Out of reach that is `FUN_00548a50`
    again, a second walk with +0x150 set again.
  - then, after the marker arm above, **+0x150 is zeroed** (LAB_00580e88; the marker, type and id stay).
  - marker 0 or more: a skill use (`FUN_006439f0`, `FUN_00643860`, `FUN_00580a70`), not traced.
  With +0x150 clear it goes idle (`FUN_0057f020(1, 0, 0)`, LAB_00580e9a) and the walk is over.
  So the server makes at most two walks for one interact packet: the second one's end finds
  +0x150 zero. The client's re-issue ("What keeps a walk to a unit going: the client") is
  separate and has no count, so a click on an NPC or object can add more walks. d2d's `use_warp` / "Short of the target with a step made" (tick.cpp,
  travel.cpp) re-plans from the stop each tick while a step is made, with no count (todo 47;
  how often `use_warp` tests was not re-checked here), so it **DISAGREES** in two ways. A crowded route (a stop short of the
  point with a step made) that takes game.exe two walks takes d2d as many as it needs; a blocked one
  ends d2d's walk at once (above, since #226), one walk where game.exe has two. And d2d tests the
  warp's reach every tick, game.exe only when the packet comes and when a walk ends (above). Read, not run; the oracle phase would
  call `FUN_00580c20` with a stub path.
- **`FUN_005868a0(op)`: a barrel opened** (OperateFn 5; `op` +0 the game, +4 the object unit, +8
  the operator, +0xc its random seed, +0x10 the object id). Only for an object unit that is
  null or in mode 0. A player operator (`*op[2] == 0`) with a skill node
  `FUN_006439b0` finds for skill id 1 (edx = 1, arg -1, LAB_005868d5) kicks it:
  `FUN_00620c10(operator, object)`, then `FUN_00580a70(game, operator, node, FUN_00644360(node),
  2, object id, 0)` (LAB_0058690e: the kick's mode from the skill, target type 2, the gates on). That
  is the one place in this package where `FUN_00580a70` has an object target. Then, for
  anyone: `FUN_00624690(object, 1)`, object flag +0xc4 bit 1 cleared, `FUN_00623830`;
  one draw of the seed (`seed * 0x6ac690c5 + hi`, the 64-bit step of drops.md): `% 10000` with a bit
  set over 0x1fff gives `FUN_005474c0` and `FUN_00582280` (the undead); a second draw `% 100 <
  0x15` the chest round; and event 1 queued (`FUN_005417d0(1, game frame +0xa8 + 1 + (Objects row
  +0xdc >> 8), 0, 0)`, the row by `FUN_00640e90`). d2d: `objects.cpp` (the kick) and
  drops.md "Casket ... Barrel (5)" (the two draws) read the same, cited from the decompile, no oracle run.

### How near the client walks

FUN_00480c80, FUN_00481600's test, for an interact (action 0x13) stops the
walking for a player or monster only at FUN_00622c40: within melee range + 1 (+ 1 for a moving target) with no
0x804 between (LAB_00480d0f), and an object at FUN_00623660 with no 0x804
(LAB_00480cde). The click sends at once only for an NPC within 2. So the
server's reach of 6 (FUN_00548b00) isn't what ends a client's walk to an NPC.

FUN_00622c40(unit, target, extra): a target monster of BaseId 0x102 / 0x105
(FUN_00463860) in reach under FUN_00622870's range + 8; else a distance
(FUN_00641530) of 0 or less, or within range + extra + 1 with FUN_00622aa0
(mask 0x804) clear. The extra is FUN_00622dc0 (FUN_00622d00): a player in
mode 2, 3 or 6, a monster in 2 or 0xf, a vulture (BaseId 0x6e) in 8 or 9; an
object 0, a missile asserts. **Verified**: `tools/emu/melee_reach.py` runs
both for real (7,767 cases, none differ).

d2d's `tick.cpp` walks on to an NPC till `rules::melee_reach` (the player's
weapon range, + 1 while the NPC walks) and still needs the server's reach,
`unit_distance` under 7.

### A walk to a spot just ends

A click on the ground queues nothing, so a player's walk to a spot that runs
out or is blocked stops there: FUN_00650840 stops the same frame (LAB_006508ff),
the server goes idle (FUN_00580c20) and the client goes neutral (FUN_00463390).
FUN_006503f0's re-path at idx ≥ count isn't reached from this.

A held button: FUN_0044efa0 calls FUN_0044c000 each frame (LAB_0044f039) while the
press was taken and 0x7a061c is set. That is FUN_00462d00 with flags 9 (the press has 5),
so FUN_00462a20 and FUN_004625b0. **Verified**: `tools/emu/queued_click.py` runs
FUN_0044c000 for real over all 16 combinations of its three gates
(`ServerLoadingComplete`, `gQueuedPrimaryClick`, `gbMouseButtonEventPending`) and
whether a player unit exists, diffing against a hooked FUN_00462d00: it's called
with clickType 1 exactly when all four hold, never otherwise.
`tools/emu/click_map.py` runs FUN_00462d00 itself for real over every clickType
(0..5, and the unhandled default), the run and stamina flag bits, and the
target-selected/not branch (112 cases, all agree): clickType 1 is flags 9 into
FUN_00462a20 (`WalkToCoordOrUnit`), as the doc says; the other five clicks carry
their own base flags (0 -> 5, 2 -> 0x11, 3 -> 6, 4 -> 0xa, 5 -> 0x12) into
FUN_00462a20's five siblings, with 0x20 added for the run key and 0x40 for a
stamina check that only applies when the run key is held. On the ground FUN_004623c0 and FUN_00461840 path anew
from where the unit stands, with the same FUN_00650840 stop, so a blocked
walk gets a new one the next frame: the stop is the same, the button restarts it. With a queued
action a held button on the ground does nothing (LAB_00462b1d) unless shift is held, so
it doesn't drop a queued interact. On a unit, an object or item
ignores the hold (LAB_00461df2, LAB_00461ee5), a monster needs the press
(LAB_004628cb), and a warp has no such test ("The server's pending interact").
What a held frame sends is in "What a held frame sends" below.

d2d's held button (`input.cpp`, `cmd::Move` not fresh) walks again after a stop too.
Before #231 it also drops a queued interact: `walk_to` clears `interact_npc` before it tests
`fresh`, so a click on a far NPC held past a frame walks there and doesn't talk. Seen in
play, and with real X button events on Warriv: held 120 ms, 2 of 8 talked; 0 ms, 8 of 8.
#231 sends no held walk while the press's interact is queued.

d2d re-plans a walk to a spot that runs out short while it moves ("Short of the
target with a step made", `tick.cpp`), so one click can walk farther than game.exe's
does. A blocked one ends there since #226.

### The mouse-button state words (FUN_0044da40 clears them)

Six words, a column for the left button and one for the right:

| | Left | Right | Written by |
|---|---|---|---|
| Phase: 4 pressed, 8 held, 0x10 up | `0x70f234` | `0x70f2bc` | left: `FUN_0044bf40` (4, 0x44bf43), `FUN_0044c000` (8, 0x44c007), `FUN_0044c060` (0x10, 0x44c060); right: `FUN_0044c180` (4, 0x44c184), 8 at 0x44c2c7, 0x10 at 0x44c342 and 0x44c373 (and 0x44c665) |
| The press was taken (the held-frame test) | `0x7a0650` | `0x7a0654` | left: `FUN_0044bf40` sets 1 (0x44bf65), then the `FUN_00462d00` result (0x44bfb9), `FUN_0044c060` clears; right: 1 at 0x44c249, the result at 0x44c27e. The frame `FUN_0044efa0` calls `FUN_0044c000` / the right-hand hold code at 0x44c2c0 while they are nonzero (0x44f031..0x44f046) |
| Hold started (left) / not traced (right) | `0x7a066c` | `0x7a0670` | left: `FUN_0044c000` sets 1 (0x44c025), `FUN_0044c060` clears; `FUN_0044bfe0` returns it (the belt popup's mouse test, menu.md). Right: set to 1 at 0x44c2dc, 0x44c351, 0x44c387 and 0x44c675, cleared nowhere read; its meaning is not traced |

`FUN_0044da40` (no arguments, 42 bytes) puts both phases back to 0x10 and the other four
words to 0, then tail-jumps to `FUN_00466fe0` (clears 0x7a6a94 / 0x7a6a98, the
cursor's unit target, cursor-units.md): the "no button is down" reset. The same four
words are also stored inline at 0x44e4c0..0x44e4cf and 0x44e565..0x44e577 (values not read).
Its callers in `game-calls.tsv` include `FUN_004b4380` (an NPC menu opening, 0x4b4438) and the
panel openers `FUN_00461250`, `FUN_0047d840`, `FUN_00489e00`, `FUN_0049cf90`, `FUN_004a10e0`.

### What a held frame sends

Read from the code, no oracle or packet log.

- **The gate.** FUN_00462a20 starts with FUN_00462930. The press (flag 4) arms it: 0x7a526c
  = 1 on the ground, 2 with a unit under the cursor (LAB_00462952). A release (flag 0x10)
  clears it. A held frame (flag 8) with it clear does nothing (LAB_00462968). With a unit under the
  cursor: an object, missile or item does nothing (LAB_0046297f); a player, monster or warp
  does nothing while the unit walks or runs, modes 2, 3 and 6 (LAB_0046298e to LAB_00462998).
  So holding on an NPC during the walk to it sends nothing and leaves the queue alone.
- **Which packet.** FUN_004621d0 takes action 3 with flag 0x40, else 1 (LAB_00462236,
  LAB_0046224b), and 4 or 2 for a unit. Flag 0x40 is a run key (FUN_0044bec0) with stat 0xa
  non-zero (LAB_00462dbf). FUN_00461840 passes it to FUN_00481030, whose FUN_00480b40 sends the
  action as the opcode (LAB_004785db). So C→S 0x03 running and 0x01 walking, 5 bytes.
- **How often.** FUN_004623c0, on the ground: a held frame of a walking or running player goes
  on only when 7 frames of 0x7a0498's clock (25 a second) have passed since the last that
  went on, 280 ms (LAB_00462537, LAB_00462542). A stopped player goes at once. Nothing goes if the
  spot is 0x100 or more away on an axis, or under 1 on both (LAB_00462482).
- **How near.** The spot goes out to a least distance (FUN_00474080, LAB_004624d9): a
  press 3, or 5 with flag 0x40; held 4, or 5 with flag 0x40 (LAB_004624c1).

  FUN_004623c0's throttle and clamp above, and FUN_00461840's path-then-send
  glue (LAB_00461879's FUN_00481030 call fires only once PATH_CalculatePath,
  FUN_00649970, finds a path), are run for real: `tools/emu/click_move.py`.
- **The skill branches** in FUN_004625b0, in order; what the fields mean wasn't followed:
  - No unit under the cursor, or a monster FUN_00464820 passes with a skill whose +7 lacks
    0x6ce268: LAB_00462671. There flag 0x20, or flag 2 with FUN_00645460 not 1, casts
    (FUN_004610c0, FUN_00461700, LAB_004626fe); otherwise the walk (LAB_004626c9).
  - A unit and FUN_00643860 passing: a skill with 0x6ce274 in +7 casts (LAB_00462781); a unit
    with bit 2 of +0xc4 and the skill's +7 masks or FUN_00465c60 takes the click by type
    (LAB_004627cc); otherwise flag 0x20 casts (LAB_0046289c).
  - FUN_00643860 failing: LAB_004628a8, as "A walk to a spot just ends" has it.

d2d's `input.cpp` sends `cmd::Move` each drawn frame and a held click re-aims each tick with no
wait, and `walk_to` has no least distance. `Town::send` passes each to `NetGame::move_to`, so a
joined game sends C→S 0x01 or 0x03 each drawn frame (the display's refresh, or `fps_cap`; 60 where none is known)
that aims at a new subtile, against about 3.6. A packet the same as the last one sent within 200 ms
is dropped (`NetGame::send`, as FUN_00478350 does), so a held button on one spot sends about 5 a second.
By reading both are small, a few subtiles of where a walk ends; neither was measured.

## The stamp family (`FUN_0064d060` .. `FUN_0064eba0`, 1.14d)

Cited (read in Ghidra, not run in the oracle). A unit's own bits go on the
room grids in three shapes, set and cleared by twin functions. Each reads
the room's grid through `FUN_0061a010` (`{x0, y0, w, h, .., +0x20 words}`,
"The grid" above) and does nothing when there is no room, grid or word
array. d2d: reads the same as "Rects and units" above, cited from the decompile, no oracle run; none is ported by name.

- **Box, `FUN_0064d060`** (room, a rect `{x0, y0, x1, y1}`, bits): looks up the
  room with `FUN_00463740(room, x, y)` (fastcall: the room and x are in registers the
  decompile hides and are not traced; the stack argument y is rect +4, the rect's
  second field, so which x it passes is unproven) and its grid, splits the rect across rooms with `FUN_0064cdf0` (a piece count; 0
  for an empty rect or no grid), ORs the bits into the first piece with
  `FUN_0064cd70` and, for a count over 1, calls itself again once per
  further piece. Callers: `FUN_0064ea00` (shape 3), `FUN_0064ddf0` (the 3x3
  square), `FUN_0064de30`. The register arguments are hidden in the C; the
  rect layout is read from `FUN_0064cd70` (+0 x0, +4 y0, +8 x1, +0xc y1).
- **Plus, `FUN_0064dc80`** (room, bits; the subtile in registers): five
  blocks in a row, each `FUN_00463740` for the cell, then its room's grid
  (`FUN_0061a010`), then `word[(y - y0) * w - x0 + x] |= bits`: (x - 1, y),
  (x, y), (x + 1, y), (x, y - 1), (x, y + 1). Each cell finds its own
  room, so an edge cell lands in the next room's grid; one with no room
  holding it is skipped. d2d: reads the same, cited from the decompile, no oracle run (this is the plus of the table above;
  `shrines.hpp` `in_doorway` uses it).
- **Point, `FUN_0064ddb0`** (room and subtile in registers, bits): the same
  block once, for the one subtile. `FUN_0064ea90` uses it for the 0x1000 /
  0x2000 footprint on the unit's own subtile. d2d: reads the same, cited from the decompile, no oracle run (collision.md
  table: "0x1000 / 0x2000 on the subtile").
- **By shape, `FUN_0064ea00`** (room, x, y, shape, bits): shape 1 the point
  (`FUN_0064ddb0`), 2 the plus (`FUN_0064dc80`), 3 the box
  (`FUN_0064d060`); any other shape does nothing. This is not the unit
  pattern of `FUN_0064ea90`: `FUN_00648c30` picks it for a path whose owner
  is type 3 (see below). Callers: `FUN_00648c30`, `FUN_00649400`,
  `FUN_005ae520`, `FUN_0064ed20`, `FUN_0064ee70`. d2d: not ported.
- **By unit pattern, `FUN_0064ea90`** (room, x, y, pattern, bits): 0 nothing;
  1 / 3 the plus with `bits` (`FUN_0064dc80`), then, only when `bits` is not
  0, the point with 0x1000 / 0x2000 (`FUN_0064ddb0`); 2 / 4 the 3x3 square
  (`FUN_0064ddf0`) with `bits`, then the plus with 0x1000 / 0x2000; 5 the
  plus only; above 5 nothing. d2d: reads the same as the table in "The grid", cited from the decompile, no oracle run;
  (components/game/gamedata/gamedata.hpp:58, objgroups.cpp:558,
  tests/doorway_cases.inc); the "only when `bits` is not 0" gate is not
  in that table (not checked against d2d's code). The ai.cpp
  `ponytail:` notes (a flight is a line) still stand.
- **Clear by shape, `FUN_0064eba0`** (room, x, y, shape, bits): the twin of
  `FUN_0064ea00`. With a room, shape 1 calls `FUN_0064db70`, 2 `FUN_0064da40`
  and 3 `FUN_0064dbc0` (point, plus, box clears; the same order as the set).
  No room or another shape: nothing. Callers: `FUN_00648c30`, `FUN_00649560`,
  `FUN_005adf10`, `FUN_005b01f0`, `FUN_0064ed20`. The pairing of 1 / 2 / 3 to
  point / plus / box is read from the set side and from `FUN_0064ec10`
  (plus `FUN_0064da40` and point `FUN_0064db70` for patterns 1 / 3); the
  bodies of the three clears were not read here. d2d: not ported.
- **Restamp a path's bits, `FUN_00648c30`** (path, new bits): the path is
  the unit's +0x2c record: room +0x1c, x +2, y +6 (words), the owner
  at +0x30 (its first dword is the unit type), shapes +0x44 and +0x48, the
  bits now stamped +0x4c. With a room it first lifts the old bits: for an
  owner of type 3 `FUN_0064eba0` with the shape at +0x44, otherwise
  `FUN_0064ec10` (the pattern clear, collision.md "Pathing") with the
  pattern at +0x48. Then it stores the new bits at +0x4c and, with a room,
  stamps them: type 3 `FUN_0064ea00` with +0x44, otherwise `FUN_0064ea90`
  with +0x48. So a type 3 owner (a missile) stamps by point / plus / box,
  every other owner, or none (null +0x30), by pattern. Callers: `FUN_0045d160`, `FUN_00460bf0`,
  `FUN_0046ccd0`, `FUN_0046d310`, `FUN_004ae8d0`, `FUN_00649f70`. d2d: not
  ported; no disagreement found. The unit-type and field readings come from
  the decompile only (not run).

## Building a path: FUN_00649d00 and its helpers

Re-check each with `python3 tools/ghidra/fn.py <addr>`. Cited, not run in the
emulator. The register arguments are hidden in the decompile (fastcall /
EAX); read the asm for them.

- **Monster mask, `FUN_00648480`** (unit in EAX): the monster's MonStats row
  (class id at unit +4, table `PTR_DAT_00744304` +0xa78, 0x1a8 a row, count
  at +0xa80) gives the path mask (path +0x50): row +0xd bit 0x40 (flying)
  0x1804; else row +0xc bit 8 (opendoors) 0x3401, else 0x3c01. Only caller
  `FUN_00649d00` (a unit of type 1, not the 0x26 class). d2d: reads the same, cited from the decompile, no oracle run, as
  `UnitShape::monster` (gamedata.hpp:60-69, masks 0x1804 / 0x3401 / 0x3c01
  in that precedence: flying first). A unit of 0, or a class id out of
  range, makes the row pointer 0 and the next line reads address 0xd: no
  null check (bugs.md #58).
- **Town or owned unit, `FUN_0063e860`** (unit in ECX): false unless the
  unit is type 1 with a class id in range and a MonStats row; true when
  the row's +0xd has mask 0x01 (npc) or 0x04 (inTown); else true when the
  unit's +0xc4 is negative (bit 31, a player's pet). Callers
  `FUN_006483a0`, `FUN_00648580`. d2d: reads the same, cited from the decompile, no oracle run (the rule is in the
  "Pattern" paragraph above and `UnitShape::monster(.., town_unit, ..)`);
  the pet bit is set by the summon code listed there.
- **Project a pair, `FUN_00643290`** (int *a, int *b, in place): from the
  old values, a = (a - b) >> 1 and b = (a + b) >> 2 (arithmetic shifts, so
  they floor for negatives). `FUN_00649d00` feeds it the path's position
  (path +0 / +4, 16.16 fixed) shifted right by 11 and stores the result at
  path +8 / +0xc. Callers `FUN_004da350`, `FUN_00648300`, `FUN_00649d00`,
  `FUN_0064fb90`. d2d: not ported; no disagreement found. What the pair
  means on screen was not checked.
- **A path's projected pair, `FUN_00648300`** (unit, &a, &b): a null unit,
  or one for which `FUN_00622d00` (combat.hpp:886) is 0, gives 0 / 0. Else
  with the path (unit +0x2c) +0x28 clear it gives 0 / 0 too; otherwise it
  reads two dwords at path +0x72 and +0x76 (byte offsets, not 4-aligned),
  shifts each right by 11 and runs `FUN_00643290` on them. Sole caller
  `FUN_004c9990`. d2d: not ported. What path +0x28 / +0x72 / +0x76 hold
  was not traced.
- **Pattern from a size, `FUN_00648580`** (unit, SizeX): SizeX over 3
  gives 1; else the table `DAT_006eb3dc` (0, 1, 1, 2). For a type 1 unit
  that `FUN_0063e860` accepts and for which `FUN_00457490` (the MonStats
  flag test, fed the row's +0xd mask 0x02, interact) is 0, a 1 becomes 3 and a
  2 becomes 4. Callers `FUN_00649ca0`, `FUN_00649d00`, `FUN_00649ff0`. d2d:
  reads the same, cited from the decompile, no oracle run:
  `UnitShape::monster` (gamedata.hpp:60) makes the same 0 / 1 / 2
  from the size and `owned()` the 1 / 2 to 3 / 4, with the npc / inTown and
  not interact test at gamedata.cpp:151 and the pet case on the caller's
  side. The register arguments of the `FUN_00457490` call were not read in
  the asm, so "+0xd & 2" is taken from the earlier "Pattern" text.
- **Path x, `FUN_006488c0`** (path): the word at path +2 (the subtile x;
  a position is a 16.16 dword at +0, so +2 is its integer part). A null
  path raises the "Unrecoverable internal error" (`FUN_00408090(0x692)`,
  `FUN_00408a60`, `FUN_00681e09(-1)`). Callers include `FUN_0045adf0`,
  `FUN_0045b1e0`, `FUN_00460830`, `FUN_00649400` (its type 0 / 1 branch).
  d2d: reads the same, cited from the decompile, no oracle run; the subtile x of the stamp (act1-end.md, monster-ai.md
  cite it); the assert line 0x692 is the same as in missiles.md.
- **Path y, `FUN_00648900`** (path): the twin of `FUN_006488c0` for the
  word at path +6 (subtile y); null path raises the same error with line
  0x69d. Callers include `FUN_0045ae20`, `FUN_0045b1e0`, `FUN_00460830`,
  `FUN_00649400`. d2d: reads the same, cited from the decompile, no oracle run (same citations as the x getter).
- **An object's footprint, `FUN_0064de30`** (room, x, y, width, height,
  bits as `FUN_00649400` pushes them; the C reads only stack words 4 to 6,
  the size and the bits, and passes the bits on; how the first three reach
  the callees was not read): a 1 x 1 footprint calls `FUN_0064caf0`
  (one subtile), any other `FUN_0064d060` (the rect box above). Callers
  `FUN_00620a70`, `FUN_00649400` (unit type 2, size from `FUN_00620510` /
  `FUN_006205a0`, oracle: `tools/emu/units_small.py check_unit_size_y`, units.md), `FUN_00649f70`. d2d: reads the same, cited from the decompile, no oracle run, as
  `set_footprint` (gamedata.hpp:1051), which cites it; `FUN_0064caf0` was
  not read here.
- **Stamp a unit, `FUN_00649400`** (unit): by the unit's type (first
  dword). Types 0 and 1 (player, monster): `FUN_0064ea90(room, x, y,
  pattern, bits)` with the room of `FUN_00620bb0`, x / y the path's
  (`FUN_006488c0` / `FUN_00648900` on unit +0x2c) and the pattern the path's
  +0x48. Type 2 (object): `FUN_0064de30` with its size. Anything else
  (missile, item, tile; also an unknown type): `FUN_0064ea00(room, x, y,
  shape from FUN_00620510, bits)`; for types 2, 4 and 5 x / y are read raw
  from the +0x2c field's +0xc / +0x10 instead of the getters. The bits are
  from the function d2d's decompile names `stamp_footprints`. Callers
  `FUN_005a7c20`, `FUN_00622b50`, `FUN_00649970`, `FUN_00649ca0`,
  `FUN_00649d00`. d2d: reads the same as the table at the top, cited from the decompile, no oracle run (units stamp by
  pattern, objects by box; world.hpp:681); a null unit is dereferenced at
  the end (the first test only skips the dispatch), not checked for callers.
- **Re-pattern a unit, `FUN_00649ca0`** (unit): with a path (unit +0x2c) it
  takes the pattern `FUN_00648580(unit, path +0x44)` (the SizeX) into path
  +0x48. If the unit is in a room (`FUN_00620bb0`) it first lifts the old
  stamp (`FUN_00649560(unit, 1)`) and, after the store, stamps the new one
  (`FUN_00649400`); with no room it only stores. So a size or town flag
  that changes on a placed unit moves its footprint. Callers
  `FUN_0045d160`, `FUN_00463c00`, a call at 0x4d946c (not a function
  entry: `python3 tools/ghidra/fn.py 004d946c` reports it missing, and
  game-functions.tsv has no entry there; which function holds the call was
  not traced), `FUN_00554340`,
  `FUN_005c07a0`. d2d: not ported (d2d builds the shape once, at spawn).
- **Add to the room's list, `FUN_0064c350`** (unit, room): `FUN_0064c2c0`
  with a third argument 1. That one (drlg.md:1299) asserts a non-null unit
  (line 0x48) and room (0x49), fails (lines 0x52, 0x57) when
  `FUN_0064bf60(1)` or `FUN_00619f60()` gives nothing, pushes the unit on
  the list the latter returns (the old head goes to unit +0xe8), runs
  `FUN_0064c040(unit)`, and for a player (type 0) or a monster with
  `FUN_006259b0` = 2 calls `FUN_00619ee0(room)`. Callers `FUN_00463bc0`,
  `FUN_00465fd0`, `FUN_004bc720`, `FUN_004c1910`, `FUN_00554850`,
  `FUN_00649d00`. d2d: not ported. The `FUN_0064c2c0` reading is from the
  decompile only; its exact meaning for `FUN_00619ee0` was not traced.
- **Make a unit's path, `FUN_00649d00`** (unit, room, x, y, flag; the C's
  parameter order is shuffled by the register arguments, so the roles are
  read from the stores): allocates 0x200 bytes (`FUN_0040b430`,
  `.\PATH\Path.cpp` line 0x453), zeroes them and links unit +0x2c to it,
  with the owner back at path +0x30. Stores: shape +0x44 (`FUN_00620510`),
  pattern +0x48 (`FUN_00648580` on it), x and y as 16.16 at +0 / +4 (the
  subtile x 0x10000 + 0x8000: the cell's centre), the room at +0x1c, the
  entered-subtile list at +0x1d8 / +0x1da with count +0x1d4 = 1, velocity
  +0x7c = 0x800, +0x93 = 0. Then by unit type:

  | unit | own bits +0x4c | mask +0x50 | other |
  |---|---|---|---|
  | 0 player | 0x80 | 0x1c09 | type 7 (`FUN_00648cf0`), steps +0x91 = 0x49, +0x92 = 0x46 |
  | 1 monster | 0x100 | `FUN_00648480` (class 0x26: 0x804 and the path's +0x48 is forced to 5) | path type +0x3c = 2, flags +0x34 & 0xfff800ff, steps +0x91 = 0xe |
  | 3 missile | 0 | 0 | type 4 (`FUN_00648cf0`) |

  With a room it then stamps the unit (`FUN_00649400`), links it in
  (`FUN_0064c350`), and sets path +0x8 / +0xc by `FUN_00643290` from the
  position >> 11; a non-zero last argument ORs 0x10 into the flags +0x34.
  Other unit types get the zeroed fields only. A monster whose path type
  were 8 or 0xb would hit the internal error (lines 0x871 / 0x872), and
  one whose owner type is 0 line 0x861; the memset path type is 0, so these
  are not reachable here. Callers `FUN_00460bf0`, `FUN_004ae8d0`,
  `FUN_004cd0a0`, `FUN_00554850`, `FUN_00554a30`. d2d: reads the same, cited from the decompile, no oracle run, on what
  collision.md and monsters.hpp:909 already cite (player mask 0x1c09, steps
  0x49, type 7); monster steps 0xe, +0x92 = 0x46 and the missile row are
  not ported, no disagreement found.

## The client's command switch and the run stop (work package 00463260, 1.14d)

Cited from the decompile and `tools/emu/disasm.py` (no oracle run); re-check with
`python3 tools/ghidra/fn.py <address>`. Addresses in call order of `FUN_00461250`.

- **`FUN_0044bf00`** and **`FUN_0044bf10`**: no arguments, each returns one global
  dword (`[0x70f234]` and `[0x70f2bc]`). `FUN_00461250` command 8 (the player dying)
  tests bit 8 of each when `FUN_00467a10` is the dying unit: with neither set it calls `FUN_00466fe0`, with either set
  `FUN_0044da40` then `FUN_00467a70`. Their other caller is `FUN_004aff60`. What the
  two globals hold is not traced. d2d: not ported.
- **`FUN_00461010`** (unit in ESI, no stack arguments; only caller `FUN_00461250`, commands 8
  and 9, each right after `FUN_00480e70`): the client's end-of-life reset. In order:
  `FUN_004c1e30()`; `FUN_00649f70(unit, 0)`; `FUN_00464810(unit)`; unit `+0xc4 &= ~4`;
  if `FUN_00639df0(unit, 7)` (state 7 on the unit) is 0, unit `+0xc4 &= ~2`;
  `FUN_0046ece0` (`gfx_cof_list_trim`) and `FUN_0046f020(unit, 0)` (`gfx_layer_flags_set`);
  last `FUN_00627260(unit, 6, 0, 0)`: SetStat, stat 6 (`kLife`, `d2s_items.hpp`) = 0. So
  commands 8 and 9 zero the unit's life on the client as well as set its mode. d2d: not
  ported (`Fight::die` sets `kLife` to 0 itself, `setup.cpp`); no disagreement found.
- **`FUN_00461180`** (unit in EAX, skill slot in ESI = `{skill id, item guid}`, one stack
  argument, `RET 4`; callers `FUN_00461250` commands 0x15 and 0x16): re-asks a queued skill
  at the walk's end. `FUN_006439b0(unit, slot[0], slot[1])` finds the unit's node; none:
  return 1 with the slot untouched. Else `FUN_004610c0(arg)` (node pointer in EBX, unit in
  EDI) and 0 returns 0 (the walk's end is refused). Otherwise slot[0] = `FUN_00643ce0` of
  the node as 4610c0 left it (it can swap in the Attack node, `FUN_006439f0`) and, when
  that id differs from the old one, slot[1] = -1 (the item guid is dropped). Returns 1.
  d2d: agrees with skills.md "Attack's entry kept in the slot when it fell back"
  (`strike.cpp:300`, `Fight::ask`); the guid dropped when the id changes is not stated
  there, and d2d's side of it (`attack_charge`) was not compared.
- **`FUN_004647d0`** (unit in ECX; callers `FUN_00453910`, `FUN_0045d4b0`, `FUN_0045d9b0`,
  `FUN_0045db20` and `FUN_00461250` command 7): unit `+0xc4 &= ~0x10000`, stored; then
  `+0xc4 |= 4` unless the unit is a monster (`*unit == 1`) whose MonStats2 flag 9
  (`FUN_004638a0(unit[1], 9)`, the same flag test names.tsv `monster_flag_4638a0` is
  built on) is clear. So a player, object or missile always gets bit 4; a monster only
  with flag 9. What flag 9 and bit 4 mean is not traced. d2d: not ported.
- **`FUN_00620210`** (stdcall `(unit, node)`, `RET 8`; callers `FUN_00461250`, `FUN_00463260`,
  `FUN_00463390`, `FUN_004aff60`, `FUN_004c6140`): a null unit is fatal (assert id 0x653,
  `FUN_00681e09(-1)`), else `FUN_00643990(unit[+0xa8], node)`, which stores `node` at the
  skill-list header's `+0x10` (nothing for a null header). act1-end.md already reads the
  call in `FUN_005dead0` (`FUN_006439b0` node, then this) as setting the current skill; the
  client callers pass 0, so they clear it. d2d: agrees with act1-end.md:335; no d2d
  function does this (not ported).
- **`FUN_00463dd0`** and **`FUN_00463de0`**: byte-identical bodies, no arguments, each returns
  `[0x7a6a70]`, the client's own player unit (skills.md:1793 and :1806 already read both so;
  `FUN_00463dd0` is called by `FUN_00461250`, `FUN_00463260`, `FUN_00463390` and the five
  at `0x44bf40`, `0x44c000`, `0x44c060`, `0x44c180`, `0x44c2c0`; `FUN_00463de0` by
  `FUN_00463260`, `FUN_00460830`, `0x453470`, `0x453b30`, `0x45ad60`, `0x45d780`, `0x45d9b0`).
  They are the "is this the local player" test the client paths below use. d2d: agrees
  (skills.md:1793, :1806; `apps/whisper/whisper.cpp:45`).
- **`FUN_004648f0`** (unit in ECX; callers `FUN_00462370`, `FUN_00462a20`, `FUN_00463180`,
  `FUN_00463260`, `FUN_004ad970`): the unit's target unit, checked first. It calls
  **`FUN_00480420`** (same ECX): a null unit is fatal (assert id 0x26), a unit without a path
  (`+0x2c`) fatal (0x2a); if the path has a target (`FUN_00648bf0`), it reads the target's
  type and GUID (`FUN_00648bb0`), looks the unit up again (`FUN_00463990`, ECX = GUID, EDX =
  type) and, when that is not the stored pointer, clears the target (`FUN_00648b90(path,
  0)`). Back in 004648f0 a null path is fatal again (id 0x946), and it returns
  `FUN_00648bf0(path)`: the target pointer, 0 for none. So it reads as none only when the
  lookup result differs from the stored target pointer (or no target was set); a dead target
  that is still registered would look up as the same pointer and is not cleared, and what the
  lookup does for freed memory that was reused is not traced. d2d: reads the same, cited from the decompile, no oracle
  run, on the use (`setup.cpp:474`, `do_sounds`:
  the target's tgtsound, none without one); d2d holds the target as an index (`attack_mon`)
  and has no GUID re-lookup (not ported).
- **`FUN_00461250`** (the client's command switch; fastcall `(ECX command, EDX unit)` plus two
  stack arguments, `RET 8`: a command record `{a, b, c}` of dwords (zero allowed for 7) and a
  flag; callers `FUN_00463180`, `FUN_00463260`, `FUN_00463b80`, `FUN_0046f950`,
  `FUN_00480c10`). Prelude, in order: `FUN_00643a00(unit, 0)` swaps the skill list header's
  `+0x14` to 0 and, if it was set, `FUN_004743d0(old)`; unless the command is 0x13,
  `FUN_004611f0(unit == FUN_00463dd0())` and `FUN_00620210(unit, 0)`; a unit in mode 0x13
  with the flag 0 returns 1 here, with the flag set `FUN_00650590(path)`;
  `FUN_00648dc0(path)` (`RestoreVelocityAndPathType`); then the flag slot is overwritten with
  the walk mode (6 in town, `FUN_0061ab00(FUN_00620bb0(unit))`, else 2) and `ebx` holds the
  neutral mode (5 or 1). The switch (jump table 0x4616a4, byte index 0x4616e4; commands 3-5,
  0xa-0x11 and above 0x19 are fatal, assert id 0x432):

  | command | does | mode set |
  |---|---|---|
  | 0 | `FUN_00480780(unit, a = type, b = GUID)`: path to a unit | walk mode |
  | 1 | `FUN_004804a0(unit, a = x, b = y)`: path to a spot | walk mode |
  | 2 | `FUN_00480930(word a, b)`; returns 1 | none |
  | 6 | `unit+0xb0 = c`; `FUN_004804e0(word a, word b, 1, 0, 0)` | 4 (GH) |
  | 7 | `FUN_00464820(unit)`, `FUN_004647d0`, `+0xc4 \|= 2`; with a record `FUN_00480ef0(unit, a, b, result)` | neutral mode |
  | 8 | the player dying (see collision.md above): `FUN_00456300(1, 0)` for the local player, `unit+0xb0 = c`, `FUN_00467a10` branch on the two globals, `FUN_004804e0(..., 0, 0, 0)`, `FUN_00461010`, `FUN_0045c470` | 0 (DT) |
  | 9 | `FUN_00461010` | 0x11 (DD) |
  | 0x12 | `unit+0xb0 = c`; `FUN_004804e0(..., 1, 0, 0)` | 9 (BL) when `FUN_0063c8f0(unit+0x60, 0)` is non-zero or `FUN_0064f380` answers 0xd |
  | 0x13 | `unit+0xb0 = c`; `FUN_00480d20`, `FUN_0063a400(unit)`, `FUN_004cc5b0(unit, mode, 1)`; `FUN_004804e0(..., 1, 0, 0)` | 0x13 (KB); `FUN_004cc5b0` gets 3 when `FUN_0063a400` is non-zero, else 0x13 |
  | 0x14 | `unit+0xb0 = c`; path type 0xb (`FUN_00648cf0`), distance 5 (`FUN_00648e40`), `FUN_00648ad0(path, a, b)`, `FUN_00649970(path, unit, 0)` | 0x13 |
  | 0x15, 0x16 | `FUN_00461180(0)`, then `FUN_004c6f40` (0x15) / `FUN_004c6eb0` (0x16) with `a` | none |
  | 0x17 | `FUN_004804a0(unit, a, b)` | 3 (RN) |
  | 0x18 | `FUN_00480780(unit, a, b)` | 3 (RN) |
  | 0x19 | `FUN_004804e0(word a, word b, 0, 0, 0)` | 0xd (S1) |

  **`FUN_00480ef0`** (fastcall unit ECX / x EDX, then y and a result flag on
  the stack — command 7's record fields `a`, `b`, `result`): compares `x`,
  `y` against the unit's current path position (its path struct `+0xc` /
  `+0x10` directly for type 2 or 4/5, else `d2d_path_get_x` / `_get_y`, or
  0 with no path) and sets a "moved" flag when either delta is over 1.
  Nothing happens (`FUN_00648750(path, 0)`, a stop) unless that flag is
  set and, for a monster (type 1), `FUN_0046c140()` also answers nonzero.
  Moved: a player (type 0) that isn't `FUN_00463de0()` (the local player)
  with a nonzero `result` instead sends `FUN_004804e0(y, 2, 0, 0)` and
  returns (no recompute); a monster (type 1) tags the record's third field
  1; every other case leaves it 0. All of those (and the local player's
  own case) end in `FUN_00480c10(&{x, y, tag}, 0)`, the same path-recompute
  call commands 0 and 1 use. `FUN_0046c140`'s own body and what `result`
  means for a player aren't traced.

  The modes are set by `FUN_00480e70(unit, mode)` (command 0x13's through `FUN_00480d20`). A command 0, 1, 0x15..0x18 whose walk or
  ask fails ends at `FUN_00460830(unit, neutral mode, 1)` and returns 0; the rest return 1.
  Commands 0x17 / 0x18 are the same two walks as 1 / 0 but set mode 3 (RN). d2d: agrees with what collision.md:242-304,
  `strike.cpp:300` and `world/tick.cpp:441` already read (commands 0, 1, 2, 8, 0x15, 0x16, the
  neutral at a failed walk); d2d has no single command switch (not ported as a function).
  Modes 0x12 / 0x13 / 0x14 and the 0xa-0x11 gap, and what the record fields `a`, `b`, `c`
  mean for 6, 0x12-0x14, are read from the code only, no packet traced here.
- **`FUN_004804a0`** (fastcall unit ECX / x EDX, y on the stack; commands 1
  and 0x17 of the table above): `FUN_00648ad0(path, x, y)` (already
  verified, `path.md:76`) stores the point as the path's target and clears
  its "has run" flag (+0x58); then `FUN_00649970(path, unit, unit+0xc4 bit
  0x15)` (already verified, `ai.cpp`) calculates the path to it, the third
  argument being the unit's own "already in position" bit. Same shape as
  `FUN_00480780` (collision.md "Out of reach" above, already verified with
  an oracle) but for a point instead of a unit lookup — no target to
  re-resolve, so no `FUN_00463990` / `FUN_00648b90` pair first. d2d: not
  ported as a function; the walk-to-a-spot path in `follow_path` (ai.cpp)
  covers the same `FUN_00648ad0` / `FUN_00649970` pair, no disagreement
  found.
- **`FUN_00480e70`** (`UNITMODE_SetAnimMode`, fastcall unit ECX / mode EDX) has two
  siblings sharing its shape: clear unit flag 0x40 (`+0xc4 &= ~0x40`), call
  `FUN_00624690` (`SetupUpdateFlag`); a true answer loads the cof
  (`FUN_00470610`, unit, 0) and (`FUN_00480e70` only) shows the hit overlay
  and plays the mode sound (`FUN_00480d20`, `FUN_004cc5b0(unit, mode, 0)`); a
  false answer skips the cof load. `FUN_00480e30` (`UNITMODE_SetModeAndLoadCof`,
  mode on the stack, unit in ESI) is the same two calls without the overlay or
  sound, and has no caller anywhere in game.exe (0 xrefs; `stubs.md`).
  `FUN_00480ec0` (`UNITMODE_SetAnimModeSilent`, same fastcall shape as
  `FUN_00480e70`) is also the silent pair, and has exactly one caller:
  `MonsterMessage`'s unique-death case (monster base id 0xbe) when the
  monster is already in mode `Skill2`, so its death doesn't replay the
  overlay/sound a second time. **Verified**: `tools/emu/unitmode_setanim.py`
  runs all three for real, hooking `SetupUpdateFlag` to answer both 0 and 1
  and the three downstream calls to record whether they ran, over a null and
  a real unit — 36 cases, no disagreement
  (`uv run python unitmode_setanim.py`).
- **`FUN_00480d20`** (`UNITMODE_ShowElementalHitOverlay`, fastcall unit ECX /
  mode EDX): a no-op for a null unit or any type but player (0) or monster
  (1); for a player, modes 0/4 are a hit and 0x13 a maybe-hit, for a monster
  0/3 and 0xd; every other mode is a no-op. On a hit, the high nibble of
  `unit+0xb0` (`hit_class`) picks the overlay via `FUN_00470390`
  (`GFX_AddOverlayToUnit`), but only when the low nibble is also set: 0x10
  `OVERLAY_doubledamage1`, 0x20 `OVERLAY_fire_hit`, 0x30 `OVERLAY_ice_explode`,
  0x40 `OVERLAY_lightning`, 0x70 `OVERLAY_bash`, 0x80 `OVERLAY_hit_thorns`,
  0x90 `OVERLAY_sanctuaryknockback`; any other high nibble is a no-op.
  **Verified**: `tools/emu/unitmode_hit_overlay.py` runs it for real over
  every (unit type, mode, `hit_class`) combination that exercises each
  branch, hooking `GFX_AddOverlayToUnit` to record the overlay id it got —
  792 cases, no disagreement (`uv run python unitmode_hit_overlay.py`).
- **`FUN_00463260`** (unit in EAX, no stack arguments; only caller `FUN_00463390`, which calls it
  for a unit whose mode `+0x10` is 3, RN): the client's run stop. Nothing happens unless the
  unit is `FUN_00463de0()`, the local player, and `FUN_00625480(unit, 10, 0)`, stat 10
  (`kStamina`, `d2s_items.hpp`), is 0. Then the walk is re-issued, in one of two ways:
  - **With a target** (`FUN_004648f0(unit)` non-zero): the command record is `{target[0]` (type),
    `target[3]` (GUID)`}` and `FUN_00461250(0, unit, &record, 0)` runs (command 0: the prelude,
    `FUN_00480780`, then mode 2 or 6 in town; a failed path goes to neutral 1 or 5).
  - **Without** one: x, y = the path's target words (`FUN_00648a00`, `FUN_00648a10`); the same
    prelude as `FUN_00461250` (`FUN_00643a00` / `FUN_004743d0`, `FUN_004611f0`, `FUN_00620210(unit,
    0)`, `FUN_00648dc0`) inline, then `FUN_004804a0(unit, x, y)`; success `FUN_00480e70(unit,
    walk mode)`, failure `FUN_00460830(unit, neutral mode, 1)`. A unit in mode 0x13 skips all of it.
  So a run that hits stamina 0 on the local client becomes a walk to the same target, not a
  stop; other players' runs are not touched here. d2d: not ported as such. The effect is
  covered by `World::stamina_frame` (`world/tick.cpp:182-223`: stamina 0 turns the run into a
  walk, 0x580c5f / `FUN_0057f090`) and by the `running && stamina > 0` tests at
  `tick.cpp:147` and `:175`; d2d has no client re-path (the walk simply continues in the walk
  mode), no disagreement found. Dead code in game.exe: after the `FUN_00648bf0` test the
  `target == 0` arms (record `{6, -1}`) cannot run.
- **`FUN_00480810`** (fastcall ECX unit, 268 bytes; the client's per-unit
  frame dispatch — no caller read here): the local player, a null unit, or
  a unit whose `+0xc4` bit 0x17 is clear takes the step below; any other
  unit (not the local player, bit 0x17 set) skips it for the "frozen" arm
  at the end. The step: `FUN_00627460(unit, 0)` (the missile freeze list,
  already verified), `FUN_004da350(unit)` (already cited at collision.md
  "view/camera" above), `FUN_00470c30(unit)` (overlay/timer tick, already
  verified), then by unit type `*unit`: 0 `FUN_00463390` (below), 1
  `FUN_004b13a0` (monster), 2 `FUN_004bdff0` (object), 3 `FUN_004d2c70`
  (missile), 4 `FUN_004c1ad0` (item) — none of the last four read here.
  After the type's own step: `unit+0xc4` bit 0x15 clear calls
  `FUN_00463990(unit)`, set calls `FUN_004639b0(unit)` (both already
  ported, `ctrl_click.hpp:17`, and both just call `FUN_00463940`, a
  unit-by-id lookup with an assert on a type mismatch); a nonzero answer
  calls `FUN_0045fa40()` (already cited, net-packets.md:52, a buffered
  net-message dispatch). The "frozen" arm (not the local player, bit 0x17
  set): `unit+0xc8` bit 5 set clears it and `+0xc4` bit 0x17, and calls
  `FUN_004786a0(unit+0xc)`; then `+0xc4` bit 0x15 set calls `FUN_00465f00()`
  and returns, skipping the type dispatch and the lookup/dispatch pair
  above entirely. Not traced: what `+0xc4` bits 0x15/0x17 and `+0xc8` bit
  5 mean, and `FUN_00463940`'s own lookup list. d2d: not
  ported as a function; the per-type dispatch is `World`'s own per-unit
  tick (`world/tick.cpp`), no disagreement found on the player arm
  (`FUN_00463390` below).
- **`FUN_00463390`** (fastcall ECX unit; the only caller is `FUN_00480810`): the client's
  per-frame step for a player, driven by a table of 12-byte rows indexed by the unit's mode
  (`+0x10`): `[0x711e00 + mode*12]` (a class: 1 = a walk-like mode, 2 = a mode with a
  skill), `[0x711e04 + ...]` (how the frame counter `+0x44` steps: 0 `FUN_00623e00`; 1
  the same unless `FUN_006217c0` says over; 2 over sets `+0x44 = +0x48 - 0x100` and clears the
  word at `+0x4c`, else steps; 3 sets `+0x44 = 0x100` and clears that word) and
  `[0x711e08 + ...]` (1 keeps the `FUN_004807c0` result as "over"; 2 or 3 re-test
  `FUN_006217c0` after the step; anything else skips the end handling). Order: class 1:
  `FUN_004807c0`, for the local player `FUN_00460de0`, then `FUN_004caf60`; class 2 with a
  skill list: `FUN_006446a0` flag 1 asks `FUN_004807c0` again (when it says over,
  `FUN_00644660(flags | 2)` and `FUN_004c68f0`; the local player `FUN_00460de0`), and
  `FUN_004c68f0` is called for a unit with `+0xc4` bit 6 clear and a `+0x4e` byte of 1, 2 or
  3; then the frame step; mode 0x12 with `+0xc4` bit 14 `FUN_00470610`. When "over": a unit in
  mode 0x13 takes the `FUN_00461250` prelude, sets `+0xb0 = 0`,
  `FUN_00480e70` and `FUN_004804e0(path y, 1, 0, 0)`; a
  type-0 unit whose data (`+0x14`) has `+0x154` set only sets `+0x150 = 1`; any other takes the
  prelude and `FUN_00464820`, `FUN_004647d0`, `+0xc4 |= 2`, `FUN_00480e70`. Then a set path
  flag 2 is cleared (`FUN_00460e70` for the local player), `FUN_00648640(path)`, mode 3 calls
  `FUN_00463260`, `FUN_0063a340` non-zero calls `FUN_004c72f0`, and `FUN_00460f10` last. The
  table's own values were not dumped here (combat.md:390 reads row 4 as `[0, 1, 2]`). d2d:
  agrees with combat.md:390 and `setup.cpp:430` (the mode ends a tick before the server);
  the rest of the step is not ported as a function.
- **`FUN_004807c0`** (fastcall ECX unit, 67 bytes; only caller `FUN_00463390`
  above, class 1 and class-2-over): `FUN_00480420(unit)` first (the stale
  path-target clear, already traced above). Then two state checks, both
  `FUN_00639df0(unit, state)` (already verified, `ai.cpp:1222`): state 0xd
  set calls `FUN_004e3ab0(unit)`, state 0x16 set calls `FUN_004e2d40(unit)`
  — both read a queued skill's Skills.txt range fields (0x15e / 0x15f) and
  the current node, and end in `FUN_004cd540` (not read here; their own
  effect, a quest or sound trigger tied to standing still with a skill
  queued, is not traced). Last, `FUN_00650840(unit, FUN_0044db10())`
  (already verified, `ai.cpp:357` — the real per-frame path-motion step,
  `DAT_007a04c4` the second argument) runs the walk; `FUN_004807c0` answers
  true ("over") exactly when that step answers 0 (no more steps queued
  this frame). d2d: not ported as a function; `FUN_00650840`'s own port
  (`move_frame`-adjacent code in `world/tick.cpp`) already covers the walk
  step itself, no disagreement found; the two state/trigger calls (0xd,
  0x16) are not ported, not traced further here.

### The per-type dispatch targets (monster, object, missile, item)

`FUN_00463390` above is the client's per-frame step for a player (unit type 0). The
client's own per-unit-per-frame dispatcher that picks it, and the three siblings
below for the other unit types, is `FUN_00480810` (traced separately, batch 6, not
in this file as of this writing); its own write-up names these four as its
"every other type" targets without reading them. Traced here, each fastcall ECX
unit, no stack arguments, in the order `FUN_00480810` calls them:

- **`FUN_004b13a0`** (573 bytes, `004b13a0`..`004b15dd`, plain `RET`): the
  monster's per-frame step. A null unit, a non-monster (`*unit != 1`), a unit
  with state 1 active (`FUN_00639df0(unit, 1)`) unless `FUN_00464820()` is
  zero, or a mode (`+0x10`) of 0x10 or over, each return with nothing done.
  Otherwise `piVar2 = FUN_004af400()` (no visible argument; a global, not a
  per-unit table row) gates the rest: a null result returns. `FUN_004af4c0()`
  runs next, then, only when `*piVar2 == 1`, `local_8 = FUN_004807c0(unit)`
  (the frame-step helper `FUN_00480810`'s own write-up names, shared with
  the player's class-1/2 step) is called and its result kept.
  Mode 1 (`+0x10`) additionally gates on `FUN_00649210` not running something,
  then adjusts a target lookup through `FUN_00648bf0` / `FUN_00648bb0` /
  `FUN_00463990` / `FUN_00648b90` (the same clear-stale-target pattern
  `FUN_004648f0`/`FUN_00480420` already read above, lines 1071-1084) or calls
  `FUN_00649210(unit)` when there's no usable target. The rest, gated on
  `FUN_00451f80(unit[1])` (a monster-class lookup; a null result skips to the
  end) being non-null, is a block that: for a monster whose MonStats row
  flag 2 is set, that isn't flagged by `FUN_004638a0`, in mode 1, passing
  `FUN_00451f30`, not class 0x219/0x21a/0x21b, not the local player
  (`FUN_00463dd0`), within 7 of the local player
  (`d2d_monsters_unit_distance`) and some further gates
  (`FUN_0045c390() < 10`, a path present) — plays something through
  `FUN_00649ef0` with two values from `FUN_0045ae20`/`FUN_0045adf0`,
  `0`. After that: `FUN_004b1280()` and a second switch on `piVar2[2]`
  (1: keep the earlier `FUN_004807c0` result, 2/3: this new result, 4: 1
  only when both are non-zero and `FUN_006217c0(unit)` is also non-zero),
  then `FUN_004af2e0()`, `FUN_004ade60()`, `FUN_00648640(path)` when there's
  a path, and, when that switch's result was non-zero, `FUN_004af6a0()`.
  A monster of class 0x210 with MonStats2 flag `+0x31` bit 0 set and
  `FUN_00451f30()` non-zero clears that bit and plays another sound-like
  call (`FUN_0045ae20(2, 0)`, `FUN_0045adf0`, `FUN_00466730`). Finally
  `FUN_0063a340(unit)` non-zero calls `FUN_004c72f0()`, then
  `FUN_004cb460(unit)` and `FUN_004caf60(unit)` (both already verified
  elsewhere: `object_sounds.hpp`, `fight/setup.cpp`, `world/tick.cpp`,
  `world.hpp`; `FUN_004bdff0` below ends the same way, the shared
  object/unique-boss "ambient sound" tail; the missile and item steps
  below do not call either). **Not traced**: `FUN_004af400`'s own
  global and what `piVar2[0]`/`piVar2[1]`/`piVar2[2]` mean beyond the two
  switches read here; `FUN_00649ef0`'s own effect; the MonStats flag 2 /
  flag 9 (`FUN_004638a0`) semantics (also unresolved at line 1054 above).
  d2d: not ported as a function; no disagreement found, nothing here was
  compared against a d2d equivalent.
- **`FUN_004bdff0`** (fastcall ECX unit; 0x30 bytes, `004bdff0`..`004be01c`,
  plain `RET`): the object's per-frame step. `FUN_004bcbb0(unit)` runs
  first — the object's own cof/frame-rate advance, already fully ported
  (`apps/d2d/ingame.cpp:169,175`, `apps/d2d/world_view.cpp:39`,
  `components/game/gamedata/gamedata.hpp:119`, `components/game/world/tick.cpp:53`).
  Then it reads the object's Objects.txt ClientFn (`+0x1b4`, objects.md:574
  already names this field and its dispatch) off the object's row (unit
  `+0x14` data, dereferenced once more to the row); when ClientFn is over 3
  it calls `FUN_004bdee0(unit)` (the ClientFn dispatch table at `0x7277f0`,
  19 entries, already cited at objects.md:560) and a zero result returns
  with nothing else done. Otherwise (ClientFn 3 or under, or `FUN_004bdee0`
  non-zero) it tail-calls `FUN_004cb460(unit)` (the same already-verified
  ambient-sound step `FUN_004b13a0` ends with above). d2d: the cof/frame
  step and the sound tail are both already ported at the citations above;
  the ClientFn gate itself has no d2d equivalent found (not ported as a
  function).
- **`FUN_004d2c70`** (fastcall ECX unit; 0xf1 bytes, `004d2c70`..`004d2d61`,
  plain `RET`): the missile's per-frame step. A null unit, an out-of-range
  or missing Missiles.txt row (`unit+4`, the same `PTR_DAT_00744304+0xb64`
  table at `0x1a4` per row missiles.md already reads throughout) end in a
  tail call to `FUN_004cd390(unit)` (a cleanup/removal-sound step: gated on
  the row's `+0x12` and unit `+0xc4` bit 0x15, calls `FUN_00621150`,
  `FUN_00465f00`/`FUN_00465ee0`; not traced further here). With a row:
  `FUN_004cd1c0()` runs first when the row's `+0x131` byte is set (reads
  unaffected `EBX` directly rather than a passed argument — a sound-chance
  check against the row's `+0x130` byte and the unit's own seed state,
  `+0x44` flags `0x300` and `+100`). Not in town (`FUN_00620bb0`): the
  unit's path position is read the same way `FUN_00480ef0` already reads
  it above (type 2 or 4/5 direct from the path struct, else
  `d2d_path_get_x`/`_get_y`), then `FUN_00463740(0, y, x)` — not the same
  call shape as the near-list walk missiles.md:81 reads for this address
  elsewhere, so this may be a different overload; not resolved here. Then,
  unless the row's `+5` byte bit 4 is set, `FUN_0061ab00` of that result
  non-zero also tail-calls `FUN_004cd390` and returns. Otherwise it reads
  the row's `+8` short and, when it's a valid index under `DAT_0072a34c`,
  tail-jumps through the pointer table at `0x72a398` — the same client
  missile-callback table missiles.md:161 already names two slots further
  in (`0x72a3a0`, `FUN_004d33e0` and the entry at `0x004d3460`); an
  out-of-range index
  is a no-op return. So `FUN_004d2c70` is the entry into that table, not a
  table member itself: the room/position gate before every missile client
  callback runs. d2d: not ported as a function; the table's own callbacks
  are the ones missiles.md's "Client missile callbacks" section already
  covers (not re-read here).
- **`FUN_004c1ad0`** (fastcall ECX unit; 0x8d bytes, `004c1ad0`..`004c1b5d`):
  the item's per-frame step. A null unit is fatal (assert id 0x348); a mode
  (`+0x10`) over 6 is fatal too (id 0x362; both share the tail
  `FUN_00408090`/`FUN_00408a60`/`FUN_00681e09(-1)`, no return). Otherwise a
  switch on the mode:
  - 0, 1, 2, 4, 6: clear the unit's selectable bit (`+0xc4 &= ~2`, the same
    bit line 475 above names "selectable"); return.
  - 3: set the selectable bit (`+0xc4 |= 2`); `FUN_0046e100(_, 0x46)` —
    which, read from its own decompile, ignores the unit it's passed and
    instead walks a global list (`FUN_00463e10()+0x2c`, linked by `+0xa4`)
    for an entry whose `+4` field is `0x46`, returning 1 if found — a
    non-zero result returns (no overlay this frame); zero falls into the
    shared overlay block below.
  - 5: `FUN_006217c0(unit)` zero calls `FUN_00621310(unit)` and returns; a
    non-zero result sets the mode (`+0x10`) to 3 and falls into the same
    overlay block, skipping the 0x46 check mode 3 has.
  - the shared overlay block: `FUN_00470390(unit, 0x46, kind=6, 8, 0, 0, 8,
    0)` (`GFX_AddOverlayToUnit`, already ported at
    `components/rules/overlays.hpp`; its own comment there notes kind 6 is
    the only kind that "waits" rather than looping). Overlay.txt row 0x46's
    name was not read here. Then return.
  d2d: the overlay call itself is already ported (`overlays.hpp`); the
  mode switch and the selectable-bit toggle have no d2d equivalent found
  (not ported as a function).

## A mercenary's level-up skill install (`FUN_004b1090`, 1.14d)

`FUN_004b1090` (fastcall ECX unit, EDX a struct pointer — ushort id at `+0`,
dword at `+4`; 339 bytes per `game-functions.tsv`, `004b1090`..`004b11e9`,
plain `RET`) has exactly two callers, both gating it the same way: unit
creation (`FUN_00466360`, already verified — `components/game/gamedata/gamedata.hpp:151`,
`components/game/world.hpp:641`/`:647`, `docs/research/re/bugs.md:68`,
`docs/research/re/monsters.md:535`) and `FUN_00478bb0`, each calling it only
when the new unit's class word is `7` and a further check (`FUN_00463990()`
/ `FUN_00478e40()`) is non-zero — the mercenary/hireling unit class.

A null unit or struct pointer returns with nothing done. Otherwise it sets
the unit's `+0xc4` flags `0x202`, reads the struct's ushort id and the dword
at `+4`, and — through `FUN_00663750` and two already-identified 0-argument
global readers, `FUN_0044dcc0` (`is_expansion`, `DAT_007a04f4`,
`docs/research/re/char-panel.md:190`) and `FUN_0044dcd0` (the difficulty
byte, `DAT_007a060c`, `docs/research/re/quests.md:537`) — looks up the
mercenary's current-level Hireling.txt row (`FUN_006562f0`, already cited
`docs/research/re/merc-ai.md:106`). With a row: the level difference
(`local_10`, row `+0x1c`, clamped to 0 or more) drives a 6-entry walk of the
row's skill table at `+0x78`. Each valid skill id (bounds-checked against
`PTR_DAT_00744304+0xba0`) gets a level from the row's per-entry bytes at
`+0xc0`/`+0xc6`/`+0xcc` (`(byte[+0xcc] * local_10 >> 5) + byte[+0xc6]`,
capped at `0x20`) and is installed with `FUN_00647280(unit, skill_id,
level, 0, ".\UNIT\Monster.cpp", 0x9c8)` — the same `AddSkill`-shaped call
already read at `docs/research/re/pet-ai.md:276`/`:290` and
`docs/research/re/unit-player-trace.md:26` — followed by
`FUN_006439f0`/`FUN_00644340` on the row's `+0xc0` byte, the same
entry-lookup/level-set pair `docs/research/re/pet-ai.md:272` already names.
After the loop, `FUN_00524a30`, `FUN_0063e810` (both already verified,
`docs/research/re/datatbls.md:715`, `docs/research/re/monsters.md:312`) and
`FUN_004c1c10` (already verified, `docs/research/re/net-packets.md:383`)
run once more.

**A decompiler artifact, resolved from raw disassembly, not the
pseudocode:** Ghidra's decompile shows the call to `FUN_00463dd0` taking
four arguments (`FUN_00463dd0(uVar5,uVar3,uVar2,puVar8)`), but
`FUN_00463dd0` itself decompiles as a bare global read with no parameters
(`undefined4 FUN_00463dd0(void) { return DAT_007a6a70; }`), and its own
disassembly confirms that: two instructions, `MOV EAX,[0x007a6a70]` then a
plain `RET` — no `RET n`, and neither instruction reads or writes the
stack. (It is already cited
elsewhere as the local-player accessor: `apps/whisper/whisper.cpp:45`,
`docs/research/re/char-panel.md:257`, `docs/research/re/skills.md:1793`/
`:1841`.) Reading `FUN_004b1090`'s own disassembly at `004b10dc`-`004b10e3`
shows the four values are pushed once, immediately *before* the call to
`FUN_00463dd0`, and nothing pops them across that call or the next one
(`CALL 0x0044dcd0`, also a bare-`RET` 0-argument leaf) — they sit on the
stack untouched until `FUN_006637f0`'s own call three instructions later.
`FUN_006637f0`'s prologue (`006637fb`-`00663814`) reads exactly four stack
words, at `EBP+0x8`/`+0xc`/`+0x10` and a fourth further in for its
`param_6` output pointer, on top of its two fastcall register args — six
total, matching its own decompiled signature
(`undefined4 __fastcall FUN_006637f0(p1,p2,p3,p4,p5,p6)`). So the
pseudocode's `FUN_00463dd0(uVar5,uVar3,uVar2,puVar8)` is Ghidra visually
attaching `FUN_006637f0`'s already-staged stack arguments to the wrong,
argument-less, intervening call; `FUN_00463dd0` reads its global and
returns, consuming none of them. Its actual return value (the local-player
pointer) is what becomes `FUN_006637f0`'s own second fastcall register
argument (`EDX`), after passing through `FUN_0044dcd0`.

Not traced: `FUN_00663750`'s own body; the Hireling.txt row layout beyond
the `+0x1c`/`+0x78`/`+0xc0`/`+0xc6`/`+0xcc` fields read here; `FUN_006637f0`'s
own stat-roll formulas past the two register arguments resolved above
(its own file already shows `d2d_merc_hireling_row`'s output consumed by
name elsewhere — not re-read here).

d2d: not ported as a function; no disagreement found. The row lookup
(`FUN_006562f0`) and the skill-install call (`FUN_00647280`) are already
cited as ported/verified at the locations above; the level formula and the
6-entry loop itself have no d2d equivalent found.
### Warming a sequence's graphics row by row: `FUN_004afb60` (batch 14)

`FUN_004afb60` (0x5e bytes, `004afb60`..`004afbbd`, plain `RET`): no
formal parameter in the decompile (Ghidra shows `unaff_EDI`), but `EDI`
is the unit — confirmed in raw disassembly at its only two call sites,
both in `FUN_004aff60` (`004b0c86`/`004b0d06`: `MOV EDI,ESI` right before
`CALL FUN_004afb60`, `ESI` being that function's own unit throughout),
reached only after `FUN_00643860(ESI,EDI)` returns mode `0xe` (a player
or monster in mode `0xe` reads its sequence, `units.md:47`).

Body: `FUN_006633b0(unit)` (the unit's sequence, `units.md:40`) into a
local, `FUN_006633d0(unit)` (the sequence's length in rows, `<< 8`,
`units.md:40`) into the loop bound, then for every row position `0`..
`rows-1` (each shifted `<< 8`): `FUN_00663410(seq, row<<8, row<<8,
&out0, &out1, &out2, &out3)` — the same four-output sequence-row reader
`units.md:39` already names (mode/frame/direction/event) — stores only
`out0` into unit `+0x10` (the same frame-data field `units.md:47` reads
as `+0x10 + frame`), then calls `FUN_00470610(unit, 0)` (`gfx_unit_mode_load`,
`names.tsv:173`) before advancing to the next row. Net effect: walk every
row of the unit's current sequence once, loading (caching) each row's
mode's graphics in turn; `unit+0x10` is left holding the last row's frame
value, not restored.

**Not traced**: `out1`/`out2`/`out3` (direction/event) are read into
locals and discarded; why the row index is passed as both the "from" and
"to" argument to `FUN_00663410` rather than one incrementing value.

d2d: not ported as a function; no disagreement found, nothing here was
compared against a d2d equivalent.

Re-check: `python3 tools/ghidra/fn.py 004afb60`; disassemble
`004afb60`..`004afbbd` and `004b0c60`..`004b0d20` to confirm `EDI` holds
the unit at both call sites and that the mode-0xe gate precedes them.

### A player-only overlay-message gate on state 0xb8: `FUN_004afc10` (batch 14)

`FUN_004afc10` (0xcf bytes, `004afc10`..`004afcde`, plain `RET`): no
formal parameter either — the decompile shows `unaff_ESI`, and `ESI` is
the unit the same way as `FUN_004d2c70` above ("reads unaffected EBX
directly rather than a passed argument"): its single call site
(`004b05b9`, also in `FUN_004aff60`) has `ESI` already holding that
function's unit for its whole body, with no register move immediately
before the call.

A null unit, or a unit whose type (`+0x0`) isn't `1` (player), returns
at once — monsters, objects, missiles and items never reach the rest of
the function. For a player: `FUN_00639df0(unit, 0xb8)` (the state test
`collision.md` already cites throughout, e.g. line 1082/1316) gates two
message shapes, both built the same way — an `0x5c`-byte record zeroed by
`FUN_00681ef0`, header word `0x2020`, both pointer fields set to the
unit — then fed to `FUN_004cd540` (not read further here; already flagged
above as "not read here; their own") twice with two different ids:

- state `0xb8` **off**: `FUN_00451fe0(unit+4)` (the class-row lookup by
  class id, `cursor-units.md:116`/`:127`) reads that row's byte `+0xb`,
  adds `0x10f` for the first id; the second id is the fixed `0x113`.
- state `0xb8` **on**: `FUN_00639df0(unit, 1)` (state `1`) nonzero skips
  both messages entirely; zero sends the fixed ids `0x1ab` then `0x1aa`.

**Not traced**: states `0xb8`/`1`'s names (no row found for either in the
states tables read so far); the string text behind the four sstr ids;
what `FUN_004cd540` does with this `0x5c`-byte record shape downstream.

d2d: not ported as a function; no disagreement found, nothing here was
compared against a d2d equivalent.

Re-check: `python3 tools/ghidra/fn.py 004afc10`; disassemble
`004afc10`..`004afcde` and `004b0590`..`004b05c0` to confirm `ESI` is the
unit at the call site and the state-0xb8/state-1 gate order.
### A fifth monster-mod dispatch slot: `FUN_004ade80` (batch 13)

Right after the four mod-dispatch slots `names.tsv` already names
(`monster_mod_dispatch_0`..`_3`, `004ade40`..`004ade7f`, each
`FUN_004add90(unit, 0, N)`): `FUN_004ade80` (0x17 bytes,
`004ade80`..`004ade97`, fastcall ECX unit / EDX a forwarded value,
confirmed in disassembly) is a fifth slot with a different gate — a null
unit **or a zero EDX** skips the call, where the first four pass a
literal 0 unconditionally — plus the unit being a monster (`*unit == 1`):
`FUN_004add90(unit, EDX, 4)`. Its one caller is `FUN_004cd540` (the
client's generic missile/event sequencer, already cited
`collision.md:1294`, `lighting.md:378`, `missiles.md:142`,
`unit-sounds.md:322`, `ponytail:0515ee35`). `FUN_004add90` itself (cited,
`names.tsv:235`-`238`, `unit-sounds.md:322`) is not re-read here: it runs
the table at `0x724e28` by `mod * 5 + event`. Not traced: what value
`FUN_004cd540` forwards as EDX, or whether it is itself a mod or an event
index into that table.

d2d: not ported as a function; no disagreement found.

Re-check: `python3 tools/ghidra/fn.py 004ade80`; disassemble
`004ade80`..`004ade97` to confirm the ECX/EDX gate (the first four slots
check only ECX).

### Two more monster type-data accessors: `FUN_004ae0a0` and `FUN_004ae170` (batch 13)

Two more accessors beside the monster-data cluster `units.md:236` already
names (`FUN_004ae080`'s `+0x28` masked word to `FUN_004ae110`'s `+0x30`
modifier list) — `structs.tsv`'s `monster_data` rows at `0x28`
(`ai_control`, `ai-control.md:3`) and `0x30` (`state_gate`,
`ai-control.md:185`) sit in the same span.

- **`FUN_004ae170`** (0x17 bytes, `004ae170`..`004ae187`, fastcall ECX
  unit, plain `RET`): a null unit, a non-monster unit, or a null
  monster-data pointer (unit `+0x14`) returns 0; otherwise returns the
  **address** monster-data `+0x34` (`LEA EAX,[ECX+0x34]`, confirmed in
  disassembly — it hands back a pointer into the struct, not a
  dereferenced value, the same shape `ai-control.md:8`'s `FUN_0058f000`
  uses for control `+0x38`). Its two callers, `FUN_0046ce40` and
  `FUN_0046d110` (both already ported, `apps/d2d/town/ambient.cpp:195` and
  `:173`, `critters.md:47`-`48`), are outside the UNIT module; what either
  does with the returned address is not traced here.
- **`FUN_004ae0a0`** (0x40 bytes, `004ae0a0`..`004ae0e0`, fastcall ECX
  unit / EDX a 16-bit value, confirmed in disassembly): the same
  null/non-monster/null-monster-data gate, then two writes to the
  monster-data record:
  1. `*(uint16_t *)(monster_data + 0x28) = EDX` — the same offset
     `structs.tsv` documents as the 4-byte `ai_control` pointer
     (`ai-control.md:3`), here written as only its low 16 bits. The two
     claims are not reconciled here: whether `ai_control` is still
     unallocated at this call site, so the overwrite is harmless, was not
     traced.
  2. the unit's own `class` (`unit +0x4`, `structs.tsv`) is checked
     against the MonStats row count (`[0x744304] +0xa80`); in range,
     monster-data `+0x0` is set to the MonStats row address
     (`[[0x744304] +0xa78] + class * 0x1a8` — `+0xa78` is a slot holding
     the table's own base pointer, dereferenced before the class offset
     is added, confirmed against `FUN_005735a0`'s identical form; the
     same table `monsters.md:10` and `collision.md:947` already name,
     both still with the single-bracket form); out of range, monster-data
     `+0x0` is zeroed.

  Callers: `FUN_00466360` (the client's monster-colour/creation step,
  already cited `bugs.md:68`, `monsters.md:554`), `FUN_004ae8d0` and
  `FUN_004aedd0` (both already cited `collision.md:936`/`:1063`,
  `missiles.md:1018`). What each passes as the EDX word is not traced.

d2d: not ported as a function for either; no disagreement found.
`structs.tsv` gets a new `monster_data +0x0` row for the MonStats-row
cache this commit adds.

Re-check: `python3 tools/ghidra/fn.py 004ae0a0 004ae170`; disassemble
`004ae0a0`..`004ae187` to confirm the EDX word write at `+0x28` and the
`LEA`-only getter at `+0x34`.

### Replaying a path-target action: `FUN_004ae1d0` (batch 13)

`FUN_004ae1d0` (0x37 bytes, `004ae1d0`..`004ae207`, no ECX/EDX move at
entry at all — the unit arrives in **ESI** already, confirmed in
disassembly, a naked convention like `FUN_004656c0`'s above): a null
unit, a non-monster unit, or a mode (unit `+0x10`) that is 0, `0xc`, or at
or past `0x10` skips everything and returns. Otherwise, in order:
`FUN_00465bf0(unit, 0)` (untouched here; its own body was not read in
this commit), `FUN_00480490(unit)` (0xa bytes, `00480490`..`0048049a`,
fastcall ECX unit, plain `RET`: one call, `FUN_00650590(unit +0x2c)` — the
unit's `path` pointer, `structs.tsv`; `FUN_00650590` is already cited
`collision.md:1137` as part of `FUN_00461250`'s command-0x13 handling,
not re-read here), then a tail call to `FUN_00480e70(unit, 1)`
(`UNITMODE_SetAnimMode`, already described `collision.md:1194`). So for a
monster in modes 1-0xb or 0xd-0xf it plays something at the unit's path
target and sets mode 1 before returning through `SetAnimMode`'s own tail.
Its one caller, `FUN_004aff60` (the client's monster unit-command
handler, verified, already cited `collision.md:1077`/`:1104`,
`net-packets.md:71`), was not re-read for which commands reach it.

d2d: not ported as a function; `SetAnimMode` itself is already verified
(`tools/emu/unitmode_setanim.py`); no disagreement found.

Re-check: `python3 tools/ghidra/fn.py 004ae1d0 00480490`; disassemble
`004ae1d0`..`004ae207` to confirm the naked ESI convention and the two
calls in order.

### A per-mode MonStats2 flag test: `FUN_004afbc0` (batch 13)

`FUN_004afbc0` (0x42 bytes, `004afbc0`..`004afc02`, fastcall ECX unit,
plus a second, naked **EAX** struct pointer — confirmed in disassembly,
not shown in the decompile's signature at all): a null unit or null EAX
struct returns 0; otherwise reads the struct's `+4` field as a presence
gate and its `+0` field as a bit index (`< 0x10`). With both set, it
calls `FUN_00451fe0(unit's class, unit +0x4)` — the MonStats2 row lookup
`unit_size.hpp` already names (MonStats row `+0x18` word → MonStats2
index, bounded, `* 0x134 + [0x744304] +0xa90`) — and, with a row, tests
the bit index of the row's own byte array starting at `+0x104`
(`(bit >> 3) + 0x104`, masked by the same `DAT_006ce268` bit-mask table
`FUN_004638a0` (`montypes.hpp:62`) uses for its own, different flag byte
at `+4`). A missing row, or either gate failing, returns 0.

Its two call sites (both in `FUN_004aff60`, confirmed in disassembly) sit
right after `FUN_00480e70(unit, mode)`, with ECX the same unit and EAX
the same struct pointer whose `+0` field supplied that mode — so the bit
tested is the mode just set, and the struct's `+4` field is the same
value `FUN_00480e70`'s own call there read. Which MonStats2 flag byte
`+0x104` is (a Monstats2.txt column, if any) is not traced; `montypes.hpp`'s
ported flag byte is `+4`, a different one.

d2d: not ported as a function; no disagreement found (nothing in
`montypes.hpp` reads `+0x104`).

Re-check: `python3 tools/ghidra/fn.py 004afbc0 00451fe0 004638a0`;
disassemble `004afbc0`..`004afc02` and `004b0280`..`004b02b0` (one call
site) to confirm the EAX struct and the post-`FUN_00480e70` placement.

### A per-event client hook: `FUN_004ae4f0` (batch 13)

`FUN_004ae4f0` (0x13c bytes, `004ae4f0`..`004ae62c`, fastcall ECX unit,
plus a naked **EAX** event code — confirmed in disassembly, the decompile
hides it as `in_EAX`): two jump tables (`0x4ae62c` for codes up to
`0x158`, `0x4ae6f4` for codes `0x159` and up) switch on the event code;
out-of-range falls through to a bare `RET`. In call order:

- `0x158`: reads the local player (`FUN_00463dd0`, already cited
  `collision.md:1110`/`:1112`/`:1136`) and advances a 64-bit counter at
  the **player's** own `+0x20`/`+0x24` fields — the same two `structs.tsv`
  names `seed_low`/`seed_high` for a monster's drop seed (`drops.md:129`),
  here advanced for the player by a different step (`low * 0x6ac690c5 +
  high`, with the carry folded in, not `FUN_00552df0`'s step; not
  reconciled with `drops.md`'s reading of those fields as fixed once at
  creation). The new low dword's low byte, masked `& 0x3f`, becomes the
  **unit's** own path direction via `FUN_006488a0(path, byte)`
  (`path.md:68`, "direction and new direction both = argument & 0x3f"),
  bracketed by `FUN_00649030(path, 1)` then `FUN_00649030(path, 0)`
  (`path.md:93`, "a zero argument sets flag 0x40, non-zero clears it").
- `0x96`: stores the unit pointer at global `0x7c0c5c`. What reads it
  back is not traced.
- `0x9d`, `0x9e`, `0x13f`: `FUN_00649190(unit, 5)` then `FUN_00648ce0(unit
  +0x2c, 0)` (both already verified, `path.md:97`, `path.md:85`).
- `0x14b`: with a local player (`FUN_00463dd0`), `FUN_00620c10(unit,
  player)` (already verified, `ai-control.md:203`, `collision.md:758`,
  `missiles.md:318`, `units.md:229`) then `FUN_00465bf0(unit, 0)`
  (untouched here, not re-read).
- `0x19d`, `0x23a` (two codes, one shared body): chains `FUN_0045ae20` →
  `FUN_0045adf0` → `FUN_004cdb40(unit, 0x188, ...)` (the same pair used
  elsewhere as a two-value input, `collision.md:1335`/`:1343`, their own
  meaning not resolved there either; `FUN_004cdb40` already cited
  `missiles.md:905`, `stubs.md:18`); the exact argument plumbing between
  the three calls was not fully resolved from the disassembly here.
  `0x19d` only, a non-null result also runs `FUN_00621ce0(result, unit)`
  (already ported, `components/game/ai.cpp:1460`/`:2075`,
  `monster-ai.md:426`).
- `0x1f1`, `0x1f2`, `499`, `500` (four codes, one shared body): tail-calls
  `FUN_004ae400` (untouched, 0xef bytes, not read in this commit).

Not traced: the event-code numbering itself (whether these are net-packet
opcodes, animation events, or another space), `FUN_004ae400`'s own body,
and what `FUN_00463dd0`'s zero/nonzero split at `0x158` and `0x14b` means
for a client with no local player yet. Its two callers (`FUN_004ae8d0`,
`FUN_004aedd0`) are already cited (`collision.md:936`/`:1063`,
`missiles.md:1018`) and were not re-read for which codes they pass.

d2d: not ported as a function; no disagreement found — every callee
above already has its own d2d comparison cited.

Re-check: `python3 tools/ghidra/fn.py 004ae4f0`; disassemble
`004ae4f0`..`004ae62c` to confirm the EAX event code and the two jump
tables.
### A monster's own `+0x2c` field, read for its name: `FUN_00463810` (batch 12)

`FUN_00463810` (0x17 bytes, `00463810`..`00463827`, fastcall ECX the
unit, plain `RET`, confirmed in disassembly: `TEST ECX,ECX` first, no
EDX or stack use): a null unit, a unit whose own type (`+0`) isn't 1
(monster), or a monster whose `+0x14` data block (units.md "Type data" —
the per-type `+0x14` block; for a monster this is the already-cited
monster-data cluster `FUN_004ae080`..`004ae3a0`, units.md:236) is null
each return 0; otherwise it returns that data block's own `+0x2c` field
(a dword, not read elsewhere in `units.md`'s monster-data list, which so
far only names `+0x28` and `+0x30`). Its one caller is `FUN_00464a60`
(the unit-name text, already cited `cursor-units.md:402`,
`npc-menu.md:228`, `npc-talk.md:69`, `automap.md:241`): reached from the
name path for a monster specifically, so `+0x2c` of the monster-data
block is read while building a monster's display name. Not traced:
`FUN_00464a60`'s own use of the returned value (what it means there, or
whether it's an id, a pointer or a count was not read here), and the
monster-data block's own layout beyond this one field. d2d: not ported
as a function; no d2d equivalent was checked against this field.

Re-check: `python3 tools/ghidra/fn.py 00463810 00464a60`; disassemble
`00463810`..`00463827` to confirm the ECX-only fastcall and the type/null
gates.

### Moving a unit and the client's own control unit toward each other: `FUN_00464420` / `FUN_004644e0` (batch 12)

Both sit right after the name getter above and both call the already
cited `FUN_00649ef0` (not traced, line 1351 above) to re-path one unit
toward a point taken from another unit's current path. Both are reached
from the same caller, `FUN_004b6dd0` (the server's 0x28 quest-info
packet handler, already cited `net-packets.md:540`,
`unit-sounds.md:435`, `collision.md:365`) — one call each, back to back
at `004b6f23` and `004b6ffa`, confirmed in disassembly.

- **`FUN_00464420`** (0xb2 bytes, `00464420`..`004644d2`, fastcall ECX
  the unit, plain `RET`; confirmed in disassembly, `MOV
  EBX,dword ptr [ECX + 0x2c]` at entry): fatal (assert id `0x7f3`) if
  `unit`'s own path (`+0x2c`) is null. Otherwise it reads the global
  client control unit `DAT_007a6a70` (already read as "the client's own
  player unit", line 1111 above) and its mode (`+0x10`): modes 2 or
  4..5 take the control unit's static path x/y straight from `+0xc`/
  `+0x10`; any other mode resolves them through `d2d_path_get_x`/
  `d2d_path_get_y` on the control unit's path. It then calls
  `FUN_00649ef0(unit's path, x, y, 0)` — so `unit` re-paths toward
  wherever the client's own control unit currently stands.
- **`FUN_004644e0`** (0xc4 bytes, `004644e0`..`004645a4`, fastcall ECX
  `param_1`, plain `RET`): the mirror. It only runs when
  `DAT_007a6a70` is set **and** its own mode (`+0x10`) is 1 or 5 (not
  resolved here); fatal (assert id `0x802`) if the control unit's path
  is null. With the same x/y resolution as above but read off
  `param_1` instead, it calls `FUN_00649ef0(control unit's path, x, y,
  0)` — so the client's own control unit re-paths toward `param_1`.

So the pair make the client's own unit and a given unit (likely the
quest NPC the 0x28 packet names) turn to face or close on each other when
a quest-info dialog opens; `FUN_004644e0`'s mode-1/5 gate is the only
asymmetry between the two directions. Not traced: `FUN_004b6dd0`'s own
body beyond these two calls, what modes 1 and 5 are here, and
`FUN_00649ef0`'s own effect (already not traced, line 1351). d2d: not
ported as a function; no d2d equivalent was checked against this pair.

Re-check: `python3 tools/ghidra/fn.py 00464420 004644e0 004b6dd0`;
disassemble `00464420`..`004644d2` and `004644e0`..`004645a4` to confirm
the ECX-only fastcall and the mode-1/5 gate in the second.

### Finding the room under a point, near list first: `FUN_00465420` (batch 12)

`FUN_00465420` (0x9c bytes, `00465420`..`004654bc`, fastcall ECX an x,
EDX a y, confirmed in disassembly: `MOV ESI,ECX` / `MOV EDI,EDX` at
entry): `(0, 0)` returns 0 without any lookup. Otherwise, when the
client control unit `DAT_007a6a70` (line 1111 above) is set, it takes
that unit's own room (`FUN_00620bb0`, already cited lines 1009/1022) and
calls `FUN_00463740(room, x, y)` — the same three-register signature
already read at line 884 ("the room and x are in registers", confirmed
again here: ECX the room, EDX the x, y on the stack) — returning that
room immediately on a hit (the control unit's own near list, per
`FUN_00463740`'s write-up). Missing a control unit, or that lookup
coming up empty, falls through to `FUN_00619da0(DAT_007a0634, x, y)` (the
client act, already cited `gdi-driver.md:124`): a wider, act-level room
search. If that also returns nothing, it's fatal (assert id `0x13c`),
after formatting a log line naming the control unit, the room found so
far, the act and the x/y (`"Unable to find room for unit...
hControlUnit:%8x hUnitRoom:%8x ghAct:%8x nX:%d nY:%d"`). Its six
callers (`FUN_00465fd0` the unit-creation helper already cited
`units.md:235`; `FUN_00466360` the monster-colour/creation step,
`monsters.md:554`; `FUN_00466200` the 0x59 assign-player handler,
`net-packets.md:122`; `FUN_004654c0` the unit's hard teleport,
`automap.md:95`; `FUN_004c2540`/`FUN_004c26f0`, not read here) all feed
it a unit's new or reported x/y to resolve its room. Not traced:
`FUN_004c2540`/`FUN_004c26f0`'s own bodies. d2d: not ported as a
function; no d2d equivalent was checked against this room-resolution
order.

Re-check: `python3 tools/ghidra/fn.py 00465420 00463740 00619da0
00465fd0 00466360`; disassemble `00465420`..`004654bc` to confirm the
ECX=x/EDX=y fastcall and the `FUN_00463740(room, x, y)` register setup.

### A per-class finishing step after a monster is made: `FUN_004656c0` (batch 12)

`FUN_004656c0` (0x195 bytes, `004656c0`..`00465855`, no ECX/EDX move at
entry at all — confirmed in disassembly, the first instruction is `CMP
EAX,0x14d` — a naked dispatch target like `FUN_004dc210`'s callees in
PR #437, reached with **EAX** a class-like code and **ESI** the new unit
already in place, no stack arguments, plain `RET`): its one caller is
`FUN_00466360` (the client's monster-colour/creation step, already
cited `monsters.md:554`, `superuniques.md:19,28`, `bugs.md:49`), which
sets EAX/ESI before the call (confirmed in disassembly, `MOV
ECX,ESI`/`MOV EAX,...` just before `CALL 004656c0` at `004665b2`). It
switches on the code:

- `0x14d`, `0xf2`/`0x2c0` (shared tail) and `0x21f` each call the
  already-identified `FUN_00470390` (`GFX_AddOverlayToUnit`, ported
  `components/rules/overlays.hpp`, first cited line 1416 above) on the
  new unit with a different overlay id and kind each time (`0xba`/kind
  2; `0x7a`/kind 3; `0x113`/kind 3).
- `0x154`..`0x157` (a 4-entry jump table) each call `FUN_004da000(unit)`
  then `FUN_004da1d0(unit, dx, dy, 0)` with one of the four diagonal
  offsets `(-0x20,-0x20)`, `(-0x20,0x20)`, `(0x20,0x20)`, `(0x20,-0x20)`
  — a diamond of four positions around the new unit.
- `0x15f`..`0x161` call `FUN_00470390(unit, 0, kind=4, class-0xb0, 0,
  0)` — the class code itself, offset by `-0xb0`, is passed through as
  one of the overlay call's own arguments.
- `0x196` calls `FUN_004da000`/`FUN_004da1d0`/`FUN_004da200`/
  `FUN_004da2a0`/`FUN_004da6e0` in sequence (a fuller per-unit setup than
  the diamond above).
- `0x1a1`/`0x1a2` (with ESI non-null) and `0x209` each tail-call
  `FUN_00470390`/`FUN_0046bff0` with no extra class-specific values.
- any other code returns with nothing done.

Not traced: `FUN_004da000`/`FUN_004da1d0`/`FUN_004da200`/`FUN_004da2a0`/
`FUN_004da6e0`/`FUN_0046bff0`'s own bodies, and what each class code and
overlay id/kind pair means for the monster class it's reached for (not
resolved to specific Monstats rows here). d2d: the overlay call itself
is already ported (`overlays.hpp`); the per-class dispatch and the
diamond-offset spawns have no d2d equivalent found (not ported as a
function).

Re-check: `python3 tools/ghidra/fn.py 004656c0 00466360`; disassemble
`004656c0`..`00465855` to confirm the EAX-code/ESI-unit naked convention
and the jump table at `0x465858`.

### Playing something at a unit's path target: `FUN_00465bf0` (batch 12)

`FUN_00465bf0` (0x6b bytes, `00465bf0`..`00465c5b`, fastcall ECX
`param_1` the unit, EDX `param_2` a value forwarded unchanged, confirmed
in disassembly: `MOV EBX,EDX` / `MOV ESI,ECX` at entry): calls
`FUN_00480420` on `param_1` first — unchanged ECX at the call site, so
the already-cited `FUN_00480420(unit)` (line 1119 above, "a unit without
a path" clears its stale target). Fatal (assert id `0x946`) if
`param_1`'s own path is null. Then `FUN_00648bf0(param_1's path)` (the
path's target pointer, already cited line 1124 above) — nothing happens
when there's no target, or the target is `param_1` itself. Otherwise:
re-reads `param_1`'s path (fatal `0x7e2` if it's gone by now — the same
double-check pattern `FUN_00465f20` below shows), then calls
`FUN_0045ae20(param_2)` and `FUN_0045adf0(that result)` — the same pair
already cited at line 1335 above — and finally `FUN_00649ef0(target,
y-ish value, x-ish value, param_2)`. So it plays or triggers something
at a position derived from `param_2`, aimed at `param_1`'s current path
target, when that target isn't `param_1` itself. Its nine call sites
span several already-documented systems: `FUN_004aff60` (the client's
post-move-packet stat/mode hook, `net-packets.md:258,769`,
`video-drivers.md:219`, two call sites), `FUN_004af6a0` (already cited
line 1340 above), `FUN_004c6eb0` (already cited line 1155 above), plus
`FUN_004ae1d0`, `FUN_004ae4f0`, `FUN_004c8ed0`, `FUN_004c9b40` (the
skill picker, `skills.md:577,1849` etc.) and `FUN_004efdf0`, none of
which were opened here. Not traced: `FUN_0045ae20`/`FUN_0045adf0`'s own
unit-to-value mapping beyond what line 1335 above already reads,
`FUN_00649ef0`'s own effect (not traced, line 1351), and what each of
the nine callers passes as `param_2`. d2d: not ported as a function; no
d2d equivalent was checked against this helper.

Re-check: `python3 tools/ghidra/fn.py 00465bf0 00480420 00648bf0`;
disassemble `00465bf0`..`00465c5b` to confirm the ECX/EDX fastcall and
the "target != self" gate.

### The unit-table re-register check on creation: `FUN_00465f20` (batch 12)

`FUN_00465f20` (0xa1 bytes, `00465f20`..`00465fc1`, no visible
parameters at all — confirmed in disassembly, `MOV ESI,EAX` at entry
with no preceding ECX/EDX move: the unit arrives in **EAX**, like
`FUN_004656c0` above, but with its own stack frame here (`PUSH
EBP`/`MOV EBP,ESP`)): calls `FUN_00463a50()` first (no visible
argument either; not traced). When that returns 0, it looks up the
unit's own type/id in the client's hash table A (`0x7a5e70`, already
described in `units.md`'s "Client-side units" section): **EAX** the
bucket (`id & 0x7f`), **EDX** the table (`0x7a5e70 + type * 0x200`),
**ECX** the id, stack the type — the exact signature `units.md:223`
already gives for `FUN_00463940`, confirmed again here in disassembly.
If the table's existing entry for that type/id *is this same unit*
(pointer-equal), that's fatal (assert id `0x1c6`). If a different entry
is found, it's unlinked (`FUN_00465e80`, already read in `units.md:234`)
and `FUN_00463a50()` is checked again: non-zero keeps this unit and
returns it unchanged; zero frees it (`FUN_00465870`, already "hooked and
logged, not run" per `units.md:240`) and returns null instead. When the
first `FUN_00463a50()` check was already non-zero, none of this runs and
the unit is returned as given. Its three callers are the same
unit-creation/assignment cluster `FUN_00465420` above is reached from:
`FUN_00465fd0` (`units.md:235`), `FUN_00466360` (`monsters.md:554`) and
`FUN_00466200` (`net-packets.md:122`). Not traced: `FUN_00463a50`'s own
condition (a global gate called twice here with no visible per-unit
argument, not a per-unit lookup). d2d: not ported as a function; no d2d
equivalent was checked against this re-register check.

Re-check: `python3 tools/ghidra/fn.py 00465f20 00463940 00465e80
00463a50`; disassemble `00465f20`..`00465fc1` to confirm the
EAX-implicit unit argument and the `FUN_00463940` register setup.
### Toggling a skill's render effect on a mode gate: `FUN_004d8520` / `FUN_004d85c0` (batch 11)

Two near-identical UNIT/PlrSkills.cpp functions, each with its own 5-byte
jump thunk right after it (`004d85b0` jumps to `004d8520`, `004d8650` jumps
to `004d85c0` — plain near `JMP`, confirmed in disassembly, not a second
body). Both are fastcall **ECX** the unit, **EDX** a value (confirmed in
disassembly: `MOV EDI,ECX` / `MOV EBX,EDX` at entry), plain `RET`, no stack
args:

- **`FUN_004d8520`** (0x83 bytes, `004d8520`..`004d85a3`) and its thunk
  **`FUN_004d85b0`** (5 bytes, `004d85b0`..`004d85b5`).
- **`FUN_004d85c0`** (0x83 bytes, `004d85c0`..`004d8643`) and its thunk
  **`FUN_004d8650`** (5 bytes, `004d8650`..`004d8655`).

Both call `d2d_unit_skill_list` (`00620250`, `names.tsv:995`) for the
unit's selected skill node; a null one is a no-op. Otherwise
**`FUN_004d84c0`** (0x52 bytes, `004d84c0`..`004d8512`, no stack args, no
register args — confirmed in disassembly: it is called with `MOV EAX,EDI`
right before it, EDI being this function's own unit, so **EAX** is
implicit) must return true: true when the unit's own `type` (`+0`) is 0
and `mode` (`+0x10`, both already in `structs.tsv`) is 0x12, or `type` is 1
and `mode` is 0xe; false for any other type/mode pair, including a null
unit (the decompiler's default `in_EAX==null -> 6` forces both branches
false). With that gate passed, `d2d_skill_node_id` (`00643ce0`,
`names.tsv:1280`) reads the skill node's id, bound-checked against the
Skills.txt row count/table (`0x744304+0xba0`/`+0xb98`, stride 0x23c,
`skills.md`'s Skills.txt row reader convention) — the two functions differ
here only in the debug line constant they pass through unread
(`0x26`/38 for `004d8520`, `0x4f`/79 for `004d85c0`; the file string is the
same `.\UNIT\PlrSkills.cpp` for both). The row's own `+0x2e` short field
then picks one of two calls, each with the unit and the original EDX value
forwarded and a literal third argument: `0x43` ('C') gates on
`skill_get_flags` (`FUN_006446a0`, `names.tsv:1299`) bit 0 before calling
**`FUN_004c9060`**; `0x4c` ('L') calls **`FUN_004c9990`** unconditionally.
The literal third argument is the only other difference between the pair:
`FUN_004d8520` passes 1, `FUN_004d85c0` passes 0.

Not traced: `FUN_004c9060`/`FUN_004c9990`'s own bodies — both are SKILLS
module, already cited but not traced (`collision.md:974`,
`video-drivers.md:178`) — and the Skills.txt row's own `+0x2e` field
identity beyond "a type byte with values 'C'/'L' seen here". d2d: not
ported as a function; nothing here has a d2d counterpart to disagree with.

Re-check: `python3 tools/ghidra/fn.py 004d8520 004d85b0 004d85c0 004d8650
004d84c0`; disassemble `004d8520`..`004d85a3`, `004d85c0`..`004d8643` and
`004d84c0`..`004d8512` to confirm the ECX=unit/EDX=value fastcall, the
5-byte `JMP` thunks, and the implicit-EAX=unit convention on
`FUN_004d84c0`.

### A skill-id's overlay-row cleanup loop: `FUN_004d8660` (batch 11)

`FUN_004d8660` (0x33 bytes, `004d8660`..`004d8693`, `RET 0x4`) takes a
single stack argument (a unit) and an implicit register input — confirmed
in disassembly: the function never sets **EBX** itself, only reads it
(`CMP EBX,-0x1` is its first real instruction), so whatever the caller's
EBX holds (an Overlay.txt row id) is what it uses, the same
register-implicit convention batches 8/9/10 found on EAX/ESI, here on EBX.
EBX of -1 is a no-op; otherwise `FUN_00664740(EBX)` (0x52 bytes,
untouched: an Overlay.txt row lookup, stride 0x84 at
`0x744304+0xbbc`/`+0xbc0` — the same table and stride `gfx.md`'s
"Overlays" section already names for `FUN_0046e1b0` — returning that row's
own `+0x4c` field as a count) bounds a loop that calls the
already-verified overlay-remove entry point `FUN_0046f0c0` (`gfx.md:100`,
with a committed oracle `tools/emu/engine_overlay_remove.py`) once per
index from EBX through EBX + count - 1. The decompiled call reads
`FUN_0046f0c0(iVar2 + unaff_EBX)`, showing only that stack word; in
disassembly `FUN_0046f0c0` is actually called with **ECX** the unit (the
stack argument this function itself received) and **EDX** 0 as well — the
decompiler drops both, the same hidden-register-argument pattern batch 10
found.

Its two call sites are both inside the already-`d2d ported`
`FUN_004d9c30` (StateRemove dispatch, `gamedata.hpp:482`,
`net-packets.md:366`), passing the removed State row's own `+0x2` and
`+0x4` signed 16-bit fields (sign-extended into EBX) whenever each is
non-negative. So `FUN_004d8660` removes every Overlay.txt row in a range a
State can carry (one range from its own `+0x2` field, a second from
`+0x4`) off the unit when that State ends.

Not traced: `FUN_00664740`'s own `+0x4c` field identity beyond "a count"
and the Overlay.txt row's own identity (`gfx.md`'s "Overlays" section
already names the table). d2d: not ported as a function; `FUN_0046f0c0` is
already verified with a committed oracle; no disagreement found with how
this caller drives it.

Re-check: `python3 tools/ghidra/fn.py 004d8660 00664740 0046f0c0
004d9c30`; disassemble `004d8660`..`004d8693` and the two call sites
around `004d9d1b`..`004d9d39` in `FUN_004d9c30` to confirm the
implicit-EBX convention and the dropped ECX/EDX on `FUN_0046f0c0`.

### Picking the active state's screen overlay byte: `FUN_004d97f0` (batch 11)

`FUN_004d97f0` (0x127 bytes, `004d97f0`..`004d9917`, `RET 0x4`): one stack
argument (the unit), not fastcall despite the bare decompiled signature —
confirmed in disassembly by the `RET 0x4` and a fallback path that reads
the unit straight from `[EBP+8]` rather than any register. It loops over
every States.txt-shaped row (count at `0x744304+0xc4`, rows at `+0xbc`,
stride 0x3c) that's active on the unit (`FUN_00639df0`, already verified,
`ai.cpp:993`), keeping the one whose own `+0x20` byte (a priority) is
strictly the highest seen. With a winner: its `+0x21` byte becomes the
unit's `+0x6c` field — unless that byte is the ASCII char `h` (0x68) and
the unit is the local player (`FUN_00463dd0`, already verified) and
`FUN_004f51c0` (0xd bytes, untouched, not traced) is non-zero, in which
case `+0x6c` is forced to 0 instead. Only when the unit is also the local
player with a non-zero `+0x64` (the light-record pointer
`unit-player-trace.md:27` already describes `FUN_00460bf0` setting), it
then calls `FUN_00474390` (0x39 bytes, untouched, not traced: `+0x23`/
`+0x24` from the winning row) with that row's own `+0x23`/`+0x24` bytes.
With no winning row, or when the only candidate index is out of bounds,
`+0x6c` resets to 0 and, for the local player with a non-zero `+0x64`,
`FUN_00474390(0xff, 0xff)` runs instead — clearing whatever the winning
path sets. `structs.tsv` has no row yet for unit `+0x6c`; from this it is
the byte a State's own `+0x21` field drives (a per-state overlay/tint
selector), added here as `state_overlay_byte`.

Its two callers, the already-`d2d ported` StateSet (`FUN_004d9b20`,
`gamedata.hpp:482`, `net-packets.md:357`) and StateRemove (`FUN_004d9c30`,
above) dispatchers, call it only when the state row's own `+0x21` byte is
non-zero.

Not traced: `FUN_004f51c0` and `FUN_00474390`'s own bodies, and the
States.txt row's `+0x20`/`+0x21`/`+0x23`/`+0x24` fields' identity beyond
what's read here. d2d: not ported as a function; no disagreement found —
nothing here was compared against a d2d equivalent.

Re-check: `python3 tools/ghidra/fn.py 004d97f0 00639df0 00463dd0 004f51c0
00474390`; disassemble `004d97f0`..`004d9917` to confirm the stdcall,
single-stack-argument convention (`RET 0x4`) despite the bare decompiled
signature.

### A state-edge reset of mode, overlays and frame rate: `FUN_004d9ad0` (batch 11)

`FUN_004d9ad0` (0x4c bytes, `004d9ad0`..`004d9b1c`, plain `RET`, no stack
args, no register args — confirmed in disassembly: the function never
sets **ESI** itself, only reads it, the same implicit convention as
`FUN_004d8660` above, here on ESI instead of EBX): it calls
`FUN_0046f000` (already verified, `gfx.md:70`/`75`: clears the unit's 16
overlay flags and frees its COF list) — the decompiled call shows no
argument, but disassembly has `MOV ECX,ESI` right before it, so the unit
is passed and simply dropped by the decompiler. Then, only when the
unit's own `type` (`+0`) is 1 (monster), or `type` is 0 (player) and
`mode` (`+0x10`) is not 0x12, it calls `FUN_00480e70` (`UNITMODE_SetAnimMode`,
already documented, `collision.md:1194`) with the unit and mode 1 — again
hidden by the decompiler, which shows a bare `FUN_00480e70()`; disassembly
has `MOV EDX,1` / `MOV ECX,ESI` right before the call. Every path then
runs the same tail: `FUN_00470610(unit, 1)` (already verified,
`gfx.md:239`, also shown argument-less by the decompiler but
register-fed in disassembly the same way), `FUN_00624390` — `names.tsv:1074`'s
`set_mode_rate`, already named — on the unit, zeroing the unit's `+0x44`
field (`unaff_ESI[0x11]`), then `FUN_00620210(unit, 0)` (already verified,
the mode/skill setter `collision.md:1103`).

Its two callers are the same already-`d2d ported` StateSet/StateRemove
dispatchers the two sections above cite, each calling it only when the
state row's own `+0x10` byte has bit 3 (mask 8) set — so this is their
shared "the state's own end/start also resets the unit's base animation,
overlays and frame-rate counter" step.

d2d: not ported as a function; `FUN_0046f000`, `FUN_00480e70`,
`FUN_00470610` and `FUN_00620210` are already verified/ported pieces this
caller sequences in a fixed order; no disagreement found.

Re-check: `python3 tools/ghidra/fn.py 004d9ad0 0046f000 00480e70 00470610
00624390 00620210`; disassemble `004d9ad0`..`004d9b1c` to confirm the
implicit-ESI convention and that the decompiler drops every callee's
ECX/EDX register arguments here.

### A lighter event-list dispatch, no removal bookkeeping: `FUN_004dc210` (batch 11)

`FUN_004dc210` (0x52 bytes, `004dc210`..`004dc262`, `RET 0x8`): fastcall
**ECX** a match value, **EDX** the unit, two stack words forwarded
unchanged into whatever handler matches (confirmed in disassembly: `MOV
EDI,EDX` / `MOV EBX,ECX` at entry). With a null unit it returns 0. It
walks the unit's own event list (`+0x90`, already in `structs.tsv`, the
same list the already-`d2d verified` `FUN_005c0c30` — `ai.hpp:590` —
walks): for each node whose own `+0` field equals the ECX match value, it
calls the node's own callback (node `+0x14`, a function pointer) with
**ECX** the original match value, **EDX** the unit, and the two stack
words plus the node's own `+0xc`/`+0x10` fields as four stack arguments —
the same node layout (`+0`, `+0xc`, `+0x10`, `+0x14` callback, `+0x1c`
next) `FUN_005c0c30` already uses, minus that function's "in use"/removal
bookkeeping (its node `+2` flags word and `+0x18` previous-pointer upkeep)
entirely: this function only reads the list, it never marks or unlinks a
node. The decompiled call shows `(**(code**)(piVar2+5))(param_3,param_4,
piVar2[3],piVar2[4])`, omitting that the callback's own ECX/EDX are the
match value and the unit again — confirmed in disassembly (`MOV
ECX,EBX`/`MOV EDX,EDI` right before `CALL EAX`), the same
hidden-register-argument the decompiler shows for a direct call now shown
on an indirect one.

Its one caller, the already-`d2d verified` `FUN_004d2d70` (the client's
hit and explosion, `missiles.md:117`), calls `FUN_004dc210(unit, 0)` right
after applying a hit's socket/state changes — dispatching event type 0 to
any handler the unit's own event list carries for it.

Not traced: what event type 0 means to a caller elsewhere, and whether any
unit ever registers a handler for it (no handler-registration call was
read here). d2d: not ported as a function; `FUN_005c0c30`'s own raise
behaviour is the part of this system with a d2d equivalent
(`components/game/ai.hpp:590`); no disagreement found with how
`FUN_004d2d70` uses this lighter dispatch.

Re-check: `python3 tools/ghidra/fn.py 004dc210 005c0c30 004d2d70`;
disassemble `004dc210`..`004dc262` to confirm the ECX=match/EDX=unit
fastcall and that the indirect call passes the callback its own ECX/EDX.

### The client's own pierce check: `FUN_004cd310` (batch 10)

`FUN_004cd310` (0x64 bytes, `004cd310`..`004cd374`, plain `RET`, no stack
args) is the only caller's dependency of the already-verified
`FUN_004d2d70` (**the client's hit and explosion**, `missiles.md:117`); it
sits right before batch 8's sound-timer cluster. It takes no stack
arguments and no register arguments either — confirmed in disassembly: it
never sets ESI itself, only pushes and pops it around the body, so
whatever the caller's **ESI** held (the missile) is what it reads,
including on the forwarded call to `FUN_0064a3e0(ESI)`. At the one call
site (`FUN_004d2d70`, `004d2e76`), ESI is that function's own ECX
parameter — the missile. This is the same register-implicit convention
batches 8/9 found on EAX; here it's ESI instead, confirmed the same way
(raw disassembly, not the decompiler's `unaff_ESI` label alone).

`FUN_0064a3e0(missile)` re-derives the missile's Missiles.txt row (same
bounds check and `* 0x1a4 + PTR_DAT_00744304+0xb64` lookup `FUN_004d2d70`
already did); a result that is `< 0` or past the table's count resets the
row pointer to 0 rather than failing. It then tests the row's own `+4`
byte against the global mask `DAT_006ce270` — the same byte and mask
`missiles.md:1030` already names **Pierce** (`FUN_00661b20`'s column list,
mask `0x04`) and `monsters.md:26` already cites for `DAT_006ce270`. Set:
`FUN_00625480(ESI, 0x148, 0)` (`GetStat`, `stats.md:34`) reads the
missile's own stat `0x148` — the same **pierce_idx** counter
`missiles.md:550` already names on the *server* side
(`FUN_005ada80`/`FUN_0059f940`); nonzero: `FUN_00627260(ESI, 0x148, n-1, 0)`
(`SetStat`) decrements it and the function returns **2**; otherwise, or
when the Pierce flag is clear, it returns the default **3**. `FUN_004d2d70`
treats these the same way `FUN_005ada80`'s caller `FUN_005adf10` treats its
own 2/3: 2 keeps the missile flying (its hit function still runs), 3 ends
it.

So `FUN_004cd310` is the **client**-side mirror of the already-documented
server pierce check `FUN_005ada80` (`missiles.md:1030`), decrementing the
same server-seeded `pierce_idx` stat on the client's own copy of the
missile so its locally-predicted flight/explosion agrees with the
server's. d2d: not ported as a function; no disagreement found —
`rules::missile_blow` and the pierce chance roll are the parts of this
system already ported, and the client-side mirror itself is not mentioned
there either way.

Re-check: `python3 tools/ghidra/fn.py 004cd310 004d2d70 0064a3e0`;
disassemble `004cd310`..`004cd374` to confirm the ESI-implicit, no-args
convention above.

### A generic client-missile spawn helper: `FUN_004cdc30` (batch 10)

`FUN_004cdc30` (0xdd bytes, `004cdc30`..`004cdd0d`, `RET 0x1c`): fastcall
**ECX** a unit, **EDX** a Missiles.txt row id, seven stack words
(confirmed in disassembly — `RET 0x1c` pops exactly the seven stack
dwords, register args aren't counted). A null ECX returns 0. When the
unit's own type (`*ECX`) is neither 0 nor 1 (player or NPC), it is
replaced with `FUN_004639d0()` — **the owner unit**, the same accessor
`gfx.md:189` already names — and a null result also returns 0. The next
two stack words (`param_5`/`param_6`, an x/y offset) are both required
non-zero, or `FUN_004c52e0(unit, &param_5, &param_6)` must fill them (not
traced further; a default offset the caller didn't supply). With the
offset resolved it zeroes a 0x5c-byte block and fills the same indices the
already-documented client-missile-spawn blocks use (`missiles.md:880`
"Client missile spawn" family): `[0]` flag **0x521**, `[1]` the unit,
`[4]` the row id (EDX), `[5]`/`[6]` the unit's own position
(`FUN_0045adf0`/`FUN_0045ae20`) plus the x/y offset, `[7]`/`[8]` the next
two stack words, `[0xb]`/`[0xc]` the last two stack words, and `[9]` the
seventh stack word or **0x1e** (30) when it's 0 — a field not seen in any
of the zeroed-block callers `missiles.md:880`-`995` already documents.
`FUN_004cd540` (**make missile**, cited throughout `missiles.md`) does the
actual spawn; its result is returned directly, untagged. Not traced:
`FUN_004c52e0`'s own default-offset logic, and index `[9]`'s meaning
(no caller here uses a non-zero seventh stack word).

Its many callers (12 call sites, mostly `UNIT/?` and `UNIT/Monster.cpp`
code not otherwise identified here) pass a constant row id and offset, or
all zeros — not read further to say what each spawns; one caller
(`FUN_004af2e0`) calls it with a null unit (`FUN_004cdc30(0,0,0,0,0,1,0)`),
which is a no-op per the null check above.

d2d: not ported as a function; `missiles.md`'s already-ported client
missile makers (`FUN_0046b680`, `FUN_0046b9a0`, etc.) are the parametrized
instances of the same zeroed-block pattern this function generalizes; no
disagreement found.

Re-check: `python3 tools/ghidra/fn.py 004cdc30 004c52e0 004cd540`;
disassemble `004cdc30`..`004cdd0d` to confirm the ECX/EDX + seven-stack-word
convention and the `RET 0x1c` cleanup.

### Andariel's quake-debris spawn helpers: `FUN_004ce530`, `FUN_004ce850`, `FUN_004cecc0` (batch 10)

Three UNIT functions clustered right after the client missile spawn
helper above, all three called only from the already-`d2d verified`
`FUN_004d5310` (**andycontrol0**, `shake.hpp:63`, `unit-sounds.md:479`) or
its untraced sibling `FUN_004d4f20` — both of which read a Missiles.txt
row's own `+0x1e`/`+0x20`/`+0x58`/`+0x5c` fields the same way
`FUN_004cc950`'s write-up (batch 9) already describes for `andycontrol0`.

- **`FUN_004ce530`** (0x119 bytes, `004ce530`..`004ce649`, `RET 0xc`): no
  register args — **EAX** is implicit (confirmed in disassembly: `MOV
  EDI,EAX` is the first real instruction, the same EAX-implicit convention
  batch 8 found, here mixed with three ordinary stack words that `RET 0xc`
  pops). At its two call sites (`FUN_004d4f20`, `FUN_004d5310`) EAX is the
  caller's own missile (`MOV EAX,ESI` right before the call in both). The
  three stack words are, in order: the missile's **owner**
  (`FUN_004639d0(missile)`, `gfx.md:189`), the row's `+0x1e` short field
  (cast to int), and the row's `+0x60` dword field. It zeroes a 0x5c-byte
  block (flag **2**; row id, index `[4]`, is left 0 — the one block here
  that doesn't set it), `[1]` the owner, `[0xb]`/`[0xc]` the missile's own
  skill/level (`FUN_0064a280`/`FUN_0064a210`, cited throughout
  `missiles.md`). It then draws two independent values from the missile's
  own 64-bit seed (`+0x20`/`+0x24`, the `× 0x6ac690c5` step already
  documented at `drops.md:73` and elsewhere) in `[0, 2×radius]` (`radius`
  the `+0x60` dword) and subtracts `radius` from each, giving an x/y jitter
  in `[-radius, radius]`; a `(0, 0)` jitter is nudged by one more draw
  (`FUN_0045c390`, `rand(n)`) so the x offset is never exactly 0. The
  jittered offsets go into the new missile's own `+0x28`/`+0x2c` words
  (`FUN_0064a710`/`FUN_0064a760`, the general-purpose missile data setters
  `missiles.md:995` already names) after `FUN_004cd540` spawns it.
- **`FUN_004ce850`** (0x1a6 bytes, `004ce850`..`004ce9f6`, `RET 0xc`):
  fastcall **ECX** a unit (the owner, forwarded straight into the spawn
  block), **EDX** the missile, three stack words — confirmed in
  disassembly at the one call site (`FUN_004d5310`): `MOV EDX,ESI` (the
  missile) and three pushes for the stack words, in order the caller's own
  row's `+0x1e` short field (a *different* row's field than
  `FUN_004ce530` reads — here it is itself used as another Missiles.txt
  row id, bounds-checked the same way), the missile's own age
  (`FUN_0064a3b0`, named "total minus remaining" at `unit-sounds.md:577`
  for this exact caller), and `FUN_004cc950`'s frame/intensity result
  (batch 9, `unit-sounds.md:570`). With a valid row: its own `+0x1e`
  (yet another row's field, same offset, different row) above 0 calls
  `FUN_004ce650` (not traced further) with it and the frame value; its
  `+0x34` above 0 and the age stack word equal to `0x13b` calls
  `FUN_004b9a00(0, 0, 0)` (queue-a-sound, `missiles.md:1034` already names
  it, here with all-zero arguments — not examined further). Then, only on
  an even age (and, when the frame value is under 3, only every 16th age
  too), it buckets the frame value into a magnitude 1/2/3/4 (under 3/6/9,
  else 4), draws two jitter values on the **global** seed
  (`d2d_seed_rand_below`, not the missile's own `+0x20`/`+0x24` seed
  `FUN_004ce530` uses) centered on 0 and scaled by that magnitude, and
  spawns a zeroed-block missile (flag **1**, `[1]` the owner, `[4]` the
  row id, `[0xb]`/`[0xc]` the EDX missile's own skill/level) at the EDX
  missile's position (read the same way `collision.md:1163`/`1384`
  already describe: data block `+0xc`/`+0x10` directly for types 2/4/5,
  else `d2d_path_get_x`/`_get_y`) plus the jitter, via `FUN_004cd540`
  (untagged, result discarded).
- **`FUN_004cecc0`** (0x5b bytes, `004cecc0`..`004ced1b`, `RET 0x8`): the
  decompiler shows only two stack parameters, but **ECX** and **EDX** are
  real inputs too — confirmed in disassembly: the function never reads
  either register itself, only forwards them unchanged into its sole
  callee, `FUN_004cea00` (its own `param_1`/EDX-as-unit, not traced
  further), alongside its first stack word (a row id, also forwarded
  unchanged) and a second value per branch. Its own gate is on the second
  stack word alone: a value in `[0xc9, 0xe1)` (201..224) calls
  `FUN_004cea00` twice with `6`; otherwise, when that value is a multiple
  of 4, it calls `FUN_004cea00` once with `0x14` (20); any other value is
  a no-op.

Its one caller, `FUN_004d5310` (andycontrol0), passes the row's `+0x22`
short field as the row id and the missile's age as the gating value, only
when the age is in `[0x74, 0x137)` (199 ticks) — the third and last of the
three row fields (`+0x1e`, `+0x20`, `+0x22`) `andycontrol0`'s own write-up
(batch 9) reads but does not use. Not traced: `FUN_004ce650`,
`FUN_004cea00` and `FUN_004b9a00`'s own bodies beyond what's cited above,
and what the `6`/`0x14` second argument means to `FUN_004cea00`.

d2d: not ported as a function; `shake.hpp`'s screen-shake and quake-loop
volume are the parts of `andycontrol0` already ported, and `FUN_004cc950`
said this decal-frame computation and its caller's use of the result were
listed there under "Not done" (the control missiles' fire/debris
visuals) — this completes that trace; no disagreement found.

Re-check: `python3 tools/ghidra/fn.py 004ce530 004ce850 004cecc0
004d5310 004d4f20`; disassemble `004ce530`..`004ce649`,
`004ce850`..`004ce9f6` and `004cecc0`..`004ced1b` to confirm the
EAX-implicit, ECX/EDX-plus-stack and the forwarded-ECX/EDX conventions
above.
### The unit's sound-timer scalars (batch 8)

Four small UNIT-module functions clustered right after `FUN_004ca380`/
`FUN_004ca320`/`FUN_004ca2c0` (the mode/class/type reads `FUN_004cb460`
and `FUN_004caf60` above already call). `FUN_004cb460` itself calls
`FUN_004cab90` directly (`iVar4 = FUN_004cab90(), iVar4 == 0`; confirmed
by decompiling `FUN_004cb460` itself, not detailed again here — its d2d
port is cited at lines 1455-1456 below); the other three sit beside it
in address order and share its fatal-check shape. The getters here use **EAX**, not ECX, as the implicit
unit pointer — a register-parameter order (EAX, EDX, EBX, ECX for up to
four arguments) this binary uses throughout, confirmed against raw
disassembly, not just the decompiler's guess (which shows it as an
unnamed `in_EAX`):

- **`FUN_004cab30`** (0x27 bytes, `004cab30`..`004cab57`, plain `RET`, no
  stack args): a null unit (EAX) is fatal (`FUN_00408090(0xcbb)` message,
  `FUN_00408a60`, `FUN_00681e09(-1)`, no return); otherwise returns the
  dword at unit `+0x7c` — `structs.tsv`'s `sound_time` row.
- **`FUN_004cab60`** (0x30 bytes, `004cab60`..`004cab90`, `RET 0x4`): same
  null-unit fatal check (message id `0xcc6`); otherwise writes its one
  stack argument to unit `+0x7c` (`sound_time`'s setter).
- **`FUN_004cab90`** (0x2a bytes, `004cab90`..`004cabba`, plain `RET`):
  same fatal check (id `0xcd1`); returns the dword at unit `+0x80`
  (`neutral_time`).
- **`FUN_004cac30`** (0x33 bytes, `004cac30`..`004cac63`, `RET 0x4`): same
  fatal check (id `0xcf2`); writes its stack argument to unit `+0x84`
  (`step_time`'s setter). `structs.tsv` has no row yet for `neutral_time`'s
  setter or `step_time`'s getter; neither was found at this address range
  (not traced further).

d2d: not ported as a function; no disagreement found, nothing here was
compared against a d2d equivalent — the already-verified caller
`FUN_004cb460` (`object_sounds.hpp`, `fight/setup.cpp`,
`gamedata.hpp:729`) is the one with a d2d port.

Re-check: `python3 tools/ghidra/fn.py 004cab30 004cab60 004cab90
004cac30`; disassemble `004cab30`..`004cac63` to confirm the
EAX-implicit, no-ECX calling convention above.

### The unit's own list of playing sounds: `FUN_004ca900` / `FUN_004caa10` (batch 8)

Two more UNIT-module functions right after the timer scalars above, both
called from the same already-verified ambient-sound sequencer
`FUN_004cb460` (confirmed by decompiling `FUN_004cb460` itself, not
detailed again here — its d2d port is cited at lines 1455-1456 above:
`FUN_004ca900` to decide whether to re-trigger, `FUN_004caa10` to stop
the old sound first) — unlike the
EAX-implicit accessors above, both use the ordinary fastcall ECX/EDX
convention (confirmed in disassembly):

- **`FUN_004ca900`** (0x8f bytes, `004ca900`..`004ca98f`, plain `RET`;
  ECX the unit, EDX a sound-category index — `MOV ESI,ECX` / `MOV
  EDI,EDX` in the disassembly): a null unit is fatal (id `0xc47`); then
  `FUN_004df880()` (cited at `menu.md:132`, the "sound enabled" global)
  zero returns 0 (sound off: never "already playing"). Otherwise
  `FUN_00481890(EDX)` and `FUN_004818c0(EDX)` read a Sounds.txt-shaped
  table row (0x8e-byte stride, fields `+0x60` and `+0x3d`) for a
  channel-count value, then the function walks the unit's own list head
  at `+0x78` (a linked list, next pointer at node `+4`, data at node
  `+0`): for each node it resolves the node's data through
  `FUN_004b9ca0` (walks the global playing-sound list `DAT_007c5458`,
  matching each entry's `+8` field, returning its `+4` field — the active
  sound's resolved index/slot) and returns that node's data the first
  time the resolved value falls in `[EDX, EDX + count)`; an empty or
  exhausted list returns 0. So `FUN_004ca900(unit, soundIndex)` answers
  "is a sound in this category already playing on this unit", returning
  the matching node data (not traced further) or 0.
- **`FUN_004caa10`** (0x5c bytes, `004caa10`..`004caa6c`, plain `RET`;
  ECX the unit only, no EDX): same null-unit fatal check (id `0xc2c`) as
  `FUN_004ca900`. With a unit it walks the same `+0x78` node list; for
  each node, `FUN_004b9ca0` resolves it the same way, then
  `FUN_004818f0(resolved)` reads the Sounds.txt-shaped row's `+0x3e` byte
  and, when set, calls `FUN_004ba790(node_data, unit, 0)` — the
  stop-sound entry point (matches by the same `DAT_007c5458` list, then
  gates on a `d2d_sound_row` field before calling `FUN_004ba760`). So
  the stop applies to each currently-playing sound this unit owns that is
  flagged `+0x3e` and passes that gate. The gate's false path is not
  traced. `structs.tsv` has no row yet for unit `+0x78`; from
  this pair it is the head of the unit's own linked list of active-sound
  nodes, distinct from the `+0x7c`/`+0x80`/`+0x84` cooldown scalars
  above — added here as `sound_nodes`.

Not traced: `FUN_004b9ca0`'s and `FUN_004ba790`'s own global sound-node
format (`DAT_007c5458`'s `+4`/`+8` fields beyond "an id" and "a resolved
index"), the Sounds.txt-shaped table's own identity and the rest of its
0x8e-byte row, and `FUN_004ba760`/`FUN_004ca8d0`/`d2d_sound_row`'s own
bodies. d2d: not ported as a function; no disagreement found, nothing
here was compared against a d2d equivalent — the already-verified caller
`FUN_004cb460` is the one with a d2d port.

Re-check: `python3 tools/ghidra/fn.py 004ca900 004caa10 004b9ca0
004ba790`; disassemble `004ca900`..`004ca98f` and `004caa10`..`004caa6c`
to confirm the ECX/EDX argument order above.

### A room's floor-tile lookup by subtile: `FUN_004cace0` (batch 8)

`FUN_004cace0` (0x74 bytes, `004cace0`..`004cad54`, plain `RET`, no stack
args) sits right after the sound cluster above and is the only caller's
dependency of `FUN_004cadb0` (already `d2d verified`:
`fight.hpp:469`, `sound_ids.hpp:118,122`, `unit-sounds.md:252,256`), which
calls it in a loop over near rooms when its own room lookup misses. Its
three arguments are register-passed in the same EAX/EDX/ECX order as the
getters above (confirmed in disassembly): **EAX** a y subtile, **EDX** the
room, **ECX** an x subtile. It calls `FUN_00619730(room, &rect)` (the
room's rect, already cited above at line 8, "The grid") to get the room's
origin, subtracts it from the x and y arguments, divides each by 5 (the
tile-to-subtile ratio the grid section already gives: 5x5 subtiles per
tile), then calls `FUN_00619660(room, &count)` (also already cited above
at line 10) to get the room's floor-tile list and count. It walks that
list's 0x30-byte-stride records (the same record size "The grid" section
names) comparing each record's `+8` field against the x-in-tiles value
and `+0xc` against the y-in-tiles value; the first match returns a
pointer to that record's start, and an empty or exhausted list returns 0.
So `FUN_004cace0(y, room, x)` finds the floor-tile record under a given
subtile position within one room — a lookup helper for the same
DT1-tile-record layout "The grid" section already describes, not a new
one. Not traced: what a caller does with the returned record beyond the
match/no-match the two outer functions above use it for. d2d: not ported
as a function; `Level::walk`/`set_level_tiles` (already cited at "The
grid" above) build d2d's own per-subtile flags without a one-to-one
per-tile-record lookup, so there is nothing here to disagree with.

Re-check: `python3 tools/ghidra/fn.py 004cace0 004cadb0 00619660
00619730`; disassemble `004cace0`..`004cad54` to confirm the
EAX=y/EDX=room/ECX=x argument order.
### The swing sound's id alone: `FUN_004ca4c0` (batch 9)

`FUN_004ca4c0` (0x3a bytes, `004ca4c0`..`004ca4fa`, plain `RET`, no stack
args) sits right before the sound-timer cluster batch 8 traced. Plain
fastcall, confirmed in disassembly: **ECX** the unit, **EDX** a weapon hit
class. A null unit (ECX) is fatal (`FUN_00408090(0xb5c)` message,
`FUN_00408a60`, `FUN_00681e09(-1)`, no return) — ECX is only ever used for
this check, never dereferenced. Otherwise it masks EDX to its low nibble,
and a value under 0xe (14) returns the dword at `DAT_00727ed8 + (EDX &
0xf) * 8`; 0xe or above is fatal too (message `0xb60`). `0x727ed8` is the
8-byte `{sound, frames}` table "The swing, FUN_004cb6a0" already names
(`unit-sounds.md:51`, ported whole as `kSwing` in
`components/rules/sound_ids.hpp:52`) — this function returns only the
`sound` half of the pair, given the hit class directly rather than
computing it. Its three callers (`FUN_004c6140` Skills.cpp's skill-start
dispatcher, ported; `FUN_004c7d50`, `FUN_004c8ed0`, `FUN_004f4c20`, all
`SKILLS` module) each call `FUN_00623c20` (the weapon's hit class,
`sound_ids.hpp:51`) immediately before, then feed this function's result
as the sound id into `FUN_004b9a00` (queue a Sounds.txt row,
`unit-sounds.md:17`) — the `weaponsnd` swing play `unit-sounds.md:138`
already describes as reading "the 0x727ed8 table".

d2d: the `0x727ed8` table itself is already ported as `kSwing`
(`sound_ids.hpp:52`); this accessor (an index into the table's `sound`
field alone, with a redundant unit-null check) has no separate d2d
function and nothing here disagrees with `kSwing`'s contents.

Re-check: `python3 tools/ghidra/fn.py 004ca4c0 004c6140`; disassemble
`004ca4c0`..`004ca4fa` to confirm the ECX=unit/EDX=hit-class order above.

### Starting and stopping a boss unique's ambient sound (batch 9)

Two UNIT functions a short distance after batch 8's sound-node pair,
both gated on `FUN_004ca320` (**GetSndUnitType**, `unit-sounds.md:38`)
returning 1 (monster) and then `d2d_uniques_boss_sound(unit)`
(`components/rules/uniques.hpp:196`, already ported) returning a non-null
MonSounds.txt row pointer (0x94-byte stride, confirmed via that function's
own tail calls `FUN_00656f90`/`FUN_00656fc0`, which compute `row * 0x94 +
DAT_0096c6b4`):

- **`FUN_004cb190`** (0x90 bytes, `004cb190`..`004cb220`, plain `RET`, no
  stack args): fastcall **ECX** the unit only (confirmed in
  disassembly — EDX is never read). When the unit's type is 1 and it has
  a boss-sound row, a null unit is fatal (id `0xc2c`, same shape as the
  other checks here); otherwise it walks the unit's `sound_nodes` list
  (`+0x78`, batch 8) exactly as `FUN_004caa10` does — `FUN_004b9ca0`
  resolves each node, and when the resolved value matches any of the
  boss-sound row's `+0x44`/`+0x48`/`+0x4c`/`+0x50` dwords (four of its own
  sound-id fields, not otherwise identified), it calls
  `FUN_004ba790(node_data, unit, 1)` — the same stop-sound entry point
  `FUN_004caa10` calls with a flag of 0 (batch 8); here the flag is 1.
- **`FUN_004cb950`** (0x61 bytes, `004cb950`..`004cb9b1`, plain `RET`, no
  stack args): **EAX**-implicit unit (confirmed in disassembly: the first
  instruction is `MOV ESI,EAX`), no stack args — the same implicit-EAX
  convention batch 8 found on the sound-timer scalars. Same type/boss-row
  gate as above; then, only once at least 4 ticks (`FUN_0044db00`, a tick
  counter) have passed since the global `DAT_007c88bc`, it calls
  `FUN_004e40a0(3, 0, 0)` for a sound-category base, adds 6, and queues it
  with `FUN_004b9a00(category, unit, boss_row->+0x78 + 6)` — the row's
  `+0x78` field (batch 8 named this `sound_nodes` on the *unit*; here
  `+0x78` is a different field of the MonSounds-shaped boss row, not the
  unit). It then sets the unit's `sound_time` (`FUN_004cab60`, batch 8,
  EAX-implicit) to the current tick and updates `DAT_007c88bc` to it too.
  So this is "play the boss unique's ambient sound, throttled to once
  every 4+ ticks globally", with `FUN_004cb190` above as its stop
  counterpart.

Not traced: the MonSounds.txt row's own field layout beyond the `0x94`
stride and the five fields read here (`+0x44`, `+0x48`, `+0x4c`, `+0x50`,
`+0x78`); `FUN_004e40a0`'s own category table. d2d: not ported as a
function; no disagreement found — `d2d_uniques_boss_sound` is the only
piece of this pair with a d2d equivalent.

Re-check: `python3 tools/ghidra/fn.py 004cb190 004cb950 004ca410`;
disassemble `004cb190`..`004cb220` and `004cb950`..`004cb9b1` to confirm
the ECX-only and EAX-implicit conventions above.

### Reserving a unit's ambient sounds on an edge-triggered flag: `FUN_004cc160` (batch 9)

`FUN_004cc160` (0xe6 bytes, `004cc160`..`004cc246`, plain `RET`, no stack
args) is fastcall **ECX** the unit, **EDX** a signed value (confirmed in
disassembly: `MOV ESI,ECX`/`MOV EDI,EDX` at entry). A null unit is fatal
(id `0xcfd`). It reads the unit's own flag at `+0x88`; when it's 0 and
EDX is negative, or it's already set and EDX is positive, the call is a
no-op (the flag only changes on a sign transition of EDX). Otherwise it
sets `+0x88` to `(0 < EDX)` and dispatches on `FUN_004ca320`'s result
(**GetSndUnitType**, the same call batch 8's `FUN_004cb460` and the pair
above also make):

- type 1 (monster): `d2d_uniques_boss_sound(unit)`; if it returns a row,
  call `FUN_004822b0(id, EDX)` — "Group reference counts"
  (`sound.md:61`, already ported) — once for each of six of the row's own
  sound-id fields, at `+4`, `+0x1c`, `+0x34`, `+0x3c`, `+0x78`, `+0x58`
  (the same `+0x78` the pair above reads).
- type 2 (object): `FUN_004ca2c0()` (**GetSndUnitClassId**,
  `unit-sounds.md:38`) as an objects.txt id, bound-checked under 0x23d
  (else fatal, id `0x1213`); if `DAT_007295f8[id]` (the per-object sound
  record "Objects (0x7295f8)" names, `unit-sounds.md:369`, dumped whole
  as `kObjectSounds` in `components/rules/object_sounds.hpp`) is set, call
  `FUN_004822b0(record->modes[i], EDX)` for all eight of its `modes`
  entries.

So `FUN_004cc160(unit, value)` bumps (`EDX` > 0) or drops (`EDX` < 0) the
group reference count of a boss unique's six listed sounds, or an
object's eight per-mode sounds, once each time the sign of `value`
changes — reserving or releasing their Sounds.txt group slots, most
likely while the unit is moused over or otherwise highlighted (not
confirmed here; no caller context ties it to a specific UI action). Its
three callers (`FUN_00465870` `CUnit.cpp`, `FUN_00463a50`,
`FUN_004cc410` struck-sound dispatcher, all already cited) were not read
further to confirm that.

Not traced: what sets `EDX`'s sign at any of the three call sites, and
the MonSounds row's six named fields' own identity beyond "a sound id".
d2d: not ported as a function; `FUN_004822b0`'s group-reference-count
behaviour is already ported and verified (`sound.md:61`); no disagreement
found with how this caller uses it.

Re-check: `python3 tools/ghidra/fn.py 004cc160 004822b0`; disassemble
`004cc160`..`004cc246` to confirm the ECX=unit/EDX=value order and the
sign-transition gate on `+0x88`.

### A missile's progress-to-frame mapper: `FUN_004cc950` (batch 9)

`FUN_004cc950` (0x48 bytes, `004cc950`..`004cc998`, plain `RET`, no stack
args, no calls) takes **EAX** a progress value and **ECX** a record
pointer (confirmed in disassembly — EDX is never read; ECX is read only
on the branch that needs it, at `+0x96`, a signed 16-bit field):

- EAX under 0x1e (30): returns 1.
- EAX in `[0x1e, 0x55)` (30..84): returns `(EAX - 0xf) >> 2` (arithmetic
  shift, so integer division by 4 for these positive values).
- EAX at or above 0x55, with `record->+0x96` (signed 16-bit) as `max`: let
  `low = max - 0x32`. EAX under `low` returns 0xc (12, the ramp's
  apparent ceiling — note the branch above can already return above 12
  for EAX near 84, so 12 is not a hard cap on the whole function, only on
  this branch). EAX at or above `low` but under `max` returns `0xc -
  ((EAX - low) >> 2)`. EAX at or above `max` falls through to the same
  default: 1.

Its only caller, `FUN_004d5310` (`andycontrol0`, client function 29,
already `d2d verified`: `components/rules/shake.hpp:63`,
`unit-sounds.md:570`, with a committed oracle,
`tools/emu/shake.py`/`tests/quake_control_cases.inc`), passes the
MissileTypeTxt-shaped record it already holds (stride `0x1a4`,
`PTR_DAT_00744304+0xb64`/`+0xb6c`) as `ECX` and `FUN_0064a3b0`'s result
("reads total minus remaining", `unit-sounds.md:573`) as `EAX`, then
feeds this function's return value into `FUN_004ce850` alongside the
record's `+0x1e` field and the same progress value — a frame or intensity
pick for Andariel's death-effect decal, not traced further.

Not traced: `record->+0x96`'s own meaning beyond "a bound used past
EAX == 0x55", and `FUN_004ce850`'s own use of the returned index. d2d:
not ported as a function; `shake.hpp`'s screen-shake and quake-loop
volume are the parts of `andycontrol0` already ported, and this
particular decal-frame computation is listed there under "Not done"
(the control missiles' fire/debris visuals) rather than disagreed with.

Re-check: `python3 tools/ghidra/fn.py 004cc950 004d5310`; disassemble
`004cc950`..`004cc998` to confirm the EAX=progress/ECX=record order and
that EDX is unused.

### The client's monster-modifier table and its five slots: `FUN_004add90`, `FUN_004ad020`, `FUN_004ade80` (batch 21)

`monsters.md` ("Mod 26's client mirror") and batch 13 (`FUN_004ade80`)
named this client table without reading it. Read in disassembly:

- **`FUN_004add90`** (`004add90`..`004ade34`, stdcall `RET 0xc`, stack
  `unit`, `unit2`, `slot`): a null unit, a non-monster (`*unit != 1`), a
  null monster data (`+0x14`) or a null mod list (data `+0x1c`, the 9-byte
  list) returns at once. The flag it forwards is `(data[+0x16] >> 3) & 1`
  (the unique bit, `monsters.md` "flag 8"). For each of the 9 list bytes
  `m` it reads the pointer at `0x724e28 + (m * 5 + slot) * 4` and, when
  non-null, calls it **fastcall**: ECX the unit, EDX the mod id `m`,
  stack the flag, `RET 4`. For slot 4 the unit passed is `unit2` (the
  other unit, a missile) and the table index is `m * 5 + 4`.
- **`FUN_004ad020`** (the apply pass; call sites `004aeb96` in
  `FUN_004ae8d0`, `0045e47a` in `FUN_0045e400`, `004af05f` in `FUN_004aedd0`): the same gate, then the 4 fixed mod
  ids at `0x6da4c8..0x6da4d8` and the 9 list bytes each go through the
  **separate** 44-entry table at `0x724d78` (`mod id * 4`), with
  `ECX` the unit, `EDX` the mod id and stack the same unique-bit flag.
  That table holds only `FUN_004ac870` (mods 1, 12, 16, 36..39),
  `FUN_004acc70` (3), the function at `004acd50` (11), `FUN_004acd70` (26) and
  `FUN_004acdb0` (8, 9, 17, 18, 23, 25, 27, 28); the rest are null.
- The five slots of `0x724e28` are filled by the callers of
  `FUN_004ade40` (slot 0, one call, `004b001b`, right after the new
  unit's `FUN_00620210(unit, 0)` in the monster-message handler),
  `FUN_004ade50` (slot 1, `004b0d81`), `FUN_004ade60` (slot 2,
  `004b1556`, right after `FUN_004af2e0` in a per-unit update), `FUN_004ade70` (slot 3, `004b03ed` after
  a mode set of 3 and `004b0475` after a mode set of 0xd) and
  `FUN_004ade80` (slot 4, one call, `004cdb2b` inside the missile
  sequencer `FUN_004cd540`). Nothing was traced about when each of those
  call sites runs beyond that.

Entries of `0x724e28` read here (from the data xrefs of the handlers
below; the rest of the table is not enumerated): mod 10 slot 2
`FUN_004ad510`, mod 11 slot 2 `FUN_004ad5f0`, mod 17 slots 2 / 3
`FUN_004ad890` / `FUN_004ad8e0`, mod 18 slot 2 `FUN_004ad510`, mod 20
slot 2 `FUN_004ad920`, mod 23 slot 0 `FUN_004ad890`, mod 29 slot 4
`FUN_004ad970`, mod 31 slot 2 `FUN_004ad0c0`, mod 32 slot 2
`FUN_004adb20`, mod 33 slot 2 `FUN_004adbc0`, mod 40 slot 2
the function at `004add80`, mod 42 slot 2 `FUN_004ad510`. Mod ids are the same ones
`monsters.md` lists for the server hooks.

**Decompiler traps** in this cluster: Ghidra's player mode enum names
(`Death`, `Run`, `Kick`) are shown for monster modes. The constants in
the disassembly are monster modes: 0 death, 3 get-hit, 0xc (12) the
`dead` mode; no player meaning applies. Its names `Griswold`,
`TheSmith`, `Nihlathak`, `Duriel...` are leads only: the handlers below
are the client halves of `monsters.md`'s mods 17 / 20 / 29 / 11.

d2d: the server rules these mirror are ported (`Fight::boss_events`, see
below); the client dispatch table itself is not (d2d renders its own
effects). No disagreement found.

Not traced: the other table entries; when slots 0, 1 and 3 run in play;
what gating, if any, limits slot 2 on a dead unit per tick.

Re-check: `python3 tools/ghidra/fn.py 004add90 004ad020 004ade80`; disassemble
`004add90`..`004ade34` (index `m * 5 + slot`, `RET 0xc`, slot-4 branch at
`004ade10`) and read dwords at `0x724e28 + 4 * (m * 5 + slot)`.

### The resistance mods' client apply: `FUN_004acdb0` (batch 21)

`FUN_004acdb0` (0x26f bytes, `004acdb0`..`004ad018`, fastcall ECX unit /
EDX mod id, stack `apply` flag, `RET 4`, confirmed in disassembly). It
is the `FUN_004ad020` handler for mods 8, 9, 17, 18, 23, 25, 27, 28
(the `0x724d78` entries above). With `apply == 0` it returns. Otherwise
(stat ids as pushed to `FUN_00625480` get / `FUN_00627260` set, layer 0):

1. mod 0x1c (28): armorclass (stat 0x1f) is doubled.
2. It reads fire 0x27, lightning 0x29, cold 0x2b, poison 0x2d, damage
   resist 0x24 and magic resist 0x25, and counts how many are `>= 100`.
   With **2 or more** it stops (no mod below applies). The doubled AC of
   step 1 stays.
3. mod 9: fire `+75`; mod 0x12: cold `+75`; mod 0x11: lightning `+75`;
   mod 0x17: poison `+75` (no cap test, one stat, return); mod 0x19:
   magic resist `+20`.
4. mod 8: cold, then fire, then lightning, each `+40` only if it was
   under 100, adding 1 to the count when the new value reaches 100 and
   stopping once the count is 2.
5. mod 0x1b: the same order (cold, fire, lightning) with `+20`, each
   only if it was under 75 (`0x4b`), same count and stop.
6. mod 0x1c: damage resist `+50`, no cap.

The server counterparts are `monsters.md`'s mod rows; the client numbers
were read here only, no server value was compared.

d2d: not ported as a function; d2d has no client/server stat split.
No comparison made.

Not traced: which MonUMod names ids 8, 0x1b and 0x19 carry (the table
rows were not read).

Re-check: `python3 tools/ghidra/fn.py 004acdb0`; disassemble `004acdb0`..`004ad018`
(the compares against `0x64`, `0x4b` and the `ADD ..., 0x28 / 0x4b / 0x14 /
0x32` immediates).

### Four death-timed client effects of the modifier table: `FUN_004ad0c0`, `FUN_004ad510`, `FUN_004adb20`, `FUN_004adbc0` (batch 21)

The four below are table-`0x724e28` slot 2 handlers (per-unit update), fastcall
ECX unit / EDX mod id / stack unique flag, `RET 4`. Each calls
`FUN_00621810(unit, &frame, &total)` for the current frame and acts only
when the unit is null **or** its mode (`+0x10`) is 0 (dead), on frame 4.
Missile makers: `FUN_004cdb40` / `FUN_004cdba0` (ECX unit, EDX missile id,
stack x, y, skill, level); the difference between the two was not traced.

- **`FUN_004ad0c0`** (mod 31, 0xd7 bytes): five missile id `0x52` at
  `(x, y)`, `(x+1, y+1)`, `(x+1, y-1)`, `(x-1, y+1)`, `(x-1, y-1)`, skill
  0, level 1, then `FUN_004b9a00(ECX 0x99a, EDX unit, 0, 0, 0)` (a sound
  row). It needs the stack flag nonzero; the decompiler dropped the EDX
  mod id from its signature.
- **`FUN_004ad510`** (mods 10, 18, 42, 0xd4 bytes): returns at once for
  mod 18 **without** the unique flag. Mod 42, in mode 0 and with **no
  frame test**: missile 0x5a (nova) ring through `FUN_004c70d0(ECX unit,
  EDX unit, missile 0x5a, skill -1, 0, level, 0, velocity)`, level =
  unit stat 0xc (level) unchanged, velocity `FUN_00663270(0x5a, 0)`. Frame
  4: mod 10 makes missile 0x9b (`corpsepoisoncloud`) at the unit's spot,
  skill 0, level = stat 0xc; mod 18 makes the same ring with missile 0xc2
  (`coldunique`), level stat 0xc, velocity `FUN_00663270(0x77, 0)`. Against
  `monsters.md`: the server's coldunique level is mlvl / 2 and its mod 42
  nova level is the owner's Shadow Warrior level / 2 + 1; the client
  passes its own unit level straight through in both, and does not floor
  mod 10's level at 1.
- **`FUN_004adb20`** (mod 32, 0x98 bytes): missile 0x2a at the unit's
  spot, skill 0, level 1, on frame 4.
- **`FUN_004adbc0`** (mod 33, 0x11b bytes): reads the unit's MonStats row
  (class `+0x4`, stride 0x1a8, `[0x744304]+0xa78`, count `+0xa80`) and the
  signed word at row `+0x56 + 2 * difficulty` (`FUN_0044dcd0` the
  difficulty byte; `monsters.md` calls it aip1). On frame 4: value 1
  makes missile 0x1ab (`suicidefireexplode`) at level 1, value 2 or more
  makes 0x1ac (`suicideiceexplode`) at level 1, and also, whatever the value
  (0 or less included), missile 0x1aa (`suicidecorpseexplode`) is made at
  its spot. The server (`monsters.md`, `deaths.cpp:234`) picks only one of
  the three; the client adds the corpse one in addition. A visual-only
  difference between halves, nothing for d2d to change.

d2d: the server rules are ported (`Fight::golem_blast`, `shadow_nova`,
`suicide_blast`, `boss_events`, `test_on_death_mods`); the client looks
are d2d's own. No disagreement in a rule found.

Not traced: how often slot 2 runs on a dead unit (mod 42's nova has no
frame gate); what `FUN_004cdb40` does differently from `FUN_004cdba0`;
the sound row 0x99a.

Re-check: `python3 tools/ghidra/fn.py 004ad0c0 004ad510 004adb20 004adbc0`;
disassemble each and read the mode compare (`CMP [ESI+0x10], 0`), the frame
compare (`CMP ..., 4`) and the `MOV EDX, imm` missile ids.

### Duriel's death overlays and spawns: `FUN_004ad5f0` (batch 21)

`FUN_004ad5f0` (0xe9 bytes, `004ad5f0`..`004ad6d6`, table slot 2 of mod
11, fastcall ECX unit, `RET 4`; Ghidra calls it `DurielDeathAndSpawn`, a
lead). Needs a non-null monster unit with monster data (`+0x14`):

- mode 0 (dead), frame 13: `FUN_0046f170(unit)` (`gfx_overlays_remove_all`,
  `names.tsv`), then `FUN_00470390(unit, id, 5, 0, 0, 0, 0, 0)` twice with
  ids `0x34` and `0x9c`, then the byte at unit `+0x4e` is cleared. The
  two ids are what the decompiler calls the `durieldead` overlays.
- mode 0xc (12): the unit's seed (`+0x20` low, `+0x24` high) advances by
  the usual `low * 0x6ac690c5 + high` step; when the new low word is a
  multiple of 100 **and** monster data `+0x18` is under 100,
  `FUN_0046c320(unit, 0xe3, 4)` runs (spawn four monsters of class 0xe3
  around the unit, per the decompiler's name; the callee was not read
  here) and data `+0x18` grows by 4.
- other modes do nothing.

Against `monsters.md` ("13 rage, 11 durieldead | none"): that row is the
**server** hooks. The client has a handler for mod 11.

d2d: not ported; no comparison made.

Not traced: class 0xe3's identity; `FUN_0046c320`'s body; what mode 0xc is
on a Duriel unit in play.

Re-check: `python3 tools/ghidra/fn.py 004ad5f0`; disassemble `004ad5f0`..`004ad6d6`
(the `0xd` frame compare at `004ad62b`, the `0x6ac690c5` and `0x51eb851f`
(divide by 100) at `004ad680` / `004ad696`).

### The client's ring of directional missiles and its three callers: `FUN_004ad6e0`, `FUN_004ad890`, `FUN_004ad8e0`, `FUN_004ad920` (batch 21)

- **`FUN_004ad6e0`** (0x19a bytes, `004ad6e0`..`004ad881`): a **custom
  convention** the decompiler lists as stack-only: stack `unit`
  (`[EBP+8]`), `missile id` (`[EBP+0xc]`), and **EAX** the level (0 is
  replaced by 1), `RET 8`. Missile 0xc3 (`lightunique`) only for a
  monster whose data word `+0x16` has bit `0x100` set; other ids have no
  gate. It zeroes a 0x5c-byte record and fills: `+0x00` flags `0x21`,
  `+0x04` the unit, `+0x10` the missile id, `+0x14` / `+0x18` the unit's
  x / y (`FUN_006488c0` / `FUN_00648900` on its path, or the static
  path words at `+0xc` / `+0x10` for unit types 2, 4 and 5), `+0x1c` /
  `+0x20` the same plus a direction, `+0x2c` skill 0, `+0x30` the level,
  `+0x54` the function pointer `0x4e2ed0` (not set for missile 0x141,
  `queenpoisoncloud`) and `+0x58` the copy index. The same 0x5c-byte
  shape `FUN_004cd540` takes (batch 13). It loops 4 directions
  `(0,-1)`, `(1,0)`, `(0,1)`, `(-1,0)`, two copies each (copy index 0, 1),
  calling `FUN_004cd540(record)`: 8 missiles, as `monsters.md` row 17
  says for the server ("N, E, S, W x 2").
- **`FUN_004ad890`** (mod 17 slot 2 and mod 23 slot 0): with the flag
  nonzero, mode 3 and frame 2, calls the ring with 0xc3 and level =
  unit stat 0xc >> 1 (arithmetic shift; no floor at 1, unlike the
  server's "at least 1").
- **`FUN_004ad8e0`** (mod 17 slot 3): with the flag nonzero and the unit
  null or **not** in mode 3: the same ring, same level. Unlike the
  server, it keeps no 10-frame limit that was visible in this function.
- **`FUN_004ad920`** (mod 20 slot 2): in mode 0, 3 or a null unit and
  frame 2: ring of missile 0xe1 (`buglightning`) at level = stat 0xc
  (no flag test, no `0x100` gate).

Mod 23's slot 0 entry points at `FUN_004ad890`, a function that tests mode 3
and frame 2 (the mod 17 slot 2 entry is the same address). `monsters.md` lists mod 23 as the poison
hit with only an init hook. Whether that table slot is a copy mistake in
the original or does something at slot 0's call site was not traced.

d2d: server halves ported (`deaths.cpp` `charged_bolts("lightunique" ..)`
and `"buglightning"`); the client ring is not. No disagreement found.

Not traced: the meaning of the `0x100` bit in monster data `+0x16`;
`FUN_004cd540`'s use of `+0x1c` / `+0x20`; `0x4e2ed0`'s body.

Re-check: `python3 tools/ghidra/fn.py 004ad6e0 004ad890 004ad8e0 004ad920`;
disassemble `004ad6e0`..`004ad881` (the `CMP [EBP+0xc], 0xc3` gate at
`004ad6f4`) and the three callers' `PUSH 0xc3` / `0xe1` at `004ad8c5`,
`004ad900`, `004ad957`.

### The client mirror of multishot: `FUN_004ad970` (batch 21)

`FUN_004ad970` (0x1aa bytes, `004ad970`..`004adb10`, mod 29 slot 4,
fastcall ECX the **missile** (slot 4 passes `unit2`), stack flag, `RET 4`;
Ghidra's name `Nihlathak` is a lead and is wrong for mod 29). With the
unique flag set and a type-3 unit, it takes the missile's owner
(`FUN_004639d0`, ECX the missile), requires: the missile's class in range
of `[0x744304]+0xb6c` (rows of 0x1a4 from `+0xb64`), the row byte `+5`
**without** the bit `0x10` (the byte at `0x6ce278` holds `0x10`), and the
owner not already flagged `0x80` (`FUN_004ac7e0(owner, 0x80)` is zero).
Then the target is the owner's confirmed target unit's position
(`FUN_004648f0`), else the missile's path words (`FUN_00648a00` /
`FUN_00648a10`). Sign of `(owner - target)` in x and y gives `sx`, `sy`
(both zeroed for missile classes `0x3f..0x42`). It sets monster data
word `+0x16 |= 0x80`, makes the same missile class (`FUN_004cdba0`, with
the original missile's `FUN_0064a210` / `FUN_0064a280` values as the last
two stack arguments) at `(tx - sy, ty + sx)` and `(tx + sy, ty - sx)`,
then clears `0x80` (`AND 0xff7f`). The two spots are one subtile either
side of the target across the owner's line, as `monsters.md` row 29
describes. The seed block it builds (`0x53454953`, the owner GUID)
is not read afterwards.

d2d: server half described in `monsters.md`; client mirror not ported.

Not traced: the meaning of `FUN_0064a210` / `FUN_0064a280` beyond their use
as the level and skill the decompiler names.

Re-check: `python3 tools/ghidra/fn.py 004ad970`; disassemble `004ad970`..`004adb10`.

### Two more UNIT leaf effects: `FUN_004adce0` and `FUN_004ae3c0` (batch 21)

- **`FUN_004adce0`** (0x97 bytes, `004adce0`..`004add76`, plain `RET`,
  fastcall ECX unit): the unit's x / y (path words, as above), then
  `FUN_004cdb40` with missile `0x75` (`monstercorpseexplode`) and again
  with `0x221` at `(x, y)` skill 0 level 1, then the sound row `0x99a`
  (`FUN_004b9a00(0x99a, unit, 0, 0, 0)`). Callers: the function at `004add80` (a
  `CALL`/`RET 4` stub, table mod 40 slot 2, the "worms on death" mod
  `monsters.md` marks as unreachable on the server) and the function at `004d93a0`
  (a state-removal function; not read here).
- **`FUN_004ae3c0`** (0x39 bytes, `004ae3c0`..`004ae3f8`, fastcall ECX
  unit, plain `RET`): `FUN_004639d0(unit)` (the owner); null returns. A
  non-missile owner (`*owner != 3`) raises the internal-error path with
  line `0x161` (the `FUN_00408090` / `FUN_00408a60` / exit sequence; the
  string argument at `0x6cc837` is an empty string, so no file name is
  given). Otherwise `FUN_004d2d70(owner, 0, 0)`, the client's
  hit and explosion (`missiles.md`). Callers: inside `FUN_004af2e0`
  (`004af3ac`) and `FUN_004af6a0` (`004af7f0`).

d2d: not ported as functions; no comparison made.

Not traced: what the owner of a monster-skill missile is in the two
callers; the function at `004d93a0`'s state.

Re-check: `python3 tools/ghidra/fn.py 004adce0 004add80 004ae3c0`; disassemble
`004adce0`..`004add76` and `004ae3c0`..`004ae3f8`.

### A catapult-search callback and the monster free: `FUN_004ae7b0`, `FUN_004aed30` (batch 21)

- **`FUN_004ae7b0`** (0x111 bytes, `004ae7b0`..`004ae8b3`): a callback for
  `FUN_0046c570` (the room-neighbour unit walk: ECX the centre unit, EDX
  a parameter, stack callback; it calls the callback with ECX centre, EDX
  each unit of the neighbouring rooms' lists (`+0x74`, next at `+0xe8`),
  two stack words, `RET 8`; the first nonzero result stops it). Its only
  reference is `004aecca` in `FUN_004ae8d0`: for a monster of class
  `0x12d` the result is stored as that unit's direction byte (class
  `0x60` stores the fixed 7 instead). The callback returns 0 unless the
  candidate (EDX) is a monster whose MonStats row word `+0x2` is `0x12a`,
  `FUN_006416d0(centre, candidate)` is at most 1, and the centre's
  x / y equal the candidate's x / y plus 3 times the composite offsets
  from `FUN_0063e7e0` of the direction after `FUN_0063e7a0` remaps it
  through the byte table `6,7,0,1,2,3,4,5`. It returns the candidate's
  **original** direction (`FUN_006487f0` on its path) as a byte.
  Ghidra's `CheckCatapultFiringPosition` is a lead; the class numbers
  are as read.
- **`FUN_004aed30`** (0x97 bytes, `004aed30`..`004aedc6`, fastcall ECX
  unit, plain `RET`): null returns. In order: `FUN_0046ec10` (ECX the
  unit), `FUN_00466cb0(unit, 1)`, free the inventory (`FUN_0063ac40` on
  `+0x60`, then `+0x60 = 0`); when the global act pointer `0x7a0634` is set:
  `FUN_00649f50`, `FUN_0064c370` (path collision and room removal) and
  `FUN_00464930(unit)`; free the path (`FUN_00648460(0, +0x2c)`, `+0x2c =
  0`); swap the light map (`FUN_00643a00(unit, 0)`) and free a returned
  one with `FUN_004743d0`; for a monster with monster data whose `+0x2c`
  pointer is set, free that block (`FUN_0040b3c0` with the string at
  `0x6da5c8`, `".\UNIT\Monster.cpp"`, line `0x66`) and zero it. Single
  caller `FUN_00465870` (`FreeAny`) at `00465903`.

d2d: not ported; d2d has no client unit pool.

Not traced: the bodies of `FUN_0046ec10`, `FUN_00466cb0`, `FUN_0064c370`;
what class `0x12d` / `0x12a` are by name.

Re-check: `python3 tools/ghidra/fn.py 004ae7b0 004aed30 0046c570`; disassemble
`004ae7b0`..`004ae8b3` (the `CMP ECX, 0x12a` at `004ae809`) and read
`004aecb7`..`004aecda`.
### Bleeding monsters' blood-trail missiles: `FUN_004af890` (batch 20)

`FUN_004af890` (0x2ce bytes, `004af890`..`004afb5b`, `RET 4`): fastcall
**ECX** unit, **EDX** default count, one **stack** flag (the decompile's
third parameter, confirmed at the seven call sites: `PUSH 0` or `PUSH 1`
before the call). It drops blood missiles round a bleeding monster:

1. **Throttle.** With the stack flag nonzero the unit's seed (`+0x20` low,
   `+0x24` high) advances once (`low' = lo32(low * 0x6ac690c5 + high)`,
   `high' = carry`, the usual game LCG) and the function returns unless
   `low' % 25 == 0`.
2. **Hit-stagger gate.** For a monster (`unit +0` = 1) whose mode (`unit
   +0x10`) is **3** the seed advances again and the function returns unless
   `low' % 3 == 0`. The decompile calls mode 3 "Run" (the *player* enum);
   `004af8dc` is a bare `CMP [ESI+0x10],3`, and for a monster that value is
   the get-hit mode, which is where `FUN_004aff60`'s case 6 puts the unit
   just before the call (see the burrow entry below for the dispatcher).
3. **Count.** `FUN_00625500(unit, 0x8c, 0)` (stat 0x8c, the decompile calls
   it `item_extrablood`) replaces the passed default when nonzero.
4. **Gate on the row.** MonStats row (`+4` class id, bounds `[0x744304]+0xa80`,
   table `+0xa78`, stride `0x1a8`) → word `+0x18` → MonStats2 row (bounds
   `+0xa98`, table `+0xa90`, stride `0x134`); the drops happen only if the
   MonStats2 byte `+0x11d` (unsigned) is above 0.
5. **Per drop** (count times): if the unit has a path (`FUN_006486c0` true)
   and a direction byte (`FUN_00620100`), slot =
   `(low' + low'/7 + (dir >> 3) - 3) & 7` (the `/7` is the usual
   `0x24924925` multiply-high with the add fix-up, i.e. floor division on
   the unsigned seed); otherwise slot = `low' & 7`. Either way the seed has
   advanced once for it. The 8 offsets are two int tables, **x** at
   `006da610` = `0,-1,-2,-1,0,1,2,1` and **y** at `006da5f0` =
   `2,1,0,-1,-2,-1,0,1` (read with `read_memory`). From the slot upward
   (wrapping `& 7`) the first cell for which `FUN_0064cb30(room, x+dx, y+dy,
   1)` returns 0 in `AX` (the collision mask 1 test; only the low 16 bits
   are tested) is used. The scan stops *before* it reaches `(slot - 1) & 7`,
   so only 7 of the 8 cells are ever tried, and if all are blocked the
   function returns, abandoning the remaining drops too.
6. **Missile.** `FUN_004cdba0(unit, 0x12 + roll, x+dx, y+dy, 0, 1)` (ECX
   unit, EDX id, stack x, y, skill 0, level 1). `roll` = 0 if `byBleed` is
   0 (cannot happen past the gate), else a seed draw below `2 * byBleed`
   (`& (n-1)` when `n` is a power of two, `% n` otherwise; both advance the
   seed the same way). Missile id 0x12 is the decompile's `MISSILE_Blood1`.

Call sites (all read in disassembly): `004c7cd4` and `004c7f34` (count 5,
flag 0), `004ca051` (1, 0), `004d9753` (5, 0), `004f118b` (3, 0),
`004b03df` (1, 0) and `004b044d` (1, **1**), the last two in
`FUN_004aff60`: case 6 (the plain hit; skipped when the message carries a nonzero life
value equal to the unit's current life; the unit is already in mode 3, so
the 1/3 gate always applies) and case 0x13 (the second hit message, the
only throttled caller).

Not traced: which MonStats2 column `+0x11d` is, stat 0x8c's row in
ItemStatCost, missile 0x12's row. d2d: no equivalent (`git grep` finds no
blood-trail code); not ported. Cited, not oracle-checked: the function
needs rooms, collision and the missile allocator.

Re-check: `python3 tools/ghidra/fn.py 004af890`; disassemble `004af890`..
`004afb5b`; read 32 bytes at `006da610` and `006da5f0`.

### The burrow / emerge hook of one monster class: `FUN_004afce0` (batch 20)

`FUN_004afce0` (0x27b bytes, `004afce0`..`004aff58`, plain `RET`): **EAX**
message type, **ESI** unit (naked, confirmed: `004afce3` onward reads
`[ESI+...]` with no load). Its two callers are in `FUN_004aff60`:
`004b0213` (the group that handles messages 5, 0xa, 0xd, 0xf, 0x10, 0x1b,
0x1d, after `UNITMODE_SetAnimMode`) and `004b0347` (4, 0xb, 0xc, 0xe, 0x11,
0x1a, 0x1c); both are gated by `cmp base-id, 0x6e` (`004b020b`, `004b033b`),
where the base id is `TXT_MonStats_GetBaseId`'s result. (`collision.md`
earlier calls base id 0x6e a vulture; the same dispatcher gives it a vulture
death overlay; no MonStats name was read here.)

- **Message 0xc, the dive:** unit flags (`+0xc4`) `&= ~0xe`; removes
  collision flags `0x1000` then `0x100` at the unit's cell
  (`FUN_0064cbe0(room, x, y, flags)`; the room is `FUN_00620bb0(unit)`);
  `FUN_00649190(unit, 5)` (width 5), `FUN_00648ce0(path, 0)` (collision
  pattern 0). If `FUN_004da0f0(unit)` is 0 (no particle yet) it allocates
  one (`FUN_004da000`), sets fade-stop (`004da690`), reverse = 1
  (`004da6e0`, EDX 1), offset vector `(0, 0, 2, 1)` (`004da200`) and bounds
  `(0, 0, 0xa0, 0)` (`004da2a0`); if one already exists it returns.
- **Messages 0xe and 0xa, the surfacing:** flags `|= 0xe`; adds `0x1000`
  then `0x100` back (`FUN_0064cb90`); width 1; pattern **0x3c01**; and only
  if a particle exists: fade-stop, reverse = 0, offset `(0, 0, -8, 1)`,
  bounds `(0, 0, 0, 0)`.
- Any other message type returns without doing anything.
- Position is read per unit type: types 2, 4, 5 from the static path
  (`+0xc` x, `+0x10` y), others through `FUN_006488c0` / `FUN_00648900`
  (0 when no path).

d2d: not ported; the burrowing animation exists (`world_view.hpp:81`, a
reverse-playing animation) but no collision swap on dive/surface was
checked. Not traced: which flag bits `0xe` at `+0xc4` are (the decompile
names them valid-target / can-be-attacked / targetable), the particle
setters' fields.

Re-check: `python3 tools/ghidra/fn.py 004afce0 004aff60`; disassemble
`004afce0`..`004aff58` and the two call sites.

### The server's NPC trade-table setup and teardown: `FUN_00535fb0`, `FUN_00536640`, `FUN_00536850` (batch 20)

All three are in `UNIT\sunitproxy.cpp` (the file-name literal is read at
`00535ff8`, `0053603f`, `00536684`.., `00536899`). Server only; d2d has no
server component, so documented, not ported.

- `FUN_00535fb0` (`InitNpcTradeItems`; custom registers **EAX** table
  index, **EBX** game, **EDI** destination): index above 0x10 raises the
  internal error with line 0xa4. Otherwise copies the table row's trade
  count, allocates `count * 0xc` bytes from the game's pool (line 0xa8) and
  copies that many bytes of the row's trade array; then the same for the
  quest list at 4 bytes a record (line 0xab). The 17-row table is the
  decompile's `gaNpcItemTables`, its address not read here.
- `FUN_00536640` (`FreeNpcControl`, fastcall ECX game): for each of
  `nNpcCount` NPC entries (stride `0x44`) frees the interaction list (line
  0x21e), the proxy data (`SUNITPROXY_FreeNpcProxyData`), two further
  blocks (0x227, 0x22c) and a fourth (0x237), nulling the pointers; then
  frees the entry array (0x242) and the control block (0x243) and nulls
  `pGame->pNpcControl`.
- `FUN_00536850` (`RefreshGambleForAllNpcs`; ECX game, stack tick, `RET 4`):
  for each NPC entry whose three state bytes/words (`+0x1c` == 1, `+0x20`
  == 1, `+0x23` == 1) all hold, pushes a 0x14-byte node (line 0x263)
  `{tick, 4, 1, 0, next}` onto the head of the list at entry `+0x14`.
  `GetTickCount()` is called and its result unused.

Not traced: what the three state fields are, what event kind 4 is.

### A server unit's inactive-list bookkeeping: `FUN_005418c0`, `FUN_00541ac0`, `FUN_005429b0`, `FUN_00543080` (batch 20)

`UNIT\SUnitInactive.cpp`. The game keeps one list per act
(`pGame->pInactiveUnitList[5]`) of nodes keyed by a room's tile
x/y holding that room's despawned units, items and monsters.

- `FUN_005418c0` (`FindAndRemoveInactiveUnitsByRoom`; stack out-pointers
  ×2, **EAX** room, **EBX** game, `RET 8`): clears both out-pointers, takes
  the room's level → act and its coordinates, walks the act's list while
  `room.xStart <= node.x` (so the list is sorted by x descending: an
  inference from the exit condition) and on an exact x and y match unlinks
  the node, returns its unit and item chains, and frees the node (line
  0xb6). No match leaves both null.
- `FUN_00541ac0` (`RestoreInactiveItemList`; **EBX** game, stack room,
  **EAX** chain, `RET 4`): per record, restores the item unless its
  despawn timer is nonzero and already passed (`timer != 0 && frame >
  timer` skips), then frees the record either way (line 0x126).
- `FUN_005429b0` (`CheckDeadMonsterAtCoords`; fastcall ECX game, EDX room,
  stack x, y, class): null game, null room or a zero x/y raises the
  internal error with lines 0x6da / 0x6db / 0x6dc. Finds the live room at
  (x, y) (nearby-room lookup, else by subtile in the act) and returns 1 for
  a monster of that class there with the mode-zero-death test true;
  otherwise looks up the inactive node for (act, x, y) and returns 1 for a
  monster record of that class with type-flag bit 4; else 0. The act comes
  from the *argument* room's level, not the found room.
- `FUN_00543080` (`FreeAllInactiveLists`; fastcall ECX game): for each of
  the 5 acts frees every node's unit chain (line 0x29a), item chain
  (0x2a4), monster list (`FreeInactiveMonsterList`) and the node (0x2ab).

Not traced: the record layouts beyond the offsets the decompile uses, the
callers of `FUN_005429b0`.

### Server unit lifecycle helpers: `FUN_00553590`, `FUN_00553c70`, `FUN_00553f00`, `FUN_005545c0`, `FUN_00555580` (batch 20)

`UNIT\SUnit.cpp`; server only, documented, not ported.

- `FUN_00553590` (`LinkPortalCoords`; fastcall ECX unit, EDX unit2): null
  unit / unit2 raise lines 0x753 / 0x755. Each portal's object data stores
  the other's x and y; each then stores the room tile-rect top-left of the
  other's room (when it has one) into two of its own fields. The decompile
  labels one of them as a monster-data field on an object, so the field
  names are unreliable; the offsets were not re-read.
- `FUN_00553c70` (`SetupAnimTimerEventsWithDelay`; fastcall ECX game, EDX
  unit, stack delay): does nothing for `delay <= 0`. Otherwise removes the
  unit's mode-change and end-anim timers, rewinds `nGfxCurrentFrame` by the
  delay and re-schedules events 1..4 at their frames (frame-flag table or
  the sequence's `ANIM_GetEventFrame`), then the end-of-animation event
  (kind 1) one frame after the last. Speed 0 schedules only that event, one
  frame ahead. Callers (4): `005c8ca0`, `005c8e30`, `005d68a0`, `005d69d0`.
- `FUN_00553f00` (`ClearAnimAndTriggerEndAnim`, 0x21 bytes): removes the
  mode-change timers, then fires the mode-change event at once.
- `FUN_005545c0` (`GetTargetRoom`): the path's next room if it is still one
  of the act's rooms (list walk), else resets the next room and returns 0.
- `FUN_00555580` (`UnlinkUnitFromRoom`; ECX unit, EDX game, stack flag): null
  unit / type >= 6 raise 0x3ac / 0x3ad. A player first clears its
  interaction; players, monsters, objects and missiles remove their path
  collision; all types then leave the room (skipped when the flag is set)
  and the clients are notified.

Not traced: the event kinds' consumers, `FUN_00553c70`'s callers' reasons.

### Server NPC inventory, hire list and scrolls: `FUN_005762c0`, `FUN_005766d0`, `FUN_00576770`, `FUN_00576e40` (batch 20)

`UNIT\SUNITNPC.CPP`; server only.

- `FUN_005762c0` (`SetItemForNpc`): marks the item identified; for a
  vendor item also sets the vendor flag, sets the reload flag if it has
  sockets, refreshes the NPC's inventory and queues the item for update.
  A non-vendor item is only identified.
- `FUN_005766d0` (`FillNpcInventory`; fastcall): null game / unit / item
  raise lines 0x1a7 / 0x1a8 / 0x1a9. Takes the item to the NPC's cursor;
  with `bDeleteItem` it flags it, refreshes, queues it and sets the
  delete command; otherwise frees it.
- `FUN_00576770` (`SendMercForHireList`; fastcall, game, client, NPC,
  first-interaction flag): only for NPC classes **0x96, 0xc6, 0x203,
  0xfc**: sends state command 0x4f, finds the NPC's game record, builds its
  hireling slots on first use, then for the hireling table row
  `(expansion, class)` sends one **0x4E** packet `{name string id, seed}`
  for each of the `nameLast - nameFirst + 1` slots whose field at `+0xc` is
  set and field at `+8` is clear. A missing row asserts line 0x1fb; a
  count of -1 asserts 0x60. The decompile's `_DAT_00000000 = 0` there was
  not read in the disassembly.
- `FUN_00576e40` (`ApplyScrollSkill`; fastcall ECX unit, EDX skill, stack
  quantity used): for a valid skill the unit has, subtracts the quantity; at
  zero or below it clamps to 0 and, if it was the right-hand skill,
  reselects the default attack; stores the quantity and sends packet 0x22
  to the owner (client lookup with line 0x40b).

Not traced: the callers' item flag meanings, which NPC each class id is.

### Damage helpers: monster-type equivalence and the item AC rules: `FUN_0057a830`, `FUN_0057b6a0`, `FUN_0057d8a0` (batch 20)

`UNIT\SUnitDmg.cpp`; the combat core, relevant to d2d's `fight` rules.

- `FUN_0057a830` (fastcall ECX type A, EDX type B): both must be 1..
  `nTxtMonTypeSize-1` and positive; returns bit A of row B of the MonType
  equivalence matrix (`dwords per row` field × B dwords, bit mask table
  `gadwBitMasks`), else 0.
- `FUN_0057b6a0` (**EAX** target, stack attacker, stack damage struct; the
  attacker's 0x400-byte stat list on the stack): for a monster target
  whose MonStats `MonType` word (`+0x1c`, stride `0x1a8`) is positive,
  reads the attacker's list of stat 0xb4 values (up to 0x80 entries,
  subindex = a MonType id) and, for each entry whose subindex is
  equivalent to the target's type by the same matrix bit as above (row =
  target's type), adds its value to the damage struct's `dwEnDmgPct`.
- `FUN_0057d8a0` (**EBX** attacker, **EDI** defender, stack to-hit pointer,
  stack AC pointer): (1) stat 0x73 (`item_ignoretargetac`) nonzero and the
  defender a monster that is not monster-type 10, not a boss and not a
  merc: AC = 0. (2) stat `item_fractionaltargetac` percent `p > 0`: halved
  (integer) against players, bosses, monster-type 2 and mercs, forced
  to 0 if below 1, capped at 100, then AC -= `D2ApplyPercent(AC, p, 100)`.
  (3) `item_demon_tohit` added to the to-hit if the defender is a demon;
  `item_undead_tohit` likewise for undead. The three stat ids other than
  0x73 are the decompile's names; their numbers were not read.
- Callers: `FUN_0057a830` ← `0057d9b0`, `005a89a0`; `FUN_0057d8a0` ←
  `0057d9b0`; `FUN_0057b6a0` ← `0057b7d0`.

d2d: `git grep` finds no `ignoretargetac`, `fractionaltargetac`,
`demon_tohit` or `undead_tohit` in `components/` or `apps/`, so item
ignore-target-AC / to-hit-vs-demon items have no d2d rule; **not checked
whether d2d reads those stats under another name**. Not traced: monster
type codes 2 and 10, the 0xb4 stat's row.

Re-check: `python3 tools/ghidra/fn.py 0057a830 0057b6a0 0057d8a0`; disassemble
`0057b6a0`..`0057b710` (target in EAX, `MonType` at `+0x1c`) and
`0057d8a0`..`0057d8c0` (stat 0x73 on EBX).

### Client player and unit helpers: `FUN_00460d50`, `FUN_00460de0`, `FUN_00461010`, `FUN_004611f0`, `FUN_004619e0`, `FUN_00461ac0`, `FUN_00462370`, `FUN_00463810`, `FUN_00463180`, `FUN_00464420`, `FUN_004644e0`, `FUN_00464600`, `FUN_00465420`, `FUN_00465bf0`, `FUN_00465f20` (batch 24)

Read from disassembly (conventions and constants); decompiler names
are leads. `0x7a6a70` is the client's player-unit pointer (`pPlayerUnit`),
`0x7a0634` the client act pointer. Unit fields used: `+0` type, `+0xc`
GUID, `+0x10` mode, `+0x14` unit data, `+0x2c` path, `+0x60` inventory,
`+0xc4` flags. Player mode numbers: 0 death, 1 neutral, 2 walk, 3 run,
5 town neutral, 6 town walk, 0xd..0x10 skill 1..4, 0x12 sequence. The
"position" idiom below is repeated in these functions: unit types 2, 4 and
5 read the static path (`+0xc` x, `+0x10` y); any other type reads the
dynamic path through `0x6488c0` (x) and `0x648900` (y), or 0 when the path
is null.

- `FUN_00460d50` (fastcall ECX unit; 0x83 bytes): the client's player-unit
  free. Calls `0x46c700` and `0x46ec10`; if the act pointer is non-null,
  `0x649f50` (remove collision) and `0x64c370` (remove from room); frees the
  static path (`0x648460`, `+0x2c` zeroed), `FUN_00466cb0` with EDX 0,
  frees the inventory (`0x63ac40`, `+0x60` zeroed), swaps the skill light
  map for null (`0x643a00`) and frees the old one (`0x4743d0`),
  `0x464930`; if the unit is `pPlayerUnit` calls `0x463d90` with ECX 0
  (clears it); `0x622060` frees the player data; ends with a tail jump to
  `0x4608c0`. Caller: `0x465870`.
- `FUN_00460de0` (**ESI** unit, plain RET): reads the unit's position (idiom
  above), `0x620bb0` (room) and `0x620f50` (needs-room-update flag), then
  `0x61acd0(room, x, y, flag)` (stack args, the pops update). Caller:
  `0x463390`.
- `FUN_00461010` (**ESI** unit; the client player-death handler): `0x4c1e30`;
  `0x649f70(unit, 0)` (corpse collision); `0x464810`; clears flag `0x4` in
  `+0xc4`; if state 7 is not set (`0x639df0`) clears flag `0x2` too;
  `0x46ece0`; `0x46f020` with DL 0; `0x627260(unit, 6, 0, 0)` sets stat 6
  to 0 (hit points). Caller: `0x461250`.
- `FUN_004611f0` (**EDI** unit,
  one stack arg, RET 4; `isLocalPlayer`): the current skill (`0x620250`);
  if null does nothing. If skill flags (`0x6446a0`) bit 0 is set: sets bit 1
  (`0x644660`) and calls `0x4c68f0` (ECX unit). Else, when the stack arg is
  0 and the unit is null or has `+0xc4` bit 6 clear, calls `0x4c68f0`.
  Callers: `0x461250`, `0x463260`, `0x463390`.
- `FUN_004619e0` (**ESI** pUnderMouse, **EDI** target GUID, plain RET): the
  mouse struct has `+4` player, `+8` target, `+0x18` attack action. If
  `0x641530(target, player)` (distance) is <= 2 and
  `0x622b50(target, player, 0x804)` returns 0, calls `0x481030` with ECX
  `0x13` (interact) and the player in EDX; otherwise `0x481030` with ECX
  `+0x18`. Then, for a player whose unit data is non-null, sets in the player
  data `+0x150 = 1`, `+0x154 = 0x13`, `+0x158 = 0`, `+0x15c = GUID`
  (a pending interact). Callers: `0x461a70`, `0x4625b0`.
- `FUN_00461ac0` (fastcall ECX skill, EDX unit, stack pnRange and pnDist,
  RET 8): `pPlayerUnit`, the skill and the unit must each be non-null, and the
  skills row (`0x644140`) non-null. `*pnRange` = skill calc (`0x646ca0`)
  of the row's field at `+0x64` (aura range calc; the skill id is the
  row's word at `+0`) at the player's skill level (`0x6442a0`, 1);
  `*pnDist` = `0x6416d0(player, unit)`; returns `dist <= range`. Callers:
  `0x461b40`, `0x480c80`.
- `FUN_00462370` (**ESI** pUnderMouse, plain RET): only for a player-type
  unit (mouse `+4`, type 0) with no confirmed target (`0x4648f0`): mode 2
  or 6 calls `FUN_00461840` with x (`+0xc`, ECX), y (`+0x10`, EAX) and stack
  1; mode 3 calls it with stack 3; other modes do nothing. Caller:
  `0x462b40`.
- `FUN_00463810` (fastcall ECX unit): for a monster (type 1) with unit data,
  returns the data field at `+0x2c` (a name pointer); else null. Caller
  `0x464a60`.
- `FUN_00463180` (fastcall ECX unit, EDX room, stack x and y, RET 8): saves
  mode (`+0x10`) and path; if mode is 2, 3 or 6 remembers it and fetches
  the confirmed target (`0x4648f0`) or, with none, the move coordinates
  (`0x4c52e0`). Calls `0x650c60(path, unit, room, x, y)` (the warp itself,
  stack args). If it was moving, builds a 7-dword message (zero except the
  first two dwords) and calls `FUN_00461250` (ECX message code, EDX unit,
  stack message and 0): with a target the first two dwords are the target's
  type and GUID (`+0` and `+0xc`) and the code is 0x18 when mode is 3, else
  0; with coordinates the dwords are x and y, and the code is 0x17 when
  mode is 3, else 1. Caller: `0x4804e0`.
- `FUN_00464420` (fastcall ECX unit; turn to face `pPlayerUnit`): a null
  dynamic path halts (assert line 0x7f3); `0x624690(unit, 1)`; then
  `0x649ef0(path, playerX, playerY, 0)` using the position idiom above
  for the player. Caller: `0x4b6dd0`.
- `FUN_004644e0` (fastcall ECX unit; the reverse, turn the player to face a
  unit): nothing unless `pPlayerUnit` is non-null and in mode 1 or 5; a null
  player path halts (line 0x802); `0x649ef0(playerPath, unitX, unitY, 0)`.
  Caller: `0x4b6dd0`.
- `FUN_00464600` (fastcall ECX unit, EDX skill; "can the player be
  interrupted"): returns 0 if the hand (cursor) item (`0x63c1e0` of
  `+0x60`) exists or the unit type is not 0. Then a jump table over mode
  (a byte index table at `0x464684`, 20 entries, and dword targets at
  `0x46466c`; read from memory): modes 0, 4, 7..0xc, 0x11, 0x13 return 0;
  modes 1, 2, 3, 5, 6 and above 0x13 return 1; mode 0xd returns 0 for
  class 3, 0xe for class 6; modes 0xf and 0x10 return 0 for classes 4, 5
  and 6; mode 0x12 returns `FUN_004645b0(skill)`'s result. Callers:
  `0x4629a0`, `0x462a20`, `0x462ba0`, `0x462c20`.
- `FUN_00465420` (fastcall ECX x, EDX y, plain RET; the client room finder):
  returns null when both are 0. With `pPlayerUnit` non-null: takes the
  player's room (`0x620bb0`) and `FUN_00463740(room, x, y)`; a non-null result
  wins. Else `0x619da0(act, x, y)` (room by subtile). If still none,
  formats "Unable to find room for unit...  hControlUnit:%8x  hUnitRoom:%8x
  ghAct:%8x  nX:%d  nY:%d" (string at `0x6d6a60`) and halts (line 0x13c).
  Callers: `0x4654c0`, `0x465fd0`, `0x466200`, `0x466360`, `0x4c2540`,
  `0x4c26f0`.
- `FUN_00465bf0` (fastcall ECX unit, EDX immediate flag, plain RET): `0x480420`
  (confirm target); null path halts (line 0x946); target = `0x648bf0(path)`;
  if non-null and not the unit itself, `0x649ef0(path, x, y, flag)` with x from
  `0x45adf0` and y from `0x45ae20` of the target (a second null-path check
  halts with line 0x7e2). Callers: `0x4ae1d0`, `0x4ae4f0`, `0x4af6a0`,
  `0x4aff60`, `0x4c6eb0`, `0x4c8ed0`, `0x4c9b40`, `0x4efdf0`.
- `FUN_00465f20` (**EAX** unit, plain RET; server-side unit creation
  finish): `FUN_00463a50(unit)` inserts it; on failure looks up the same
  GUID (`FUN_00463940`) in the table at `0x7a5e70 + type * 0x200` (128
  pointers a type; the **server** table, in the first lookup, whatever the unit's flags);
  finding the unit itself halts (line 0x1c6). If another unit holds the GUID,
  it is removed (`FUN_00465e80`; the table is `0x7a5270` instead when its
  flag `0x200000` is set, the client table) and insertion retried. If it
  still fails, `FUN_00465870` frees the new unit and null is returned.
  Callers: `0x465fd0`, `0x466200`, `0x466360`.

### MonUnique frame modifiers and two monster helpers: `FUN_004ad890`, `FUN_004ad8e0`, `FUN_004ad920`, `FUN_004ade80`, `FUN_004ae3c0` (batch 24)

Monster modes here are the **monster** numbering (0 death, 3 get-hit, per
the usual trap): the decompile prints the player names (Run, Death).

- `FUN_004ad890` (fastcall ECX unit, one stack arg, RET 4; the stack arg is
  both the "modifier enabled" flag and, after `0x621810(unit, &arg, &total)`,
  overwritten with the current frame): if the arg was non-zero, the unit is
  non-null in mode 3 and the frame is 2, calls `FUN_004ad6e0(unit, 0xc3)` with
  EAX = the unit's stat 0xc (`0x625480(unit, 0xc, 0)`) shifted right by 1.
  The decompile's `nUMod` is not read.
- `FUN_004ad8e0` (ECX unit, stack flag, RET 4): flag non-zero and the unit
  null or not in mode 3: the same `FUN_004ad6e0(unit, 0xc3)` with stat 0xc >> 1.
- `FUN_004ad920` (ECX unit, one ignored stack arg, RET 4): frame from
  `0x621810`; if the unit is null, or in mode 3 or 0, and the frame is 2:
  `FUN_004ad6e0(unit, 0xe1)` with EAX = stat 0xc (unshifted). The missile ids
  0xc3 and 0xe1 were not looked up in Missiles.txt; the decompile's names
  are leads.
- `FUN_004ade80` (fastcall ECX attacker, EDX other): both non-null and ECX
  a monster (type 1): `FUN_004add90(ECX, EDX, 4)`. Caller `0x4cd540`.
- `FUN_004ae3c0` (fastcall ECX unit): the missile's owner (`0x4639d0`); if
  non-null it must be a missile (type 3) or the call halts (line 0x161);
  then `0x4d2d70(owner, 0, 0)` (the client hit-explode, ECX owner, EDX 0,
  one stack 0). Callers: `0x4af2e0`, `0x4af6a0`.

d2d: `git grep` over `components/`, `apps/` and `tests/` finds none of
these addresses; **not checked** whether equivalents exist under other
names. Not traced: `FUN_00461840`, `FUN_00461250` message codes,
`FUN_00463740`, `FUN_00463a50`, `FUN_004ad6e0`, `0x4add90`'s modifier table,
which modifier index each of the three frame functions belongs to, the
Missiles.txt rows 0xc3 and 0xe1.

Re-check: `python3 tools/ghidra/fn.py 00464600 00465f20 004ad890`; the
jump tables with `read_memory 0x46466c 56`; asserts at
`0x4644e0`/`0x464420`/`0x465bf0` (lines 0x802, 0x7f3, 0x946, 0x7e2).
