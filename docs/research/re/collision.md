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
  Players and small units use the plus.
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
  tested (LAB_006501c0 on).
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
  path, sends C→S 0x02 or 0x04 (LAB_0048137a). Path or no path it queues the
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
so FUN_00462a20 and FUN_004625b0. On the ground FUN_004623c0 and FUN_00461840 path anew
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
