# Which units the cursor takes — game.exe 1.14d

The client's unit under the cursor (`FUN_00467a10`, kept at 0x7a6a94 /
0x7a6a98) is picked each frame by `FUN_00467ac0`. It walks the units of
the rooms round the player and keeps those the hit test
`FUN_00466870` (asserts `.\UNIT\CUnit.cpp`) passes, by type priority
(0x711f98).

## The hit test

`FUN_00466870(ECX unit, EDX x, y, ...)`, with the player as 0x7a6a70:

1. Not in the half of the screen a side panel covers; not a unit with
   +0xc4 bit 21 set.
2. **May the player interact with it** (`FUN_00465c60`, ECX player, EDX
   unit), or a skill-cursor exception (`FUN_00464ec0`, `FUN_00464f70`).
   Otherwise no.
3. Selectable: +0xc4 bit 1, or a unit the left or right skill's target
   kind takes (`FUN_00464310` → the callbacks at 0x711f34, which take
   dead monsters, mode 0xc); and +0xc8 bit 7.
4. The pixel or box test (per-layer selectable rectangles when the class
   asks for them, else the frame's box).

## May the player interact with it (`FUN_00465c60`)

- **In town** (`FUN_0061ab00` → `FUN_006426a0`: levels 1, 40, 75, 103,
  109): yes, apart from player-on-player cases.
- **Elsewhere**, each side is first traded for its owner: a monster in
  the client's owner list (`FUN_00479150`, 0x7bb5bc) or with +0xc8 bit 10
  (owner id at +0x98) counts as that player. Then:
  - the same unit or the same owner: no (the player's own pets);
  - two players: the hostility flag (`FUN_004dc440(8)`);
  - the player and a monster: MonStats2 `alSel` (flags +4 bit 4,
    `FUN_004638a0`) yes; `noSel` (bit 5) no; else yes unless they are
    friends (`FUN_00650d70`).

Friends (`FUN_00650d70`) are two units of the same alignment, evil (0)
or good (2). A unit's alignment is stat 172 under state 105
(`FUN_006259b0`; set by `FUN_005543b0`). A player's is 2 (net-packets.md
0xaa capture). So outside
town a monster turned good, a converted one (alignment 2, `FUN_005d0350`),
isn't under the cursor. The column names come from the MonStats2
field table in `FUN_006571a0` (`alSel` 0x6ecf94 bit 4, `noSel` 0x6ecf8c
bit 5).

In 1.14d's MonStats2, `alSel` is set on cain1, tyrael1, navi,
boneprison1..4, darkwanderer, act2guard4, act2guard5, izualghost,
malachai, tyrael3, drehyaiced and ancientstatue1..3; `noSel` on 29
critters and helpers (chicken, rat, cow, act2hire, invisopet, the
catapult spotters, the injured barbarians, ...).

## Show Items labels (`FUN_004c0810`, UI\showitems.cpp)

Each ground item of the player's near rooms (unit type 4, flag +0xc8 bit
7, at most 0x20) gets a label rect {left, top, right, bottom} kept at
0x7c54a8 + 0x120 n (count 0x7c54a0). The columns it may use: min_x 0 and
max_x the screen width (`DAT_0071146c`), or max_x W/2 − 0x20 with a panel
on the right (`FUN_0045ae90` 1), min_x W/2 with one on the left (2). An
item whose spot has x outside min_x..max_x, or y outside −8..(the
screen height − 0x10), gets none. Then up to 12 tries of
`FUN_004c0640` (fastcall ECX &left, EDX &top; &right, &bottom, try,
max_x, min_x; `ret 0x14`), H = screen height
(`DAT_00711470`) − 0x30:

1. left < min_x: right += min_x − left, and left = 6 (6, not min_x).
2. top < 6: bottom += 6 − top, top = 6.
3. right ≥ max_x: d = right − max_x + 1, right −= d, left −= d.
4. bottom ≥ H: d = bottom − H, bottom −= d, top −= d.
5. The first placed label it meets (left ≤ R, right ≥ L, top ≤ B,
   bottom ≥ T): tries 0..6 move it vertically: up by T − bottom − 3 when
   (bottom − top) / 2 ≠ 0 and top ≤ H, else down by B − top + 5; tries
   7..11 horizontally: by L − right − 1 when (right − left) / 2 ≠ 0 and
   left ≥ max_x (after step 3 never), else by R − left + 1 (to its right).
   It returns 0 (moved). None met: 1.

