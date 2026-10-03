# DRLG — game.exe 1.14d

Level generation: `.\DRLG\Drlg.cpp` (0x641800..0x643600), `Preset.cpp`,
`Maze.cpp`, and the outdoor files `Outdoors.cpp` / `OutPlace.cpp` /
`DrlgGrid.cpp` / `OutRoom.cpp` (0x673000..0x67e000).

## Seeds

All of it rolls D2's seed (`rules::Rng`): 64 bits `{low, high}`, a step is
`low · 0x6AC690C5 + high` split back into low/high (FUN_0045c370); a roll
below n takes `low & (n−1)` for powers of two, else `low % n`
(FUN_0045c3e0). A fresh seed's high word is 666 (0x29a).

An act's seed (FUN_00642da0) starts `{init, 666}` from the game's map
seed (passed down through FUN_006194a0, Dungeon.cpp) and is stepped
once; its low word is kept at act +0x470. A level's seed (+0x1c4) is
`{act+0x470 + level id, 666}` — set when the level record is made
(FUN_00642ae0) and set again just before it's generated (FUN_006424a0),
so every level's contents depend only on the map seed and its id.

## The game's map seed

- **A new game** (`FUN_0052c280`): the seed is random (QueryPerformanceCounter
  into the game's seed), unless one was forced (`DAT_00731004`, set by
  `FUN_0052c320`), which sets game +0x84.
- **A single-player join** (`FUN_0056a090`): when the game is type 3, the
  seed wasn't forced, and the save's difficulty byte for this game's
  difficulty (d2s +0xa8) has 0x80, the game takes the save's map id
  (d2s +0xab) as its seed. So a character keeps its Act 1 layout (the camp's
  side, where the Blood Moor goes) game after game on one difficulty.
- **Saving** (PlrSave2.cpp, `FUN_00568f20`): d2s +0xab = game +0x7c.

d2d: `game_seed` (main.cpp) picks it, `set_map_seed` (load.hpp) lays act 1
out again and rebuilds the camp, and the World writes it back on saving.
`--seed` forces one.



Allocates the act (0x48c bytes), seeds it, then FUN_00641f60,
FUN_0061b7e0 and **FUN_00678ad0 (act placement, by act 0..4)**. A level's
own record (FUN_00642ae0, 0x230 bytes) comes from its Levels.txt row;
FUN_006424a0 generates it by DrlgType: 1 maze (FUN_00673b30), 2 preset
(FUN_00668100), 3 outdoor (FUN_00675360). FUN_00642d10 positions a level
at its OffsetX/Y plus its Depend level's position; size is SizeX/SizeY
for the difficulty.

## Act 1's layout (FUN_00677750)

Two chains, each run through FUN_006772c0 on a **copy** of the act seed
(so both start from the same seed), then FUN_00677680 links neighbours.

Chain records are 16 bytes `{place fn, level, link record, -}`:

| table | records |
|---|---|
| 0x6f0750 | 4 Stony Field (anchor), 3 Cold Plains ← 0, 2 Blood Moor ← 1, 1 town ← 2, 17 Burial Grounds ← 1 |
| 0x6f0840 | 39 Moo Moo Farm (anchor), 26 Monastery (anchor), 7 Tamoe Highland ← 1, 6 Black Marsh ← 2, 5 Dark Wood ← 3 |

**Placement** (dir 0 below, 1 left, 2 above, 3 right of the link L;
W/H the new level's size):

- anchor (FUN_006760f0): at its Levels.txt Offset.
- beside (FUN_00676150): first side rolled `seed & 3`, then the next side
  each retry until back at the first. Spots: (L.x−16, L.y2),
  (L.x−W, L.y−16), (L.x2−W+16, L.y−H), (L.x2, L.y2−H+16).
- Blood Moor (FUN_00676650): side and alignment (`seed & 3`, then
  `& 1`); retries step (dir + flip) & 3 and flip ^ 1 through all 8. Size
  96×56 left/right, 56×96 above/below. Spots, flip 1 as beside; flip 0:
  (L.x2−W+16, L.y2), (L.x−W, L.y2−H+16), (L.x−16, L.y−H), (L.x2, L.y−16).
- town (FUN_00676450): as the Blood Moor with 8-tile offsets: flip 1
  (L.x, L.y2), (L.x−W, L.y+8), (L.x2−W, L.y−H), (L.x2, L.y2−H−8);
  flip 0 (L.x2−W, L.y2), (L.x−W, L.y2−H−8), (L.x, L.y−H), (L.x2, L.y+8).
- fixed (FUN_006768c0): below the link, left-aligned.

**Driver** (FUN_006772c0): place record i; if its function has tried
every side, reset it and back up to i−1 (which tries its next side);
else if the check passes, move to i+1. Afterwards each level gets its
rectangle; the town's LvlPrest file index = its dir (File1 townN1,
File2 townE1, File3 townS1, File4 townW1 — named for the Blood Moor's
side); Black Marsh (6) sets level 27's variant from a roll.

**Checks** (FUN_00676dd0, chain 1): clear of every earlier level except
its link (FUN_0066b800: per-axis gap, overlap unless a gap ≥ 0); the town
only where 0x6f1158[dir + 4 flip + 8 bm_dir + 32 bm_flip] is 1; the
Burial Grounds not on the same side of Cold Plains as another level from
it. Chain 2 (FUN_00676eb0) checks overlaps shifted by 200 — not ported.

**Outdoor flags** (FUN_00677180, table 0x6f1258, 15 rules of
`{level or 0, not, not, dir[i], dir[i+1], flag}`): OR'd into DrlgType-3
levels' outdoor flags (0x4, 0x8, 0x10, 0x80, 0x100, 0x200, 0x400) — the
outdoor generator reads these (phase B).

d2d: `components/drlg` `place_chain` / `act1_layout` / `town_file`,
`tests/test_drlg.cpp`.

## Outdoor levels (DrlgType 3) — FUN_00675360

The level's outdoor record (0x268 bytes, FUN_00675320) holds four cell
grids of (w/8)×(h/8) ints (FUN_0067cb80; a grid is `{cells, row offsets,
w, h}`), and an outline:

| grid | holds |
|---|---|
| +0x04 | the LvlPrest Def placed with its top-left in this cell (0 = none) |
| +0x18 | per-cell value bits: 1 << (neighbour vis slot + 4) on contact spans, 0x10000 / 0x20000 markers, the 0x1000.. table values |
| +0x2c | flags: 1 border, 2 (with 1) town side, 0x80 road, 0x100 no room, 0x200 part of a preset (file index in bits 16..19), 0x400 exit, 0x800 / 0x1000 reserved; 0x1b81 = "taken" |
| +0x40 | a third per-cell value handed to plain rooms |

Cell writes go through an operator table at 0x749bac (FUN_0067c4f0):
0 OR, 1 AND, 2 XOR, 3 SET, 4 set-if-zero, 5 clear bits.

FUN_00675360: cell grids of (w/8)×(h/8) (+0x5c, +0x60; rect +0x54 =
{0, 0, w, h}), the outline, then by act (FUN_006427f0; level 134 counts
as act 2): act 1 FUN_006807f0 … act 5 FUN_0067e600, then the finish
FUN_006750f0.

