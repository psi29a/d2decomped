# Units.cpp and friends — game.exe 1.14d

The small unit accessors in `.\UNITS\Units.cpp`, `Missile.cpp`,
`UnitFinds.cpp` and `MisStream.cpp` (`game-srcfiles.tsv`) that no other
page covers. Unit types: 0 player, 1 monster, 2 object, 3 missile, 4 item,
5 tile (`UNIT_WARP` in other tools' names). Types 2, 4 and 5 have a static path at `+0x2c` (`+0xc` x, `+0x10`
y, `+0x1d` flag 2), the others a dynamic one ([path.md](path.md)).
Nearly every accessor is fatal on a null unit (`FUN_00408090(line)`,
`FUN_00681e09(-1)`); the lines are given where they tell neighbours
apart. Re-check with `tools/ghidra/fn.py <address>`. "No caller" rows
were byte-scanned (rel32 calls, jumps, raw pointers) and are in
[stubs.md](stubs.md).

## Position and path

| Address | What |
|---|---|
| `FUN_00620360` | A tile's static path `+0xc..+0x18` from four arguments (0x6af, 0x6b1). No caller |
| `FUN_006203b0` / `FUN_00620410` | Precise x / y, 16.16: a static path's x / y << 16, else `FUN_00648940` / `FUN_00648970` |
| `FUN_00620470` / `FUN_006204c0` | Sets a static path's x / y; any other type is fatal (0x6fa, 0x711) |
| `FUN_00620710` / `FUN_00620780` | \|x1 − x2\| / \|y1 − y2\| of two units. No caller |
| `FUN_006207f0` / `FUN_00620830` | Sets the path's screen x / y (`FUN_006489e0` / `FUN_006489f0`). No caller |
| `FUN_00620980` | A tile's static path `+4` / `+8`. No caller |
| `FUN_00620ae0` | Fills a static path: `+0` the room, `+0xc` / `+0x10` x, y, `+0x1c` byte 0, `+4` / `+8` its world pixel spot (`FUN_00643260`) |
| `FUN_00620b50` | Clears the unit's room (static `+0` = 0, else `FUN_00648a90(path, 0)`) |
| `FUN_00620c70` / `FUN_00620cd0` | A player's, monster's or missile's path target type / id (`FUN_00648bb0`). No caller |
| `FUN_00620d30` | Sets a player's or monster's path target (`FUN_00648b90`). No caller |
| `FUN_00620f50` / `FUN_00620fa0` | Path flag 2 get / set. The getter is static (`+0x1d`) for types 2 and 4 only, the setter for 2, 4 and 5 (`+0x1d = (v != 0)`); a tile (type 5) reads through `FUN_00648b30` (path `+0x34` & 2) but writes `+0x1d` (bugs.md 80). The dynamic getter returns 0 or 2, not 0 or 1 |
| `FUN_00621c00` | `FUN_00649ef0(path, a, b, 0)` when the unit has a path |
| `FUN_00623520` / `FUN_00623570` | Allocate (32 bytes, zeroed, line 0x151e) / free (0x152b) a static path at `+0x2c` |
| `FUN_006238c0` | At the path's target (`FUN_00648a00` / `FUN_00648a10`) keeps the new direction (`FUN_00648810`); else faces it (`FUN_00621dc0`) |

## Animation

