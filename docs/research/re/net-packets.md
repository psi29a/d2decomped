# Net packets: gameplay layouts (1.14d game.exe)

What a d2d client needs to join a TCP/IP game hosted by the original
1.14d game.exe (and, later, to host one): the server → client packets a
host sends during Act 1 play, the client → server packets d2d would
send, what game.exe's client works out for itself, and how it all maps
onto d2d's `View` (world.hpp) and `Command` (protocol.hpp).

Research notes, 2026-09-30. Read network.md first: it has the server
loop, the C→S dispatcher and both size tables. The join handshake (0x00
.. 0x06, 0x0b, 0x67 / 0x68 / 0x6a, 0xaf / 0xb0..) and the packet
compression are out of scope; another note covers them.

Everything here was read in game.exe's code (handlers and, where named,
the server builders). **(?)** marks what wasn't confirmed; community
names (d2-clientless, D2MOO) are labels only (re/unverified.md).

Byte order little-endian. Offsets count from the id byte at +0. Unit
types: 0 player, 1 monster (NPCs too), 2 object, 3 missile, 4 item,
5 warp. Positions are u16 subtiles (a DS1 cell is 5 × 5 subtiles).

## How the client takes S → C packets

- **Table** `0x7114d0`: 12 bytes an id, `{handler, size, unit handler}`
  for 0x00..0xae (−1 variable; `0x45c900` is a no-op). Past 0xae the
  table holds other data.
- **Sizes**: the server's table is `DAT_00730ae8` (u32 an id; 0 = never
  sent), `FUN_0052b920` works out the variable ones. The two tables agree
  except 0x17 (server 0: never sent) and 0x80 (server 0, client 4).

  | Id | Length |
  |---|---|
  | 0x16, 0x5b | u16 at +1 |
  | 0x26 | 12 + both string lengths (two NUL-terminated strings from +10) |
  | 0x3e | u8 at +1 |
  | 0x94 | (u8 at +1 + 2) × 3 |
  | 0x9c, 0x9d | u8 at +2 |
  | 0xa6 | u16 at +2 |
  | 0xa8, 0xaa | u8 at +6 |
  | 0xac | u8 at +0xc |
  | 0xae | u16 at +1 + 3 (0 if > 0x1fd) |

- **Dispatch** (`FUN_0045f7b0`): walks the received buffer packet by
  packet; an id over 0xae or a size that doesn't match the table is a
  fatal error (the game exits, error 0x1449 / 0x1450). A pre-pass unpacks
  0x18, 0x95, 0x96 (`FUN_0045d900`, `FUN_0045da90`, `FUN_0045dbe0`).
- **Unit handlers** (0x0c..0x10, 0x17, 0x4c, 0x4d, 0x67..0x72): the unit
  is looked up first (`FUN_00463990`: for 0x67..0x6d type 1 and the u32
  at +1, otherwise the type at +1 and the u32 at +2; hash at `0x7a5e70`,
  128 buckets a type). **An unknown unit's packet is dropped**; a known
  one is queued (`FUN_0045f730`) and run later in the frame by
  `FUN_0045fa40` (ECX = unit, EDX = packet). So a host must send a unit's
  assign packet before anything about it.
- **Server side**: builders append to a per-client 0x200-byte buffer
  (`FUN_0053b280`), flushed every 40 ms (network.md).
- **Bitstreams** (0x18, 0x95, 0x96, 0x3e, 0x9c, 0x9d, 0xa8, 0xaa, 0xac):
  LSB-first, the same order as d2d's `d2s::detail::Bits`. Writer
  `FUN_00410eb0(buf, value, bits)`, reader `FUN_00411020(n)` (signed
  `FUN_00411030`).

### The unit command (`FUN_00480c10`)

Most unit packets become one "unit command": a command number and seven
dwords `s[0..6]`, handed to `FUN_00480c10(cmd, unit, &s, 1)`, which
switches on the unit's type: player `FUN_00461250`, monster
`FUN_004aff60`, object `FUN_004bd6d0`, item `FUN_004c1b80`. The client's
own clicks go through the same function (with 0) before they're sent
(below). Players and monsters share the numbering; for monsters
`DAT_006da4d8` maps each to `{mode, has target}`.

| Cmd | Mode | Meaning |
|---|---|---|
| 0 / 1 | WL | walk to a unit / to x, y |
| 4 / 5 | SC | cast at x, y / on a unit |
| 6 | GH | get hit (`s[2]` → unit +0xb0) |
| 7 | NU | stop at x, y |
| 8 / 9 | DT / DD | dying / dead |
| 0xa / 0xb | A1 | on a unit / at x, y |
| 0xc / 0xd | S1 | at x, y / on a unit |
| 0xe / 0xf | S2 | at x, y / on a unit |
| 0x10 / 0x11 | A2 | on a unit / at x, y |
| 0x12 | BL | block |
| 0x13 | (none) | hit: no mode change (what 0x0c sends) |
| 0x14 | KB | knockback (the player re-paths, type 0xb, to s0, s1) |
| 0x15 / 0x16 | SQ in the table | skill at x, y / on a unit (0x4d / 0x4c) |
| 0x17 / 0x18 | RN | run to x, y / to a unit |
| 0x19 | S1 | |
| 0x1a / 0x1b | S3 | at x, y / on a unit |
| 0x1c / 0x1d | S4 | at x, y / on a unit |

"At x, y" commands read `s[0], s[1]` as the point; "on a unit" ones
read them as the target's type and id. Objects take two: 3 (new mode,
`FUN_004bcf60` → `FUN_00624690`) and 0x15 (the object class's client
function, table `DAT_006da8c4`, 0x14 bytes a class).

**Position correction** `FUN_004804e0(unit, x, y, a, b, c)`: runs when a
packet reports where a unit is. Nothing happens within a tolerance
(subtiles):

| Unit | Tolerance |
|---|---|
| Own player | (ping `FUN_0044ce60` + 50) / 128, plus 3 walking, 7 running, else 5 |
| Other players | 15 |
| Monsters | 15; 5 or 7 while moving |
| a = 1 / a = 2 | 10 / 0 (always) |

Outside it: another player snaps (`FUN_00463180`); a monster re-paths
from where it is to the reported point (`FUN_00650c60`); the own player
is **never snapped**: the client sends C→S 0x5f with where it thinks it
is, and the server walks it there, resyncs it (`FUN_0054cb10` /
`FUN_0054cc40`) or, for a big gap, reassigns it (S→C 0x15).

## Server → client, by subject

### Units: assign and remove

**0x59 assign player** (26; `0x45e4c0` → `FUN_00466200`)

| Off | Size | Field |
|---|---|---|
| +1 | u32 | unit id |
| +5 | u8 | class (0..6) |
| +6 | char[16] | name |
| +0x16 | u16 | x |
| +0x18 | u16 | y |