**Neighbours** (FUN_00677680, per outdoor level): every Vis slot with a
level and Warp −1. Side (FUN_00642240, a = this level, b = it): b.x <
a.x and a.x = b.x + b.w → 0 (left); b.x ≥ a.x and b.x = a.x + a.w → 2
(right); else by y: 1 (above) / 3 (below); else −1 (fatal later).
Nodes `{level, side, is_town, 0, &level.rect, next}` go into the list
at +0x264 (FUN_0066b720): empty → head; one node → before it when
cmp(new, it), else after; longer → after the head, before the first
later node where cmp holds (the head itself is never passed). cmp
(FUN_0066b6a0): side ascending; same side 0: a.y < b.y, 1: a.x > b.x,
2: a.y > b.y, 3: a.x < b.x.

**Outline** (FUN_0067d050, DrlgVer.cpp): vertices `{x, y, byte style,
flags, next}`, rect with w, h less 1 (and each neighbour's too, for the
duration): v0 (x, y+h), v1 (x, y), v2 (x+w, y), v3 (x+w, y+h), closed —
up, right, down, left. FUN_0067ce20 splices each neighbour into the
edge of its side (0: v0→v1, 1: v1→v2, 2: v2→v3, 3: v3→v0), working in
c-space (c = −1 on the up and left edges, else 1) with the edge from s
to e and the neighbour's span [A, B] on that axis (A its near end): A
≤ s ≤ B → the corner vertex gets flag 1 (| 2 when the neighbour is a
preset level, DrlgType 2: the town, the Monastery Gate — FUN_00677680) and, if B <
e, a vertex at B is inserted after it; s < A ≤ e → a vertex at A is
inserted after the corner, flagged the same, then one at B if B < e.
Inserts always go right after the corner, which is why the list is
sorted far-to-near. Then everything is made level-relative, and
FUN_00675080 divides by 8 (toward zero) and merges equal neighbours
(flags OR'd, the later style kept). Style stays 0 except where
FUN_00680070 sets 1 (cliffs).

**Contact spans** (FUN_00675770): each vertex v with flag 1: its side
from its cell — x = 0: 1 if y = 0 else 0; y = 0: 2 if x = w−1 else 1;
x = w−1: 3 if y = h−1 else 2; y = h−1: 3 — then the neighbour of that
side whose rect holds the probe (level + 8v + (−4, 4) / (4, −4) /
(12, 4) / (4, 12)) gives bit 1 << (its Vis slot + 4). The span v → next
(FUN_0067c760): the cells strictly between, then v, then next (its last
argument, 1 here), so both ends are inclusive; +0x18 |= bit, +0x2c |= 1, or 3 with a
style.

**Borders** (FUN_00675850), for each outline vertex v (from the list
head) with n = v.next, m = n.next: (dx1, dy1) = sign(n − v), (dx2, dy2)
= sign(m − n) (FUN_0067d280); len = |n − v| on the moving axis.

- style column: act 1 wilderness (LevelType 2) → 1 (fence) when
  v.style is 0, else 0 (cliffs); preset table 0x6f0630, 4 columns,
  rows = kind 0..3 then corner rows 5..12 (1-based row r at 0x6f0620 +
  16r):

  | row | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|
  | cliff (col 0) | 0 | 16 | 17 | 0 | 18 | 19 | 22 | 0 | 0 | 23 | 0 | 0 |
  | fence (col 1) | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |

  (columns 2, 3 are other acts' defs 364.. / 799..);
- **edge**: kind = 0x6f0fd0[dx1 + 3·dy1] (left 0, up 1, right 2, down
  3), preset = row kind+1; unless v has flag 2 (town contact), step
  (x, y) += (dx1, dy1) until n, each cell getting the preset
  (FUN_006743c0 file −1, not border) and +0x2c |= 1, or 3 when v.style;
- **exit**: v flag 1 without 2, act 1 (and act 4): the cell at the
  edge's lower end + len·|d|/2 along it (min of v, n per axis) → +0x2c
  clear 0xf0000, |= 0x30400 (file 3, exit; 0x40400 file 4 for level
  17); act 2 places a pair from 0x6f112c instead;
- **corner** at n: flags b = 1, | 2 if v.style or n.style; column from
  v.style, or n.style when v's is 0. With sx(t) = t + 2·sign(t):
  - v, n without flag 2: e = sx(2dx1) + 9·(sx(2dx2) + 2dy2) + 2dy1;
  - n flag 2: e = sx(2dx1) + 9·(sx(dx2) + dy2) + 2dy1;
  - v flag 2: e = sx(dx1) + 9·(sx(2dx2) + 2dy2) + dy1;
  - both: e = sx(dx1) + 9·(sx(dx2) + dy2) + dy1;

  row = 0x6f1088[e] (−1 = nothing), nonzero entries: −40 1, −39 9, −38
  9, −35 1, −34 8, −31 12, −26 12, −25 4, −22 5, −21 2, −20 2, −19 10,
  −14 10, −13 1, −12 9, −11 9, 11 11, 12 11, 13 3, 14 12, 19 12, 20 4,
  21 4, 22 7, 25 2, 26 10, 31 10, 34 6, 35 3, 38 11, 39 11, 40 3. The
  preset from that row; 19 becomes 19 + (n.style ≠ 1) when v.style is
  1, else 21; 0 → nothing; else placed at n (file −1), +0x2c |= b.

After the last vertex: FUN_00675670 scans in from each side and marks
cells 0x100 (no room) until it meets a border cell (bit 1) — the
outside of a non-rectangular outline; a no-op on rectangles.

**Placing presets.** A def covers LvlPrest SizeX/8 × SizeY/8 cells.

- place (FUN_006743c0(x, y, def, file, border)): file −1 → the def's
  rotation (FUN_00674320, list at level +0x1cc): the first use rolls
  `rand(Files)` on the level seed, every use (the first too) steps it to
  (f + 1) % Files. Each covered cell: +0x2c clear 0xf0000, |= file << 16
  | 0x200, | 1 when border and def 4..15 (or 0x16c..0x177); +0x04 = 0.
  Then +0x04[x, y] = def.
- fits (FUN_00674230(x, y, def, margin, mask)): def 0 → 1×1; the
  margin grows the box — mask 1 up (y −= m, h += m), 2 right (w += m),
  4 down (h += m), 8 left (x −= m, w += m); every cell in the grid and
  free of 0x1b81.
- inner shuffle (shared by FUN_00674730 / 00674920 / 00674b70 /
  00674e40): list the (w−2)(h−2) inner cells row-major, then n times
  swap entries `rand(n)`, `rand(n)`; visit in order at (x+1, y+1).
- anywhere (FUN_00674730(def, file, margin, mask)): the first shuffled
  cell that fits gets placed (not border).
- by a road (FUN_00674920(def, file)): shuffled cells; on a road cell
  (+0x2c & 0x80), offsets (−1,0) (0,−1) (0,1) (1,0) (−1,−1) (1,1)
  (1,−1) (−1,1) (0x6f0614 x / 0x6f060c y), the first that fits(def, 0,
  0xf) gets placed; none → anywhere(def, file, 0, 0xf).
- farthest (FUN_006744f0(rect, def, file, margin, mask)): sx =
  `rand(w−2)`, then sy = `rand(h−2)`; for i 0..h−2 and j 0..w−2
  (inclusive, so one row and column come round twice), y = (i + sy) %
  (h−2) + 1, x = (j + sx) % (w−2) + 1; each fit scores d = (min + 2·max)
  / 2 of |level + 8·cell + 4 − (rect.pos + rect.size/2)| per axis; a
  strictly greater d wins; placed not border.

**Act 1 (FUN_006807f0)** — levels 2..7 (the Blood Moor is 2):

1. cliff styles (FUN_00680070) unless level 2, 3 or 17: walking the
   outline from its first vertex, find a vertex v that starts a run —
   (v.x < next.x and v.y < prev.y) or (next.y < v.y and v.x < prev.x),
   neither v nor prev flagged 1 — then follow edges while each goes
   right or up (next.y ≤ v.y, next.x ≥ v.x) with no flag-1 vertex,
   remembering the last vertex where the following edge turns (right
   then up, or up then right). Found → style 1 (cliffs) on every vertex
   from v to it, outdoor flags |= 0x20 (a cliff cave may come). Stops
   once the walk passes the first vertex. Then contact spans, borders
   (above);
2. border LvlSub type 0 (Border - Cliffs); FUN_00680200:
   - flags & 0xc: the **river** in column w−2 (FUN_0067fe90), if no cell
     of columns w−2, w−1 is on the town side (FUN_0067fc70): per row,
     River Upper (26) at x and River Lower (27) at x+1, the file by the
     def already there — none: 3, or 0 on a 0x100 cell; Wild Border 4
     (def 7) opening (file 3): 3; else table 0x6f2680[def] {upper,
     lower} (defs 4..15: {2,2} {0,3} {1,1} {3,0} {0,2} {0,1} {1,0}
     {2,0} {2,3} {1,3} {3,1} {3,2}); flags & 0x14 also gets a bridge
     (FUN_0067fd20): r = rand(h−2), rows (i + r) % (h−2) + 1 in turn,
     the first where (x−1, y) is free of 0x1b81 — and (x+2, y) too
     unless flags & 4 (that branch jumps past the x+2 setup) — and
     both river cells hold file 3 gets def 28
     file 1 at x, file 2 + (flags & 4 ≠ 0) at x+1;
   - flags & 0x20 (no entrance yet): a cliff cave (def 25 on a def-16
     cell, 24 on 17), scanning rows or columns first on `rand & 1`;
   - flags & 0x1c (no entrance yet): r = rand & 3, the cave / Den
     entrance (51 + (level == 2)) at (r & 1 ? 3 : w − 4 − !(flags &
     0x10), r >> 1 ? 3 : h − 4); flags |= 0x40;
   then border types 1, 2 (Middle, Corner);
3. FUN_006803d0: flag 0x10 → the river in column w/2 − 1 (as above);
   the town transition from the layout flags — 0x80 def 3 (Transition
   S) file 1 at (0, 0), 0x100 def 3 file 2 at (w−7, 0), 0x200 def 2
   (Transition E) file 1 at (0, 1), 0x400 def 2 file 1 at (0, h−6);
   no entrance yet → the Den of Evil (52) as far from the town's
   rectangle as it fits (FUN_006744f0(52, −1, 1, 0xf)), other levels a
   cave (51) anywhere (FUN_00674730); flags |= 0x40;
4. border type 3 (Border - Border); roads (FUN_00681420, below);
5. levels 3..6: a waypoint (FUN_00674b70) — the Blood Moor has none.
   Cold Plains (3): the first cell (rows, then columns) with the Blood
   Moor's contact bit in +0x18 (1 << (its slot in the level's Vis list
   + 4)) and the exit flag 0x400, clamped to x 1..w−2, y 1..h−2, gets
   +0x18 |= 0x20000 (Act 1 Waypoint S) and +0x2c |= 0x800. Otherwise
   (and levels 4..6): shuffle the inner cells as for shrines; the first
   free of 0x1b81 gets 0x10000 (Waypoint L) and 0x800;
   shrines (FUN_00674e40, 5): k = rand & 3; shuffle the inner cells
   ((w−2)·(h−2), n swaps of two rolls), then the first 5 free of 0x1b81
   get +0x18 |= 0x1000 << k (table 0x6f061c), +0x2c |= 0x1000, k = k+1
   & 3 — the plain room there stamps that SubShrine row;
