# The skill function tables: untouched entries

The gap map (`python3 tools/gaps.py`, run 2026-11-26 on `3216cddf`) listed
these functions as untouched: its `SKILLS/?` rows, the rest of
`0x4f0830`..`0x4f4ec0`, and the callees and table entries they lead to. Nearly all are entries in
Skills.txt's function tables. `SKILLS/?` is the map's range attribution,
not a source file name. Each entry below was
read from the decompile, with the disassembly for register arguments,
`RET` sizes and seed ranges. "No caller" and every table slot come from a
byte scan of game.exe for rel32 calls and jumps and for raw pointers. No
oracle was run. The d2d column is as in [game-frontier.md](game-frontier.md):
d2d draws its own client effects, so the client rows are "not compared"
unless d2d has code for that skill.

## The tables

| Table | Count | Indexed by | Dispatcher |
|---|---|---|---|
| cltstfunc `0x727a90` | 70 (`0x727a8c`) | Skills `+0xf2` cltstfunc | `FUN_004c6140` at `0x4c643a` |
| cltdofunc `0x727ba8` | 130 (`0x727db0`) | `+0xf4` cltdofunc; `+0x16c` ItemCltEffect in its place when the skill is cast from an item and the value is over 1 | `FUN_004c6680` at `0x4c67a6` |
| the same `0x727ba8` | 130 | `+0xf6..` cltprgfunc1..3, the charge-ups | `FUN_004c6930` at `0x4c69d8`; `FUN_004c6ac0` at `0x4c6c18`, `0x4c6cfe` |
| the same `0x727ba8` | 130 | States.txt clteventfunc (`+0x34` of a 0x3c-byte States record, tables `+0xbc`) | `FUN_004c72f0` at `0x4c7383` |
| srvstfunc `0x732140` | not read | srvstfunc | `FUN_0056f640` (skills.md) |
| srvdofunc `0x7322b0` | not read | `+0x2e` srvdofunc; `+0x16a` ItemEffect in its place when cast from an item and over 1 | `FUN_0056f7f0` at `0x56f943` |

An index at or past the count runs nothing. Skills.txt's only ItemEffect
values above 1 are 36 (Frost Nova, Nova) and 151 (Chain Lightning). Its only
ItemCltEffect values are 10 and 96, and the only clteventfunc in States.txt is chillingarmor's 1.

## Shared pieces

- **The arguments.** A client function gets ECX the unit, EDX the skill id
  and the level on the stack (`RET 4`), and returns 1 or 0. A server function
  gets ECX the game, EDX the unit, then the skill and level on the stack.
- **The client missile request** is 0x5c bytes, handed to `FUN_004cd540`
  (ECX the request). These are the fields this batch writes, as
  `FUN_004cd540` reads them:

  | Offset | Field |
  |---|---|
  | `+0` | flags: 1 the position is at `+0x14`; 2 `+0x1c` is an offset from the position; 4 the velocity is at `+0x28`; `0x20` the target point is at `+0x1c`; `0x200` the frames at `+0x40` taken off its range and put, << 8, in the missile's `+0x44`; `0x400` velocity fitted to the distance; `0x800` `+0x44` replaces the row's byte `+0x136`; `0x2000` a random facing; `0x8000` the frames at `+0x4c` |
  | `+4` | the owner; `+8` the unit whose position is used without flag 1; `+0xc` a target unit |
  | `+0x10` | the Missiles.txt row |
  | `+0x2c` / `+0x30` | the skill and the level |
  | `+0x54` / `+0x58` | a callback run on the new missile, and a value kept for it |

- **The row's client missiles.** Skills `+0xe8` cltmissile, `+0xea`..`+0xf0`
  cltmissilea..d (checked against Skills.txt: Grim Ward's a / b / c are its
  medium, small and large starts). Calcs: `+0x64` aurarangecalc, `+0x114`
  cltcalc1, then `+0x118`, `+0x11c`; `+0x138`..`+0x140` calc1..3.
- **`FUN_004f21b0`** (cited, missiles.md; ECX unit, EDX skill) picks the
  client missile. With no row it returns −1. For a charge-up (flags `+4` bit 2, the
  byte at `0x6ce270`) with a valid aurastate (`+0x80`) and aurastat1
  (`+0x54`), and that state on the unit (`FUN_006256b0`), it returns
  cltmissilea/b/c by the state's charge count (`FUN_00625d00`, held to
  1..3). Otherwise it returns cltmissilea.
- **"State 12".** Several entries test or set state 12 (`inferno` in
  States.txt) with `FUN_00639df0` / `d2d_unit_set_state_checked`.

## Client entries, in address order

