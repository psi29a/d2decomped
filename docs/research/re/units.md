# Units.cpp and friends — game.exe 1.14d

The small unit accessors in `.\UNITS\Units.cpp`, `Missile.cpp`,
`UnitFinds.cpp` and `MisStream.cpp` (`game-srcfiles.tsv`) that no other
page covers. Unit types: 0 player, 1 monster, 2 object, 3 missile, 4 item,
5 tile. Types 2, 4 and 5 have a static path at `+0x2c` (`+0xc` x, `+0x10`
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
| `FUN_00620f50` / `FUN_00620fa0` | Path flag 2 get / set (static `+0x1d`; dynamic `FUN_00648b30` / `FUN_00648b40`) |
| `FUN_00621c00` | `FUN_00649ef0(path, a, b, 0)` when the unit has a path |
| `FUN_00623520` / `FUN_00623570` | Allocate (32 bytes, zeroed, line 0x151e) / free (0x152b) a static path at `+0x2c` |
| `FUN_006238c0` | At the path's target (`FUN_00648a00` / `FUN_00648a10`) keeps the new direction (`FUN_00648810`); else faces it (`FUN_00621dc0`) |

## Animation

| Address | What |
|---|---|
| `FUN_00620d80` | A monster's speed word `+0x4c`: MonStats `+0x36`; with stats, stat 0x43 % of `+0x38` (mode 0xf) or `+0x36`, clamped to 0..0x7fff. No caller |
| `FUN_006211c0` | Sets a mode: `+0x40` for a player or monster with `+0x30`, else `+0x10`. No caller |
| `FUN_006212c0` | Sets the whole frame: `+0x38` = frame << 8, keeping the low byte |
| `FUN_00621310` | Advances `+0x44` by the speed `+0x4c` (a word), wrapping at the length `+0x48` |
| `FUN_00621330` | The same, wrapping back into (start, end) frames given as arguments |
| `FUN_00621420` | A 20-byte row: `0x6e8b90 + EAX × 0x14` when arg < 0x19a, else `0x6e8cd0 + ...`. No caller |
| `FUN_00621870` | (unit, &frame, &length): `+0x44` and `+0x48`; an object's length is its Objects.txt `+0xd8 + mode × 4` |
| `FUN_006218d0` | Copies a frame's event (1..4) from the animation data (`+0x50` `+0x10 + frame`, frame < 0x90) to `+0x4e`. No call; one raw-pointer-shaped match at `0x6b41e3` |
| `FUN_00621920` | A frame's event: a player in mode 0x12 or a monster in mode 0xe reads its sequence (`FUN_006633b0`, `FUN_00663410`); others the animation data; 0 from frame 0x90 |

## Type data

| Address | What |
|---|---|
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
| `FUN_00623600` | A dead monster (mode 0xc) whose MonStats flags have `+4` bit 7 and not `+6` bit 3, and `FUN_0063a770` false |
| `FUN_00623990` | The weapon a skill uses (unit, flag): by the skill's Skills `+0x168`: 1 the left hand's; 2 the right unless it's a weapon and the left is one too with skill flag 0x2000; 3 alternate by the frame (`+0x38 >> 8`, odd or above 5); 4 none; else the right hand's if a weapon, else the left. Only a usable weapon (`FUN_0062a4e0`); otherwise `FUN_0063c9b0`'s |

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