6. FUN_00680580, per level. FUN_006804e0(extra, def): `rand & 3 == 0`
   → two of def by a road, else one (when extra, a second roll `& 1` adds
   def 49 by a road). The Blood Moor: Pond (46) by a road
   (FUN_00674920(46, −1)), FUN_006804e0(0, 47), Stone Fill 1 and 2 (29,
   30) anywhere (FUN_00674730(def, −1, 0, 0xf)). Cold Plains:
   FUN_006804e0(1, 48), Fallen Camp Bishibosh (44), 29, 30 anywhere.
   With `a(d)` anywhere, `r(d)` by a road, `c(e, d)` FUN_006804e0 —
   Stony Field (4): r(160 Cairn Stones), r(45), a(162), c(1, 47),
   c(0, 42), a(31), no 29 / 30; Dark Wood (5): a(161 Inifuss), a(41),
   a(40), c(1, 48), c(0, 43), 29, 30; Black Marsh (6): a(163 Tower),
   a(38), a(39), c(1, 47), c(0, 42), 29, 30; Tamoe Highland (7):
   c(1, 48), c(0, 43), a(31), no 29 / 30; Burial Grounds (17): def 108
   at (1, 1) only; Moo Moo Farm (39): a(50), a(46), a(31), a(38),
   a(39), 29, 30.

Steps 2–4 and the shrines are levels 2..7 only; Burial Grounds (17) gets
the outline, borders and fills, Moo Moo Farm (39) those plus border
types 0..3 in a row.

**Border LvlSub** (FUN_006752a0(type) → FUN_00670750): the LvlSub
engine on the cell grid. For each LvlSub row of the type (Border*.ds1,
substitution type 2, GridSize 1): BordType 0 → start at group
`rand(groups)` and stop after the first placement; 1 → one placement per
group; 2 → every match of every group (FUN_0066f990). Per group
(FUN_0066f690): spots x 0..(w − gw + m − 1), y 0..(h − gh), m = −1 for
Border - Middle when the level has flag 0x4 or 0x8, else 1; shuffled
(n swaps of two rolls, level seed); Border - Middle in act 1 skips
(2, 2) when both counts are ≤ 5. A spot matches (FUN_0066f3b0) when,
per group cell: a wall tile (bit 1) of sequence s wants preset def
4 + s − 1 there (s − 1 = 62 matches anything) and the cell not an exit
(0x400); a floor-only tile (bit 2) wants the cell free (FUN_00674230);
nothing = don't care. On a match, variant k = `rand(variants)` at x +
(gw+1)(k+1) is applied (FUN_0066f520): wall tile → place def 4 + s − 1
(file 0, border); floor-only → +0x04 = 0, +0x2c = 0; empty → +0x04 = 0,
+0x2c = 0x100 (no room).

The finish (FUN_006750f0) turns cells into rooms: a preset anchor
(+0x2c 0x200 and +0x04 set) becomes a preset room (FUN_00666ed0 on the
level seed, FUN_00667ed0: when the LvlPrest row has Scan or Pops,
FUN_00667970 first rolls its DS1's units to stay (FUN_00667620) on the
level seed, then the rooms are allocated), other preset cells nothing, 0x100 cells
nothing, the rest plain 8×8 rooms (FUN_0067d540 with +0x18, +0x2c,
+0x40 and a LevelType word: 2 → 0x44103).

### Plain rooms (OutRoom.cpp, FUN_0067d2d0)

