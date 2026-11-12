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
  so the "a walking player is busy" of network.md, `walk.cpp` and `World::apply`'s comment is
  not what `FUN_0057eec0` reads; whatever drops a packet from a walking player is in the
  handler, not traced here. **`FUN_00580a70`** calls it as `(mode, 1, skill)` (LAB_00580af6), and
  then it goes on: the unit's skill list (`FUN_00620250`) and the skill record of its head
  (`.\PLAYER\PlrModes.cpp` line 0xb9, `FUN_0045c4b0`); none of either returns 1. The skill
  argument equal to the list head with record +0x2e 0x43 or 0x4c returns 1. With the record's
  +7 flag (`[0x6ce284]`, 0x80 in the decompile) clear: current mode 1 calls `FUN_005809d0`
  and returns 1; a new mode of 7, 8, 10, 0xb, 0xd or 0x12 returns `FUN_0057ed70`; else 0.
  With the flag set: state 0x2a on the unit and its list's stat 0xa4 over a roll
  (`FUN_0045c390(unit seed, 100)`) go to the mode test above; otherwise state 0xf absent returns 1,
  present goes to the same mode test. The roll, the states and `FUN_005809d0` are not ported
  or traced further. **DISAGREES** with d2d on what "busy" is (issue).
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
  and `FUN_0055b300(1)` answers (not traced). When the corpse's owner id equals the taker's
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

d2d's `tick.cpp` talks at the server's reach, `unit_distance` under 7, with no
line test: sooner than game.exe's client, and through a wall.

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
