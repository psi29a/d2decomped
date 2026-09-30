# The Barracks (level 28, LevelType 7) — FUN_00673120

game.exe 1.14d. Validated: a Python model of this spec, started from
game.exe's state on entering FUN_00673120 (emulator hook), reproduces its
rooms (x, y, def, file) and the level seed on leaving, for map seeds 1–300
(300/300). Our ring and grow (maze.hpp) already match game.exe's rooms on
entry for seeds 1–100.

Offsets: level +0x10 room list (newest first, next = room +0x24), +0x14
record (+4 = preset file), +0x1c {x, y, w, h}, +0x1b4 act, +0x1c4/+0x1c8
level seed (`seed` in maze.hpp), +0x1d0 id. Room +0x00 link list (entry
{other, side, 0, type, ..., next @+0x14}), +0x20 preset record {def, file,
?, flags; flags & 2 = special}, +0x34 {x, y, w, h}.

## Driver (FUN_00673b30)

Type 7 is `FUN_00670f60(2)` (ring) then falls through to `FUN_00671210`
(grow). No specials function for type 7. Then, by level id (+0x1d0):
0x1c (28) → FUN_00673120; 0x6b → FUN_00673320; else FUN_00642590. Then
theme FUN_006735f0 and split FUN_00673a60 as usual.

## Level 27's file

`FUN_00642bb0(act, 0x1b)` finds (or makes, FUN_00642ae0) level 27; the
file is its record +0x14 [1]. The act layout driver (FUN_006772c0, at
0x6774a0) sets it when placing Black Marsh (6), on the act seed: Black
Marsh above → `2 - (low & 1)`; below → `~low & 1`. Level 27 is not built
when 28 is (its room list is empty) — only this pre-set value is read.
Seeds 1–300: file 0 ×100, 1 ×174, 2 ×26, never −1 (−1 would call through
0x6f0464 = 2 and crash; Black Marsh never went sideways). Ours:
`courtyard_file(layout, act_seed)` (components/drlg/drlg.hpp) — same.
Level 27's rect is its Depend position (3000, 960 on every seed) and
Levels.txt size 56×40 — what `level_origin` + SizeX/SizeY give.

LvlPrest: 27 = "Courtyard 1" File1..3 CourtW / CourtN / CourtE.
0xa7 (167) = "Barracks Court Connect", File1..3 CourtWb / CourtNb / CourtEb
(file index = level 27's). 0xc6–0xc9 = "Barracks Next W/E/S/N" (warp to
the Jail), 0xca–0xcd = "Barracks Forge W/E/S/N".

## FUN_00673120 (EDI = level 28)

```
L27  = level 27; file = L27.record[1]              // 0, 1, 2
W, H = LvlMaze room size (level +0x14 +0x10/+0x14)  // 10 x 14
anchor = PTR_FUN_006f0468[file](level)              // ECX = level
side   = {2, 3, 0}[file]                            // EAX into 670de0 (case 2 gets 0 from the switch's SUBs)
R = FUN_00670de0(def 0xa7, file, set_def 1)          // ECX = anchor, EAX = side
FUN_0066b790(R, L27, side, 0)                        // cross-level link (see below)
target = file 0: (L27.x - W,            L27.y + L27.h/2)       // R's right edge on L27's left
         file 1: (L27.x + L27.w/2 - 6,  L27.y - H)             // R's bottom on L27's top
         file 2: (L27.x + L27.w,        L27.y + L27.h/2 + 1)   // R's left edge on L27's right
dx, dy = target - (R.x, R.y)                         // /2 is C signed division
turn = file
if (seed.next() & 1) { special(A[turn]); special(B[turn]); }   // FUN_006724e0, turn++ & 3 after each
else                 { special(B[turn]); special(A[turn]); }
every room += (dx, dy)
level rect (+0x1c..+0x28) = rooms' bounding box (FUN_00642520)   // replaces Levels.txt's
```

### Anchor pickers (table 0x6f0468 = {0x672460, 0x6724a0, 0x672420})

| file | fn | keeps room with | probe side |
|---|---|---|---|
| 0 | 0x672460 | largest x (strictly >) | 2 (right) |
| 1 | 0x6724a0 | largest y (strictly >) | 3 (below) |
| 2 | 0x672420 | smallest x (strictly <) | 0 (left) |

(0x6723e0 is the unused smallest-y / side 1 one.)

```
best = null
for r in level list (newest first):
    if best && !(key(r) better than key(best)): continue    // no probe, no RNG
    if probe(r, side): best = r
return best
```

