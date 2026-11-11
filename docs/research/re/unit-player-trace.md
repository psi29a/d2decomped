# UNIT/Player.cpp: client player routines

Static reads of the `UNIT/Player.cpp` functions the gap map at
`962ffce2` lists as cited or untouched, in stock 1.14d game.exe
(SHA-256 `631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`).
No Ghidra project was available for this pass, so every row is read from the
instruction listing (`objdump -d --x86-asm-syntax=intel`), which also settles
the register arguments, stack cleanup and return values the decompiler hides.
These are static observations, not oracle results or claims about d2d.

A click record is the block `FUN_00462d00` builds and passes down: byte `+0`
flags (`0x8` a held frame, `0x20` the shift variant, bits 0 and 1 the two
send paths below), `+4` the acting unit, `+8` the target unit, `+0xc` / `+0x10`
the spot, `+0x14` the walk's send argument, `+0x18` the walk action and
`+0x1c` a skill node. Those meanings come from the reads below and
"What a held frame sends" in [collision.md](collision.md); the bits not named
there keep their numbers. The pending interact is player data (unit `+0x14`)
`+0x150` flag, `+0x154` action, `+0x158` target type, `+0x15c` target id, as
`FUN_00460780` stores it (collision.md "The pending interact's fields").
A fatal assert raises the internal-error path (`FUN_00408090`, `FUN_00408a60`)
and exits -1 through `FUN_00681e09`; it is not a failure return. "Returns
nothing" means EAX is left as the last callee set it.