| Address | Slot (Skills.txt rows) | Reading | d2d |
|---|---|---|---|
| `FUN_004b11f0` | callee of `FUN_004f4000` | ECX unit: 1 for a monster (type 1) whose class passes `FUN_00623470` and for which `FUN_004ac7e0(unit, 8)` and `FUN_004ac7e0(unit, 2)` are both 0; else 0 | not compared |
| `FUN_004c5390` | callee of `FUN_004f11d0`, `FUN_004f4610` | ECX and EDX point to a pair (a, b): when a² + b² < 4 both are multiplied by 4; then when a² + b² < 16 both are doubled | not compared |
| `FUN_004c53d0` | callee of `FUN_004f11d0`, `FUN_004f4610` | ECX and EDX point to (a, b): it stores (b, −a), both halved (signed, toward zero) while the sum of their squares is over 3: a short perpendicular | not compared |
| `FUN_004c7e10` | cltdo 42 (Double Throw) | Weapon `d2d_skill_weapon(unit, 1)`, none 0. Sets `+0xc4` bit `0x40` once the unit's frame (`+0x38` high bytes) is 9 or more. A weapon that isn't throwable (`d2d_skill_checks_throwable`) but has stat `0x7d` gives 0. With a missile type (`d2d_item_missiletype`) above 0 and below the row count: a Missile Potion (`FUN_00629bb0(weapon, 0x26)`, tpot) through `FUN_004cdc30(0, 0, 0, 0, skill, level, 0)` (0 gives 0), anything else through `FUN_004c5420(skill, level)`; 1. No missile type: 0 | not compared |
| `FUN_004c7f50` | cltdo 41 (Grim Ward) | The target (`d2d_unit_target_checked`) must pass `FUN_00621f60`; `+0xc4` bit `0x40`. A free spot near the target (`FUN_0064e7b0(room, &xy, 2, 0x1000, 1)`). For a monster target: cltmissilea, or cltmissilec when the record `FUN_00451fe0` gives has byte `+5` bit 3, or cltmissileb when it has bit 2; a request `0x2001` at the spot with that row, then the same with row `0x73` (corpseexplosion); 1 | the server side is `components/game/fight/missiles.cpp:694` |
| `FUN_004c80d0` | callee of `FUN_004c83c0` (SkillsBar.cpp) | EAX skill; stack unit, level, &x, &y. Range: the `+0x64` calc at the level. Distance d from the unit (`d2d_search_enemy_near_apart`, under 2 taken as 1); a point past the range is pulled back along the line to it. It tries the point, then the point ± (s, t), with s the y offset and t the x offset (unit − point), each held to −2..2. A try counts when `FUN_0064e7b0(room, &pt, FUN_00620510(unit), 0x1c09, 1)` finds a spot, `FUN_0064d910` says it's open for the unit's pattern (`FUN_00649180`, `0x1c09`), `FUN_00645910(…, 0x804)` sees a line to it, and it's within range: the point is written back, 1. After three misses a player (type 0) gets `d2d_ctrl_click_impossible_voice`; 0 | not compared |
| `FUN_004c9320` | cltdo 45 (Whirlwind) | The unit's path (`+0x2c`) and skill node (`FUN_006439f0`), both needed. x = `skill_get_18`, y = `skill_get_1c`. When the node's flags (`skill_get_flags`) have both bits 0 and 1: w = 0x80 for a player, 0x100 for others; `FUN_0064ec10(room, x, y, FUN_00649180(unit), w)`, `path_restamp_bits(path, w)`, `FUN_00648ce0(path, 0x1c09 player / 0x3c01 other)`, `skill_set_flags(node, 0)`, `FUN_004ca900`, `FUN_004ba840`. With bit 0 alone: `unit_set_frame(unit, 3)`, `+0x48` = 0x500. Returns 1 | not compared |
| `FUN_004c9490` | cltdo 34 (Sacrifice) | Needs a valid cltmissilea, a target and a path; `+0xc4` bit `0x40`. Direction: the unit's facing (`FUN_00620100` >> 3) while the path moves (`path_velocity`), else seed % 8 (unit seed `+0x20`). Spot d = (dir + 4 − 1 + seed % 3) & 7; the position is the unit's plus `0x6daea8[d]`, `0x6dae88[d]`. The row is cltmissilea plus seed % cltcalc1 when that calc is positive. Then `FUN_004cdba0(ECX unit, EDX row, x, y, skill, level)`; 1 | not compared |
| `FUN_004c95e0` | cltdo 35 (Blessed Hammer) | Row `FUN_004f21b0`; `+0xc4` bit `0x40`; the target pair (`FUN_004c52e0`). Request `0x20` (owner and `+8` the unit, the row, the pair, skill, level). On the missile: `FUN_00648cf0(path, 0xe)`, `d2d_internal_move_route(path, missile, 0)`; 1 | not compared |
| `FUN_004c96d0` | cltdo 36 (Fist of the Heavens) | Row `FUN_004f21b0`; `+0xc4` bit `0x40`; a target unit. Request kind 1 at the unit's position; the missile gets `missile_set_data_28` = the target's type and `missile_set_data_2c` = its id (`+0xc`); 1 | not compared |
| `FUN_004c97c0` | cltdo 86 (no row) | Not in town (`FUN_0061ab00` of the room); row `FUN_004f21b0`; r = the `+0x64` calc at the level and r1 at level 1, both positive. n = r² / r1² / (seed % 18 + 1). n tries: dx, dy each `seed_rand_below(2r + 1)` − r; when dx² + dy² < r², a request `0x2001` at the unit + 2 + (dx, dy), facing seed % 64 (`d2d_ai_face`). Returns 1 | not compared |
| `FUN_004e3170` | callee of `FUN_004e31c0`, `FUN_004f11d0` | ECX a missile (type 3) with a valid row (`+4`), EDX n ≠ 0: `FUN_00621780(missile, (row byte +0x178 << 8) / n)`, the animation rate (`FUN_004cd540` puts the same byte << 8 at `+0x48`) | not compared |
| `FUN_004e3330` | cltdo 24 (Inferno, Arctic Blast) | Returns whether `FUN_004e31c0(unit, skill, level)` made a missile | not compared |
| `FUN_004e31c0` | callee of `FUN_004f0b50` and of `0x4e3337` (Inferno's cltdo 24, `FUN_004e3330`) | Valid cltmissilea, and cltmissileb or −1; the target pair. When cltmissileb is valid and seed % 2 is nonzero, it uses cltmissileb. f = calc1 (at least 1). Request `0x8020` (owner and source the unit, the pair, skill, level, frames f); `FUN_004e3170(missile, f)`. With state 12 on, a unit with `+0x30` set gets `unit_set_frame(unit, 8)` and `+0x48` = 0x500; otherwise `+0x44` = (`+0x44` & ~0xff) − 0x200. Returns the missile | not compared |
| `FUN_004e37c0` | cltdo 96 (Chain Lightning's ItemCltEffect) | Row `FUN_004f21b0`; `+0xc4` bit `0x40`; c = calc1. Request `0x21` from the target unit's position (the unit's without one) to the target pair, which must be there with both values nonzero (unit, row, skill, level); the missile gets `missile_set_data_28` = c; 1 | not compared |
| `FUN_004e3eb0` | cltdo 87 (no row) | Row `FUN_004f21b0`, the skill node, and a = cltcalc1, b = `+0x118`, c = `+0x11c` all positive. The node's count (`skill_get_18`) goes up by one; when the old count % a is 0: `FUN_004cdc30(ECX unit, …, 0, 0, x + rand(2b + 1) − b, y + rand(2b + 1) − b, skill, level, rand(c))` with the unit's seed and position; 1 | not compared |
| `FUN_004f04f0` | callee of `FUN_004f0650` | EAX unit, EDI count; stack row, skill, level (`RET 0xc`). Request kind 3 at the target pair (none: 0), callback `FUN_004e2ed0`; count missiles with offsets (`0x6dbf50[i & 7]`, `0x6dbf70[i & 7]`) and the index at `+0x58`; 1 | not compared |
| `FUN_004f0590` | callee of `FUN_004f0f30`, and of `0x4f06b6` (`FUN_004f0650`) | EAX unit; stack row, skill, level, flag (`RET 0x10`): `FUN_004cdb40(ECX unit, EDX row, its x, y, skill, level)`, 0 without one; the missile faces the unit (`FUN_00620100`, `d2d_ai_face`), `FUN_00649030(path, 0)`, and with the flag `FUN_00464810(ECX missile)`; 1 | not compared |
| `FUN_004f0650` | cltst 49 (Vine Attack, CorpseCycler, VineCycler) | `+0xc4` bit `0x40`. A valid cltmissilea: `FUN_004f0590(row, skill, level, 0)`. A valid cltmissileb: `FUN_004f04f0(EDI cltcalc1; row, skill, level)`; 1 | not compared |
| `FUN_004f0830` | cltdo 76 (DeathMaul) | cltmissileb's record and the target pair needed; d = the distance (at least 1). Step Param1 (`+0x38`) and delay Param2 (`+0x3c`) of that record, each at least 1. For k = 1, 2, … while k × step < d: a cltmissileb (`FUN_004cdb40`) at the unit + k × step along the line, facing the unit (`FUN_00648820`), its frames (`+0xe`, `+0x10`) the total plus (k − 1) × delay and `+8` the total less it, flags `+0xc8` bit `0x40000`. Then with a valid cltmissilea: `d2d_cast_to_target_in_range(skill, level)` (0 gives 0), calc1 > 0 sets its rate (`FUN_00621780`), and the same frame shift by the count × delay; 1 | not compared |
| `FUN_004f0ab0` | cltst 52 (mon inferno sentry) | The selected skill (`d2d_unit_skill_list`): `skill_set_18` = the frame (`FUN_0044da90`) + calc2 (at least 1); state 12 on when off; 1 | not compared |
| `FUN_004f0b50` | cltdo 77 (mon inferno sentry) | When `skill_get_18` of the selected skill is before the frame, state 12 off and 0. Else `FUN_004e31c0`; with a missile, `d2d_motion_new`, `d2d_motion_set_pos(m, 0, 0, cltcalc1)`, `d2d_motion_flag_20`, and `+0xc4` bit `0x40` cleared; 1 | not compared |
| `FUN_004f0dd0` | cltst 51 (fenris rage) | A target passing `d2d_corpse_revivable`: `skill_set_18` / `skill_set_1c` its x, y and `skill_set_20` its `+4`; 1 | not compared |
| `FUN_004f0e30` | cltdo 79 (fenris rage) | x = `skill_get_18`, y = `skill_get_1c`, both nonzero (else 0): `FUN_00648ad0(path, x, y)` and, with a valid cltmissilea, `FUN_004cdb40` at (x, y); 1 | not compared |
| `FUN_004f0f30` | cltdo 89 (no row; asserts name SkillsEMon.cpp, lines 0x35c / 0x364) | A monster (type 1) only. Without a skill node it adds skill 0x126 (Vine Attack, `FUN_00647280(unit, 0x126, 1, 0, …)`) and retries. The node's count (`skill_get_18`) goes up by one; x = `get_1c`, y = `get_20`. c = the `+0x118` calc (read twice); done when c ≤ the distance to (x, y) or c ≤ the old count. Mode (`+0x10`) 1: when done, a unit at the unit's position (`d2d_search_enemy_search_covers(room, x, y, LAB_004f0f10, 0, 1)`) gets `FUN_004d2d70`; the row is cltmissiled. Mode 2: cltmissilec + seed % 2. Other modes return 1. When done and the row is positive: count 0, x, y = the unit's, `FUN_004f0590(row, skill, level, 1)`; 1 | not compared |
| `FUN_004f11d0` | cltdo 80 (no row) | Row `FUN_004f21b0`; the target pair; n = calc1; d = pair − unit through `FUN_004c5390` then `FUN_004c53d0`. Start = pair − d × n / 2. Request `0x20` (unit, row, skill, level, calc2 at `+0x4c`); n missiles at start + k × d, each `FUN_004e3170(m, missile_total_frames(m))`. With state 12 on: `unit_set_frame(unit, 8)`, `+0x48` = 0x600; 1 | not compared |
| `FUN_004f1370` | cltdo 81 (Baal Cold Missiles) | Row `FUN_004f21b0`; the target pair; `FUN_004c5420(skill, level)`. The missile gets `missile_set_data_28` = −(pair y − unit y) and `_2c` = pair x − unit x; 1 | not compared |
| `FUN_004f1430` | cltdo 82 (no row; bugs.md 138) | Radius = Skills row 285 (Baal Corpse Explode) Param5 (`+0x158`) + (level − 1) × Param6 (`+0x15c`); 0 with under 0x11e rows or a level under 1. A unit search round the unit (`FUN_0065a950(0, ctx, room, x, y, radius, 0, {0x3002, unit, x, y, radius})`, `FUN_0065ac70`). Each found unit: `FUN_004cdb40(ECX unit, EDX 0x24b (baalcorpseexplodedelay), its x, y, skill, level)`; frames = the total − 10 + seed % 20. `FUN_0065aa00(ctx)`; 1 | not compared |
| `FUN_004f16f0` | callee of cltdo 67, 68, 69 | ECX unit, EDX count; stack row, skill, level, rate, first, spin (`RET 0x18`). The target pair (else 0). With first: one fewer, and `FUN_004c5420(skill, level)` makes one, with spin `FUN_00649030(path, 0)` and rate `FUN_00621780`. Then request `0x21` (the unit, its position, the pair, row, skill, level, callback `FUN_004e2ed0` at `+0x54`, the index at `+0x58`; spin adds `0x2000`), count missiles each with the same spin and rate; 1 | not compared |
| `FUN_004f1850` | cltdo 67 (Firestorm) | Row `FUN_004f21b0`; `+0xc4` bit `0x40`; n = calc1 > 0: `FUN_004f16f0(unit, n, row, skill, level, 0x200, 1, 0)` | not compared |
| `FUN_004f18f0` | cltdo 68 (Twister) | The same code with rate 0x100, first 0, spin 1 | not compared |
| `FUN_004f1990` | cltdo 69 (Tornado) | The same with rate 0x200, first 0, spin 1 | not compared |
| `FUN_004f1a30` | callee of `FUN_004f1c80` (three calls) | EAX unit; stack row, skill, level. d = the unit seed's low 3 bits; position = the unit + (`0x6dbfa8[d]`, `0x6dbfc8[d]`). Row cltmissilea (negative: nothing). With cltmissileb valid: no cltmissilec, an even seed draw takes b; with c, seed % 3 gives 0 b, 1 c, 2 a. Request kind 3 (that position, offset (`0x6dbfc8[d]`, −`0x6dbfa8[d]`), row, level; the skill field left 0). The missile: `d2d_motion_set_pos(m, 0, 0, seed % the +0x118 calc)`, `missile_set_data_28` / `_2c` the unit's x, y | not compared |
| `FUN_004f1c80` | cltdo 90 (no row) | "Town" is no room or `FUN_0061ab00`. With a prgsound (`+0x10e`): `FUN_004ca900`, and out of town nothing playing starts `FUN_004b9a00`, in town a playing one stops (`FUN_004ba790(1)`). In town: 0. Otherwise cltcalc1 rounds of three `FUN_004f1a30(row, skill, level)`; 1 | not compared |
| `FUN_004f1d80` | cltprg 92 (Armageddon, Diablogeddon) | The `+0x64` calc, a = cltcalc1, b = `+0x118`, c = `+0x11c` all positive; the target pair. Request kind 1 at the pair (unit, skill, level). cltmissilea, then cltmissileb, each when not negative (no upper check): `d2d_motion_set_pos(m, c × a, 0, b × a)`, `d2d_motion_set_vel(m, −c, 0, −b, 1)`; 1 | not compared |
| `FUN_004f1f20` | cltprg 91 (Volcano) | The target pair; request kind 1 there (unit, skill, level). A valid cltmissilea makes one; a valid cltmissileb makes one with `missile_set_data_28` = the caster's `+0x28`; 1 | not compared |
| `FUN_004f2010` | cltst 5 (Psychic Hammer, Dragon Flight) | A target, neither its room nor the unit's in town, of type 0 or 1, and `FUN_00465c60` (may the player interact with it) nonzero: returns that test | not compared |
| `FUN_004f2060` | cltdo 3 (Psychic Hammer) | The same tests, then `+0xc4` bit `0x40` and 1 | not compared |
| `FUN_004f20c0` | cltst 6 (Dragon Talon) | The selected skill: `skill_set_18` = calc1, or 0 with no target; 1 | not compared |
| `FUN_004f2260` | callee of cltdo 5, 6, cltprg 9, 11, 83, 15 | ECX unit, EDX skill, stack level: for a charge-up with the state and charge count as in `FUN_004f21b0`, prgcalc n (`+0x34 + 4n`, n held to 1..3) at the level; otherwise prgcalc1 (`+0x38`). 0 with no row | not compared |
| `FUN_004f2340` | cltdo 5 (Shock Field) | `+0xc4` bit `0x40`; row `FUN_004f21b0` ≥ 0; n = `FUN_004f2260` > 0: `FUN_004c7700(ECX unit, EDX row, n, the +0x64 calc, skill, level)`; 1 | not compared |
| `FUN_004f2590` | cltst 8 (Blade Fury) | Jumps to `FUN_004f2450` (char-panel.md) with the same arguments | not compared |
| `FUN_004f25a0` | cltdo 6 (Blade Fury) | Row `FUN_004f21b0`, the skill node, n = `FUN_004f2260` > 0; clears `+0xc4` bit `0x40`; the target pair. When `skill_get_18` is under 1: request `0x20` (unit, row, the pair, skill, level), `d2d_motion_set_pos(m, 0, 0, 0xf)`, and the count becomes n; otherwise it drops by one (`skill_set_18`). With state 12 on: `unit_set_frame(unit, Skills byte +0x16 − 1)`, `+0x48` = 0x800; 1 | not compared |
| `FUN_004f2720` | cltdo 7 (Dragon Tail) | A target and a row (`FUN_004f21b0`): `d2d_cast_to_target_in_range(skill, level)`, `+0xc4` bit `0x40`; 1 | not compared |
| `FUN_004f28a0` | callee of `FUN_004f29f0` | ECX unit; stack row, x, y, r, skill, level (`RET 0x18`). A local seed (`FUN_00650e40`). r² tries: dx = r − rand(2r), dy = r − rand(2r); inside the circle and with a room at (x + dx, y + dy), a request kind 1 there | not compared |
| `FUN_004f29f0` | cltprg 9 (Fists of Fire, Blades of Ice: charges 2 and 3) | Row `FUN_004f21b0`; r = `FUN_004f2260`, or the `+0x64` calc when under 1. Centre: the target unit, else the target pair (none: 0). `FUN_004f28a0(unit, row, x, y, r, skill, level)`; 1 | not compared |
| `FUN_004f2ac0` | the callback `FUN_004f2b20` stores at `+0x54` (pointer at `0x4f2b75`) | ECX missile: frames over 0x4d are cut to 0x4d (`FUN_0064a2b0`, `d2d_missiles_missile_range`); `FUN_00650e40`; `FUN_00648cf0(path, 10)`, `FUN_00648e70(path, frames)`, `d2d_internal_move_route(path, missile, 0)` | not compared |
| `FUN_004f2b20` | callee of `FUN_004f2f20` | EBX skill; stack unit, row, x, y, level, step (`RET 0x18`). Request kind 3 at (x, y) with callback `FUN_004f2ac0`. For i = 0, step, 2 × step … under 64: offset (`0x6dbfe8[i]`, `0x6dc0e8[i]`), a local seed draw kept at `+0x58`, a missile, `missile_set_data_28` = Param2 (`FUN_004efc20`) + 1 | not compared |
| `FUN_004f2be0` | the callback `FUN_004f2dd0` stores (pointer at `0x4f2e25`) | ECX missile: the same frame cut, seed and path calls as `FUN_004f2ac0`, then it writes the path's points (`FUN_006487d0`): from the missile's position, direction `FUN_00621dc0(missile, path target)`; sign ±1 from the missile seed's bit 0, step seed % 3 + `[0x72d990]` (2). Each frame the direction moves by step × sign, `FUN_0063e7a0` maps it to an entry of `0x6dc208` / `0x6dc1e8` (x, y steps), and the point is stored as two shorts. Every `[0x72d98c]` (15) frames the sign flips and the step is redrawn. `FUN_00648790(path, frames)` | not compared |
| `FUN_004f2dd0` | callee of `FUN_004f2fe0` | As `FUN_004f2b20` with the offset tables `0x6dc228` / `0x6dc328` and callback `FUN_004f2be0` | not compared |
| `FUN_004f2e90` | cltprg 10 (Claws of Thunder, charge 2) | Row `FUN_004f21b0` and a target: `FUN_004c70d0(ECX unit, EDX target, row, −1, skill, level, d2d_missile_velocity(row, level))`; 1 | not compared |
| `FUN_004f2f20` | cltprg 11 (Claws of Thunder, charge 3) | Row and target; n = `FUN_004f2260` or the `+0x64` calc; `FUN_004f2b20(EBX skill; unit, row, unit x, y, level, n)`; 1 | not compared |
| `FUN_004f2fe0` | cltprg 83 (Royal Strike, charge 2) | As `FUN_004f2f20` through `FUN_004f2dd0` | not compared |
| `FUN_004f3290` | callee of `FUN_004f3430` | EAX count; stack unit, x, y, skill, level, row (`RET 0x18`). Request kind 3 at (x, y). Each missile: offset dx = seed % 40 − 20, dy the same (both 0: dx 20); `missile_set_data_28` = `FUN_00650e50` of the local seed, `_2c` = dy << 16 \| dx & 0xffff | not compared |
| `FUN_004f33b0` | cltprg 14 (Royal Strike, charge 1) | Row and the target pair: `d2d_cast_to_target_in_range(skill, level)`; 1 | not compared |
| `FUN_004f3430` | cltprg 15 (Royal Strike, charge 3) | Row; the target pair; n = `FUN_004f2260` (0 gives 0); `FUN_004f3290(EAX n; unit, x, y, skill, level, row)`; 1 | not compared |
| `FUN_004f34d0` | cltst 18 (Amplify Damage, Dim Vision, Weaken, Iron Maiden, Terror, Confuse, …) | With a valid cltmissilec, `FUN_004c5420(skill, level)`; 1 | not compared |
| `FUN_004f3680` | callee of `FUN_004f3810` | ESI unit; stack skill, level, row (`RET 0xc`). The target pair (else 0); (dx, dy) = pair − unit, d the distance (under 2: 1). Request kind 3: position the unit + 2 × (dx, dy) / d, offset 5 × (dx, dy). The missile faces `FUN_00621dc0(unit, pair)` (`FUN_00648820`); 1 | not compared |
| `FUN_004f3810` | cltst 19 (Teeth, Bone Spear) | With a valid cltmissilec: `FUN_004f3680(skill, level, cltmissilec)`; else 0 | not compared |
| `FUN_004f3870` | cltst 20 (Raise Skeleton, Raise Skeletal Mage) | A target passing `d2d_corpse_consumable(target, 0)`: `skill_set_18` / `_1c` its x, y; 1 | not compared |
| `FUN_004f38d0` | cltdo 31 (Raise Skeleton, Raise Skeletal Mage) | Row `FUN_004f21b0`: request `0x2001` at (`skill_get_18`, `get_1c`), then both set to 0; 1 | not compared |
| `FUN_004f3ce0` | callee of `FUN_004f3e50` | ECX unit, EDX skill; stack level, x, y, class (`RET 0x10`). Needs a valid cltmissilea, class a MonStats row (count `+0xa80`), and x, y nonzero. `+0xc4` bit `0x40`; request `0x2001` at (x, y). A positive cltmissilec makes a second one at level 2. On the first: a positive cltmissileb runs `FUN_004cefe0(ECX missile, EDX cltmissileb, 0, 2, 0)` (missiles.md); with no target and a tgtsound (`+0x10a`), `FUN_004b9a00(ECX tgtsound, EDX missile, 0, 0, 0)`; 1 | not compared |
| `FUN_004f3e50` | cltdo 33 (Poison Explosion) | The selected skill: `FUN_004f3ce0(unit, skill, level, get_18, get_1c, get_20)`; returns no value (`RET 4`) | not compared |
| `FUN_004f3eb0` | cltst 22 (Bone Wall, Bone Prison) | A room for the unit, the target pair, and a room there (`FUN_00463740`). 0 only when Skills byte `+5` bit 0 is clear and that room is town (`FUN_0061ab00`); else 1 | not compared |
| `FUN_004f4000` | cltst 24 (Revive) | A monster target with `FUN_004638a0(ECX class, EDX 8)`, `d2d_corpse_consumable(target, 0)` and `FUN_004b11f0(target)`. The class's `FUN_00451fe0` record byte `+0xc`: 1 picks cltmissileb, 3 cltmissilec, else cltmissilea. Request `0x2000` (unit at `+4`, the target at `+8`, so it starts on the corpse); returns whether a missile came | not compared |
| `FUN_004f4610` | cltdo 17 (Multiple Shot, Teeth, PrimePoisonball, Shock Wave) | `+0xc4` bit `0x40` first. cltmissilea, or cltmissileb when it exists and `FUN_00623c60()` isn't 1. n = calc1; the target pair; d through `FUN_004c5390` and `FUN_004c53d0`; start = pair − d × n / 2. Request `0x820` (unit, row, skill, level, calc2 at `+0x44`); n missiles along d; 1 | not compared |
| `FUN_004f47b0` | cltdo 18 (Guided Arrow, Bone Spirit, MonBoneSpirit) | `+0xc4` bit `0x40`; the target pair; row `FUN_004f21b0`. t the target unit. Request `0x20` (unit, t at `+0xc`, row, the pair, skill, level), `0x420` without t. The missile: `missile_set_data_28` = 1 with t, 2 without; `_2c` = (pair y − unit y) << 16 \| (pair x − unit x), as shorts; 1 | not compared |
| `FUN_004f48f0` | cltdo 19 (Charged Strike) | A target t and a row (`FUN_004f21b0`). n = calc1. Request `0x21`: unit, row, position t's x, y, target point 2 × t − unit (past it), skill, level, callback `FUN_004e2ed0`, the index at `+0x58`; n missiles; 1 | `components/game/fight/strike.cpp` names it; not compared |
| `FUN_004f4d90` | cltdo 22 (Lightning Strike) | Row `FUN_004f21b0`; a target t; r = calc1. f = `FUN_004c5eb0(ECX unit, EDX t x, t y, r, 3, t id, 0)` (none: 0). Request `0x20` (owner the unit, `+8` t, row, target point f's x, y, skill, level); `missile_set_data_28` = calc2; 1 | not compared |
| `FUN_004f4ec0` | none: no call, jump or pointer (stubs.md) | ECX unit, EDX id, stack value: a monster whose class's MonStats base (`+2`) is 0x10b (bloodraven) and EDX 0xa7: `FUN_00470390(ECX unit, EDX 0x55, 2, 0, 0, 0, 0, 0)` and returns 0x54; else returns the value | not compared |

## Server entries, in address order

| Address | Slot (Skills.txt rows) | Reading | d2d |
|---|---|---|---|
| `FUN_005c91c0` | srvst 13 (Thunder Storm) | The skill node (`FUN_006439f0`): `skill_set_18` −1, `skill_set_1c` 1; 1 | not compared |
| `FUN_005c9220` | srvst 14 (Hydra) | A room for the unit, `d2d_skill_target_point`, and a room there that isn't town; returns that test | not compared |
| `FUN_005c9640` | srvdo 19 (Inferno, Arctic Blast) | With a valid srvmissilea (`+0x48`), returns `FUN_005c8ca0(skill, level, srvmissilea)`; else 0. skills.md calls `FUN_005c8ca0` "Do 19"; the slot (`0x7322fc`) holds this wrapper | not compared |
| `FUN_005c98f0` | srvdo 21 (Telekinesis) | A player (type 0) not `d2d_player_busy`, and t = `FUN_00553540(game, unit)`; `+0xc4` bit `0x40`. A player or monster t, hostile (`FUN_00554200`), with neither room in town: knockback when `FUN_0045c390(unit seed, 100)` < Param2 (`FUN_004efc20`); a 0x70-byte hit record from `d2d_skill_phys_roll` and `FUN_0056e0c0`, word `+4` \|= Skills `+0x12e` (\| 9 with the knockback), dword `+0` \|= `+0x130`, `+0x60` = `+0x134` when set; `d2d_objects_take_blast(t, 1, &rec)`, `FUN_0057cee0(t, &rec)`. An object: `FUN_00584420(2, t id, …)`. An item of type gold, poti, scro, tpot, key or misl (ItemTypes 4, 9, 0x16, 0x26, 0x29, 0x38): `FUN_00563560(t id, …)`; any other item: `FUN_00553380(unit)`. Returns 1, or 0 for a friendly or in-town t | `components/game/fight/strike.cpp:1396` (damage and knockback); not compared |
| `FUN_005ca260` | srvdo 151 (Chain Lightning's ItemEffect) | Valid srvmissilea; `+0xc4` bit `0x40`; c = calc1. (dx, dy) = target − unit (`FUN_00553540`; 0, 0 without one). `FUN_0056ecb0(unit, skill, level, dx, dy, 0, 0, 0)`; the missile gets `missile_set_data_28` = c; 1 | not compared |
| `FUN_005cd910` | srvst 55 (Mosquito) | The selected skill and t = `FUN_00553540`. A local seed (`FUN_00650e30`, `FUN_00650e40` with t's id + the unit's, −1 for none). `skill_set_18` = calc1 + `seed_rand_below(calc2 − calc1)` (at least 1), `_1c` t's id, `_20` its type; 1 | not ported |
| `FUN_005cda00` | srvdo 107 (Mosquito) | The selected skill's unit (`FUN_00552f60(get_1c, …)`), which `FUN_00622c40(unit, it, 0)` must allow. lo, hi = `d2d_skills_skill_phys` and `FUN_00647d00` (each >> 8). A hit record: `d2d_skill_phys_roll`, `FUN_0056e0c0`, `+0x28` = (lo + rand(hi − lo)) × 2, `+0x2c` = `d2d_skills_length_bonus`, `+0x3c` and `+0x40` = (lo + rand) << 8, word `+4` = 1. The unit's life (stat 6) goes up by `FUN_00483360` of calc3 (`+0x140`), at most `d2d_unit_max_life` (`FUN_00627260`). `d2d_objects_take_blast`; when the victim's life is then 0, word `+4` \|= 2; `FUN_0057cee0`. While the count (`get_18`) − 1 is positive: it's stored, `unit_set_frame(unit, Param1 (+0x148))`, and `+0x48` gets 0x100 added to its high bytes; 1 | not ported |
| `FUN_005ce4f0` | called from `0x5a84bb`, code past `FUN_005a83e0`'s mapped end that calls it when the unit has state 22 (spiderlay) | ECX game, EDX unit; needs path flag 8 (`d2d_path_flag_8`). Direction `FUN_0063e7a0(FUN_00648810(path))` picks one of the bytes {10, 8, 0x16, 0x14, 0x12, 0x10, 0xe, 0xc}; `FUN_0063e7e0` turns it into (dx, dy). Level `d2d_skill_level` of the node. A server missile (`FUN_0059fa30`) with request `0x21`: row 0x8f (spidergoolay) at the unit + (dx, dy) toward 2 × (dx, dy) further, `+0x28` = 3, skill 0xad (SpiderLay) | not compared |
| `FUN_005d16a0` | srvdo 127 (Suck Blood) | t = `FUN_00553540` (else 0); `+0xc4` bit `0x40`; r = `FUN_0057d690()` (else 0). When r's byte `+4` bit 0 is set: the healer is the unit, or a monster's `d2d_monster_owner_unit` when it has one; its life (stat 6) + `FUN_00483360` of calc1, at least 1 and at most `d2d_unit_max_life` (`FUN_00627260`); it also reads r `+8` and t's life without using them. Then `FUN_0057d4f0(t, 0)`; 1 | not ported |
| `FUN_005d27b0` | srvdo 138 (no row) | Needs a srvmissilea above 0. With `d2d_skill_target_point`: d through `FUN_0056d370` and `FUN_0056d3b0`; n = calc1; start = point − n × d / 2; request `0x20` (unit, row, skill, level, calc2 at `+0x4c`); n server missiles (`FUN_0059fa30`) along d. With state 12 on: `unit_set_frame(unit, 8)`, `+0x48` = 0x600. Returns 1 even with no point. The server side of cltdo 80 | not compared |
| `FUN_005d89b0` | srvdo 75 (Grim Ward) | t = `FUN_00553540` passing `d2d_corpse_revivable`; a free spot by t (`FUN_0064e7b0(room, &xy, 2, 0x1000, 1)`). A monster t: srvmissilea, or srvmissilec (`+0x4c`) / srvmissileb (`+0x4a`) on the same `FUN_00451fe0` bits as `FUN_004c7f50`. Request kind 1 at the spot (`FUN_0059fa30`), then t gets states 0x68 (corpse_nodraw) and 0x76 (corpse_noselect) and `FUN_0064c040(t)`; 1 | `components/game/fight/missiles.cpp:694`; not compared |
| `FUN_005da8f0` | srvst 5 (Jab) | A target (`FUN_00553540`): returns whether there is one | not compared |

## Slots no shipped row reaches

Neither Skills.txt (its function and item-effect columns) nor States.txt
(clteventfunc) names these slots: cltdofunc 80 (`FUN_004f11d0`), 82 (`FUN_004f1430`),
86 (`FUN_004c97c0`), 87 (`FUN_004e3eb0`), 89 (`FUN_004f0f30`) and 90
(`FUN_004f1c80`, with its callee `FUN_004f1a30`), and srvdofunc 138
(`FUN_005d27b0`). cltdofunc 85 (`FUN_004ca030`, collision.md) and 88
(`FUN_004f0ef0`, frontier-ring.md) are named by no row either. srvdofunc 151
(`FUN_005ca260`) is reached, through Chain Lightning's ItemEffect, and so is
cltdofunc 96 (`FUN_004e37c0`) through its ItemCltEffect.

Slot 82 reads Skills row 285, Baal Corpse Explode. That row's cltstfunc is
82, past the 70-entry cltstfunc table, so `FUN_004c6140` runs nothing, and
its cltdofunc is blank. The client's corpse bursts never play (bugs.md 138).

Re-check: `python3 tools/ghidra/fn.py <address>` for any row; the table
slots and counts with a dword read of game.exe at the addresses above.