A room is 0xec bytes (FUN_0066b3e0): allocating one steps the level seed
(+0x1c4) and seeds the room from its low word (FUN_00650e40: `{low,
666}`), stepped once more. A plain outdoor room (+0x48 = 1) keeps three
(w+1)×(h+1) grids of DS1 tile words in its data at +0x20: +0x00
orientation, +0x14 wall, +0x28 floor. Tile words are the DS1's own
(prop1 bit 1 = wall present, bit 2 = floor present, sequence bits 8..15,
style 20..25, 0x8000000 shadow); `& 0x3f0ff00 == 0` means plain style 0
/ sequence 0.

1. floor[0..7][0..7] = 0x40002 (grass);
2. act 1 only (FUN_00680c80): **roads** — a (w+3)×(h+3) mask over the
   room grown by one (origin room.x−1, room.y−1) gets every path segment
   from the outdoor record (+0x68 lists, +0x260 of them; nodes `{x, y, …,
   next@+0x10}`) drawn as a 2-wide Bresenham line (FUN_0067c8e0: err += 
   minor, step when err > major; the brush spans x on y-major lines, y
   otherwise). Then every mask cell set, for x 1..w+1 and y h+1..1
   (FUN_00680b10), takes its neighbour bits — 0 (x−1,y+1), 1 (x−1,y), 2
   (x−1,y−1), 3 (x,y+1), 4 (x,y−1), 5 (x+1,y+1), 6 (x+1,y), 7 (x+1,y−1) —
   through the 256-byte table at 0x6f2700; nonzero → floor[x−1][y−1] =
   seq << 8 | 0x82;
3. **LvlSub stamps** (FUN_006707a0) with {room, grids, type, theme, mask}:
   SubWaypoint (Levels.txt, LvlTypes +0x40) with the room's flag bits
   16..17 as the mask, SubShrine (+0x44) with bits 12..15, then SubType
   / SubTheme with the mask FUN_006706a0 rolled when the room was made:
   for each LvlSub row of the type in order, `rand % 100 < Prob[theme]`
   sets its bit (and ORs its Dt1Mask into the room's +0x50).

**LvlSub** rows (0x15c bytes, loader FUN_0061f500): Type +0, File +4,
CheckAll +0x40, BordType +0x44, Dt1Mask +0x48, GridSize +0x4c, then the
loaded DS1 at +0x50 and its grids; Prob[5] +0x11c, Trials[5] +0x130,
Max[5] +0x144. For each chosen row (FUN_006704e0 loads the DS1):

- CheckAll 0 (FUN_00670170), Max[theme] times: pick a group
  `rand(groups)`; spots x 1..w−gw, y 1..h−gh; Trials[theme] = −1 → list
  every spot, shuffle (count swaps of two rolls), try in order; else
  Trials random spots (`rand(w−gw)+1`, `rand(h−gh)+1`); first that fits
  gets stamped.
- CheckAll 1 (FUN_0066ff50): DS1 substitution type 1 stamps every group
  at every inner spot that fits; type 2 looks for an exact match of the
  group (FUN_0066fe00) and, `rand % 100 > Prob[theme]`, stamps variant
  `rand(group.variants)`, which sits (gw+1)·(k+1) to the right.
- fits (FUN_0066fcf0): every group cell with a floor (or a wall) needs a
  plain grass floor under it and no wall.
- stamp (FUN_0066fad0): floors SET | 0x80, walls and orientations SET
  where present, shadow tiles and the DS1's objects inside the group
  (FUN_0066fa10) go to the room.

DS1 groups (substitution type 1/2, v12+): `{x, y, w, h}` + `variants`
from v13. Stone.ds1 (type 1): 24 small stones; BorderMiddle.ds1 (type
2): pattern at x 0, variants to its right.

### Roads (FUN_00681420)

Points in act tiles; a cell's centre is `level + 3 + 8·cell`. Arrays of
0x14-byte `{x, y, byte dir}` in the outdoor record, n at +0x260 (≤ 6):
A +0x80 (ends), B +0xf8, C +0x170, D +0x1e8 (hub); `snap` (FUN_00680cc0)
moves a point one cell inward by its dir — 0: x = 8⌊x/8⌋+11, 1: y
likewise, 2: x = 8⌊x/8⌋−5, 3: y likewise, 4: unchanged (level-relative).

1. **Ends** (FUN_00680d70): per neighbour on the +0x264 list — the town
   (id 1) at (T.x+59, T.y+19) / (T.x+29, T.y+35) / (T.x+4, T.y+22) /
   (T.x+29, T.y+3) for side 0..3, dir = side; level 26 at (x+27, y+13)
   dir 1. Then every cell by the preset whose top-left it holds (+0x04)
   and its file (+0x2c bits 16..19), at the cell centre: Wild Border 1..4
   (defs 4..7) file 3 (the openings) → dir 3, 0, 1, 2; defs 24, 25 → 1,
   0; the bridge (28) file 1 → 2 if in column w−2; cave / Den entrances
   (51, 52) → file ≠ 0. Others don't count. B[i] = snap(A[i]).
2. **Hub** (FUN_00681000): one end → the middle cell; else the mean of
   the ends' cells (`Σ(A−level) / 8n`). Then rings r 0..7, offsets
   (−r,0), (0,r), (0,−r), (r,0) (0x6f2810 / 0x6f2800): the first in-grid
   cell free of 0x1b81 (FUN_00674200). Every D[i] = that cell's centre,
   dir 4. River levels (flag 0x10) take the bridge from FUN_0067fc20
   instead: D.x = centre + 8, dir 0 when left of A[i], else dir 2. C[i]
   = snap(D[i]).
3. per end (FUN_006817d0), cells s = B/8, g = C/8: adjacent (|dx|+|dy| ≤
   1) → the list [s, g]. Else **IDA\*** (FUN_00681630) on a 900-node
   pool: h = min + 2·max of |dx|, |dy|; bound 1.5h, +5 per pass while
   below 1.5h + 35; pool exhausted → no road. A node is `{f, h, g, x, y,
   tries, turn row, dir, parent, child}`; dirs 0 +x, 1 +y, 2 −x, 3 −y
   (0x6f2854 / 0x6f2850). Step cost 2. Moving on: the goal, or a cell in
   the grid's rect, not a preset (+0x2c & 0x200), not on the node's own
   ancestry. A child's dir: d = goal-dir(cell → goal) / 2 (FUN_00678cf0:
   25-entry table 0x6f1518 by 5·cdx + cdy + 12, where the minor axis is
   squashed to −1 / (v & 1) when the major is ≥ 2× it and both clamp to
   ±2: {5,4,4,4,3, 6,5,4,3,2, 6,6,6,2,2, 6,7,0,1,2, 7,0,0,0,1}); its turn
   row 0x6f2840 + 4·((parent.dir − d) & 3) = {0,1,2,3}, {0,1,1,1},
   {3,2,1,2}, {0,3,2,1}; dir = d + row[0], each retry dir += next byte.
   A node gets 3 tries, the root (tries −1, row 0) 4; out of tries →
   back up to the parent's next; the root out → fail this bound. The
   list runs goal → start.
4. found → +0x2c |= 0x80 along it (FUN_0067c890), then (FUN_00681240,
   level seed): k = rand & 3; the goal node := C, the start node := B,
   every node between := centre + ((rand & 1) + 2) · (0x6f2820[k],
   0x6f2830[k]) — x rolled first, k = (k+1) & 3 per node (offsets
   {1,0,−1,0}, {0,1,0,−1}); A appended after B; D prepended before C
   unless its dir is 4.

Plain rooms then paint these polylines (see Plain rooms, step 2).

### Room tiles (RoomTile.cpp)

