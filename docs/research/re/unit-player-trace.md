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

## How to re-check

Each row is one function; disassemble it from its entry to the next
function's entry in `game-functions.tsv`, for example:

```sh
objdump -d --x86-asm-syntax=intel --start-address=0x461dc0 --stop-address=0x4621d0 /path/to/game.exe
```

With a Ghidra project, `tools/ghidra/fn.py 00461dc0` gives the decompiled
view of the same body.