| Address | What |
|---|---|
| `FUN_00620d80` | A monster's speed word `+0x4c`: MonStats `+0x36`; with stats, stat 0x43 % of `+0x38` (mode 0xf) or `+0x36`, clamped to 0..0x7fff. No caller |
| `FUN_006211c0` | Sets a mode: `+0x40` for a player or monster with `+0x30`, else `+0x10`. No caller |
| `FUN_00621210` | ESI unit, (old position): the sequence row at `+0x38` (`FUN_00663410`, with the events of the rows passed since the old one): its frame into `+0x44` (<< 8), its mode into `+0x40`, its event into `+0x4e`; true when the mode changed |
| `FUN_00621260` | Starts a sequence: `+0x30` the sequence (`FUN_006633b0`; none: nothing), `+0x34` its length (rows << 8, `FUN_006633d0`), `+0x38` its position 0 (256ths of a row), `+0x3c` its rate 0x100, `+0x48` the length again (`FUN_006633f0` << 8), then `FUN_00621210(0)`: row 0's event; `+0xc4` \|= 0x4000 |
| `FUN_006212c0` | Sets the whole frame: `+0x38` = frame << 8, keeping the low byte |
| `FUN_00621310` | Advances `+0x44` by the speed `+0x4c` (a word), wrapping at the length `+0x48` |
| `FUN_00621330` | The same, wrapping back into (start, end) frames given as arguments |
| `FUN_00621420` | A 20-byte row: `0x6e8b90 + EAX × 0x14` when arg < 0x19a, else `0x6e8cd0 + ...`. No caller |
| `FUN_00621870` | (unit, &frame, &length): `+0x44` and `+0x48`; an object's length is its Objects.txt `+0xd8 + mode × 4` |
| `FUN_006218d0` | Copies a frame's event (1..4) from the animation data (`+0x50` `+0x10 + frame`, frame < 0x90) to `+0x4e`. No call; one raw-pointer-shaped match at `0x6b41e3` |
| `FUN_00621920` | A frame's event: a player in mode 0x12, or a player or monster in mode 0xe, reads its sequence (`FUN_006633b0`, `FUN_00663410`); others the animation data (`+0x50`, `+0x10 + frame`); 0 from frame 0x90 (signed, so a negative frame reads below the data) |
| `FUN_00623e00` | A step, `+0x4e` cleared first. No sequence: `+0x44` on by `+0x4c`, wrapping at `+0x48`, the frames' events into `+0x4e`. A sequence (`+0x30`): `+0x38` on by `+0x3c`, less `+0x34` once it reaches it; `+0x48` less `+0x3c`; `FUN_00621210(the old position)`, a new mode setting `+0xc4` \|= 0x4000. So a sequence's event is read with `+0x38` at its row: under 0x100 only for row 0 |

## Type data

| Address | What |
|---|---|
| `FUN_006205a0` | A unit's collision size: player 2; monster MonStats2 `+9` SizeY (`FUN_00451fe0` by class, 0 if not found; the field is a signed char, sign-extended); object its data row's `+0xd4` SizeY; missile Missiles `+0x18a` Size (`FUN_0046ace0` by class, 0 if not found; an unsigned byte); item 1; any other type 0. No null check. Oracle: `tools/emu/units_small.py check_unit_size_y` (collision.md cites it from the caller side; d2d loads the same MonStats2/Objects SizeY fields at gamedata time, gamedata.cpp:154/674, instead of dispatching per unit at runtime) |
| `FUN_00620900` | A unit's target coordinates (unit, out point): types 0 (player), 1 (monster), 3 (missile) and anything past 5 take the dynamic path's ScreenPos (`FUN_006489c0`/`FUN_006489d0`, path `+8`/`+0xc`); types 2 (object), 4 (item) and 5 (tile) take the static path's own `+4`/`+8` screen x/y instead. 0x7e7 null. Oracle: `tools/emu/units_small.py check_target_coords`. (unit-sounds.md's citation doesn't give the per-type split; this is it, read from the decompile) |
| `FUN_006205a0` | A unit's footprint height in subtiles (stdcall; null asserts, 0x74b): a player 2, an item 1; a monster its MonStats2 SizeY (`FUN_00451fe0`, byte `+9`, signed; the loader `FUN_006571a0` maps SizeY to 9), an object its objects.txt SizeY (`*(+0x14)` `+0xd4`; loader `FUN_0063f690`, 0x63f89a), a missile its Missiles.txt Size (`FUN_0046ace0`, byte `+0x18a`, unsigned; loader `FUN_00661b20`, 0x662f94); 0 for a row not found or any other type. The width is `FUN_00620510` (`+8`, `+0xd0`). d2d: `rules::unit_size_y`, checked by `tools/emu/unit_size.py` |
| `FUN_006219c0` | An object's Objects.txt `+0x120 + mode`; 1 for others |
| `FUN_00621a70` | An object's Objects.txt `+0x13a`, 0 for others |
| `FUN_00621ac0` | Objects.txt `+0x13c` == 1 (not an object: fatal 0xe1e) |
| `FUN_00621b00` | Objects.txt `+0x167` bit 0 |
| `FUN_00621b70` | An object's data `+8` (0xe4e, 0xe55) |
| `FUN_00621f60` | A monster with `FUN_004638a0` non-zero |
| `FUN_00622270` | Objects.txt `+0x168` |
| `FUN_006222c0` / `FUN_00622300` / `FUN_00622350` | An object's data byte `+5`: get, set, test bits |
| `FUN_00622830` | A monster's weapon (`FUN_0063c9b0` on its inventory) |
| `FUN_00622dc0` | `FUN_00622d00` != 0 |
| `FUN_00622de0` | (missing life + missing mana, both >> 8) × stat 0xc >> 2; 0 when nothing is missing |
| `FUN_00623600` | A dead monster (mode 0xc) whose MonStats2 row (`FUN_00451fe0`) has byte `+4` & 0x80 and not byte `+6` & 8, and `FUN_0063a770` false |
| `FUN_00623990` | The weapon a skill uses (unit, flag): by the skill's Skills `+0x168`: 1 the left hand's; 2 the right unless it's a weapon and the left is one too with skill flag 0x2000; 3 alternate by the frame (`+0x38 >> 8`, odd or above 5); 4 none; else the right hand's if a weapon, else the left. Only a usable weapon (`FUN_0062a4e0`); otherwise `FUN_0063c9b0`'s |
| `FUN_00623d60` | `GetItemComponent` (unit, item): off an arm slot (`FUN_00627d40` not 4 R_ARM / 5 L_ARM) it's `FUN_00628660`'s default (ItemsTxt `+0x115`). On an arm slot: both grid slots 4/5 (`FUN_0063bde0`) that pass `FUN_0062a4e0` count; with both present, neither two-handed (`FUN_006289c0`) and the unit able to dual-wield (`FUN_006235a0`), the item that isn't `FUN_0063bef0`'s own equipped weapon returns 6 instead of `FUN_00628660`'s default. No null check. Oracle: `tools/emu/units_small.py check_item_component`, all seven callees mocked — a standalone check of the branch logic, not a diff (d2d builds neither the 0x9c nor 0x9d item packet this feeds; net-packets.md) |