A try returning 1 places it, if then right ≤ max_x and left ≥ min_x;
twelve moves without a clear spot, or a spot outside the columns, and
the item gets no label.

## d2d

`Monster::ally_not_interact` (components/game/ai.hpp) is that rule for a
monster: in town nothing; elsewhere `alSel` passes, `noSel` refuses, else
alignment 2 (the player's) refuses. Traced from the code, not compared
against an oracle; the hit test's selectable and pixel checks still follow.

## The click's own path

Read from the code, no oracle run.

- `FUN_00467a10` (0x467a10) returns the latched unit (0x7a6a94 non-zero)
  only if `FUN_00466870(0,0)` still passes for it; if not it clears
  0x7a6a94 and 0x7a6a98 and returns 0. It has no alignment test of its
  own: the friends rule above is inside `FUN_00466870`, so a converted
  monster is never latched outside town.
- `FUN_00462d00` (the click map: kind 0 left, 3 right, 1 held left; flags
  4 press, 8 held, bit 4 of the flags byte shift, `stat 0xa` run) calls
  `FUN_00467a10` once (0x462d72) and stores the result as the click's
  target (struct +8). The left press (kind 0) with no target calls
  `FUN_00466fe0` (clears 0x7a6a94/98); with one, `FUN_00467a70`; then
  `FUN_004629a0` -> `FUN_004625b0`.
- `FUN_004625b0` with a null target takes the ground branch (LAB_00462671):
  shift (flag 0x20) is `FUN_00461700(0)` (the attack in place) and
  `FUN_00467a70`; otherwise `FUN_004623c0` + `FUN_00461840`, a walk to the
  click's spot. So a left click on a converted monster walks there,
  as on empty ground.
- A held frame (kind 1, `FUN_00462a20`) is let through by `FUN_00462930`
  when the press armed 0x7a526c (a null target arms it to 1, a unit to 2)
  and the target is null, or is a type 0, 1 or 5 unit whose owner is not
  walking, running or in mode 6. A monster target then needs the press
  flag to attack (the existing note in collision.md). A converted monster
  is a null target on every frame, so a held button never attacks it: it
  keeps re-walking to the spot. That is d2d's "a held attack drops it".
- `FUN_004621d0` re-picks the target through `FUN_00467880` for skills
  whose Skills.txt +6 bits are 0x40 (`FUN_00467490`), 0x20 with no target
  (`FUN_00467660`) or 0x80 (`FUN_004677b0`). `FUN_00467490` and
  `FUN_00467660` filter candidates with `FUN_00466870(0,0)` (the same
  rule), and the converted monster stays out of them.
- `FUN_004677b0` (0x80) is not a pick and has no `FUN_00466870` call (the
  asm at 004677b0..00467876 calls only the position getters 006488c0 /
  00648900, `FUN_004646a0` (the local player's room), `FUN_0064cb30` and
  `FUN_0064e780`). It takes the current target's subtile x, y (the click's
  own x, y when the target is null), asks `FUN_0064cb30(room, x, y, 1)`
  (the collision flags at that spot, mask 1) and `FUN_0064e780(room, &xy,
  1, 1, 0, 7)` (a free-spot search, not read) and, if both pass, writes the
  spot back to the click's x, y and returns null: the unit target is
  dropped for the ground. Else it returns the target as it was. So it can
  only turn a target into a spot; it never adds a unit, and a converted
  monster (null from `FUN_00467a10`) enters it as the click's own spot.
- The right click (kind 3 of `FUN_00462d00`) with a converted monster is
  the same null target as the left: `FUN_00466fe0` (not `FUN_00467a70`),
  then `FUN_00462ba0`: `FUN_00462930` (a press arms 0x7a526c to 1 for a
  null target), the right skill (`d2d_right_skill`), and, when
  `FUN_00464600` passes and the skill row's byte +0x14 (the 0x23c-byte
  table at 0x744304 + 0xb98, row by `Player.cpp` 0x9dd) is 1 and bit 0x40
  of the click flags is clear, the flag 0x80. Then the same
  `FUN_004625b0` as the left click, so the same ground branch
  (`LAB_00462671`) and the same `FUN_004621d0` re-pick. There is no
  separate rule for a converted monster on the right button.

Not traced: what `FUN_0064e780` / `FUN_0064dea0` search, and the byte +0x14
meaning.