The look comes afterwards, from the items the player wears (0x9d).

**0xac assign monster** (variable; builder `0x53e2e0`, handler
`0x45f190`). NPCs, the merc and summons are monsters too.

| Off | Size | Field |
|---|---|---|
| +1 | u32 | unit id |
| +5 | u16 | MonStats row (class) |
| +7 | u16 | x |
| +9 | u16 | y |
| +0xb | u8 | life / 128 (`FUN_005a5650`) |
| +0xc | u8 | total length |
| +0xd | bits | the tail |

The tail, in order:

1. **Mode**, 4 bits: 0, 8, 9, 0xc kept; anything else becomes 1 (NU).
2. **Components**, 1 bit; if set 16 components, each 1 bit when
   MonStats2's count for it (+0x15 + i) is under 3, else
   `bitlen(count − 1)` bits. (MonStats2 rows 0x134 bytes at data
   +0xa90; MonStats +0x18 names the row; MonStats rows 0x1a8 at +0xa78.)
3. **Boss**, 1 bit; if set: 5 flag bits (champion 0x4, unique 0x8,
   superunique 0x2, minion 0x10, ghostly 0x40 in the client's flags); if
   superunique 16 bits of SuperUniques row; mods 8 bits each until a 0
   (at most 9); 16 bits of name seed; 1 bit, then if set 32 bits of an
   owning player's id.
4. **Owner**, 1 bit; if set 31 bits of the owner's id (`+0x98 &
   0x8fffffff`, sent when flags2 bit 10 is set and the owner is a
   player; `FUN_00621cc0(unit, 0, id)`).
5. **Stats**, 1 bit; if set a stat list as in 0xa8, only stats whose
   ItemStatCost flags (+4) match `DAT_006ce268`.

The client makes the monster (`FUN_00466360`), then sets stat 7 = 0x8000
and stat 6 = life << 8 (not for its own merc). **The exact hit points
never cross**: every monster's life is a byte, 0..128 of its maximum.

**0x51 assign object** (14; `0x45cbd0` → `FUN_00466300`)

| Off | Size | Field |
|---|---|---|
| +1 | u8 | type (2) |
| +2 | u32 | unit id |
| +6 | u16 | Objects.txt row (class) |
| +8 | u16 | x |
| +0xa | u16 | y |
| +0xc | u8 | mode (a door's open / closed, a used chest or shrine) |
| +0xd | u8 | interact type |

Doors, chests, shrines, waypoints, the stash, town portals and the
Cairn Stones' portal all come this way.

**0x09 assign warp** (11; `0x45cb90` → `FUN_004661c0`, then
`FUN_00470b70`): +1 u8 type (5), +2 u32 id, +6 u8 warp class (the
LvlWarp row), +7 u16 x, +9 u16 y.

**0x0a remove unit** (6; `0x45cc10` → `FUN_00465ee0`): +1 u8 type, +2
u32 id. Ignored for the client's own player.

**0x0b** (6; `0x45cc50`): +1 type, +2 id: `FUN_00463d90` on the unit,
then `FUN_0046f360` (the handshake note covers its join-time use).

**0x15 reassign player** (11; `0x45d160`): +1 type, +2 u32 id, +6 u16
x, +8 u16 y, +0xa u8 (**(?)**, passed on). A hard teleport
(`FUN_004654c0`: act, room, COF, path); for the own player also
`FUN_0044db40(3000)`. Waypoints, portals and level warps arrive as this.

**0x07 / 0x08 map reveal / hide** (6; `0x45cab0` / `0x45cb20`): +1 u16
x, +3 u16 y, +5 u8 level (Levels.txt id). `FUN_0061a070` /
`FUN_0061a0c0`: rooms of the act's map come into / leave play around
(x, y); what the client builds and frees as the player moves.

**0x16** (variable; `0x45d2e0`): +1 u16 length, +3 u8 count (not 0),
then count × 9 bytes `{u8 type, u32 id, u16 x, u16 y}`, each put through
the position correction. A batch of unit positions.

### Units: movement and modes

The generic unit commands (players, and any unit type the table allows):
+1 u8 type, +2 u32 id, +6 u8 cmd (the table above).

| Id | Size | Handler | Rest | Command |
|---|---|---|---|---|
| 0x0c hit | 9 | `0x45cc70` | +7 u8 hit class (→ unit +0xb0); +8 u8 life / 128 (−1 when > 1; bit 7 set by `FUN_005a0180`) | cmd 0x13 always (builder `0x53b430`, from `FUN_00597cf0`); `{+7, +8}` |
| 0x0d stop | 13 | `0x45ccc0` | +7 u16 x, +9 u16 y, +0xb u8, +0xc u8 life / 128 | `{x, y, +0xb}`; a player's party life bar (`FUN_0047a690`) (builder `0x53b4b0`) |
| 0x0e object state | 12 | `0x45cd10` | +7 u8 **(?)**, +8 u32 new mode | object cmd 3 (mode) or 0x15 (operate effect) |
| 0x0f move to x, y | 16 | `0x45cd40` | +7 u16 target x, +9 u16 target y, +0xb u8, +0xc u16 x, +0xe u16 y (where it is) | `{tx, ty, +0xb}`, correction at (x, y) (builder `0x53b570`) |
| 0x10 move to unit | 16 | `0x45cd90` | +7 u8 target type, +8 u32 target id, +0xc u16 x, +0xe u16 y | `{type, id}`, correction (builder `0x53b520`) |

Monsters (type always 1): +1 u32 id, +5 u8 cmd.

| Id | Size | Handler | Fields |
|---|---|---|---|
| 0x67 move to x, y | 16 | `0x45cde0` | +6 u16 x, +8 u16 y, +0xa u8 (path +0x93, + 1), +0xb u8 0, +0xc u8 path type (5 / 6 → 1 with the path's target; 8 → 0xb), +0xd i16 velocity (stat 0x43, clamped), +0xf u8 (path +0x91). Builder `0x53b710` in `FUN_00597e20` |
| 0x68 move to unit | 21 | `0x45ce30` | +6 u16 x, +8 u16 y (where it is: correction), +0xa u8 target type, +0xb u32 target id, +0xf u8 **(?)**, +0x10 u8 **(?)**, +0x11 u8 path type (5 / 6 → 2; 8 → 0xb), +0x12 i16 velocity, +0x14 u8 (path +0x91). Builders `0x53b5f0` / `0x53b7f0` |
| 0x69 | 12 | `0x45cea0` | +6 u16 x, +8 u16 y, +0xa u8, +0xb u8 (a mode at x, y: attack, cast) |
| 0x6a | 12 | `0x45cef0` | +6 u8 target type, +7 u32 target id, +0xb u8 (the same on a unit) |
| 0x6b / 0x6c | 16 | `0x45cf40` / `0x45cfb0` | 0x69 / 0x6a plus +0xc u16 x, +0xe u16 y (correction) |
| 0x6d stop | 10 | `0x45d010` | +5 u16 x, +7 u16 y, +9 u8 life / 128; cmd 7, correction, stat 0x148 + 1 (builder `0x53bb70`) |
| 0x6e..0x72 | 1 | no-ops | |
| 0xab heal | 7 | `0x45f120` | +1 type, +2 u32 id, +6 u8 life / 128 (a monster: stat 6 = life << 8; a player: the party bar) |

The server picks 0x67 or 0x68 by the mode table `DAT_006e1d90` (0x18
bytes a mode: +0x10 the command for "at x, y", +0x14 "on a unit").
Which of the builders `0x53b9f0 / 0x53ba40 / 0x53baa0 / 0x53bb00` makes
which of 0x69..0x6c isn't settled **(?)**.

**The server sends one packet per decision, never per step.** A move
names the goal (and, in the correcting forms, where the unit is now);
the client paths and animates the unit there itself (below).

### Skills and missiles

**0x4c skill on a unit** (16; unit handler `0x45df00`, cmd 0x16)

| Off | Size | Field |
|---|---|---|
| +1 | u8 | caster type |
| +2 | u32 | caster id |
| +6 | u16 | skill (Skills.txt id) |
| +8 | u8 | skill level (`FUN_006442a0`) |
| +9 | u8 | target type |
| +0xa | u32 | target id |
| +0xe | u16 | 0 |

**0x4d skill at x, y** (17; `0x45df60`, cmd 0x15): +1 u8 caster type,
+2 u32 caster id, +6 u32 skill, +0xa u8 level, +0xb u16 x, +0xd u16 y,
+0xf u16 0. The server sends 0x4d instead of 0x4c when the recipient
can't see the target (the target's x, y).

Builders `0x53d530` / `0x53d4d0` (from `FUN_00548090` for players,
`FUN_00597d70` for monsters). With the builder's last argument set
(caller `0x571cd0`) the ids become 0x99 / 0x9a with the same layout,
handled by `FUN_004ca200` / `FUN_004ca230` **(?)** what they mean.

**0x73 missile** (32; `0x45e6d0` → `FUN_004cd540`): fills a missile
create struct (flags 0x40000001, or 0x40000021 when +0xf and +0x13 are
both non-zero): +5 u16 Missiles.txt row, +7 u32, +0xb u32, +0xf u32,
+0x13 u32 (**(?)**: source / target positions), +0x17 u16
(`FUN_0064a330`), +0x19 u8 owner type, +0x1a u32 owner id, +0x1e u8,
+0x1f u8 (stat 0x148).

Most missiles don't cross at all: the client plays a cast from 0x4c /
0x4d and makes the skill's missiles itself (Skills.txt's client
functions). Known D2 behaviour (D2MOO); in game.exe only the entry
point was read **(?)**.

### Life, mana, stamina and the own player's position

Bitstreams, unpacked by the dispatcher's pre-pass.

| Id | Size | Builder | Bits |
|---|---|---|---|
| 0x18 | 15 | `FUN_0053c230` | 8 id, 15 life, 15 mana, 15 stamina, 7 hpregen (stat 74), 7 manarecovery (stat 26), 16 x, 16 y, 8 dx, 8 dy |
| 0x95 | 13 | `FUN_0053c320` ✓ | 8 id, 15 life, 15 mana, 15 stamina, 16 x, 16 y, 8 dx, 8 dy |
| 0x96 walk verify | 9 | `FUN_0053c3f0` | 8 id, 15 stamina, 16 x, 16 y, 8 dx, 8 dy |

Life / mana / stamina go into stats 6 / 8 / 10 as value << 8. (x + dx,
y + dy) goes through the correction (dx, dy are signed, but only
converted when > 0x80, a quirk). Life ≠ 0 while the player's mode is
0x11 (dead) revives it (`FUN_00480e70`, mode 5). These are the only
way the own player's life, mana and stamina arrive.

### Stats, experience, level

**0x19..0x1f** (own player; handler `0x45d780` → `FUN_0045d4b0`)

| Id | Size | Layout | Effect | Builder |
|---|---|---|---|---|
| 0x19 | 2 | +1 u8 | gold (stat 14) += | `FUN_0053e9b0` |
| 0x1a | 2 | +1 u8 | experience (stat 13) += | `FUN_0053bdd0` |
| 0x1b | 3 | +1 u16 | experience += | `FUN_0053bdd0` |
| 0x1c | 5 | +1 u32 | experience = | `FUN_0053bdd0` |
| 0x1d | 3 | +1 u8 stat, +2 u8 | stat = | `FUN_0053be40` |
| 0x1e | 4 | +1 u8 stat, +2 u16 | stat = | `FUN_0053be40` |
| 0x1f | 6 | +1 u8 stat, +2 u32 | stat = | `FUN_0053be40` |

`FUN_0045d4b0`: stat 6 ≠ 0 while dead revives (as above); stat 12
(level) fires the level-up event (`FUN_0045d3b0` → `FUN_004c1bc0(0x47)`,
the own player also `FUN_0045d3e0`) and refreshes the panel
(`FUN_004c1c10`); stats 0, 2 refresh the panel. **There is no level-up
packet**: the level arrives as stat 12. New stat / skill points
presumably as stats 4 / 5 through 0x1d..0x1f **(?)**.

**0x20 stat of any unit** (10; `0x45d880`): +1 type, +2 u32 id, +5 u8
stat, +6 u32 value (set).

**0x9e..0xa2 stat of another unit** (handler `0x45d540`; builders
`FUN_0053bee0` set, `FUN_0053bfd0` delta): +1 u8 stat, +2 u32 id, +6
value: 0x9e u8 set (7), 0x9f u16 set (8), 0xa0 u32 set (10), 0xa1 u8
delta (7), 0xa2 u16 delta (8). Stat 12 fires the level-up event too.

**Skills**

| Id | Size | Handler | Layout |
|---|---|---|---|
| 0x21 skill level | 12 | `0x45dcd0` (builder `FUN_0053c4a0`) | +1 u8 type, +2 u8 **(?)**, +3 u32 id, +7 u16 skill, +9 u8 base level, +10 u8 bonus level, +11 u8 **(?)** |
| 0x22 item skill | 12 | `0x45ddb0` (`FUN_0053c520`) | +1 type, +3 u32 id, +7 u16 skill, +9 u16 quantity / level **(?)**, +11 u8 flag (≠ 0: ignored) |
| 0x23 set skill | 13 | `0x45de10` (`FUN_0053c590`) | +1 type, +2 u32 id, +6 u8 hand (0 right, 1 left), +7 u16 skill, +9 u32 item id (−1 none: a scroll / book's skill otherwise) |
| 0x94 skill list | var | `0x45dd60` (`FUN_0053c5d0`) | +1 u8 count, +2 u32 id, then count × `{u16 skill, u8 level}` |
| 0x7b hotkey | 8 | `0x45e8d0` | +1 u8 slot, +2 u16 skill (\| 0x8000 left), +4 u32 item id |

### States (auras, curses, shrines, buffs)

- **0xa7 delayed state** (7): +1 type, +2 u32 id, +6 u8 state
  (`FUN_004d9b20` / `FUN_004d9e60`).
- **0xa8 set state** (var; `0x45ee20`): +1 type, +2 u32 id, +6 u8
  length, +7 u8 state (States.txt), from +8 a stat list: 9 bits stat id
  until 0x1ff; if ItemStatCost +9 (param bits) ≠ 0 that many param bits;
  the value is ItemStatCost +8 bits (signed when under 32 bits and +4 &
  `DAT_006ce26c`). ItemStatCost rows 0x144 bytes at data +0xbcc. Applied
  by `FUN_004d9d70(state, stat, value, param)`. These are **send bits**
  (+8 / +9), not the save bits items use.
- **0xa9 end state** (7): +1 type, +2 u32 id, +6 u8 state
  (`FUN_004d9f40` / `FUN_004d9c30`).
- **0xaa add unit's states** (var; `0x45efa0`): +1 type, +2 u32 id, +6
  u8 length; bits from +7: 8 bits state (0xff ends), 1 bit has stats,
  then a stat list as in 0xa8. Sent with a unit as it comes into view.
- **0x57 monster enchants** (14; `0x45e400`, type 1 only): +1 u32 id, +5
  u8 type, +6 u16, +8 u16, +0xa u8, +0xc u16 **(?)** field names.

### Items

Both item packets carry the item as a bitstream, parsed by the same
function that reads saves (`FUN_0062e430(unit, bits, len, is_save = 0,
sockets = 0, version 0x60, &err)`; full items `FUN_0062cbe0`, compact
`FUN_0062a970`). The server encodes with `FUN_006313e0` →
`FUN_006312b0(item, buf, is_save, with_sockets, header_only)` (full
`FUN_0062fff0`, compact `FUN_0062af80`).

**0x9c item in the world** (var; `0x45eb10`, builder `FUN_0053eae0`)

| Off | Size | Field |
|---|---|---|
| +1 | u8 | action (0..4, 0xa..0x10, 0x12; else error 0xf6d) |
| +2 | u8 | total length (< 0xfd) |
| +3 | u8 | category (`FUN_00623d60`: ItemsTxt +0x115, or 6 for the two-hand / switch case) |
| +4 | u32 | item unit id |
| +8 | bits | the item |

**0x9d item owned by a unit** (var; `0x45ec70`, builder `FUN_0053cef0`):
as 0x9c, then +8 u8 owner type, +9 u32 owner id (type 6 / −1: none),
the item from +13. Actions 5..9, 0x11, 0x13..0x17 (else error 0xfb7).

| 0x9c action | Handler | What |
|---|---|---|
| 0x00 | `4c25b0` | new on the ground at x, y (header mode 3; mode 5 plays the drop) |
| 0x01 | `4c2650` | ground → cursor (pick up) |
| 0x02 | `4c26f0` | dropped to the ground (clears the cursor) |
| 0x03 | `4c2810` | a ground item comes into view |
| 0x04 | `4c2ad0` | into a grid at x, y, page (`FUN_0063bcf0`) |
| 0x0a | `4c3b30` | re-parsed into the existing item (an update) |
| 0x0b / 0x0c | `4c3c00` | into / out of a store's stock (the store must be open) |
| 0x0d | `4c40d0` | swapped inside a grid (with the cursor's) |
| 0x0e / 0x0f / 0x10 | `4c4130` / `4c42a0` / `4c45c0` | into / out of / swapped in the belt |
| 0x12 | `4c20b0` | onto the cursor |

| 0x9d action | Handler | What |
|---|---|---|
| 0x05 | `4c2c80` | out of a grid |
| 0x06 | `4c2e90` | equipped in the slot of header +0x11 (slots 0xb / 0xc: the switch) |
| 0x07 | `4c3070` | swapped hands (4 / 0xb ↔ 5 / 0xc) |
| 0x08 | `4c3380` | unequipped |
| 0x09 | `4c3920` | swapped on the body |
| 0x11 | `4c4740` | unequipped into the grid at x, y |
| 0x13 | `4c4990` | into the owner's grid; owner an item: socketed **(?)** |
| 0x14 / 0x15 / 0x16 | `4c4aa0` / `4c4c70` / `4c2340` | placed by the header's mode (grid, body, belt, cursor); 0x16 recreates it with quantity 1 on the cursor **(?)** names |
| 0x17 | `4c3b00` | weapon switch |

Where things are: ground 0x9c / 0, 2, 3; inventory, stash, cube 0x9c / 4
(page in the header); belt 0x9c / 0xe..0x10; body 0x9d / 6..9, 0x11;
cursor 0x9c / 1, 0x12, 0x9d / 0x16; store stock 0x9c / 0xb, 0xc; gold on
the ground an item of type 4 (compact) with the amount in stat 14;
socketed items 0x9d / 0x13 with the parent item as owner. Header modes:
0 stored, 1 equipped, 2 belt, 3 ground, 4 cursor, 5 dropping.

**The item bitstream on the wire**, next to the .d2s one (d2s_items.hpp):

| | .d2s | Network |
|---|---|---|
| JM marker | 16 bits | none |
| Flags | 32 bits | 32 bits: 0x80000 cleared, 0x800000 set; compact 0x200000 when ItemsTxt +0x143; unidentified (0x10 clear): socketed 0x800 cleared |
| Version, mode | 10, 3 bits | the same (version 0x60) |
| Position | slot 4, x 4, y 4, page 3 | the same, except modes 3 / 5 (ground): x 16, y 16 |
| Code | 32 (compact: 32; ear: class 3, level 7, 7-bit name) | the same |
| Uid / seed | 32 | none |
| Quality fields | all | rare / crafted names only when identified; charm / normal fields gated likewise |
| bit_after, realm data | present | none |
| Stat lists | always | **none when unidentified**; else base, set lists (5-bit mask), runeword |
| Stat widths | ItemStatCost save bits (+0x19), save add (+0x1c), save param (+0x24) | the same: **save bits, not send bits** |
| Sockets' items | inline, byte-aligned | separate 0x9d / 0x13 packets |
| Header-only (flag 0x2000000) | — | stops after the code |

Full item order after the code: 3 filled sockets, 7 ilvl, 4 quality, 1
(+3) gfx, 1 (+11) class affix, the quality fields (low / superior 3;
magic 11 + 11; set 12; unique 12; rare / crafted 8 + 8 and 6 × (1 + 11)),
16 runeword (flag 0x4000000), personalized 7-bit name (0x1000000),
defense (armour, stat 31), max / current durability (stats 73 / 72),
gold amount (1, then 12 or 32), quantity 9 (stackable), sockets (stat
194, flag 0x800), then the stat lists (9-bit id … 0x1ff; stats 17/18,
48/49, 50/51, 52/53, 54/55/56, 57/58/59 carry their partners without
ids).

**Other item packets**

- **0x3e item stat** (var; `0x45e130` → `4c1f30`): +1 u8 length, bits
  from +2: item id (1 bit, then 1 bit ? 32 : 16, else 8), 1 bit set, 9
  bits stat, value (the same width rule), param (1 bit ? 16 : 8). Stat
  0xcc (charged skill) → `FUN_004c1e40`; quantity (0x46) > 0 clears
  item flags 4, 0x4000.
- **0x3f stackable used** (8; `4c4620`): +1 u8 code (list `DAT_00727a40`
  **(?)**; 0xff clears flag 4), +2 u32 item id, +6 u16 (0xffff special).
- **0x40 item flags** (13; `4c2020`): +1 u32 item id, +5 u32 mask, +9
  u32 on / off (`FUN_006280d0`).
- **0x42 clear cursor** (6; `4c2050`): +1 type, +2 u32 id; the own
  player's cursor item goes.
- **0x2a NPC transaction** (15; `4b6390`, copied to `0x7c0d87`): +2 u8
  result (0 bought, 1 sold (refreshes the store, `FUN_004b9a00`), 5
  `FUN_004939b0`, 7..0x10 errors), +7 u32 item id; +1, +3, +11 not read
  by the client (community: type, gold **(?)**).

### Quests

- **0x28 quest info** (103; `0x45d370` → `FUN_004b6dd0`; builder
  `FUN_0053d670`): +1 u8 unit type, +2 u32 unit id, +6 u8, +7 96 bytes.
  Type 6: the 96 bytes become the player's quest state (`DAT_007c0d43`,
  `FUN_0065c4d0(…, 0x60, 0)`); otherwise the NPC is looked up and the
  quest log opens.
- **0x29 game quest info** (97): 96 bytes at +1 → `DAT_007c0d47` (the
  game's quest words).
- **0x52 player quest info** (42): the whole packet, id included, →
  `DAT_007bf355` (a jump to `FUN_004a40d0`), so 41 bytes of data
  (`FUN_00483350`, then the log refresh `FUN_004a3220`); what each byte
  means **(?)** (quests.md's log states, one a quest, by the size).

### NPCs, UI, waypoints, portals

- **0x27 NPC info** (40; `0x45e0a0` → `FUN_004a1600`): +1 u8 mode (1 /
  2), +2 u32 unit id, +6 u16 **(?)**, +8 u16 (checked against 3), +10
  u16 **(?)**; the rest **(?)**.
- **0x8a NPC interaction** (6; `FUN_004b3380`): +1 type, +2 u32 id.
  Class 0x216 closes the UI (`FUN_004b9a00`); if it isn't the unit being
  talked to, the player resets (`FUN_00470390(3)`); class 0x14b runs
  quest checks.
- **0x58 open UI** (7; `0x45e490`): +1 u32 unit id, +5 u8 UI (0 open;
  1 / 4 / 5 / 6 / 7 close variants) **(?)** which is which.
- **0x77 button action** (2; `FUN_004b8cf0`): +1 u8 action (0 open, 1
  request, 2 close, 5 state 3 → 5 / 4 → 6; 0..0x15 in all) **(?)** the
  rest. Trade and stash windows.
- **0x62** (7; `FUN_004b5320`): +1 u8, +2 u32 (UI / NPC state, types 1,
  2, 4, 6) **(?)**.
- **0x63 waypoints** (21; `0x45e670`): +1 u32 waypoint object id, +5 the
  known waypoints' bits (`FUN_00661030`).
- **0x4e / 0x4f / 0x50 hirelings**: 0x4e (7) +1 u16 name id, +3 u32
  seed (one offer); 0x4f (1) clears the list; 0x50 (15) switch on the
  u16 at +1 **(?)**.
- **0x81 assign merc** (20; `FUN_00478bb0`): +1 u8, +2 u16, +0xc u32,
  +0x10 u32 (community: owner and merc ids **(?)**).
- **0x60 town portal state** (7; `FUN_004bdf30`): +1 u8 state, +2 u8
  area, +3 u32 unit id.
- **0x82 portal owner** (29): +1 u32 owner id, +5 char[16] name, +0x15
  u32 local portal id, +0x19 u32 remote portal id.

### Sounds, text

- **0x2c unit sound** (8; `0x45e110` → `FUN_004cbde0`): +1 type, +2 u32
  id, +6 u16 sound (ids 10..0x12, 0x54..0x5d special-cased). Only the
  sounds the server decides (quest speech, NPC greetings **(?)**); a
  unit's mode sounds are the client's own.
- **0x26 chat / overhead** (var; → `FUN_0049f490`; builder
  `FUN_0053c750`): +1 u8 type, +2 u8 language, +3 u8 unit type, +4 u32
  unit id, +8 u8 colour **(?)**, +9 u8 **(?)**, +10 name (≤ 16, NUL),
  then the message (NUL).
- **0x5a event message** (40; `FUN_0049eb10`; builder `FUN_0053c850`):
  +1 u8 event (0..0x12: joined, left, slain ...), +2 u8 colour **(?)**,
  +3 u32 argument, +7 u8, +8 char[16] name, +0x18 char[16] second name.

### Players and party

- **0x5b player joins** (var; `0x45e4e0`, builder `FUN_0053c940`): +1
  u16 length, +3 u32 id, +7 u8 class, +8 char[16] name, +0x18 u16 level,
  +0x1a u16 party (0xffff none), +0x1c, +0x1e u16 0, +0x20 u16 **(?)**,
  +0x22 two NUL strings (account / realm **(?)**).
- **0x5c player leaves** (5): +1 u32 id.
- **0x65** (7): +1 u32 id, +5 i16 (kills, roster +0x18).
- **0x75 party roster** (13; `FUN_0047a850`): +1 u32 id, +5 u16 party,
  +7 u16 level, +9, +11 u16 **(?)**.
- **0x7f party member** (10): +1 u8 is player, +2 u16 life %, +4 u32 id,
  +8 u16 area.
- **0x8b relationship** (6): +1 u32 id, +5 u8 (roster +0x30).
- **0x8c player relation** (11): +1 u32 id, +5 u32 id, +9 u16 flags.
- **0x8d assign party** (7): +1 u32 id, +5 u16 party (roster +0x22).
- **0x8e corpse** (10): +1 u8 add / remove, +2 u32 player id, +6 u32
  corpse id (list at roster +0x38).
- **0x76 player in proximity** (6; `FUN_0049f8c0`).
- **0x89 unique event** (2): +1 u8 (< 0x14) → a bit of `DAT_007a7458`.
- **0x8f pong** (33): zeros; the latency the correction uses.
- **0x97 weapon switch** (1): toggles `DAT_007bcc4c`.

### Not traced

0x11 (report kill: +1 type, +2 id, +6 u16 → `FUN_00464e50`), 0x12..0x14
and 0x66 (no-op handlers), 0x45 and 0x54 (no-ops), 0x47 / 0x48
relators, 0x53 darkness, 0x5d / 0x5e / 0x5f, 0x61, 0x74, 0x78, 0x79,
0x7a, 0x7c / 0x7d, 0x7e (loads `cmncof_aN.d2`), 0x90..0x93, 0x98, 0x9b,
0xa3..0xa6: handlers are known (the table), fields not.

## Client → server: layouts and d2d's commands

The client builds a packet and sends it through `FUN_00478350`, which
**drops a packet identical to the last one** sent within 200 ms (50 ms
for the skill ids 05..0a, 0c..11; 0x3a never) — a held button doesn't
flood. Raw send `FUN_0052ae50`; the server's dispatcher `FUN_0054d750`
(network.md), handlers return 0 ok, 2 bad value, 3 bad length.

### Movement and skills

| Id | Size | Layout | Server | d2d |
|---|---|---|---|---|
| 01 / 03 walk / run to x, y | 5 | u16 x, u16 y | `5497e0` / `5498d0` → `FUN_005496f0` (bounds `FUN_00548ef0`, flood 0x19 frames), `FUN_005809d0` mode 2 / 3 | `cmd::Move` (+ `cmd::Run`) |
| 02 / 04 walk / run to a unit | 9 | u32 type, u32 id | `549890` / `549920` → `FUN_00549830`, `FUN_00580a70` | **none** |
| 05 / 0c left / right skill at x, y | 5 | u16 x, u16 y | `549d00` / `549fc0` → `FUN_00549ad0`; the skill is the selected one | `cmd::UseSkill` (no unit) |
| 08 / 0f | 5 | the same, held | the same | the same |
| 06 / 0d left / right on a unit | 9 | u32 type, u32 id | `549d80` / `54a040` → `FUN_00549ba0(type, id, approach 1)` | `cmd::UseSkill{unit}` |
| 07 / 0e | 9 | the same, no approach (shift **(?)**) | approach 0 | the same |
| 09, 0a / 10, 11 | 9 | the same, held (0a / 11 held + no approach) | as 06, 07 / 0d, 0e | the same |
| 53 / 54 run / walk | 1 | — | `54c940` / `54c990` | `cmd::Run` |
| 5f where I am | 5 | u16 x, u16 y | `54cd50`: within 4 walks there; 0xf..0x2e resyncs; farther reassigns | **none** |

The skill packets carry **no skill id**: the server uses the skill last
put on that button with 0x3c. The client picks the id in `FUN_00461700`
from a flag word (bit 0 left, bit 1 right, 0x8 held, 0x20 no approach):
on a unit left 06 / 07 (0x20) / 09 (0x8) / 0a (both), right 0d / 0e /
10 / 11; at x, y left 05 / 08 (0x8), right 0c / 0f. A unit target must
be type 0, 1 or 3 unless the skill allows others; with approach a
melee-range skill runs up first (`FUN_00548a50`).

### Interaction, NPCs, quests

| Id | Size | Layout | Server | d2d |
|---|---|---|---|---|
| 13 interact | 9 | u32 type, u32 id | `54aa90` → `FUN_00548b00`: player (corpse / trade), NPC (walk up; ≤ 6 talk, `FUN_00573020`), object (range < 0x33, `FUN_00584540`) | `cmd::Interact` (by `Level::npcs` index) |
| 2f start NPC chat | 9 | u32 type, u32 NPC id | `54b930` → `FUN_00572e60` | `cmd::Chat{npc}` |
| 30 end NPC chat | 9 | the same | `54b9f0` → `FUN_00572f20` | `cmd::Chat{-1}` / `CloseTrade` |
| 31 quest message heard | 9 | u32 NPC id, u16 message (+5) | `54ba90` → `FUN_005443b0` | `cmd::QuestMessage` |
| 3d operate object | 5 | u32 object id | `54bf10` → `FUN_005845d0` | in `Interact` |
| 3f phrase | 3 | u16 phrase (0x19..0x20) | `54c070` | none |
| 40 update quests | 1 | — | `54c0c0` → `FUN_00546040` | none |
| 41 resurrect | 1 | — | `54c0e0` (dead only) | `cmd::Resurrect` |
| 49 waypoint | 9 | u32 waypoint object id, u16 level (+5) | `54c5d0` → `FUN_00584f60` | `cmd::Waypoint` |
| 58 quest state | 3 | u16 quest (< 0x2a) | `54c9c0` | none |
| 4b unit update | 9 | u32 type, u32 id | `54c6d0` | none |
| 59 unit position | 17 | u32 type, u32 id, u32 x, u32 y | `54ca10` | none |

### NPC trade

| Id | Size | Layout | Server | d2d |
|---|---|---|---|---|
| 38 NPC action | 13 | u32 action (0 trade / Go East / imbue, 1 gamble; 2, 3 **(?)**), u32 NPC id, u32 extra | `54bca0` → `FUN_00579d60` | `OpenTrade`, `OpenHire`, `Respec`, `GoEast`, `Imbue` (a kind byte of d2d's own) |
| 32 buy | 17 | u32 NPC id, u32 item id, u32 flags (bits 16..30 tab, 0x80000000 fill), u32 cost | `54bac0` → `FUN_00577f30` | `cmd::Buy{stock}`: **stock index, not item id** |
| 33 sell | 17 | u32 NPC id, u32 item id, u16 mode (+9), u32 cost (+0xd) | `54bb20` → `FUN_00579510` | `cmd::Sell` |
| 35 repair | 17 | the same shape | `54bb60` → `FUN_00578050` | `cmd::Repair` (−1 = all **(?)**) |
| 34 Cain identifies | 5 | u32 NPC id | `54bba0` | `cmd::Identify` |
| 36 hire | 9 | u32 NPC id, u16 merc id (+5) | `54bbd0` | `cmd::Hire{offer}`: **index, not merc id** |
| 37 gamble confirm **(?)** | 5 | u32 item id | `54bc30` | none |

### Items

| Id | Size | Layout | Server | d2d |
|---|---|---|---|---|
| 16 pick up | 13 | u32 type (4), u32 id, u32 action **(?)** | `54aad0` → `FUN_00548b00` | `cmd::Pickup` (no action) |
| 17 drop the cursor's | 5 | u32 id | `54ab40` → `FUN_00563c00` | `cmd::Drop` |
| 18 cursor → grid | 17 | u32 id, u32 x, u32 y, u32 buffer (0 inventory, 2 trade, 3 cube, 4 stash; only 4 proven) | `54abb0` → `FUN_00560200` | `cmd::ToGrid`: d2d's panel 1 / 4 / 5 → 0 / 3 / 4 |
| 19 grid → cursor | 5 | u32 id | `54acd0` | `cmd::ToCursor` |
| 1a equip | 9 | u32 id, u8 body slot 1..10 | `54ad90` | `cmd::ToBody` |
| 1c body → cursor | 3 | u16 body slot | `54aec0` | `cmd::ToCursor`: **slot, not id** |
| 1d swap cursor ↔ body | 9 | u32 id, u8 slot | `54af50` | `cmd::ToBody` |
| 1f swap cursor ↔ grid item | 17 | u32 cursor id, u32 target id, u32 x, u32 y | `54b0f0` | `cmd::ToGrid` (swap) |
| 20 use item | 13 | u32 id, u32 x, u32 y (the player's position) | `54b1e0` → `FUN_0055e170` | `cmd::UseItem` |
| 23 cursor → belt | 9 | u32 id, u32 belt slot | `54b3e0` | `cmd::ToBelt` |
| 24 belt → cursor | 5 | u32 id | `54b450` | `cmd::ToCursor` |
| 26 use belt | 13 | u32 item id, u32 flag, u32 (unused) | `54b560` → `FUN_00562390` | `cmd::UseBelt{slot}`: **column, not id** |
| 27 identify with a scroll | 9 | u32 scroll id, u32 item id | `54b280` | none |
| 21 / 25 / 28 / 29 / 2a / 4c / 63 | | stack, belt swap, socket, scroll to book, to cube, transmute, auto-belt | | none |
| 50 drop gold | 9 | u32 player id, u32 amount | `54c800` | none |

### Stats, skills, system

| Id | Size | Layout | Server | d2d |
|---|---|---|---|---|
| 3a stat point | 3 | u8 stat (< 16), u8 count − 1 (< 100) | `54bd10` | `cmd::StatPoint` (order matches) |
| 3b skill point | 3 | u16 Skills.txt id | `54bd90` | `cmd::SkillPoint`: **class index 0..29** |
| 3c select skill | 9 | u32 skill (bit 31: the hand, set by `FUN_004a9bd0` when `DAT_007c07fc`; left **(?)**), u32 item id (−1) | `54be70` | `cmd::SelectSkill` |
| 51 hotkey | 9 | u32 (hotkey << 16, bit 15 left, skill), u32 item id | `54c870` | none |
| 4f click button | 7 | u16 button, u16, u16 | `54c7c0` | none |
| 5d / 5e party | 7 / 6 | | `FUN_005a6000` | none |
| 14 / 15 chat / overhead | var | u8, u8 type, message\0, name\0 | `54a290` / `54a5d0` | none |
| 6d ping | 13 | u32 tick, u32 (`FUN_0044ce70` >> 1), u32 checksum | system | **none** (the host expects it, at most every 5 s) |

## What the client does itself

The host sends decisions, not frames. game.exe's client fills in the
rest, and a d2d client talking to it must too.

- **Its own player moves at once.** Every move or skill click goes
  through `FUN_00481030`: the unit command is started on the local unit
  first (`FUN_00480c10(…, 0)` → `FUN_00461250`: the path for a walk /
  run, `FUN_00480780` / `FUN_004804a0`; the swing for a skill), then
  sent (`FUN_00480b40`). The server never acknowledges a move; it only
  corrects: 0x96 / 0x95 / 0x18 carry its idea of the position, the
  client sends 0x5f when they differ past the tolerance, and 0x15 snaps
  it if the gap is big.
- **Other units are walked by the client.** A 0x0f / 0x10 / 0x67 / 0x68
  gives a goal (x, y or a unit), a path type and a velocity; the client
  sets the mode (`DAT_006da4d8`), the path (`FUN_00648cf0`) and walks it
  with its own pathing, frame by frame. There is no interpolation
  buffer: a correcting form re-paths from where the unit is.
- **Animation is the client's.** Modes come as commands; the frame
  pacing, the COF / AnimData timing, where an attack's frame falls are
  local (d2d already does this from its tables). The host's hits come as
  0x0c (get-hit with life), deaths as mode commands.
- **Missiles of a cast** are made on the client from 0x4c / 0x4d (the
  skill's client functions); 0x73 is the exception **(?)**.
- **Mode sounds, overlays, light** are the client's; the server sends
  only 0x2c sounds.
- **Rooms come and go** by 0x07 / 0x08: the client builds the act's map
  from the seed (0x03, the handshake note) and adds rooms as they come
  into play; units are only sent for rooms in play.
- **Monster life** is a byte (life / 128) and the client shows only
  that; exact numbers stay on the server.
- **A held button** repeats the same packet; the send throttle drops
  repeats inside 200 ms (50 for skills).
- **Only 0x6d is periodic** (at most every 5 s); 0x5f goes out only on
  drift.

Not traced **(?)**: whether the client's pathing (`FUN_00480780`,
`FUN_004804a0`) is the server's path code; the per-frame stepping of a
walking monster; how the own walk is paced against the server's frames.

## Mapping onto d2d's View

What game.exe's host sends for each part of d2d's `View` (world.hpp)
and `Event`s, and where the View can't hold it. "Client" means game.exe
works it out locally; d2d's View gets it from its own World today.

| View field / Event | From (S → C) | Gap |
|---|---|---|
| `level` | 0x03 (act, seed), 0x07 room level, 0x15 | d2d names the level; game.exe has the client find it from its rooms. The client must work out its level from its position |
| `player` (UnitState) | 0x59, 0x15, 0x18 / 0x95 / 0x96, own prediction | positions u16 subtiles, not float cells; d2d's client has no prediction |
| `running`, `dead` | own (0x53 / 0x54 state), mode commands; 0x18 revive | player death mode packet not pinned down **(?)** |
| `pmode`, `prate`, `seq*` | client, from the unit command + skill | d2d computes them on the server; a client must compute them |
| `gfx` | the items worn (0x9d / 6..9, 0x17) | d2d sends the look; game.exe sends items and the client composes |
| `merc` | 0x81, 0xac (owner bit), then monster moves | |
| `pets` | 0xac with owner, monster moves | |
| `monsters` (`Monster`) | 0xac, 0x67..0x6d, 0x0c, 0xab, 0x57, 0xa7..0xaa | **exact hit points, stats, level, curse / cry / poison timers aren't sent**: life / 128 and states only. `Monster` needs a client form (class, life byte, mods, states, command) |
| `missiles` | client (from 0x4c / 0x4d), 0x73 | the View carries missiles from the server; with game.exe's host the client must make them |
| `attack`, `attack_skill` | own | |
| `ground` | 0x9c / 0, 2, 3 (+ 0x0a remove) | the net item codec; labels and colours are the client's already |
| `fires` | objects / missiles **(?)** | not traced |
| `portals` | 0x51 (TownPortal objects), 0x60, 0x82 | d2d's View has the player's only; objects have unit ids there |
| `corpses` | 0x8e, a player unit in a dead mode | |
| `npc_states` | 0xac (NPCs are monsters) and their moves; objects 0x51 / 0x0e | **by `Level::npcs` index in d2d, by unit id in game.exe**; a host's NPCs have ids d2d must map to (class, position) |
| `boost`, `aura`, `buffs` | 0xa8 / 0xaa / 0xa9 (states with stat lists) | the View lists skills, game.exe states: map States.txt ↔ skills |
| `gold_lost` | stat 175 (0x1d..0x1f) **(?)** | |
| `day` | not sent **(?)** | game time on the client |
| `den_cleared`, `quest_log`, `game_quests`, `den_left` | 0x28 / 0x29 / 0x52 | d2d keeps Act 1's 7; game.exe sends all 41 quests' 16-bit words (96 bytes) |
| `light_bonus` | client (item stats) | |
| `header` | 0x59 (name, class), stat 12, 0x5b | no single packet; build it |
| `stats` | 0x1a..0x1f, 0x18 / 0x95 / 0x96, 0x94, 0x21..0x23 | fine grained; d2d sends the whole section |
| `items` | 0x9c / 4, 0xe..0x10; 0x9d / 5..9, 0x11, 0x13..0x17; 0x3e, 0x40 | **item ids**: game.exe's, not d2d's runtime ids; the codec |
| `held` | 0x9c / 1, 0x12; 0x9d / 0x16; 0x42 | |
| `store` | 0x9c / 0xb, 0xc (stock as items with ids), 0x2a | d2d's stock is by index |
| `hire_offers` | 0x4e / 0x4f | d2d's offers are by index |
| `sounds` | 0x2c; the rest client | d2d cues world sounds on the server |
| `ev::LevelChanged` | 0x15, room changes | derived on the client |
| `ev::OpenUI` | 0x58, 0x27, 0x63, 0x77, 0x8a | |
| — | 0x59 / 0x5b / 0x5c / 0x75 / 0x7f / 0x8b..0x8d (other players, party) | **no field**: the View has one player |
| — | 0x26 / 0x5a (chat, events) | **no field** |
| — | 0x07 / 0x08 (rooms in play) | d2d reveals client-side; fine for drawing, but units only exist in rooms in play |
| — | 0x20, 0x9e..0xa2 (other units' stats) | **no field** |
| — | 0x63 known waypoints | d2d has them in the character; the packet overrides |

## What d2d needs

For "Bret joins his daughter's game.exe host with d2d", in order. Sizes
are rough.

1. **A D2GS codec** beside protocol.hpp / replication.hpp: packet
   framing by the two size tables (including the variable rules), the
   bitstream reader / writer (d2s's `Bits` reused). Plus the join and
   compression from the other note. ~400 lines.
2. **The net item codec**: `d2s_items.hpp` / `d2s_write.hpp` with a
   `net` switch (the table above: no JM, ground x / y 16 bits, no uid /
   realm, unidentified items without stat lists, sockets separate).
   Round-trip tests (identified, unidentified, ground gold). ~250 lines.
3. **A client world**: units by (type, id) built from 0x59 / 0xac /
   0x51 / 0x09 / 0x9c and freed on 0x0a; the unit command
   (`FUN_00480c10`'s table) setting mode and path; the client walking
   units with d2d's own pathing; the position correction with game.exe's
   tolerances. This is the big one: d2d's client today draws the
   server's positions every tick and walks nothing. ~800–1200 lines.
4. **Own-player prediction**: start walks and swings locally, send
   01 / 03 / 05.. with u16 subtiles, send 0x5f on drift, take 0x15 /
   0x95 / 0x96 corrections. ~200 lines.
5. **Client-made casts**: the skill's missiles and effects from 0x4c /
   0x4d (d2d's Fight code has the missile rules; they'd run for display
   only on the client). ~300 lines, after tracing the client skill
   functions.
6. **Commands in game.exe's shapes**: 0x3c before a skill with a
   different skill; skill ids from hand, target, held and shift; units
   as (type, id); item ids and body slots; Skills.txt ids for 0x3b;
   merc ids for 0x36; item id + cost for 0x32; 0x02 / 0x04, 0x5f, 0x6d.
   Mostly the codec; `Interact` and `UseSkill.unit` need a unit type.
   ~200 lines.
7. **View additions**: other players and party, chat / event text,
   per-unit states, monster life as a fraction, quest words for all
   acts, store stock and hire offers by unit id. ~300 lines plus the
   client UI for chat and the party.
8. **Id mapping for NPCs and objects**: the host's NPCs / objects come
   with unit ids; d2d's client code indexes `Level::npcs`. Either match
   (class, position) to the index or move the client to unit ids
   throughout (better, and needed for hosting). ~150 lines.

Later, for d2d hosting game.exe clients: the same codec in the other
direction, plus the server sending decisions rather than Views (one
packet per move / cast, not positions per tick), assign packets for
units entering rooms in play, 0xac's component and boss tail, and the
host-side checks game.exe's server makes (flood guards, ranges).

## Open questions

- Player death: which packet puts another player (and the own one, when
  the server kills it) into DT / DD. Not 0x0c (always cmd 0x13).
- Which builders make 0x69..0x6c; what 0x68's +0xf / +0x10 are.
- 0x99 / 0x9a (skill packets with the builder's flag set) and 0x73's
  fields.
- Whether the client's pathing is the server's; how a walking monster
  is stepped per frame.
- 0x0e's +7 byte; 0x57's fields; 0x27 past +10; 0x58's UI values.
- 0x52's 41 bytes against quests.md's log states.
- New stat / skill points after a level-up: stats 4 / 5 through
  0x1d..0x1f?
- The 0x16 pick-up action value (the client looks it up in
  `DAT_007a27c0`); 0x18's buffer numbers other than 4 (stash).
- Time of day: sent, or the client's own clock?
- Which host packets a joining client gets for the units already in its
  rooms (0xaa with 0xac, item 0x9c / 3), and in what order; best settled
  by capturing a real game.exe host's traffic.