## Player data (unit `+0x14`)

| Address | What |
|---|---|
| `FUN_00622060` | (pool, unit): frees the three header pairs of the data at `+0x14` (`FUN_0065c490`, `FUN_00660fd0`), `FUN_0066a260`, then the data (0xf76) |
| `FUN_006220f0` | Copies the name into the player data (`FUN_004135d0`, at most 0x7fffffff) |
| `FUN_00622150` | Only its checks (player, data); does nothing |
| `FUN_00622ea0` / `FUN_00622f10` | Keeps the right / left skill: its id at `+0x70` / `+0x74` (line 0x13fa, 0x1409), `FUN_00643ad0` at `+0x78` / `+0x7c` |
| `FUN_00623060` / `FUN_006230c0` | Read those two pairs (0 / -1 for a non-player) |
| `FUN_00623220` / `FUN_00623290` | Read the switch's kept pairs `+0x84` / `+0x8c` and `+0x80` / `+0x88` ([weapon-switch.md](weapon-switch.md)) |
| `FUN_00623300` / `FUN_00623350` | Set them |
| `FUN_006233a0` / `FUN_00623410` | An item's id at `+0x90` (0 for none) / read it |

## Misc

| Address | What |
|---|---|
| `FUN_00620300` | Frees a unit: stats (`FUN_00626d10`), `FUN_00643a60`, a monster's extra block (0x69a); type 6, id -1, then the unit (0x6a4) |
| `FUN_00621150` | Takes `+0x64` (returns it and clears it) |
| `FUN_006219f0` | (unit, Skills row): the unit's skill node for it (skill list `+0xa8`); a bad row is fatal (0xdfb). No caller |
| `FUN_00621c30` | (ESI unit, arg, EBX): `+0x94` / `+0x98` = them; with stats, state 0x62 and its stat list hold them as stats 0x161 / 0x162; flag 0x400 in `+0xc8` |
| `FUN_006234c0` / `FUN_006234f0` | `+0x7c` set / get |
| `FUN_0064a100` / `FUN_0064a150` | Missile.cpp: allocate (0x34 bytes, `+0x1c` = -1, line 0x22) / free (0x3e) a missile's data |
| `FUN_0065a900` | UnitFinds.cpp: grows a find list (ESI) by 15 entries (line 0xc5) |
| `FUN_00663d40` / `FUN_00663d90` | MisStream.cpp: allocate (8 bytes, 0x2e) / free (0x3b, 0x3c) the stream record (`FUN_0064a6f0` / `FUN_0064a6d0`). The first has no caller |
| `FUN_00663e00` | Walks a missile's path points (after `FUN_00648cf0(path, 0x10)` and `FUN_00649970`): each one that collides (`FUN_0064d9b0`) and touches a unit (`d2d_missiles_missile_touches`) calls the callback (a bad pointer is fatal, 0x71). No caller |
| `FUN_00663ed0` | Streams a missile along its path: per point the callback (x, y, value, kind 0 / 1 / 2, direction) into the stream's slots, the value dropping by the step; leftover slots get the cleanup callback. No caller |