When a room is brought up (FUN_0061b730):

1. its DT1s (FUN_0066f240) into the 32-slot list at room +0x68: for each
   set bit i of the room's mask (+0x50) LvlTypes File(i+1) of the
   level's type, then Act1/Outdoors/Blank.dt1, Act1/Barracks/InvisWal
   and Warp.dt1. Plain act-1 wilderness rooms: 0x44103 → Floor, Trees,
   Objects, Stones, puddle — plus each chosen LvlSub row's Dt1Mask
   (FUN_006706a0); preset rooms: the LvlPrest Dt1Mask;
2. the room seed is reset to `{room +4, 666}` (FUN_0066ee40; +4 is the
   low word from allocation), then the room's own init — plain rooms:
   grass, roads and LvlSub stamps (above), which roll that seed;
3. the tiles (FUN_0066ee70): plain rooms (FUN_0067d710) walk the wall
   grid (+0x14, orientations +0x00), then the floor grid (+0x28,
   orientation 0); preset rooms (FUN_00666ac0) walk each floor layer,
   then each wall layer, then the shadow — over the room's slice of
   the DS1, one row/column more when the room sits on the preset's
   right/bottom edge and LvlPrest KillEdge is set. Grids are
   (w+1)×(h+1), row by row.

Per tile word (FUN_0066e9b0; style = bits 20..25, sequence 8..15):
orientation 10/11 with style > 7 is skipped; style 30 sequence 0/1 on
a floor is hidden; a floor (bit 2) picks with orientation 0, a wall
(bit 1) with its orientation — orientation 3 also adds a second tile
picked with orientation 4 — and a shadow (0x8000000) with orientation
13. FillBlanks on a preset's first floor layer fills empty floors with
0x1e00000 (0x1e00100 on level 74).

**Pick** (FUN_0066d820(room, orientation, word)): up to 40 tiles with
that orientation / style / sequence, from the room's DT1s in list order
(FUN_00604ae0), each DT1's list for that key coming from its 128-bucket
hash (FUN_0060d040; bucket (2·style − orientation + sequence) & 0x7f,
node `{style, sequence, orientation, tiles, next}`) in **reverse file
order**, because loading (FUN_0060a440 → FUN_0060cfa0) inserts at the front; total = Σ rarity (tile
+0x20); r = `rand(total)` on the room seed (no roll when total is 0);
the first tile whose running rarity sum reaches r + 1 (a single
candidate is taken as is). None
at all → orientation 10, style 0, sequence 0; still none → fatal.

**Plain-room details** (checked against game.exe tile for tile):

- The room seed isn't reset between the room's init and its tiles: the
  picks continue from wherever grass, roads and LvlSub stamps left it.
- A stamp's shadow cells (0x8000000) are picked at stamp time
  (FUN_0066fad0 → FUN_0066e060, orientation 13) into the room's shadow
  list, so they roll the room seed between stamps.
- The plain room's mask is 0x44103 OR'd with each LvlSub row
  FUN_006706a0 chose (Dt1Mask; Swamp adds 0x400000).
- After init, FUN_0067c600 ORs 4 into every edge cell of the wall and
  floor grids (rows 0 and 8, columns 0 and 8, of the 9×9 grids).
