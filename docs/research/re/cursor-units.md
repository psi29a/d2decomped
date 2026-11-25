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

**The walk.** The rooms are the player's room's near list
(`FUN_00620bb0`, then `FUN_00619790`: room +0, its count +0x24) in order,
and each room's units from its list's head (room +0x74, the next at unit
+0xe8). `FUN_0064c2c0` adds a unit at the head (and `FUN_0064c370` takes
it out), so a room lists its newest unit first. The 0x20 is a count of
placed labels (0x7c54a0, `cmp edi, 0x20` at 0x4c08d6): past it the walk
goes on, placing none. How a room's list comes to be in that order (server
side `FUN_00537b50`, run when a player's room changes, read from the
decompile): each room of the new room's near list (`d2d_act5_rooms_near`)
that is not in the old one's goes through `FUN_0053a8e0`, which walks the
server room's own list from its head (+0x74, next +0xe8, so newest first,
`FUN_0064c2c0` heading it the same way) and calls `FUN_00571f90` for each
unit but the player's; for an item (type 4) that sends the add packet only
when unit flag `+0xc4` bit 4 is set (`FUN_0053ec00` / `FUN_0053ec90`; what
the bit means was not traced). Each room only in the old list goes through
`FUN_0053a9b0`, which calls `FUN_00571600` for each unit of the room (the
removal packet; the client's side not read). The client heads its room list
with each arriving item (`FUN_004c1910` modes 3 and 5: `FUN_00620ae0`,
`FUN_00620a70`, `d2d_unit_link_in`), so a drop made while the room is in
play lists first, and the items a room brings with it when it comes into play
list after, oldest first (the reverse of their server order). d2d keeps
`View::ground`'s one order (`rank_ground_labels` takes the last first): right
for items dropped in play, reversed for a floor that comes back with its
room; the level come back to and a room re-entering play on a walk both
need the arrival order kept, which `world/tick.cpp`'s push and
`world/view.cpp`'s copy would carry, so it is not ported. The spot is the item's client position less the
view's, 16 up: x = `FUN_00620650` − `FUN_0045afc0`, y = `FUN_006206b0` −
0x10 − `FUN_0045afd0` (an item's static path +4 / +8). The rect: left =
x − (w + 8) / 2 (`sar`), right = left + w + 8, top = y − h + 4, bottom =
y, w and h from `FUN_00502520`.

**Lit.** While no label is lit yet (a local, 0 at the start), a placed
label goes lit when the mouse (`FUN_00468730` / `FUN_00468740`) is within
left..right and top..bottom + 4, all inclusive (0x4c0ac9..0x4c0ae0;
`FUN_00467a70` then makes it the cursor's unit), or else when its item is
the cursor's unit (`FUN_00467a10`). Lit: colour the palette's nearest to
(0, 64, 128) (`FUN_004fb180`), draw mode 5, kind 0; else colour 0, mode
1, the kind by quality (`FUN_00627e70`). The labels are drawn
(`FUN_005023b0`) after the walk, in placing order.

Oracle: `tools/emu/show_items_walk.py` runs `FUN_004c0810` itself (the
walk, `FUN_004c0640`, the hover test) on generated rooms and units;
`test_show_items_walk` gives d2d's `rules::show_items` the same cases
(fixture `show_items_walk_cases.inc`).
## The hit test after the selectable test (`FUN_00466870`, 0x466870, 1053 bytes)

Read from the decompile, no oracle run. Arguments: ECX unit, EDX x, then
y, and a fourth, `param_4`. The steps above end with +0xc8 bit 7 and
`local_10` (selectable) both set. Then:

- `param_4` == 0: yes, no pixel test (the callers that only ask
  "could the cursor take it").
- Otherwise the unit is first morphed to its shown look
  (`FUN_00645270`, `.\UNIT\CUnit.cpp` line 0x6ed: type, class), and by that type:
  - 0 a player: the pixel test below.
  - 1 a monster: when its MonStats2 row (`FUN_00451fe0`) `+4` bit 0 is set,
    a `PtInRect` on the box `+0x110 / +0x112 / +0x10e / +0x114` (s16
    left offset, width, top offset, height) from the unit's screen x / y
    decides, with no pixel test; else the pixel test.
  - 2 an object (Objects.txt row `FUN_00640e90`; a missing row asserts, id
    0x741): `d2d_gfx_unit_hit` (the frame's pixels), or yes when the row's
    `+0x13a` or `+0x19d` byte is set. With `+0x13a` and the unit's mode 2,
    4 or 5, or with `+0x19d` alone, `FUN_00463fe0(y)` answers; otherwise the
    row's `+0x161` selects the layered test below when it is 2 or more.
  - 4 an item: mode must be 3 (`param_1[4]`) and `FUN_004538d0` 0.
  - 5 a tile: `FUN_00463ee0(y)` answers.
- The layered test (monster whose `FUN_00451fe0` row has `+0xec` of 2 or more, or an
  object as above): up to 16 composite layers are tried
  (`FUN_004640a0`, `d2d_gfx_unit_layer_ready`), each against its box. Any
  one containing the point passes; none after 16: no. Without layers, one
  `FUN_004640a0` box. A player in mode 0x12 with class 4 skips the box test
  and passes.

d2d keeps this as `selectable` / `ally_not_interact` plus its own bounds
(`session/view.cpp`); the layered test, the object special cases and the
MonStats2 box are not ported.

## Unit flag test (`FUN_00451f30`)

`FUN_00451f30(ECX unit, EDX mask)` (0x451f30, 22 bytes, no callees): false
for a null unit, else `unit +0xc4 & mask != 0` (`test ecx, ecx` / `mov eax,
[ecx+0xc4]` / `and eax, edx` / `neg; sbb; neg`). The same word as
`FUN_00463720` (+0xc8) tests for the other flag word. Cited from the
disassembly, no oracle run. The five callers in this chain pass:

| caller | mask | where |
|---|---|---|
| `FUN_00466870` (hit test, 0x46694a) | 4 | `FUN_00464310` true, bit 1 clear, a monster (type 1) for which `FUN_00464820` holds: bit 2 selects it too (`local_10` = 1) |
| `FUN_00481030` (0x48110a) | 2 (`ebx - 2`, ebx = 4) | action 0x13 on an item target (type 4): not selectable, nothing is sent (the function returns) |
| `FUN_00481600` (0x4816b1) | 2 | the queued target of a type other than 0, 1, 2 (item, tile): not selectable, `FUN_00467a70` abandons the queue; selectable goes to `FUN_00481030` |
| `FUN_00454f30` (0x4550a8) | 0x200 | a monster (not classes 0x1a1 / 0x1a2) with the flag set gets no in-world label: `FUN_00452be0(0)` sets its name and life bar as the hover text (see "Draw now or keep for later") and the three name buffers are cleared; unset, it labels (`FUN_00454ad0`) |
| `FUN_00459bc0` (0x459da4) | 0x200 | a monster of `FUN_00478d90` kind 1 (a pet): flag set or not, the mark is teal `0x7a51b4` (the `je` at 0x459dab goes to 0x459d56, which loads the same byte as the fall-through at 0x459dad), so the test changes nothing |

Bit 31 (`0x80000000`) is the pet bit (combat.md, the pair percent
`FUN_0057c060`). What sets 0x200 was not traced (objects.md:324 lists it as
open). d2d: bit 1 is `targetable` / selectable in `ai.hpp`; the 0x200 label
rule is not ported.

## The name over a unit (`FUN_00454f30`)

`FUN_00454f30(ECX unit)` (0x454f30, 1373 bytes, one caller, `FUN_00456ee0`)
builds and draws the text above the unit under the cursor. Read from the
decompile and the disassembly, no oracle run:

- Nothing when `d2d_automap_scroll_automap_scroll` (the side-panel mode,
  as in the hit test above) is 3, or the unit is null. A type 5 unit
  further than 100 from the player (`FUN_006416d0`) gets nothing.
- Position (`0x70f854` / `0x70f858`): the unit's screen x / y
  (`FUN_00620650` / `FUN_006206b0`) less `FUN_0045afc0` / `FUN_0045afd0`;
  with the automap on (`FUN_004f51d0`), the map point of its precise
  position (`FUN_004f6760`) with y + 0x10, x shifted by a quarter of
  `0x71146c` for scroll modes 1 and 2.
- By unit type:
  - 0, a player: its name (`FUN_00452af0`, 0x80 units), drawn 0x48 above
    (`FUN_005022f0`).
  - 1, a monster: classes 0x1a1 / 0x1a2 take the name from their MonStats
    row (`FUN_00451f80`; no row clears the three buffers). Any other class:
    unit flag 0x200 (`FUN_00451f30`) set clears the buffers (no label);
    clear, class 0x164 has its own text (`FUN_00452c70`) and the rest go
    through `FUN_00454ad0`.
  - 2, an object (when `FUN_004b3230` is 0 and `FUN_004a0000` is not 1):
    Objects.txt `+0x168` (the name's y offset, `d2d_object_txt_168`) is
    added to y. Objects.txt `+0x167` bit 2: the text is the level name
    `FUN_00453e70(byte [unit data + 4])` (0x455221 to 0x455236), and with
    `0x7a0610` not 0 or 1 a " (%s) " is appended (`_sprintf`). Bit 6
    (0x40): the room's level (`FUN_00620bb0` → `FUN_0061a1b0`), its Levels
    row (`FUN_0061db70`), two strings (`FUN_00524a30`) and `FUN_00524e20`.
    What those two flags mean in Objects.txt, and the `0x7a0610` states, were
    not traced. Other objects draw the name buffer `FUN_00464a60` fills.
  - 4, an item: nothing when `0x7a27f4` is set, else `FUN_004527d0(0)`.
  - 5, a tile (a level warp): the destination level id from the room
    (`FUN_00620bb0`, `FUN_0061a1d0` with the tile's class); its Levels row
    `+0x1be` (0x50 bytes after the `+0x16e` name, so a second 40-unit wide
    string, the entrance text), except level 8: string 0x13de when
    `FUN_004b92e0(0, 1)` is nonzero (quest 1 open in this game), else
    0x13dd (0x455444 to 0x45545b). The wide buffer is 199 units; the label
    sits at the unit's centre (`+0x10 >> 1 + +8`).
  - Other types (3 missiles): nothing.

d2d: `world_view.cpp` (the hover name, `name_offset` = Objects.txt NameOffset
added to the box) agrees on the object offset; the type 5 label, the Den's
quest-dependent string and the 0x200 rule are not ported. The strings
0x13dd / 0x13de were not read.

## d2d

`Monster::ally_not_interact` (components/game/ai.hpp) is that rule for a
monster: in town nothing; elsewhere `alSel` passes, `noSel` refuses, else
alignment 2 (the player's) refuses. Traced from the code, not compared
against an oracle; the hit test's selectable and pixel checks still follow.

## The click's own path

Read from the code; `FUN_004623c0`, `FUN_00461840`, `FUN_004677b0` and
`FUN_0064e780` are run for real below (`tools/emu/click_move.py`,
`tools/emu/click_free_spot.py`). The rest has no oracle run.

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
  as on empty ground, unless shift is held: then it is the attack in
  place (`FUN_00461700(0)`), as on empty ground. `FUN_00461700` itself is run on game.exe by `tools/emu/attack_command.py`: with the argument 0 it never looks at the shift flag, so the spot command is 5 (left, 8 for the alternate) and 0xc (right, 0xf), the same code a plain click sends; at a unit shift and the alternate pick 6 / 7 / 9 / 0xa (left) and 0xd / 0xe / 0x10 / 0x11 (right). `FUN_004623c0`'s own glue
  (the held-frame throttle at 7 frames / 280 ms, rejecting a move when
  either axis distance is 0x100 or more or both axes are under 1, the
  per-flag minimum step) and `FUN_00461840`'s (a failed
  `PATH_CalculatePath` returns 0 and never reaches `UNITMODE_ExecuteSkillAction`
  at `0x481030`/LAB_00461879; a found one does, with the path's last point)
  are run for real: `tools/emu/click_move.py`.
- A held frame (kind 1, `FUN_00462a20`) is let through by `FUN_00462930`
  when the press armed 0x7a526c (a null target arms it to 1, a unit to 2)
  and the target is null, or is a type 0, 1 or 5 unit whose owner is not
  walking, running or in mode 6. A monster target then needs the press
  flag to attack (the existing note in collision.md). A converted monster
  is a null target on every frame, so a held button never targets it: it
  keeps re-walking to the spot (with shift, the attack in place). That is
  d2d's "a held attack drops it".
- `FUN_004621d0` re-picks the target through `FUN_00467880` for skills
  whose Skills.txt +6 bits are 0x40 SearchEnemyNear (`FUN_00467490`), 0x20
  SearchEnemyXY with no target (`FUN_00467660`) or 0x80 SearchOpenXY
  (`FUN_004677b0`; "The skill's own target search" below). The
  Skills.txt field table built in `FUN_00613f80`
  gives TargetableOnly bit 0x14, SearchEnemyXY 0x15, SearchEnemyNear 0x16,
  SearchOpenXY 0x17 (0x616a21..0x616a9f; TargetableOnly's is EDX, 0x14
  since 0x6141a7), tested as byte +6 against the
  masks at 0x6ce268 (0x6ce278 = 0x10, 0x6ce27c = 0x20, 0x6ce280 = 0x40,
  0x6ce284 = 0x80). `FUN_00467490` filters candidates with
  `FUN_00466870(0,0)` (the same rule), so the converted monster stays out
  of it; `FUN_00467660` has no `FUN_00466870` call and asks
  `FUN_00465c60` only for a TargetableOnly skill whose monster isn't
  MonStats2 shiftSel.
- `FUN_004677b0` (0x80) is not a pick and has no `FUN_00466870` call (the
  asm at 004677b0..00467876 calls only the position getters 006488c0 /
  00648900, `FUN_004646a0` (the local player's room), `FUN_0064cb30` and
  `FUN_0064e780`). It takes the current target's subtile x, y (the click's
  own x, y when the target is null; an object's, item's or tile's from its
  static path +0xc / +0x10), asks `FUN_0064cb30(room, x, y, 1)` (the
  collision word at that spot & 1, a wall; 0x27 off the rooms) and, when it
  is set, `FUN_0064e780(room, &xy, 1, 1, 0, 7)` (a thin wrapper over
  `FUN_0064dea0`: size 1, mask 1, no keep flag, limit 7, so rings of radius
  1 to 6), and, when that finds a spot, writes it back to the click's x, y
  and returns null: the unit target is dropped for the ground. Else (open
  ground, or no free subtile within 6) it returns the target as it was. So
  it can only turn a target into a spot; it never adds a unit, and a
  converted monster (null from `FUN_00467a10`) enters it as the click's own
  spot. The whole function (both branches: open ground unchanged, a blocked
  null target or unit target moved or left alone) runs natively on
  `sk_tele_spot`'s rooms: `tools/emu/click_free_spot.py`. Its `gen` / `game`
  modes give d2d's `game::search_open_xy` (gamedata.hpp) the same cases:
  `tests/test_search_open_xy.cpp`, fixture `search_open_xy_cases.inc`
  (2000). In 1.14d's Skills.txt SearchOpenXY is on Fire Wall, Meteor,
  Blizzard, Eruption and Volcano, and the monster rows SkeletonRaise,
  VampireRaise, Resurrect, RegurgitatorEat and Resurrect2 (all but
  RegurgitatorEat have SearchEnemyNear too, which goes first). d2d: `ClientSession::open_spot`
  (apps/d2d/session/input.cpp) on each cast from the mouse.
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

- Each button has a press, a held and a release handler, and each calls
  `FUN_00462d00`: left `FUN_0044bf40` (state 4) / `FUN_0044c000` (8) /
  `FUN_0044c060` (0x10), right `FUN_0044c180` / `FUN_0044c2c0` /
  `FUN_0044c370`. The right press stores the click map's answer in
  `0x7a0654` (not set for a press on the left or right panel, the
  `FUN_0045ae90` checks), and the right held handler calls the click map
  each frame only while `0x7a0654` is set. The click map's kind 4 is
  `FUN_00462c20`, the held twin of the right press's `FUN_00462ba0`: the
  same `FUN_00462930` gate, the right skill (`FUN_006201d0`, `Player.cpp`
  0x9fd where the press has 0x9dd), the same byte +0x14 / flag 0x80 rule and
  `FUN_004625b0`. It has no walk branch: a held frame `FUN_00462930` lets
  through hands the right skill to `FUN_004625b0`, at the cursor or the
  click's target. A held frame reads its hand's skill afresh
  (`FUN_00620190` left, `FUN_006201d0` right), never the press's; the target
  is one for both buttons, `0x7a6a94`: a left or right press sets it
  (`FUN_00467a70`) or, with nothing under the cursor, clears it
  (`FUN_00466fe0`), and `FUN_00467a10` checks it each frame. d2d:
  `apps/d2d/input/hand_hold.hpp`, used by `ClientSession::input` for both hands.

Not traced: the byte +0x14 meaning, `FUN_00467a10`, `FUN_00462930`,
`FUN_00462a20` and `FUN_00462d00` itself (read from the code only; that
kind 4 is the held frame is read from `FUN_00462c20` matching the press, as
the callers pass the kind through registers the decompiler loses).

## The skill's own target search (`FUN_00467880`)

Read from the disassembly; `FUN_00467660` and the `FUN_00641cb0` under it
run natively in `tools/emu/search_enemy_xy.py` (below). `FUN_004621d0`
calls `FUN_00467880(ECX &x, EDX target, &y, flags, near)` with the click
struct's x, y (+0xc, +0x10), its target (+8), flags (+0) and `near`: flag
0x20 (shift) or flag 2 with no target (+8 null). The answer is the new
target (+8).

**The Skills.txt bits.** The skill row's flag dword (+4) is filled from the
field table built in `FUN_00613f80`, an entry {name, type, bit, ...} each
(0x616a11..0x616ac5): TargetableOnly bit 0x14 (EDX, set at 0x6141a7), SearchEnemyXY 0x15,
SearchEnemyNear 0x16, SearchOpenXY 0x17, TargetCorpse 0x18 (the strings at
0x6e6bbc..0x6e6bec, 0x6e6bac). The tests read byte (bit >> 3) + 4 against
the mask table at 0x6ce268 (1, 2, 4, ..., 0x80): TargetableOnly +6 & 0x10
(0x6ce278), SearchEnemyXY +6 & 0x20 (0x6ce27c), SearchEnemyNear +6 & 0x40
(0x6ce280), SearchOpenXY +6 & 0x80 (0x6ce284), TargetCorpse +7 & 1
(0x6ce268). Re-check: `read_memory 006ce260 40` and disassemble
0x616a10..0x616ac5.

**`FUN_00467880`** (0x467880, ret 0x10):

1. A target: a dead player (type 0 and `FUN_00464820`: +0xc4 bit 0x10000,
   or mode 0 / 0x11; a monster's modes are 0 / 0xc) stays the target. A
   dead unit with a skill not TargetCorpse becomes the ground at its spot
   (`FUN_00466de0`, x / y = `FUN_0045adf0` / `FUN_0045ae20`, target null).
   A click on the player's own subtile calls `FUN_004c51e0`.
2. A held frame (flag 8) with flag 1 and without shift (0x20), while the player
   walks or runs (`FUN_00622d00`: mode 2, 3 or 6) keeps the target and runs no
   search. Flag 1 is the left button's (`FUN_00462d00` ORs 9 for kind 1, 0xa
   for the right's held kind 4, 5 / 6 for the presses): the right hand's held
   frame is not gated. d2d: `rules::skill_search_goes` (click_spot.hpp), asked
   by `ClientSession::input`'s `use_hand` on a held frame; oracle
   `tools/emu/search_gate.py` runs `FUN_00467880` itself (the searches, the
   status and the walking test hooked) over every flag byte.
3. The skill's status (`FUN_004d9fc0`) not 0 keeps it. d2d: the World leaves
   each hand's status in the View (`World::hand_statuses`,
   `View::hand_status`), and `ClientSession::input`'s `use_hand` searches only at 0.
4. SearchEnemyNear: `FUN_00467490`. Else SearchEnemyXY with no target and
   `near`: `FUN_00467660`. Else SearchOpenXY: `FUN_004677b0` (above).
   Else the target as it was.

**`FUN_00467660` (SearchEnemyXY)** (EBX the player, the skill row on the
stack, ret 4): the player's facing (`FUN_006238c0`: its path's +0x65 when
it stands at its path's target, else the way to that target), its
eighth `(dir & 0xff) >> 3`, and five turns of it from 0x6d6b34 (0, -1, 1,
-2, 2). Each turn's spot is the player's subtile plus x 0x6d6b14 {0, -2,
-3, -2, 0, 2, 3, 2} and y 0x6d6af4 {3, 2, 0, -2, -3, -2, 0, 2} for that
eighth: about 3 subtiles out, the facing's own first. A spot is taken when

- its collision word has a unit's own bit (`FUN_0064d9b0(room, x, y, 1,
  0x180)` -> `FUN_0064d450`: 0x80 a player, 0x100 a monster; 0x27 off the
  rooms);
- `FUN_00641cb0(room, x, y, LAB_00467640, 0, 1)` finds a unit there: the
  near rooms (`FUN_00619790`) whose rect is near (`FUN_00641930`), each
  room's units from its list head (+0x74, next +0xe8), only players not in
  mode 0 / 0x11, monsters not in 0 / 0xc, and missiles; its size
  (`FUN_00620510`) taken to 1..3 (under 1: never): 1 its own subtile, 2 the
  plus round it, 3 the 3 x 3 square; and the first that `LAB_00467640`
  (0x467640: a monster) passes;
- that unit has +0xc4 bit 2;
- with TargetableOnly, it is MonStats2 shiftSel (`FUN_004638a0(class, 6)`:
  the MonStats2 field table puts shiftSel at bit 6, 0x657f8f; noSel 5,
  corpseSel 7) or `FUN_00465c60` lets the player interact with it.

A spot that fails any of these moves on to the next turn, not the next
unit. The first that passes is set as the player's path target
(`FUN_00620c10` -> `FUN_00648b90`: path +0x58 the unit, +0x5c its type,
+0x60 its id) and returned; none returns null.

In 1.14d's Skills.txt SearchEnemyXY is on Attack, Kick, Left Hand Swing and
the class melee skills (Jab, Bash, Smite, Zeal, Dragon Claw, Maul and the
rest; 49 rows, 35 of them class skills), so a shift-click at the ground beside a monster swings at
it. SearchEnemyNear (`FUN_00467490`, below) is on Raise Skeleton, Corpse
Explosion, Psychic Hammer, Fist of the Heavens, Taunt and the other corpse
skills.

**The facing (`FUN_006238c0`, `ret 4`, the unit on the stack)** reads the unit's
subtile (a player's or monster's dynamic path +2 / +6 through `FUN_006488c0` /
`FUN_00648900`; a type 2, 4 or 5 unit's static path +0xc / +0x10) and compares
it with the path's target words (`FUN_00648a00` / `FUN_00648a10`: path +0x10 /
+0x12, SP1, path.md). Both equal: the answer is the path's +0x65
(`FUN_00648810`, the facing it turns to). Else the way from the unit's subtile
to that target, `FUN_00621dc0` -> `FUN_0064fdc0` (rules::direction64, 64 ways),
whatever +0x65 holds (nothing in it asks whether the unit walks). The function
does not write the unit, path or globals; its only non-fatal effects are the calls
to the position and target getters and, off target, the direction calculation.
The only caller is `FUN_00467660`, which uses the returned byte for the
SearchEnemyXY spot order. Whether the target stays set after a walk ends is not
traced. `tools/emu/search_facing.py` runs it natively on
4000 cases (the target on the unit, one off, a few and far away; the model
matches, and each of its three mutants is caught). d2d: `rules::search_facing`
(`search_facing.hpp`, fixture `search_facing_cases.inc`, 1500 of them,
`test_search_facing`). `World::view` sends `Hero::target_x` / `target_y`, d2d's
SP1, through `View::path_target_x` / `path_target_y`; `ClientSession::shift_pick`
uses it for standing and walking players alike through `rules::search_facing`.
`test_search_facing_view` checks that a standing player's off-position SP1
changes the search facing, while a player on SP1 keeps path +0x65.

**The walk order (`FUN_00641cb0`).** Its `FUN_00619790` near list is the room's
own array (room +0 the rooms, +0x24 their count: `d2d_act5_rooms_near`), and
it takes each room in that order, each room's units from the list head
(+0x74, next +0xe8), the first that covers the spot and passes the filter.
`tools/emu/search_walk.py` runs `FUN_00467660` natively over a 5 x 5 room level
with the player near the room edges and units on both sides, each room's list by
a stamp (the highest first, equal: the later unit first, the order d2d keeps in
`UnitState::room_stamp`, `rules::room_order`): 6000 cases, 0 differ against
a model of it, and the view-order and tie-order mutants are caught.
`rules::search_enemy_xy_walk` (`search_walk.hpp`, fixture
`search_walk_cases.inc`, 2000; `gen 20000 3`: 9817 picks, no difference;
`test_search_walk`, which also counts the cases the view order gets wrong) is
`ClientSession::shift_pick`'s call. Not covered: the order of the near list on a
real level (d2d's `near_rooms`, not game.exe's room array) and the room stamps,
which `Fight::restamp` sets only when a server rule asks, so a client's
copies are as old as that.

Oracle: `tools/emu/search_enemy_xy.py` runs `FUN_00467660` and everything
under it but the hooks its docstring lists, on generated rooms (units of
every type, mode and size round the five spots, collision words with and
without the unit bits); `tests/test_search_enemy_xy.cpp` gives d2d's
`rules::search_enemy_xy` the same cases (fixture
`search_enemy_xy_cases.inc`, 2000; `gen 20000 3`: 20000 cases, 4965 picks,
no difference). d2d: `ClientSession::shift_pick` (apps/d2d/session/input.cpp) runs it on
a click with stand still held and no unit under the cursor, with the units'
own bits from the players' and live monsters' collision shapes.

**`FUN_00467490` (SearchEnemyNear)** (EAX the skill row, the skill, the
target, &x, &y on the stack; ret 0x10; `.\GAME\Select.cpp` 0x301):

1. A target: with TargetCorpse (+7 & 1, 0x6ce268) and dead
   (`FUN_00464820`) it is returned, x and y untouched. Else x, y become its
   subtile (`FUN_006488c0` / `FUN_00648900`, or a static path's +0xc / +0x10).
2. The mask: 0x2003, or 0x3002 with TargetCorpse (`neg`, `sbb`, `and 0xfff`,
   `add 0x2003` at 0x467524). The skill id (`FUN_00643ce0`: the row's first
   word) 0x5a, Iron Golem: a target item (type 4) of a base with Items
   bitfield1 & 2 (`FUN_00629cc0`) is returned; else the mask is 0x20 and the
   filter `LAB_00467470` (ECX the unit: an item that `FUN_00629cc0` passes).
3. The unit finder from the local player's room (`FUN_004646a0`), x, y and
   size 9 (`FUN_0065a950`), the mask also at its +0x14 (0x4675c2), run by
   `FUN_0065ac70`: the room alone when the square of 9 round x, y lies inside
   it (`FUN_0065a6b0`), else its near list, rooms of a town skipped when the
   mask has 0x2000 (`FUN_0061ab00`: room +0x10 → +0x58 → level +0x1d0, then
   `FUN_006426a0`: 1, 40, 75, 103, 109), every room passing `FUN_0065a710`
   (bugs.md 23), each room's units from +0x74 (next +0xe8). A unit is kept by
   the filter when there is one (Iron Golem), else by `FUN_0065aa40` with a
   struct {mask, 0, the local player 0x7a6a70, x, y, 9}: dx² + dy² ≤ 81;
   type 0 with mask 1, not mode 0 / 0x11 (0x1000: only 0x11), not the local
   player; type 1 with mask 2, not mode 0 / 0xc (0x1000: only 0xc); types 2,
   3, 4 need masks 0x10, 8, 0x20, which neither mask has.
4. Of those, the one nearest x, y by `FUN_006417f0` (max + min / 2) under 10
   for which `FUN_00466870(unit, 0, 0, 0)` holds (its `param_4` 0 returns 1
   once the selectable tests pass, before the box test), the first of
   equals (`jge`). None returns the target as it came (null for none).

So the corpse skills take only corpses in mode 0xc (not one still falling,
mode 0), the others only live players and monsters, nothing in town but
Iron Golem's metal items, and a target is itself a candidate at distance 0.
In 1.14d's Skills.txt SearchEnemyNear is on Raise Skeleton, Corpse
Explosion, Raise Skeletal Mage, Poison Explosion, Attract, Bone Prison, Iron
Golem, Revive, Fist of the Heavens, Find Potion, Taunt, Find Item, Grim
Ward, Psychic Hammer, Dragon Flight and the monster rows SkeletonRaise,
VampireRaise, Resurrect, Resurrect2 and NihlathakCorpseExplosion.

Oracle: `tools/emu/search_enemy_near.py` runs `FUN_00467490` and the finder
natively on generated rooms (the hit test and `FUN_00629cc0` hooked to the
case's answers); `tests/test_search_enemy_near.cpp` gives d2d's
`rules::search_enemy_near` the same cases (fixture
`search_enemy_near_cases.inc`, 2000; `gen 20000 3`: 20000 cases, 9571
picks, no difference). d2d: `ClientSession::near_pick` (apps/d2d/session/input.cpp) on
each cast of such a skill from the mouse, with the client's monsters, other
players and (for Iron Golem) ground items in room-list order.

**Another player as a candidate.** Outside town `FUN_00465c60` (ECX the local
player, EDX the other) takes the player-versus-player arm when both units are
type 0 and differ: a live other player (`FUN_00464820` 0: not mode 0 / 0x11,
no +0xc4 bit 0x10000) answers `FUN_004dc440(local id, other id, 8)`. That
function reads the local player's roster record (`FUN_0047a310`: ECX into
EDX, `FUN_004792e0`, its +0x34) and returns the node for the other id's
flags & 8: the hostile bit, the same flags S->C 0x8c carries (`{0x8c, player,
toward whom, flags}`, net-packets.md). A dead other player passes instead when
`FUN_0047a4f0` says its corpse is ours or loot bit 1 is set. `FUN_00466870`
also passes a unit `FUN_00465c60` refuses when `FUN_00464ec0` or
`FUN_00464f70` does (a hand's skill with Skills row byte +7 bit 2 or 4: our
own pet, a party member); with the hit-test argument zero it then returns 1
after the unit flags (+0xc4 & 0x200000 refuses; +0xc4 & 2 or `FUN_00464310`,
and +0xc8 & 0x80, pass), before any pixel or box test.

`tools/emu/hostile_player_search.py` runs `FUN_00465c60`, `FUN_004dc440` and
`FUN_0047a310` natively on two synthetic live type-0 units, the roster lookup
hooked to give a record only for the local id. Across all low four relation
bits both answer true exactly when bit 8 is set; the always-not-hostile and
bit-1 mutants are caught, and the record looked up by the other id differs
(the lookup goes by the local id).

d2d: `ClientSession::near_pick` takes the other players from
`add_near_players` (apps/d2d/session/near_players.cpp): each `View::others`
player is type 0, `hit` its `hostile` (bit 8 of a joined game's S->C 0x8c flags
toward it, `ClientSession::net_overlay`; the View codec carries it), and dead
(mode 0, dropped unless the skill takes corpses) when a local co-op ghost or,
joined, after the host's 0x0d command 8 / 9 on it until its 0x15. Local co-op has no
relation nodes, so its players are never hostile, and a hosted World reads
`UseSkill::unit` only as a monster. The dead-player arm and the
`FUN_00464ec0` / `FUN_00464f70` passes for a player aren't ported.
`test_hostile_player_search` runs `add_near_players`: two non-hostile players
fail, a hostile one is picked and a dead hostile one isn't, the flag survives
the View codec, and the cast keeps type 0 through the command codec and the
C->S packet.

The native send is `FUN_00461700` -> `FUN_00481030` -> `FUN_00480b40` ->
`FUN_004786a0`: `FUN_00461700` takes the target's +0 as its type and +0xc as
its id, and `FUN_004786a0` writes the type at packet +1 (after 0x06 or 0x0d)
and the id at +5, so a player target goes as type 0, a monster as 1.
`ClientSession::send` resolves `UseSkill::unit_type` 0 against `View::others`
and passes it to `NetGame::skill_on`.

**The finder's walk order (`FUN_0065ac70`).** `tools/emu/search_walk_near.py` runs
`FUN_00467490` natively on the 5 x 5 room level of `search_walk.py` with the
local player near the room edges and units on both sides: the finder takes
the near list of the player's room (`FUN_0065a6b0` picks the room alone when
the square of 9 lies inside it, which gives the same units), each room's units
from its list head, and the first of equally near units wins. 6000 cases, 0
differ against a model of it; the view-order and tie-order mutants are caught.
`rules::search_enemy_near_walk` (`search_walk.hpp`, fixture
`search_walk_near_cases.inc`, 3000; `gen 20000 3`: 9407 picks, no difference;
`test_search_walk_near`, which counts the cases the view order gets wrong) is
`ClientSession::near_pick`'s call; a unit outside the near rooms stays only as
the click's target. Not covered: the near list's order on a real level, and the
items' places in their room's list.

Not traced: the click flag 2, `FUN_004c51e0`, `FUN_00466de0`, the complete
pixel/box arms of `FUN_00466870` for a player (the search passes hit-test
argument zero), and pets as units of either search (`FUN_00465c60` with a pet,
the pet list).

## The label helpers of `FUN_00454f30`

Read from the decompile and the disassembly (`tools/emu/disasm.py`), no
oracle run. d2d: none of these is ported (`not ported`; d2d has no party
life bar over a unit).

### Class 0x164's text (`FUN_00452c70`, 0x452c70, 168 bytes)

`FUN_00452c70(EAX unit, EBX &flag, stack &name, &width)` (`ret 8`, one
caller: `FUN_00454f30`, which sets its local flag to 3 and its width to 0
before the call). The unit is a client monster; its id (+0xc, or -1 for a
null unit) goes to the pet list (`FUN_00479150`: the node whose +8 is the id
gives its +0xc, the owner's id; the list is 0x7bb5bc).

- No such pet (-1): returns 0, the outputs untouched. The caller then
  falls back to the ordinary monster label (`FUN_00454ad0`).
- A null unit, a unit in mode (+0x10) 0 or 0xc (death, dead): returns 1
  with the outputs untouched. The flag is still 3, so `FUN_00454f30`
  draws nothing.
- Otherwise the owner's roster entry (`FUN_0047a1f0`: `FUN_004792e0`, the
  roster by player id, +0x66 the name; no entry returns 0 and the function
  returns 0) gives the text: the name is widened into the caller's buffer
  (`d2d_utf8_to_units`, at most 0x80 units). The width is
  `(FUN_0047a6b0(owner) << 7) / 100`, with `FUN_0047a6b0` the roster's +0x14
  byte (the life byte of the 0x0d stop packet, `FUN_0047a690`;
  net-packets.md), but 0 when the owner is the local player (`FUN_00463dd0`
  = 0x7a6a70, its +0xc) or has no entry. The flag is 1 when
  `FUN_0047a070(local player id, owner id)` holds, else 0:
  that function is true when either has no roster entry, false when both
  are in the same party (u16 +0x22, not 0xffff), else the hostility flag 8
  (`FUN_004dc440`; party.md). So the colour flag is "hostile or unknown".
- Returns 1.

The sentinel 3 is outside the flag's 0 / 1, so it tells `FUN_00454f30`
that nothing was filled; after a 1 return with the flag changed it draws
the name at (x, y − 0x48) with the width as `FUN_005022f0`'s last
argument (`LAB_00455112`). Which monster class 0x164 is (356) was not
traced: it is the class for which the pet list holds an owner.

### A player's name (`FUN_00452af0`, 0x452af0, 234 bytes)

`FUN_00452af0(ECX unit, EAX &width, EBX &flag, stack &dest, limit)` (`ret 8`;
callers `FUN_00454f30` with limit 0x80, and `FUN_00453330`, not read).

- The narrow name comes from `FUN_0047a210` (Roster.cpp): a null unit is
  fatal (line 0x5db), so is a unit whose type (+0) is not 0 (line 0x5dc).
  The roster entry of the unit's id (+0xc, `FUN_004792e0`) gives +0x66;
  without one the player data (`d2d_player_data`, unit +0x14, line 0x5e5
  when null) is read from its first byte as the name.
- A player in mode 0x11 (a corpse; automap.md): width 0, flag 1. A 0x100-byte
  local is zeroed, the name widened into it (`FUN_00526f20`, 0x80 units
  at most, stops at the NUL), and `FUN_005272b0(dest, name, string 0x90b,
  limit, style)` writes the label. The style is `FUN_00525150`, a value
  below 0xe read once from a table (0 when 0xe or more); `FUN_005272b0`
  switches on it (0..0xc, a jump table at 0x527628) to place the string
  and the name, some with brackets. The string 0x90b and that switch were
  not read.
- Any other mode: the same width and flag as class 0x164's label above
  (`(FUN_0047a6b0(id) << 7) / 100`, 0 for the local player; flag = hostile
  or unknown, `FUN_0047a070`), then the name widened into `dest` (0x80
  units).
- The `jne` on a null unit (0x452b0b) is dead: `FUN_0047a210` has already
  ended the run by then.

So over a dead player's body the bar is empty and the name carries the
0x90b string; a living player gets the plain name with a party life bar.

### The byte-to-unit widening (`FUN_00526f20`, 0x526f20, 70 bytes)

Read from the decompile and the disassembly (`tools/emu/disasm.py 526f20`),
no oracle run. d2d: `not ported` (no d2d function does this; `d2d_utf8_to_units`,
`FUN_00526320`, is the UTF-8 reader and is a different function: this one
has no decoding step).

`FUN_00526f20(ECX dst, EDX src, stack count)`, `ret 4`, `dst` an array of
16-bit units, `src` bytes. 85 callers in `game-calls.tsv`
(`grep 00526f20 docs/research/re/game-calls.tsv`), among them
`FUN_00452af0` (the name labels above), `FUN_005272b0` and `FUN_00430cd0`.

- It copies `src[i]` to `dst[i]`, each byte zero-extended (a byte 0x80..0xff
  becomes the unit 0x0080..0x00ff, Latin-1, no code page), while `i < count`
  and `src[i]` is not 0.
- Then it looks at `src[i]` again (the byte at the index the loop stopped on):
  - 0 (the string ended, or it is exactly `count` bytes long): `dst[i] = 0`.
    A string of exactly `count` bytes therefore writes `count + 1` units.
  - not 0 (cut by the count): `dst[i - 1] = 0`, so only `count - 1` characters
    survive.
- `count` of 0 or less skips the loop with `i = 0`: an empty `src` writes
  `dst[0] = 0`, but a non-empty one writes `dst[-1] = 0`, two bytes **before**
  `dst`.

So "at most 0x80 units" in the notes above is 0x80 units plus the NUL for a
string that is exactly 0x80 bytes, and 0x7f characters for a longer one. The
roster name is at most 15 characters, so the 0x100-byte local of
`FUN_00452af0` is not reached.

`FUN_00430cd0` (the credits text loader) uses the same call on a file with
no UTF-16 byte order mark: `dst` is a block of `2 * size + 1` bytes
(0x430e83), `src` a block of exactly `size` bytes (0x430d8b..0x430d99), and
`count = size`. A file with no NUL reads `src[size]` past its block, and when
that byte is 0 `dst[size]` writes two bytes at offset `2 * size` of a block
that has `2 * size + 1`: one byte past it. Which branch is taken depends on
the heap byte after the block, and the allocator's rounding was not read
(`FUN_0040b380` -> `FUN_0040a080`). The two UTF-16 branches of the same
function do not use it. bugs.md #98.

### Draw now or keep for later (`FUN_00452be0`, 0x452be0, 137 bytes)

`FUN_00452be0(ESI unit, stack mode)` (`ret 4`; the unit is in ESI from
the callers, `FUN_00454f30` and `FUN_00452d20`). Nothing at all when
`FUN_004b3230(&local)` is nonzero (what it tests was not read).
Otherwise:

- width = `FUN_004525f0(unit)`: 0 unless the unit is a monster (type 1)
  that `FUN_00464820` does not hold. Then the unit's life scaled to 0x80
  (`FUN_00625480(unit, 6, 0) << 7 / max life`; 0x80 for a zero maximum), or,
  when the pet list's value for it (`FUN_00479080`, node +0x1c) is 1 or
  more and not (the monster's +0xc4 bit 2 set with `FUN_00465c60` letting
  the local player interact), `value << 7 / 100`. Clamped to 2..0x80;
- name = `FUN_00464a60(unit)` (wide text by unit type; a null unit is
  fatal, `.\UNIT\CUnit.cpp` line 0x99d);
- position x = 0x70f854, y = 0x70f858 − 0x48.

Mode 0: `FUN_005022f0(name, x, y, 0, 1, width)`, which is `FUN_00502280`
(char-panel.md "Hover texts": keeps the text, 0x841ec8, and its place,
0x8426c8..d4; an empty or null name empties it) followed by storing
the width at 0x8426d8. It draws nothing itself. Mode not 0: the word
at 0x7a2c80 is cleared, then, if the name is non-null, copied to 0x7a2c80
(`FUN_005267e0`) and x, y, width are stored at 0x7a2d20, 0x7a2d24, 0x7a2d28
(who reads them was not traced).

So `FUN_00454f30`'s `FUN_00452be0(0)` for a monster with unit flag 0x200 is
not "no label" (as the flag table above has it): the unit's own name and
life bar become the hover text, and the three in-world buffers
(0x7a2a78, 0x7a2d30, 0x7a2f38) are the ones that are emptied.

### The hireling's label (`FUN_00452d20`, 0x452d20, 291 bytes)

`FUN_00452d20()` (no arguments; one caller, `FUN_00456ee0`, the frame's
draw order). Position and bar as `FUN_00454f30` computes them, for the
local player's mercenary:

1. Nothing when `d2d_automap_scroll_automap_scroll` (`FUN_0045ae90`) is 3.
2. The pet list node (`FUN_00478f20(player 0x7a6a70, PetType 7, 0)`) of
   the local player's id with PetType 7 (the merc, net-packets.md 0x7a) and
   node +0x20 equal to 0 (the third argument is 0; non-zero skips that
   test). No node (-1): nothing.
3. `FUN_00478e40(id)` gives node + 0x24; its +8 (node +0x2c) must be 1,
   else nothing (what that word is was not traced).
4. The unit is the client's monster with that id (`FUN_00463990`, type 1,
   the table at 0x7a5e70 + 0x200 per type, slot id & 0x7f); none: nothing.
5. Position (0x70f854 / 0x70f858): screen x / y less the view's
   (`FUN_00620650` / `FUN_006206b0` less `FUN_0045afc0` / `FUN_0045afd0`),
   or with the automap on (`FUN_004f51d0`) the map point of its precise
   position (`FUN_004f6760`; nothing when `FUN_004f66e0` says it is off the
   map), y + 0x10, x ∓ a quarter of the screen width (0x71146c) for scroll
   modes 1 / 2. The same code as `FUN_00454f30`'s.
6. `FUN_00452be0(1)`: the name and life bar are kept (0x7a2c80, 0x7a2d20..)
   for a later draw, not set as the hover text.

So the merc's label is a second path that does not go through the
cursor's unit. Who draws the kept text was not read. d2d: not ported.

### The death screen (`FUN_00453100`, 0x453100, 548 bytes)

Not a label either (neighbour address). `FUN_00453100()` is the last
call of the frame's `FUN_00456ee0` but one (0x4572ce, unconditional).
Read from the disassembly against `apps/d2d/session/view.cpp:292`, no
oracle run:

1. No local player (`FUN_00463dd0`): nothing. Not dead (`FUN_00464820`:
   +0xc4 bit 16, or a player in mode 0 or 0x11, or a monster in mode 0 or
   0xc): 0x7a2890 = 0 and nothing. Else 0x7a2890 = 1 (once the first
   image has been tried).
2. y = H/2 − 0x5e (H = `DAT_00711470`, rounded toward zero). The cel
   file `DATA\LOCAL\UI\<language>\youdiedhardcore` (`FUN_00525260` gives
   the language directory; `FUN_004788b0` loads it once, kept at
   0x7a2894; a failed load, which leaves it 0, is tried again on the next call) is drawn with
   `FUN_00502680` centred on W/2 (`DAT_0071146c`), mode 5; then
   `youdiedinst` (0x7a2898) 0x30 lower. Both are drawn in softcore and
   hardcore alike.
3. Font 2 (`FUN_00502ef0`; d2d `font30`). The first text line's bottom is
   y = H/2 + 2, then 0x30 per line shown, each centred on `DAT_007a5220`
   / 2 (the width, automap.md), colour 1 (red; `FUN_00502320`'s colour
   argument):
   - hardcore (`FUN_0044b850`, bit 4 of the byte at +0x1ef of the game
     record 0x7a0438): string 0x13e8;
   - else, if stat 0xaf (175, gold lost) is not 0: string 0x13e6
     formatted (`FUN_005269d0`, limit 0x100) with the stat's value;
     with 0, no line and the next line takes its place;
   - then, not hardcore and `FUN_0044dcd0` (byte 0x7a060c, the difficulty:
     2 is Hell, quests.md) above 0: string 0x13e7.

d2d reads the same, cited from the decompile, no oracle run: the image
names, centring, the 0x30 steps, H/2 − 0x5e and H/2 + 2 starts, the string
ids and their conditions, the colour (red) and font match line for line.
Differences, none of which d2d shows: the
game's "dead" test also takes +0xc4 bit 16 and a monster's modes
(d2d: `pmode == kModeDD` only; d2d plays its death animation as mode 0
while `pmode` is DD, `session/draw.cpp:229`, so the screen shows during
the animation as in the game, but when `pmode` becomes DD was not
checked); d2d keeps the images for ENG only (`load.cpp:210`); and it has
no 0x7a2890 (who reads it was not traced). The `ghost` / `all_dead`
condition is d2d's co-op addition.

### The kept label's draw (`FUN_00453060`, 0x453060, 150 bytes)

The reader of the kept text of `FUN_00452be0` / `FUN_00452d20`. `FUN_00453060()`
(one caller, the frame's `FUN_00456ee0`; read from the disassembly and run:
`tools/emu/ui_label.py` runs it with the width and height getters hooked
and diffs 2004 anchors, widths, heights and screen sizes against the rule
below. The decompile hides the x: `FUN_00502320` takes it in EDX). Nothing when the text at 0x7a2c80 is empty (`FUN_00526800`).
Otherwise, with W = the text's width in the current font
(`FUN_00501820` = `FUN_005017d0`, the glyph widths) + 8 and
H = the font's line height (`FUN_00501a40`, byte +10 of its header;
frontend-text-box.md) + 7:

- x = 0x7a2d20 − W / 2 (`sar`), at least 0, and at most
  `DAT_0071146c − W` (the screen width) when x + W passes it;
- y = 0x7a2d24 + 2, at least H, at most `DAT_00711470 − 0x1f` (the screen
  height); the text is drawn at y − 2 (`FUN_00502320(text, x, y − 2, 0, 1)`,
  colour 0, the same call as the char panel's plain labels);
- then the text buffer is cleared (`_memset`, 0xa0 bytes), so the label is
  one frame's.

The anchor (0x7a2d20, 0x7a2d24) sits right after the 0xa0-byte text buffer,
so the `_memset` leaves it; the oracle also checks that both anchor coordinates survive the clear. The width kept at 0x7a2d28 is not read here.
d2d: not ported. Re-check: `cd tools/emu && uv run python ui_label.py`.

### The frame of cels (`FUN_00452e50`, 0x452e50, 518 bytes)

Not a label helper: it shares this chain only through the work package
(neighbour address). `FUN_00452e50(ECX rect {x0, y0, x1, y1})` draws a
frame of one cel file's pieces (`DAT_007a2798`, which `controls.cpp` calls
boxpieces; its load site was not read) with `d2d_gamemenu_cel` (draw mode
5, colour -1). Callers: `FUN_0049d5a0`, `FUN_004a5270`, `FUN_004b8100`,
`FUN_004bee90` (game-calls.tsv). With R = x1 + 1, B = y1 + 1 and a piece
counter n that runs on through all four runs (starting 0, `++n` before use):

| run | cels | at |
|---|---|---|
| top | frame n % 6 + 2 | x from x0 + 12 step 12 while x < R − 13; y = y0 + 12 |
| bottom | n % 6 + 0x10 | x from x0 + 13 step 12 while x < R − 13; y = B + 10 |
| left | n % 3 + 10 | y from y0 + 0x16 step 12 while y < B − 1; x = x0 − 4 |
| right | n % 3 + 0xd | y from y0 + 24 step 12 while y < B − 1; x = R − 7 |
| corners | 0, 1, 8, 9 | (x0, y0 + 12), (R − 12, y0 + 12), (x0, B + 1), (R − 12, B + 1) |

d2d reads the same, cited from the decompile, no oracle run:
`apps/d2d/panels/controls.cpp` (the `box` lambda at line 434) has the same runs,
steps, limits, frame formulas and corners (the draw call is the cel getter,
and the frame choice is plain arithmetic).