| Address | Static trace |
|---|---|
| `FUN_004609f0` | Stat change hook that `FUN_00460bf0` installs with `FUN_00626d40(unit, 0, FUN_004609f0, 0)`. EDX unit, four stack words: a source unit, a packed word (stat id in the high half, layer in the low), one unread word, the new value. `RET 0x10`, returns nothing. A null unit or a stat outside `0x59..0xcc` returns. Stat `0x59`: `FUN_00460930(ECX unit, EDX value)`. Stat `0x5a`: `FUN_004609a0(ECX unit, EDX value)`. Stats `0x61` and `0x6b`, player units only, layer as skill id: look the skill node up with `FUN_006439b0(unit, skill, -1)`. Value 0 with a node whose `FUN_006442a0(unit, node, 0)` is 0 or less calls `FUN_006470f0(unit, skill, "UNIT\Player.cpp", 0xdd)`. A nonzero value with no node calls `FUN_00647280(unit, skill, 0, 0, file, 0xe8)`; for stat `0x6b` only when `FUN_0045c4b0(skill)` is nonnull and its signed byte `+0xc` equals unit class `+4`. Stat `0x61` skips the row test. Stat `0xcc`, player units with a nonnull source: the layer splits into `layer >> [tables+0xc6c]` and `layer & [tables+0xc70]`; the low byte of `FUN_00625480(source, 0xcc, layer)` is nonzero and `FUN_00625820(source, 0)` is the unit: `FUN_00647530(unit, source +0xc, first, second, that byte, 0)`, otherwise `(..., 0, 1)`. Every other stat in the range does nothing. |
| `FUN_00460bf0` | Set up a player unit on the client. ECX unit, EDX room, stack x and y, `RET 8`, returns nothing. In order: `FUN_0046c6e0` (ECX/EDX still unit/room); `FUN_00649d00(0, room, x, y, unit, 1)`; an inventory from `FUN_0063abd0(0, unit)` only when `+0x60` is null; `+0xa8 = FUN_006438b0(0)`; `FUN_00647ee0(unit)`; the stat hook `FUN_004609f0`; stats `0x44`, `0x43`, `0x45` set to 100 through `FUN_00627260`; `0x7a5260 = FUN_0061ab00(room)` stored raw (`FUN_00460e70` stores it as 0/1); `FUN_00624690(unit, 5)`; flags `+0xc4` lose `0x20` and gain `0xe`. Mode 0 or 0x11 adds `FUN_00649560(unit, 1)`, `FUN_00649190(unit, 5)`, `FUN_00648c30(path, 0x8000)`. When `FUN_0061ab30(room)` is 0, `FUN_00474160(ECX unit, EDX other, 0xd, 0xff, 0xff, 0xff, 0xff)`, where other is 1 only for a unit that is not the local player (`FUN_00463dd0`) while one exists; a nonzero result goes in `+0x64` and `FUN_00474350` of it sets the light radius (`FUN_00464cf0`). Then `FUN_00621f90(unit)`. A unit that is not the local player steps its seed `+0x20`/`+0x24` once (`low * 0x6ac690c5 + high`) and passes `low & 0xffffff3f` to `FUN_006488a0(path)`. Last, `FUN_004608c0()`. |
| `FUN_00460d50` | Tear a player unit down; the reverse of `FUN_00460bf0`. ECX unit, no stack arguments. `FUN_0046c700`, `FUN_0046ec10`; while `0x7a0634` is set, `FUN_00649f50(unit)` and `FUN_0064c370(unit)`; `FUN_00648460(0, path)` and `+0x2c = 0`; `FUN_00466cb0(ECX unit, EDX 0)`; `FUN_0063ac40(inventory)` and `+0x60 = 0`; `FUN_00643a00(unit, 0)` and `FUN_004743d0` on a nonzero old value; `FUN_00464930`; `FUN_00463d90(ECX 0)` when the unit is the local player; `FUN_00622060(0, unit)`; then a tail jump to `FUN_004608c0`. No null-unit check. |
| `FUN_00460de0` | ESI unit, no arguments, plain `RET`. x and y come from the static path `+0x2c` words `+0xc` / `+0x10` for types 2, 4 and 5 (no null check there), otherwise `FUN_006488c0` / `FUN_00648900` of the path, or 0 with none. Returns `FUN_0061acd0(FUN_00620bb0(unit), x, y, FUN_00620f50(unit))`. Only caller `FUN_00463390`, for the local player. |
| `FUN_00461010` | ESI unit, no arguments; ECX is not read. `FUN_004c1e30`, `FUN_00649f70(unit, 0)`, `FUN_00464810`; a nonnull unit loses flag `0x4` of `+0xc4`, and also `0x2` when `FUN_00639df0(unit, 7)` is 0; `FUN_0046ece0`, `FUN_0046f020(ECX unit, DL 0)`, then stat 6 set to 0 through `FUN_00627260(unit, 6, 0, 0)`. The null tests come after calls that already took the unit. Returns nothing. |
| `FUN_004611f0` | EDI unit (pushed again as the argument of `FUN_00620250`, which pops it), stack flag, `RET 4`. A null skill list returns. Skill flags `FUN_006446a0` with bit 0 set: set `flags \| 2` and call `FUN_004c68f0(ECX unit)`. Otherwise, only with flag 0, call `FUN_004c68f0` for a null unit or one without `+0xc4` bit 6. Returns nothing. Callers `FUN_00461250`, `FUN_00463260`, `FUN_00463390`. |
| `FUN_00461700` | Send a click's skill. ESI click record, stack mode, `RET 4`, returns nothing. Nothing goes when the mouse word `0x7a6aac` (`FUN_00468740`) is signed greater than `[0x711470] - 0x31` (`FUN_00454970`). Mode 1 sends on the target, type and id (6 and -1 without one): flag bit 0 sends action 6, 7 with `0x20`, 9 with `0x8`, `0xa` with both; bit 1 sends `0xd`, `0xe`, `0x10`, `0x11` the same way. Any other mode sends at the spot: bit 0 action 5 (8 with `0x8`), bit 1 `0xc` (`0xf` with `0x8`). Each send is `FUN_00481030(ECX action, EDX unit, a, b, 0)`; both bits send twice, neither sends nothing. Callers `FUN_00461b40`, `FUN_00461c70`, `FUN_004625b0`. |
| `FUN_00461ac0` | ECX skill node, EDX target, stack range and distance out-pointers, `RET 8`, clean 0/1 in EAX. A null local player, node, target or `FUN_00644140(node)` record answers 0 and writes neither out. Otherwise range = `FUN_00646ca0(player, record dword +0x64, signed record word +0, FUN_006442a0(player, node, 1))`, distance = `FUN_006416d0(player, target)`, answer distance <= range, signed. Callers `FUN_00461b40` and `FUN_00480c80`; collision.md lists only the second, and writes the dword as `record+0x32` (the word index). |
| `FUN_00461b40` | Cast at a unit, or walk into range first. EAX target id, ECX target type, EDX click record, plain `RET`, returns 1 on every path that returns. Target `+8` null asserts (id 0x52f), unit `+4` null asserts (0x530). Flag `0x20`, or `FUN_00461ac0(+0x1c, target)` in range, calls `FUN_00461700(1)`. Otherwise bit 0 queues action 6 and bit 1 action 0xd through `FUN_00460780` (both: 0xd last). Then distance - range of 0 or less asserts (0x548), and so does distance 0 (0x549). A null node or record leaves both outs 0, so it ends at 0x548. The walk goes to `unit + (target - unit) * (distance - range) / distance` on each axis (`FUN_0045adf0` x, `FUN_0045ae20` y, signed division truncated toward zero) through `FUN_00461840` with the click's `+0x14`. Only caller `FUN_00461c70`. |
| `FUN_00462560` | EAX skill node, clean 0/1 in EAX. A null node answers 0. Otherwise `FUN_00643ce0(node, "UNIT\Player.cpp", 0x75a)` gives the skill id, the Skills row is computed inline (`id * 0x23c + [tables+0xb98]`, 0 outside `0..[tables+0xba0]`), and the answer is byte `+5` bit 0 (mask byte from `0x6ce268`). An id out of range reads byte 5 of a null row: there is no row null check. Only caller `FUN_004625b0`. |

## How to re-check

Each row is one function; disassemble it from its entry to the next
function's entry in `game-functions.tsv`, for example:

```sh
objdump -d --x86-asm-syntax=intel --start-address=0x461dc0 --stop-address=0x4621d0 /path/to/game.exe
```

With a Ghidra project, `tools/ghidra/fn.py 00461dc0` gives the decompiled
view of the same body.