- A word with bit 4 is an edge tile other rooms may share (FUN_0066e940):
  the near rooms (FUN_0066bc20: this level's rooms, list order, less than
  6 tiles apart on both axes, then bubble-sorted by FUN_0066bbc0 so a room
  wholly left of or above the one before it moves ahead) are searched,
  skipping itself, for a tile at that spot in one of their edge chains
  (FUN_0066e4c0: rooms already up, point inside the room's rect edges
  inclusive, floor chains for orientation 0, not orientation 4, shadow
  bit only against shadows, and the tile's flags bits 14..16 clear or
  one more than the word's bits 18..19 — FUN_0066db20 ORs each sharer's
  layer + 1 in, so a tile two layers share stops matching either; an
  orientation 3 merge ORs 0xc000). None → FUN_0066e620
  picks it (rolling this room's seed) and chains it. Found →
  FUN_0066e740 keeps the neighbour's tile unless its word had 0x80, or
  the orientations merge (tables 0x6ef620, 0x6ef574) to something else,
  or it's a style-30 / sequence-0 floor, when it re-picks and overwrites
  the neighbour's tile — on the neighbour's seed and DT1s (FUN_0066d820
  with ECX = the tile's room, 0x66e8f2; an added orientation 3 wall's
  pick at 0x66e87c is this room's). The neighbour's seed has already
  stepped once for its room1 (the walk's end), and every such re-pick
  moves it on.
- So edge tiles, and everything picked after them in the room, depend on
  which rooms are up first. The game brings rooms up as the player gets
  near; d2d builds in the level's room list order (newest first, i.e. the
  reverse of the cell order they were made in), then lays the tiles again
  in the player's order as he brings rooms up (`relevel`).
- Act1/Outdoors/Trees.ds1 (v12) declares 14 substitution groups and ends
  12 bytes into the 14th; FUN_00665950 reads past its buffer. In the
  emulator that's zeros (a 0×0 group that stamps nothing); on real
  hardware it's whatever follows the allocation.

**Preset-room details:** the room keeps a (w+1)×(h+1) slice of every DS1
layer (presets' DS1s include the shared edge row and column), FUN_006667d0
ORs 0x84 into every edge cell (walls layer 0, floors, shadow) and the
layer's index << 18 into wall layers past the first and every floor layer
(FUN_0067c590 with op 0, OR), and the walk (FUN_00666ac0: floor layers, the first
with FillBlanks, then wall layers, then the shadow) is one short
where KillEdge is set and the room sits on the preset's right / bottom
edge. FillBlanks: an empty floor cell inside the room picks style
30 sequence 0 (0x1e00100 on level 74). The room seed is fresh
(`{room +4, 666}`): nothing between reset and walk rolls it. A hidden
warp tile (orientation 10/11, style = warp slot ≤ 7) makes the warp unit
(FUN_0066e1c0) and, when the slot's LvlWarp row has LitVersion, a 2×2
lit floor up-left of it (FUN_0066e360: style = the tile's sequence,
sequences 4..7, offsets 0x6ef554). A visible one (FUN_0066e260, not
on level 133) keeps its wall; at sequence 0 or 4 it places the warp unit
and stops there if that fails; then a LitVersion slot adds a twin wall
at the same spot, sequence | LvlWarp Tiles << 8 (the cliff cave's
lit mouth).

Room tiles as game.exe keeps them (room +0x54): walls +0x08 count /
+0x14 array, floors +0x0c / +0x1c, shadows +0x10 / +0x24; 0x30-byte
entries with room-relative x, y at +0x08, +0x0c, the tile header at
+0x18 and the orientation at +0x1c. A tile header points into its DT1's
loaded file (0x60-byte records from `buf + 0x110`, rarity at +0x20);
loaded DT1s are a list at 0x8adbb4 `{name[0x104], buf, lib, next}`,
cached by name.

## Maze levels (DrlgType 1) — FUN_00673b30

The level's maze row (LvlMaze by level, FUN_0061f490: `{level,
rooms[3] by difficulty, SizeX, SizeY, Merge}`, the level's +0x14) sizes
every maze room. All rolls are on the level seed unless a room's seed is
named; a room's seed is set when it's allocated (FUN_0066b3e0, stepping
the level seed, as outdoors).

1. **First room** (FUN_00673b30): allocated, SizeX × SizeY, centred in
   the level ((level w − SizeX) / 2), added to the level's list
   (FUN_0066b970, at the front).
2. **Grow** (FUN_00671210, LevelTypes 3, 4, 7, 8, …): while the level has
   fewer rooms than `rooms[difficulty]` (×3 for the act's boss-tomb level,
   ×2 for its staff-tomb level, act 2 only): pick room `rand(n)` from the
   list (FUN_006711a0); roll **that room's** seed & 3 for a side (0 left,
   1 above, 2 right, 3 below); if it isn't special: allocate a room, put
   it against that side (FUN_00670880) and keep it only if it overlaps no
   room but that one (else freed, the level seed step stays spent). Kept:
   link both ways (FUN_0066b5e0: side, and (side − 2) & 3 back; links go
   to the front of a room's list, FUN_0066b560); then merge (FUN_00670c70):
   every non-special listed room sharing an edge with it (gap < 1 on both
   axes, not just a corner) and not yet linked rolls its own seed
   % 1000 < Merge and, if FUN_00642240 gives a side, gets linked and its
   preset redone; then the new room is added and both rooms' presets are
   redone.
3. **Preset by links** (FUN_006709b0): mask 1 left, 8 above, 2 right, 4
   below; act 1 caves (LevelType 3) take def 0x34 + mask (53 Cave W … 67
   Cave NSEW), file −1; this clears the room's special flag (flags & 2).
4. **Special rooms** (LevelType 3, FUN_00672550): k = rand & 3; records
   `{def to replace, def to set, file, side}` at 0x6ef8a8 + 0x10 k (the
   entrance: 60→86 below, 54→84 left, 56→85 above, 53→83 right), then
   0x6ef8e8 (Den of Evil: → 98 / 96 / 97 / 95) or 0x6ef928 (other caves),
   plus 0x6ef968 for level 9 and 0x6ef9a8 for level 10; k = (k + 1) & 3
   after each. FUN_006724e0: the first non-special listed room with that
   def becomes the special; none → FUN_00670eb0 puts a new room on that
   side of the first listed non-special room it fits beside (allocating,
   and freeing on a miss, per try), links it, redoes the anchor's preset,
   and makes the new room the special.
5. FUN_00642590 moves every room so the rooms' top-left is the level's.
6. **Theme rooms** (FUN_006735f0, not level 8): base by LevelType (3 →
   0x34, 4 → 0x6c, 7 → 0xa7, 8 → 0xcd, 10 → 0x101, …); at = rand % 15; a
   0..14 permutation gets 15 swaps (two rolls % 15 each). Up to
   max(2, rooms / 5 + 1) rooms over rooms × 2 tries: each try takes the
   first unmarked listed room with def == base + perm[at] (def += 15,
   file −1, marked), then at = (at + 1) % 15.
7. **Presets** (FUN_00673a60, list order): FUN_00666ed0 rolls
   rand(Files) for the file; a room with a fixed file keeps its own, a
   plain cave (0x34 < def < 0x44) takes the level's rotation for that def
   (FUN_006738c0: first use rand(Files), then +1 each time, list at level
   +0x1cc), a special keeps the roll. FUN_00667ed0 then makes the rooms:
   one when the preset is under 13×13, else one per 8×8 block (rows, then
   columns), each allocated in turn.

A **preset level** (DrlgType 2, FUN_00668100) is one preset: the first
LvlPrest row with that LevelId (FUN_0061f0e0), its file rolled on the
level seed (FUN_00666ed0) unless the level's record (+0x14 [1]) already
holds one, then FUN_00667ed0 (a sizeless preset takes the level's size).
Only Courtyard 1 (27) is set ahead: the act layout (FUN_00677180) rolls
it on the act seed from the side the Black Marsh went — above 2 −
(rand & 1), below ~rand & 1. A level with a Depend sits at its OffsetX /
Y plus that level's (FUN_00642d10: 27 on 26, 33 on 32).

Tile quirk (FUN_0066e360, FUN_0066e260): the room keeps a record per
LvlWarp id (room +0x4c) with a tile list linked through tile +0x20 — the
same link the edge-sharing chains use. A hidden lit warp, after its 2×2
lit floor, puts every unlit warp floor in the room (DT1 style = the
warp's seq, sequence < 4) on it; a visible warp wall (FUN_0066e260) puts
itself on it. Either cuts the chain it was on, so a later room's
FUN_0066e4c0 misses the tiles past it and adds its own copies (L23).

**Doors** (FUN_0066d9e0): an orientation 8 / 9 wall makes a door unit —
hidden (FUN_0066e9b0, no tile), newly added (FUN_0066dc50 ->
FUN_0066db20), or shared with a neighbour after the merge (FUN_0066e740,
also when the neighbour's tile has flags & 1). FUN_0066d960 finds the
row: 0x6eefc8 `{level, first, last}` (inclusive), then 0x6ef188 28-byte
rows `{style, seq, orientation 9, id, type, dx, dy}` matching the word's
style / seq. The unit sits at (x − room x)×5 + dx, (y − room y)×5 + dy,
dropped when outside the current room; type 1 is a monster (mode 1),
objects 0x5b / 0x5c roll 1 in 3 on the room seed (FUN_0045c390). A made
door sets the tile's flags |= 0x20 so the room sharing it doesn't repeat
it. Act 1: Barracks / Jail rows 0–3, Tristram 4–6, Monastery / Outer
Cloister 5–9, Catacombs 10–11 (+12 on 37), all objects.

d2d: `components/drlg/maze.hpp` (`generate_maze`, `generate_preset`). The Den of Evil
(LvlMaze Rooms 1) is the first room, the entrance and the Den's own room:
three 24×24 caves, 27 rooms. 1.14d's LvlMaze.txt has one Rooms column
(the game reads the .bin); d2d uses it for every difficulty.

## Preset units (Preset.cpp)

A room's unit list (room +0x5c, `{mode, id, x, next, path, type, y,
flags}`, FUN_0066bf30, new units at the front; x, y subtiles from the
room's corner) is what the server spawns from. Sources, in the order
they're added:

- A DS1's units as its loader lists them (FUN_00665950, file order, each
  put at the front): type 1 monsters (v5+) through MonPreset for the DS1's
  act (`monpreset.bin`: count, then `{act, kind, u16 id}`; kind 1 MonStats
  row, 2 superunique → MonStats rows + i, 0 MonPlace → + superuniques + i),
  mode 1; type 2 objects (v6+) through the act's table at 0x748ad8
  (`components/drlg/obj_preset.hpp`), ids ≥ 150 less 150; flags from v6.
  Row counts are the .bin's: 734 MonStats, 66 SuperUniques (the .txt
  files' Expansion rows don't count).
- A preset copies them into its record the first time one of its rooms
  comes up (FUN_00667890 → FUN_00667620, each copy put at the front
  again, so the record runs in file order; a maze did this at
  generation, FUN_00667970). Some ids only stay on a roll of the room
  seed (outdoors) or the level seed (maze): monsters 0xcc, 0xcd, 0x173,
  0x174 (1 in 3), MonPlace 0x21 (3 in 4), 0x22 (1 in 2), 0x23 (1 in 4),
  objects 0xc4, 0x105 (1 in 2), 0x245 (3 in 4).
- Then, the first time: the Blood Moor's border openings (defs 4..7 file
  3) get Flavie (MonStats 266, mode 1) at the preset's centre
  (FUN_006664a0), at the front.
- The room's preset init takes the record's units inside its rect
  (FUN_00666710, record order, each put at the front of the room's list).
- LvlSub stamps (FUN_0066fa10): the stamp DS1's units strictly inside the
  group, moved to where the group went.
- Warp tiles (FUN_0066e1c0): a type-5 unit, id the slot's LvlWarp Id,
  at the tile (×5) plus LvlWarp OffsetX / Y, unless the tile is on the
  room's far edge.

d2d: `components/drlg/units.hpp` (`ds1_units`), `room_tiles.hpp`
(`BuiltRoom::units`); the app makes them objects, NPCs and monsters
(`load_npcs`, `load_monsters`).

**Units off every room.** FUN_00666710 is the only hand-out (its one
caller is the preset room init FUN_006667d0). A unit of the record that
lies inside no room of the preset stays in the record and nothing ever
makes it. d2d drops them the same way (`level_room_tiles`' leftovers), so
every `Level::units` entry has a room.

## A room's life on the server (2026-10-03)

The room2 status (+0x44) is the shallowest depth at which a player's
near-list walk holds it: 0 the player's room, 1 its near list, 2 and 3
the lists of those lists, 4 none. Each depth has a u16 counter (+0xc + 2 ×
depth). A room2 sits in one of five status lists (DRLG act +0xa0 + 0xec ×
status, links +0x1c / +0xe8; FUN_0061b210 moves it).

- **Entering a room** (FUN_0061a110(new room1, old room1) → FUN_0061b6f0
  (ECX old room2, EDX new room2)): FUN_0061b490 walks the new room
  (BL = 0) and FUN_0061b390 its near list three deep. The walk is depth
  first: a room's deeper visits run before its own callback. A callback
  (table 0x744384, by depth) runs only when the room's status is ≥ that
  depth and no counter at that depth or shallower is set yet. Then the
  counter goes up.
  - depth 3, FUN_0061b320: the DT1 list (FUN_0066f240) and, for a preset
    room, its preset units (FUN_00667890 → FUN_00667620; the roll steps
    the room's own +0x14 seed in place, but FUN_0066ee40 resets it before
    the tiles). Status 3.
  - depth 2, FUN_0061bb10: status 2.
  - depth 1, FUN_0061b2d0: tiles and room1 (FUN_0061b190), if it has
    neither.
  - depth 0, FUN_0061b2c0: status 0.
  Each near list includes the room itself, so a depth-1 room gets all four
  callbacks in the order 3, 2, 1.
- **Leaving** (the old room2): FUN_0061b5b0 lowers each counter it raised
  and runs 0x744394's callbacks. These set the status to the shallowest
  set counter (FUN_0061b4f0). At status 4, FUN_0061b560 frees the tiles
  (FUN_0066f1a0) only when DRLG act +0x8c & 1 (FUN_00642a00). That bit is
  1 on the client (FUN_006194a0's client branch passes 1) and 0 on the
  server. The client's frame (FUN_0044c790 → FUN_0061b920) also brings
  up one status-2 room every 5 frames. The server never does: its room1s
  come only from depth 1 (and a warp's landing room, FUN_0052d0f0).
- **Freeing** (FUN_0052d240, every 12th server frame from FUN_0052d870):
  for each act's room1 (list +0x10, next +0x7c), FUN_0061a790 resets the
  room1's idle count (+0xc) while a player is in it (+0x78), else counts
  one more. Past 10 sweeps (≥ 132 frames, about 5 s), and if FUN_0061ba30
  agrees, the room1 goes. FUN_0061ba30 wants no flag 0x400000 and status
  > 1. In a town (FUN_006426a0: levels 1, 0x28, 0x4b, 0x67, 0x6d) or the
  Arreat Summit (0x78), every room of the level must be at ≥ 2.
  Then:
  - each unit of the room1 (+0x74, next +0xe8) goes to FUN_005433f0
    (SUnitInactive.cpp). Monsters are stored (FUN_005431f0). Objects are
    stored on the level's or the unit's terms: town portals 0x3b / 0x3c
    always, and objects.txt +0x173 / +0x174. Items and warp tiles are
    stored too. Every unit but an item is then removed (FUN_00555600).
  - FUN_0061a910 unlinks the room1 from the near rooms' lists and calls
    FUN_0066b4c0. That clears room2 +0x30 and keeps the room1's
    populated bit in room2 +0x60. If the room has tiles (0x100000),
    FUN_0066f1a0 frees them along with the plain room's grids or the
    preset's DS1 slice.
- **Coming back** (a player's walk reaches it at depth 1 again):
  FUN_0061b190 rebuilds the tiles from the reset seed (FUN_0066ee40),
  against whichever near rooms are up now. FUN_006422a0 steps the room2
  seed again, so the new room1 has a new seed. Its flags start from
  room2 +0x60, so FUN_0052d160 sees it populated: no presets, no object
  groups, no monsters. FUN_00542b40 remakes the stored units instead:
  monsters through FUN_005424f0 (a superunique by FUN_005a4440,
  superuniques.md), everything else through FUN_005557d0 with its stored
  mode. An object whose objects.txt row has +0x167 & 0x20 catches up on
  the frames it was away, one FUN_005417d0 event per step it missed.
  Not traced: whether the remakes step the game seed.
- Proven by the emulator: the preset units come out the same whether the
  rooms come up one by one (FUN_0061b730) or through the real walk
  (FUN_0061b6f0, every room stepped into in a shuffled order; levels 2–7,
  17, 18, 20 seeds each).

d2d: a room comes up the first time a player's near list reaches it
(`player_moved`). Its room1 seed is the first one, and it is populated
once (`LevelState::up`). ponytail: no room is ever freed. game.exe frees a
room about 5 s after the last player moves 2+ rooms away, and rebuilds it on
return, with a new room1 seed, its edge tiles re-laid against the rooms
up then, and its units restored from storage. Matching that needs the
unit storage (SUnitInactive.cpp) and tiles laid as events (up and freed),
not as one order.

## Checking against game.exe — what's proven, what isn't

**How it's proven.** `tools/emu` runs game.exe 1.14d itself under unicorn:
its CRT start-up, its own table loader (FUN_00619300, every .bin plus the
LvlPrest / LvlSub DS1s from the MPQs) and its own act builder
(FUN_006194a0, server side). `drlg.py` reads the result out of game.exe's
memory; `build/tools/drlg-dump` prints d2d's in the same form;
`diff_drlg.py <first>-<last> <level> [tiles]` diffs them line for line.
"Proven" below means identical output on every seed listed, not a
reading of the decompile. `tests/test_outdoor.cpp` pins a few of those
values (seed 3) so a regression shows without the emulator.

### Proven identical to game.exe (2026-09-26)

| What | Level | Seeds checked |
|---|---|---|
| The level's rectangle in the act (act 1 layout, chain 1) | Blood Moor (2) | 1–5000, 0x7fff0000–0x7fff0fff, 0xffffe000–0xffffffff |
| Outdoor flags after generation | Blood Moor | same |
| The three cell grids (+0x04 presets, +0x18 values, +0x2c flags): borders, border LvlSubs, river / bridge, Den entrance, roads, shrine markers, fills, transitions | Blood Moor | same |
| Every room: position, size, kind, seed (so every preset's file roll too) | Blood Moor | same |
| Every room's tiles: walls, floors, shadows, each as DT1 file + tile index (grass, roads, LvlSub stamps and their shadows, preset rooms, FillBlanks, edge sharing, the Den entrance's lit floor) | Blood Moor | 1–1500, 0xfffffc18–0xffffffff |
| Maze: rooms grown, special rooms, presets by links, file rotation, the 8x8 split, every room's seed | Den of Evil (8) | 1–2000 |
| Every room's tiles | Den of Evil | 1–1000 |
| Every room's units (room +0x5c, list order): preset monsters, superuniques (Corpsefire), objects (shrines, torches, chests), LvlSub stamp objects, Flavie at the border opening, warp units | Blood Moor | 1–1500, 0xfffffe00–0xffffffff |
| Every room's units | Den of Evil | 1–1500 |
| Every room's room1 seed (room1 +0x6c: the tiles' seed stepped once, what population rolls) | Blood Moor, Den of Evil | 1–300 |
| Grids, rooms, flags (cliffs, cliff caves, bridges, waypoints, fills) | Cold Plains (3), Stony Field (4), Dark Wood (5), Black Marsh (6), Tamoe Highland (7) | 1–100 |
| Grids, rooms | Burial Grounds (17), Moo Moo Farm (39) | 1–10 |
| Every room's tiles and units (lit warp walls, Scan / Pops presets) | levels 2–7, 17, 39 | 1–20 |
| Grids, rooms (theme rooms, specials of 9 / 10, preset levels) | caves 9–16 | 1–50 |
| Every room's tiles and units (sizeless presets' KillEdge, the warp-list quirk) | caves 9–16 | 1–20 |
| Grids, rooms (Depend positions, Courtyard 1's file) | 20, 25, 26, 27, 32, 33, 37, 38 | 1–10 (27: 1–30) |
| Every room's tiles and units (KillEdge column, shared warp walls, doors) | 20, 25, 26, 27, 32, 33, 37, 38 | 1–20 |
| Grids, rooms, tiles, units (the ring / star makers, their specials, 12×12 rooms, re-picks on the owner's seed, the bring-up roll) | crypts 18, 19, 21–24; Jail 29–31; Catacombs 34–36 | 1–10 |
| The Barracks' placement by level 27 (FUN_00673120, docs/research/re/drlg-barracks.md): the anchor probes, Court Connect, Next / Forge in rolled order, the rect from the rooms' box; level 27's file from the layout, else its own roll on its level seed (the layout's value 0 leaves it) | Barracks 28 | 1–20 |

Conditions those results hold under, so they aren't overstated:

- **Normal difficulty only.** Every run passes difficulty 0. The Blood
  Moor's sizes don't change with difficulty; the maze's room count
  would (LvlMaze.txt in 1.14d has one Rooms column, game.exe's .bin has
  three; d2d uses the one for all) — untested for Nightmare / Hell.
- **Tiles depend on room bring-up order** (edge sharing, see Room tiles).
  Proven for game.exe's room-list order with only that level's rooms up,
  and the collision grids for shuffled and reversed orders
  (`diff_drlg.py ... collision`). The town's or Cold Plains' rooms may be
  up first in the game; that isn't checked.
- **Emulator shortcuts.** String-table lookups return "" (names only),
  C++ static constructors (`__cinit`) aren't run, and memory past a file's
  end reads as zero. The last matters once: Act1/Outdoors/Trees.ds1
  declares 14 groups and ends inside the 14th, which game.exe reads past
  its buffer; on real hardware that memory holds whatever follows, so the
  14th group may not be the empty one both sides use here.
- Only what the dumps print is compared: rectangles, flags, grids, rooms,
  tiles. Anything else a level carries (below) isn't.

### Not proven yet (ported from the decompile, or not ported)

| What | State |
|---|---|
| The rest of act 1's layout: other levels' rectangles, chain 2 (Moo Moo Farm, Monastery, Tamoe Highland, Black Marsh, Dark Wood), chain 2's overlap check shifted by 200 (FUN_00676eb0) | ported partly (chain 2 simplified); only the Blood Moor's rectangle and flags are diffed |
| The town's own layout beyond its DS1 choice | not diffed |
| Act 2+ maze types; the Barracks' cross-level link to 27 (FUN_0066b790) | not ported |
| Preset units that roll to stay (FUN_00667620) | ported outdoors (`stays`, units.hpp); diffed on Cold Plains' Cottages 2, whose row has Pops: presets with Scan or Pops roll at generation on the level seed (FUN_00667970), the rest on the seed of the room that copies them (room +0x14, the whole {low, high} — a maze room's was stepped once, so its high word isn't 666; diffed on the Jail and Catacombs). In the preset record's order, one step each — monsters 0xcc, 0xcd, 0x173, 0x174 kept when `low % 3 == 0`; MonPlace 0x21 when `low & 3`, 0x22 when odd, 0x23 when `!(low & 3)`, 0x24 always; objects 0xc4 / 0x105 when even, 0x245 when `low & 3`. It runs after the room's DT1 list and before its tiles (FUN_0061b730), whose seed FUN_0066ee40 resets, so tiles don't shift. Act 1: the Crypt, Jail, Catacombs, Fence Fill 1, Cottages 2 |
| Units in other acts: the act 2 / act 4 MonPreset remaps, type-4 units (NPCs by name) | not ported |
| LvlSub CheckAll stamps | not ported (no act 1 wilderness row uses them) |
| Doors outside act 1 (FUN_0066d9e0's type 1 and objects 0x5b / 0x5c), tile word bit 4 on non-plain paths beyond what these levels hit | not ported |
| Logical areas (FUN_0066ccb0 / FUN_0066d110; room_tiles.hpp `logic_areas`) | diffed bit-exact on every Logicals room of act 1, seeds 1-3 (ids are ours, only id != 0 is used) |
| Room collision, automap | not diffed |
| Monster population on these rooms (components/rules/monsters.hpp) | uses the proven room seeds; its own rolls not diffed |
| The app's use of it: d2d draws and walks the proven picks in the Blood Moor and the Den of Evil (`Level::picks`), and its warps take the player between them; where a warp puts the player is traced (below, "Taking a warp"); what counts as clicking one (2 cells, not LvlWarp's Select box) is a guess | wired up; warp arrival traced, not diffed |
| What the server does with units: superunique mods and stat bonuses, minion placement, MonPlace units; objects' behaviour (shrines, chests); monster populating in general (d2d populates every room at load, in cell order, one game seed across both levels) | partly built, not diffed |

d2d: `components/drlg/outdoor.hpp` (`generate_outdoor`), data loading in
`outdoor_data.hpp`, `tests/test_outdoor.cpp`; the app's `load_wilderness`
and `Town::cross_level`.

### Taking a warp (FUN_005550b0)

The server's warp: FUN_006195a0(the tile unit's room, its class) finds
the room on the other side through the room's tile links (FUN_0066ab00,
room +0x4c, made by FUN_0066be10 with the other side's LvlWarp record)
and in its unit list (+0x74, next +0xe8) the tile unit (type 5) of the
linked class, with that record. The player is put at the free spot
nearest that unit (FUN_0064e7b0, collision 0x1c09, FUN_00554ea0), then
told to walk (FUN_005809d0(0, 2, ..)) to that spot + the record's
ExitWalkX / ExitWalkY (subtiles, +0x14 / +0x18). The tile unit stands at
its tile's cell × 5 + LvlWarp OffsetX / OffsetY (FUN_0066e1c0). The
record comes by id and direction (FUN_0061f310: 'b', or 'l' / 'r' for
wall orientations 10 / 11, FUN_0066e160); act 1's are all 'b'. d2d:
`Level::Warp::unit_x/unit_y`, `World::use_warp`.