## Verified in the emulator

Two oracles run game.exe's own code on random fake units (a 0x100-byte unit
of random bytes with the type, `+0x14` data, `+0x2c` path and an Objects.txt
row set) and compare the return value, every byte of the unit, path, data and
row blocks after each call that returns (an unexpected write fails; a call that
ends in the fatal path is compared on its line, return and log), the ordered
log of the hooked callees (with their registers), the bytes `ret n` popped and
the fatal line (`FUN_00408a60`'s third argument;
`FUN_00408090` runs for real and returns its return address). Planted
model defects (33 and 33) are each caught by `--mutation-check`. Stock
game.exe 1.14d only (SHA-256 checked).

| Oracle | Functions |
|---|---|
| `tools/emu/units_small.py` (64 functions) | `FUN_00620360`, `006203b0`, `00620410`, `00620470`, `006204c0`, `00620710`, `00620780`, `006207f0`, `00620830`, `00620980`, `00620ae0`, `00620b50`, `00620c70`, `00620cd0`, `00620d30`, `00620f50`, `00620fa0`, `00621150`, `006211c0`, `006212c0`, `00621310`, `00621330`, `00621420`, `00621870`, `006218d0`, `00621920`, `006219c0`, `00621a70`, `00621ac0`, `00621b00`, `00621b70`, `00621c00`, `00621f60`, `00622150`, `006220f0`, `006221e0`, `00622230`, `00622270`, `006222c0`, `00622300`, `00622350`, `00622dc0`, `00622e90`, `00622ea0`, `00622f10`, `00622f80`, `00622ff0`, `00623060`, `006230c0`, `00623120`, `006231a0`, `00623220`, `00623290`, `00623300`, `00623350`, `006233a0`, `00623410`, `006234c0`, `006234f0`, `00623520`, `00623570`, `006205a0`, `00620900`, `00623d60` |
| `tools/emu/units_calls.py` (17 functions) | `FUN_00620300`, `00620d80`, `00621c30`, `00621cc0`, `00621ed0`, `00621f90`, `00622060`, `00622830`, `00622de0`, `00623600`, `006238c0`, `006219f0`, `0064a100`, `0064a150`, `0065a900`, `00663d40`, `00663d90` |

What the runs settled (the earlier rows above stand; these are additions and
corrections):