`probe` = FUN_00672340(ECX room, EAX side):
```
if room.special: return false                  // no RNG
if room has a link with that side: return false   // no RNG
n = alloc(W, H)                                 // FUN_0066b3e0: seed.next()
if !place(n, room, side): free(n); return false // FUN_00670880
link(room, n, side); add(n); set_def(n)         // then
free(n); return true                            // FUN_0066c100 unlinks and unlists n
```
Net effect: one `seed.next()` per probe that gets past the two checks; no
change to rooms (room's def is never recomputed). Ties keep the first in
list order. A failed probe leaves `best` unchanged (so the next room is
probed even with an equal key while best is null).

### FUN_00670de0(def, file, flag) — ECX next_to, EAX side

```
n = alloc(W, H)                                 // seed.next()
if !place(n, next_to, side): free(n); return 0  // can't happen here: the probe passed
link(next_to, n, side)                          // FUN_0066b5e0, both ways
add(n)                                          // list front
if flag: set_def(next_to)                       // def from links, file -1, special cleared
n.special = 1; n.def = def; n.file = file
```
No merge (FUN_00670c70), no set_def(n).

### FUN_0066b790(ECX room, EDX level, side, 0)

Allocates a 0x18 link {L27, side, 0, 0 (type), &L27.rect, next} and
inserts it into R's link list (+0x00) in order (FUN_0066b720 /
FUN_0066b6a0). Layout-neutral; it's the room's link to level 27 for later
(room adjacency / tiles across levels). Side = 2 / 3 / 0 as above.

### Specials (FUN_006724e0 + FUN_00670eb0) — same as maze.hpp's `special`

Tables (16-byte `{from, to, file, dir}`, indexed by turn):
- A 0x6f03e8 (Next / warp): {0xaf,0xc9,−1,3} {0xa9,0xc7,−1,0} {0xab,0xc8,−1,1} {0xa8,0xc6,−1,2}
- B 0x6f0428 (Forge):       {0xaf,0xcd,−1,3} {0xa9,0xcb,−1,0} {0xab,0xcc,−1,1} {0xa8,0xca,−1,2}

The caller passes `table + 16 * turn` (EDX) and `&turn`; 6724e0 bumps turn
after, so the second call uses the other table at turn + 1. Replace: first
non-special room in list order with def == from → special, def = to, file.
Else attach: for each non-special room in list order, alloc (seed.next()),
place at dir; on success link, add, set_def(room), new special {to, file};
on failure free and try the next room. (Identical to maze.hpp's lambda.)

## RNG order (level seed)

ring + grow (as now) → one step per passing probe → one step for R →
one step for the A/B roll (`low & 1`) → the two specials' allocs → (then
theme FUN_006735f0 and split as now). No `seed.next() & 3` turn roll for
type 7; turn = level 27's file.

## Level rect

Rooms end in world coordinates beside level 27; the level's rect becomes
their bounding box. Level-relative room coordinates are therefore the
same as the current min-normalisation; the level's origin is
`target - (R - min)` where min is the pre-shift bounding-box corner, size
= bounding box. Checks: seed 1 (file 0) 2940,966 60×56, R rel (50,14);
seed 3 (file 1) 3022,862 30×98, R rel (0,84); seed 13 (file 2)
3056,953 60×42, R rel (0,28). Maze rooms 13 (10 + R + 2 specials) × 4
split rooms = 52 rooms.

## Diffs for components/drlg/maze.hpp (and callers)

1. `generate_maze` needs level 27's file and rect for level 28 (extra
   params, e.g. `int court_file, Rect court`), and must hand back the
   level rect (origin + size) it computes.
2. Specials block: for type 7 drop the `turn = 0` special case; `turn =
   court_file`, no `& 3` roll. Replace the `notes` branch with, in order:
   pick anchor (probe loop above, one `seed.next()` via `alloc` per real
   probe; use the existing `place`; drop the probe room again — pop it
   from `rooms`/leave `list` untouched, no link), then `arm`-like
   placement of R *without* merge and without set_def(R): alloc, place,
   link(anchor, R, side), add(R), set_def(anchor), R = {special, 0xa7,
   court_file}; then `seed.next() & 1` → A,B or B,A with new
   `kSpecials` rows for 0x6f03e8 / 0x6f0428.
3. Type 7 skips FUN_00642590 but the room normalisation is the same;
   additionally compute the level origin `target(file) - (R - min)` and
   size (bbox). Theme and split unchanged (theme's count includes R and
   the two specials).
4. tools/drlg-dump/drlg-dump.cpp prints Levels.txt's origin/size for 28
   (`-1,-1 size 200x200`); it must print the computed rect (game:
   e.g. `level 28 at 2940,966 size 60x56`). components/game/gamedata.cpp
   `build_maze` must pass level 27's file (`courtyard_file(...)`, already
   computed for 27) and rect (`level_origin` of 27 + its SizeX/SizeY) and
   use the returned origin for `world_x/world_y` instead of
   `level_origin(28)`.
5. Optional later: R's cross-level link to 27 (FUN_0066b790) for
   cross-level room adjacency.
