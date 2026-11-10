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
goes on, placing none. The spot is the item's client position less the
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
(`town/view.cpp`); the layered test, the object special cases and the
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
| `FUN_00454f30` (0x4550a8) | 0x200 | a monster (not classes 0x1a1 / 0x1a2) with the flag set gets no label: `FUN_00452be0(0)` clears the three name buffers; unset, it labels (`FUN_00454ad0`) |
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
  as on empty ground, unless shift is held: then it is the attack in
  place (`FUN_00461700(0)`), as on empty ground.
- A held frame (kind 1, `FUN_00462a20`) is let through by `FUN_00462930`
  when the press armed 0x7a526c (a null target arms it to 1, a unit to 2)
  and the target is null, or is a type 0, 1 or 5 unit whose owner is not
  walking, running or in mode 6. A monster target then needs the press
  flag to attack (the existing note in collision.md). A converted monster
  is a null target on every frame, so a held button never targets it: it
  keeps re-walking to the spot (with shift, the attack in place). That is
  d2d's "a held attack drops it".
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