- Fatal lines per function are exact: null unit and wrong type give different
  lines in each getter (the table above and `names.tsv` give the first; the
  oracle's cases hold them all). `FUN_00621ac0` (ECX) and `FUN_006219c0` have
  no null check (disassembly; the oracle requires game.exe's own unmapped
  read for a null unit). `FUN_006221e0` / `FUN_00622230` check null and type
  but not the data pointer, unlike `FUN_006220f0` / `FUN_00622150` (0xf99 /
  0xfa7); with no data the oracle sees game.exe write / read unmapped
  address 0x2c.
- Results compared whole (EAX): the byte getters zero-extend (`movzx`) and the
  tests (`FUN_00621ac0`, `FUN_00621f60`, `FUN_00622dc0`, `FUN_00623600`) return
  0 or 1. `FUN_00621f60` calls `FUN_004638a0` with ECX = the monster's class
  (`+4`) and EDX = 0xc.
- Static means types 2, 4 and 5 everywhere except `FUN_00620f50` (2 and 4).
  `FUN_006203b0` / `00620410` shift the static x / y left 16 and wrap in 32
  bits; the |dx| / |dy| subtract signed and negate, so INT_MIN stays negative;
  a dynamic unit with no path counts as 0 (`FUN_00620710`).
- `FUN_00620ae0`: `+0` room, `+0xc` x, `+0x10` y, byte `+0x1c` = 0, then
  `FUN_00643260(&x, &y)` and its outputs go to `+4` / `+8`.
- Animation: `FUN_00621310` adds the signed word `+0x4c` and wraps by `+0x48`
  when the sum is at or past it (signed); `FUN_00621330` wraps by
  `(end - start) * 256`; `FUN_00621420`'s compare is signed (`jge`).
  `FUN_006212c0` keeps only the low byte of `+0x38` and adds `frame * 256`.
  `FUN_006218d0` copies an event byte of 1..4 only.
- Player kept skills: `FUN_00622ea0` / `00622f10` keep right / left at
  `+0x70/+0x78` / `+0x74/+0x7c`; `FUN_00622f80` / `00622ff0` keep right / left
  at `+0x80/+0x88` / `+0x84/+0x8c` (the switch pair, [weapon-switch.md](weapon-switch.md)).
  The pair is (id from `FUN_00643ce0(node, file, line)`, word from `FUN_00643ad0`).
  `FUN_00623120` reads the right switch pair (`+0x80/+0x88`) and
  `FUN_006231a0` the left (`+0x84/+0x8c`); each also resets the kept pair of
  its hand (`+0x70/+0x78` / `+0x74/+0x7c`) to (0, -1) and `+0x90` / `+0x94` to
  (-1, 0). A non-player or null unit reads as (0, -1). A player
  without data halts at 0x1438 / 0x1449 / 0x145a / 0x1470 / 0x1487 / 0x1499.
- `FUN_006233a0` stores the item's id (`+0xc`) or 0 for none; another unit type
  than an item halts 0x14c9, a non-player is ignored.
- `FUN_00620d80`: class out of range or a null row gives 0 (the range test is
  signed, `jl` / `jge` in the disassembly; the oracle's tables are too small
  to tell it from an unsigned one); without
  stats the raw `+0x36` word; with stats the signed word (`+0x38` in mode
  0xf, else `+0x36`) times stat 0x43 divided by 100 truncating toward zero
  (32-bit product), 0 when not positive, 0x7fff at and above 0x7fff.
- `FUN_00622de0`: `(max - current) >> 8` only when current is below max
  (unsigned) for life and mana; both 0 returns 0 without reading stat 0xc;
  otherwise `(stat 0xc * (life + mana)) >> 2` as a **logical** shift of the
  32-bit product.
- `FUN_00621c30` (ESI unit, EBX second value, first on the stack) and its
  wrapper `FUN_00621cc0 (unit, a, b)`: `+0x94` / `+0x98` = a / b; with stats the
  state-0x62 list is found or made (`FUN_006251f0(pool, 0, 0, type, id)`,
  `FUN_006252d0(list, 0x62)`, `FUN_00626e10(unit, list, 1)`), stats 0x161 / 0x162
  = a / b, then flag 0x400 of `+0xc8`. If the list cannot be made it returns
  before setting the flag.
- `FUN_00623600` tests the MonStats2 row (`FUN_00451fe0`) bytes `+6` and `+4`
  against entries [3] (8) and [7] (0x80) of the bit-mask table `0x6ce268`
  (the one `FUN_00410b10` indexes; byte tests), not immediates. The oracle
  leaves the table at its stock values;
  `FUN_00622830` returns the first output of `FUN_0063c9b0` only when it
  reports found.
- Allocation: `FUN_00623520` (0x20 zeroed bytes at `+0x2c`, pool `+8`, line
  0x151e) and `FUN_00623570` (release, 0x152b) leave `+0x2c` as it was after the
  release. `FUN_0064a100` / `0064a150` (0x34 bytes, `+0x1c` = -1, lines 0x22 /
  0x3e) clear `+0x14`. `FUN_00621f90` builds the 0x16c-byte player data
  (zeroed; `+0x10/+0x14/+0x18` from `FUN_0065c430(FUN_004dc0b0)`,
  `+0x1c/+0x20/+0x24` from `FUN_00660f30(FUN_004dc0b0)`, `+0x2c` =
  `FUN_0061ae30(1)`, `+0x4c` = 0, then `FUN_0066a250`); `FUN_00622060` releases
  the six handles in pairs, `FUN_0066a260`, the block (0xf76), `+0x14` = 0.
  `FUN_0065a900` grows a find list by 15 entries (`(count + 15) * 4` bytes via
  `FUN_0040b4b0`). `FUN_00663d40` / `00663d90` act on the stream slot `+0x30` of a
  missile's data; for any other unit `FUN_00663d40` still allocates and
  leaks (bugs.md 82).
- `FUN_00620300` frees a monster's data (0x69a, the unit still intact), then
  writes type 6 / id -1 **before** the unit's own release (0x6a4); the pool
  release hook reads the unit at each release to check that.

Not covered: the allocator's copy on growth (`FUN_0040b4b0` is hooked, its
size and arguments are checked), the callees (`FUN_00648940` ... are
recorded, not run), `FUN_00623990` (the skill weapon), the
missile stream walkers `FUN_00663e00` / `00663ed0`, `FUN_00621dc0`.
`FUN_00623d60` (`GetItemComponent`, the item's visual component for the
0x9c/0x9d item packets) is now covered (`check_item_component`, below):
off an arm slot it's `ITEM_GetItemsTxt_bComponent`'s default; on an arm
slot with both hands one-handed and the unit able to dual-wield, the item
that isn't the inventory's own equipped weapon gets component 6 instead.
All seven callees are mocked, so this run is a standalone check of the
branch logic against the decompile, not a diff against d2d (which builds
neither of those packets — net-packets.md).

## Client-side units (CUnit.cpp, CUnitEvent.cpp, monster-flag helpers)

`tools/emu/unit_client.py` runs 51 more functions in the emulator, same rig
as the two oracles above (fake units, whole-block comparison, hooked callees
logged with their registers and stack arguments, `ret n` bytes, fatal lines
of `.\UNIT\CUnit.cpp`). `names.tsv` has the rows. Planted model defects
(61) are each caught by `--mutation-check`; stock game.exe 1.14d only.

Layout the oracle settled: the client keeps two hash tables of units,
table A `0x7a5e70` and table B `0x7a5270` (role labels unverified, see
unverified.md), each six unit types of 0x200 bytes (0x80 chain heads); a chain runs through `+0xe4`, a node has its
type at `+0`, its id at `+0xc`, the bucket is `id & 0x7f`.

| Function | What it does |
|---|---|
| `FUN_00463720` / `00464810` / `004647d0` | `+0xc8 & mask`; `+0xc4 \|= 0x10000` (dead); clears 0x10000 and sets 4 unless a monster whose `FUN_004638a0(class, 9)` is 0 |
| `FUN_00463830` / `00463900` | MonStats row word `+2` (BaseId) of a 0x1a8-byte row (table `+0xa78`, count `+0xa80` of the block at `0x744304`); an out-of-range id is returned **unchanged** by `FUN_00463830`, `-1` by `FUN_00463900` |
| `FUN_00463940` | (EAX bucket, EDX table, ECX id, stack type) first node with that id; its type must equal the stack type or fatal line 0x5a |
| `FUN_004639b0` | the same on table B, bucket `id & 0x7f` |
| `FUN_00463c70` | zeroes both tables (0x300 dwords each) |
| `FUN_00463c90` | (stack table, EBX callback) every node of the 0x80 buckets in order, the next link read before the call; `FUN_00464950` / `00464990` / `004649d0` run it over table A's type 0 / 1 / 2 with ECX as the callback (null fatal 0x961 / 0x96c / 0x977) |
| `FUN_00463e20` / `00463e50` / `00463e80` / `00463eb0`, `004ca8a0` / `004ca8d0` | null-checked tail jumps: ECX = unit `+0x54` (lines 0x563, 0x56c, 0x575, 0x57e) or unit `+0x78` (0xc2c); EDX passes through untouched |
| `FUN_00464370` | false when the selected unit is an object whose row flag byte `+0x167` has 0x80, else selected == unit |
| `FUN_004643f0` | (unit, type, stack id) the lookup in table A, then `FUN_00620c10(unit, found)`; a type mismatch is fatal 0x5a |
| `FUN_004645b0` | (ESI unit) ready when no skill, no row is 0, anim != 0x12 is 1, else `seqinput` (0 and 0xff never) <= `+0x38 >> 8` (arithmetic shift) |
| `FUN_00464770` / `004647a0` / `00464cf0` / `00464c90` / `00464c40` | `+0xd8` get / set (null fatal 0x8a5 / 0x8af), `+0x68` set (0xa4a), `+0x58` = 3 (0xa34), `+0x58` counted down with `FUN_00639df0(unit, 0x8f) == 0` as the result (0xa19; 0 and no call when it was 0) |
| `FUN_00464860` | dead by type: player mode 0x11, monster mode 0xc without `FUN_004638a0(class, 0x14)`; object row byte `+0x1b7 & 2`, or mode 2 with `+0x1b7 & 1`; missile flag 0x10000; item mode 3 (on the ground) |
| `FUN_00464a10` / `00464a30` / `00465400` / `00464930` | first type 5 unit on the `+0xe8` chain; warp name through `FUN_0061a1d0` and `FUN_0061db70 + 0x1be`; party id (type 0 only, else `EAX \| 0xffff` with EAX's upper half kept); frees the light map `+0x64` |
| `FUN_00465e80` / `00465ee0` / `00465f00` | unlink the first node matching id and type (ECX bucket, EDX table, EDI type, EBX id; the wrappers take id and type and pick table A `0x7a5e70` / B `0x7a5270`). A type 4 node first goes through `FUN_004879d0`, then `FUN_00465870` frees it. EAX on return is `FUN_00465870`'s result, or the last node of the chain when nothing matched (0 for an empty bucket) |
| `FUN_004661c0` (`unit_create`) | `FUN_00620290(0, type)`, then `FUN_00465fd0` with EAX = the new unit and EBX the first stack argument; no unit, 0 |
| `FUN_004ae080` / `004ae110` / `004ae320` ... `004ae3a0` | monster data word `+0x28` & mask, data `+0x30`, `FUN_004ac7e0(unit, 4 / 0x40 / 8 / 0x10 / 2)` for monsters only; `FUN_004ade40` ... `004ade70` call `FUN_004add90(unit, 0, 0..3)`; `FUN_004ccf20` / `004ccf30` store / read `0x72a348` |
| `FUN_004dc110` / `004dc190` / `004dc0c0` | unit event list at `+0x90` (nodes 0x20 bytes, next at `+0x1c`, previous at `+0x18`): add at the head from the unit's pool (`+8`, line 0x48), remove every event of a type and state (line 0x80), free all (0x2f) |

Not covered: the callees themselves (`FUN_004ac7e0`, `FUN_004add90`,
`FUN_00465fd0`, `FUN_00465870`, `FUN_00620290`, `FUN_00620c10`,
`FUN_0046f1c0` ... are hooked and logged, not run), `FUN_00464d20` /
`00464d90` (inventory walk), `FUN_00466820` / `004667f0` (preset units).
No original bug found in these.

## Unit allocation (`FUN_00620290`)

The two stack arguments are the memory-pool pointer and unit type (`ret 8`).
The allocator `FUN_0040b430` is called with ECX = pool, EDX = 0xf4,
`.\\UNITS\\Units.cpp`, line 0x66e and flag 0. Its result is passed directly
to the CRT clear for 0xf4 bytes, then unit +0 is set to type and +8 to pool.
For type 1 only, a second allocation uses the same pool, size 0x60, line
0x67b and flag 0; its pointer is stored at unit +0x14 and the block is
cleared. The function returns the first pointer; no other unit bytes are
set. The allocation and clear calls are visible at `0x6202a4–0x6202ec`.

`tools/emu/unit_allocate.py` runs the original function and CRT clear on
240 type/pool combinations with the pool allocator hooked to return
poisoned guarded blocks. It checks all block bytes, guard bytes, allocation
arguments, return address and stack cleanup. Two model mutations are caught.
Three additional cases inject null at the first or second allocation: all
fault on the null write inside the CRT (`0x681f4f`), rather than returning
null. In the second-allocation case the already-zeroed unit retains type 1,
the pool, and a null data pointer at the fault. This is an injected-failure
result: the real pool allocator and its exhaustion/error handling are not
run, so it does not establish what a real out-of-memory event does. The
script compares stock code to Python expectations, not to a d2d port.

Re-check with `cd tools/emu && uv run python unit_allocate.py --exe
/path/to/Game.exe --mutation-check`; the executable's 1.14d SHA-256 is
checked before execution. No MPQs are needed.
