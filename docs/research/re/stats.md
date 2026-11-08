# Stat lists — game.exe 1.14d (`.\Stats\StatsEx.cpp`)

A unit's stats live in the stat list at unit +0x5c. The list holds a
sorted array of 8-byte entries {key, value} (key = stat id << 16 | layer),
found by binary search (`FUN_00624b90`), inserted by `FUN_00624c80` and
removed by `FUN_00624d10`; both resize with the assert file name
`.\Stats\StatsEx.cpp` (lines 0xe8 and 0x110).

## SetStat — FUN_006270b0 (list, stat, value, layer)

Cited, not run (no oracle in this pass). Read from the decompile:

- A null list returns 0.
- It looks the key up. Missing and `value` 0: returns 0, nothing changes.
  Missing otherwise: a new entry is made (`FUN_00624c80`, old value 0).
- `value - old` equal to 0: returns 0, nothing is touched.
- `value` 0 removes the entry (`FUN_00624d10`); else the entry's value
  is overwritten (a set, not an add). Then `FUN_00626920(delta, 0)`
  is called with the change.
- If the list's flags (+0x10) have bit 31 and list +8 is 0, the key
  `stat << 16 | (layer & 0xffff)` is passed to `FUN_00624dd0`, which
  queues it in the list's changed-stat array (+0x50, assert line 0x162)
  only when the stat's ItemStatCost record (`PTR_DAT_00744304 + 0xbcc`,
  0x144 each) has bit 0x10 in byte +5, and never for stats 6, 8, 10,
  0xd and 0xe. It returns 1 when it changed something.

Re-check: `python3 tools/ghidra/fn.py 006270b0`, `python3 tools/ghidra/fn.py 00624dd0`.

d2d: reads the same, cited from the decompile, no oracle run (not a
contradiction). d2d keeps stats in plain arrays
(`d2s::Stats`), has no key list and no changed-stat queue;
objects.md:81 already notes "sets, not adds" for this function.

## SetStat on a unit — FUN_00627260 (unit, stat, value, layer)

Cited. It is `FUN_006270b0(unit +0x5c, ...)`; when that returned nonzero
and the unit's type (`*unit`) is 0 (a player) it also calls
`FUN_00624dd0(stat << 16 | (layer & 0xffff))`, so a player's changed stat
is queued even when the list's own check (list +8 == 0) did not apply.
Callers are many (game-calls.tsv lists 100+); drops.md:360 and
tools/emu/boss_level.py hook it to record stat writes.

Re-check: `python3 tools/ghidra/fn.py 00627260`.

d2d: reads the same, cited from the decompile, no oracle run. components/rules/monsters.hpp:1190 and d2s_write.hpp:60 cite
it only as "stat set"; d2d has no player-only queue (no net-sync of stats).

## Capped gold set — FUN_00530ea0 (ECX unit, EDX stat, stack value)

Cited. `value < 0` sets the stat to 0. For a player (`*unit == 0`) and
stat 0xe (gold carried) it compares `value` with `FUN_00622e70` (stat 0xc,
the level, x 10000); for 0xf (stash gold) with `FUN_00623460` (2,500,000).
Past the cap the stat is set to 0, not clamped to the cap; otherwise
`FUN_00627260(unit, stat, value, 0)`. Same cap rule as `FUN_0053ff00`
(the add form, bugs.md 50), which also passes through `FUN_006272b0`
(a no-op when the delta is 0).

Re-check: `python3 tools/ghidra/fn.py 00530ea0`, `python3 tools/ghidra/fn.py 0053ff00`.

d2d: the add form (rules::pay, test_repair_all) reads the same, cited from
the decompile, no oracle run. The set
form's callers (00535510, 005589a0, 00558d90, 0055a6d0) are not traced, so
whether any can pass a value past the cap is open; loot.hpp:208 keeps
gold pickups within `room`.

## Starting stats from a class record — FUN_005706d0 (ECX, EDX unit, stack byte n)

Cited; not compared with d2d (no d2d function cites it). The unit's class
(+4) indexes the 0xc4-byte record table at `PTR_DAT_00744304 + 0xbc4`
(count at +0xbc8; the table is not named here). With record `r`, it sets
through `FUN_00627260`:

| Stat | Value |
|---|---|
| 0, 1, 2, 3 | r+0x30, r+0x32, r+0x31, r+0x33 (bytes; stat 1 and 2 swap places) |
| 0x13, 0x14 | 0 |
| 6, 7 | (r+0x35 + r+0x33) x 256 |
| 8, 9 | r+0x32 x 256 |
| 10, 11 | r+0x34 x 256 |
| 0xc | 1 (level) |
| 0x1e | `FUN_00611800(class, 1)` |
| 0x43, 0x44, 0x45 | 100 |

Last, when the stack byte `n` (clamped to 4) is nonzero: `n` is zeroed if
it is not below `DAT_007326b4`, then `FUN_0057eb10(DAT_006e1520[n])` runs.
The 256 factor is the 8-bit fixed point of life, mana and stamina.

Re-check: `python3 tools/ghidra/fn.py 005706d0`.

## Stat list helpers (cited, read from the decompile, no oracle)

Layout as the code below uses it (list = what `unit +0x5c` points to):
+0x04 the unit it hangs on, +0x10 flags, +0x24 / +0x28 the plain
entry array and its count (short), +0x2c next / +0x30 previous sibling,
+0x34 parent list, +0x38 removal callback, +0x3c first child, +0x40 first
child of kind 0x2000, +0x44 the owning unit, +0x48 / +0x4c the expanded
array and count (short). Flag bits seen: 0x80000000 expanded (has
children, uses +0x48), 0x40000000 switched off, 0x20000000 holds a stat
whose ItemStatCost +0x53 is set, 0x2000 a list the walks below skip.
ItemStatCost rows are 0x144 bytes at `tables +0xbcc`, count at `+0xbd4`.

### Stat array of a list — FUN_00624f40 (EAX list)

Returns `list + 0x48` when flag 0x80000000 is set, else `list + 0x24`:
the {array, count} pair the walks below read (callers `FUN_00626e10`,
`FUN_00627860`, `FUN_00627a40`). Evidence: 17 bytes, one
flag test. d2d: not ported (d2d keeps stats in `d2s::Stats`).
Re-check: `python3 tools/ghidra/fn.py 00624f40`, `python3 tools/ghidra/calls.py 00626e10`.

### ItemStatCost row — FUN_0045c4f0 (ECX stat id)

Returns `stat * 0x144 + *(tables + 0xbcc)`, or 0 when the id is negative
or not below `*(tables + 0xbd4)` (`tables` = `PTR_DAT_00744304`). The
walks named here repeat this bounds test inline (`FUN_006269f0`,
`FUN_00626920`, `FUN_006266c0`) or calls it (`FUN_00626e10`,
`FUN_00626200`). Callers include `FUN_00565010`, `FUN_005665f0`,
`FUN_005beda0`, `FUN_005c37c0`, `FUN_005c3f20`. d2d: not ported (its
tables are vectors indexed with their own checks).
Re-check: `python3 tools/ghidra/fn.py 0045c4f0`, `python3 tools/ghidra/calls.py 0045c4f0`.

### Refresh a derived stat — FUN_006266c0 (ECX list, EDX key, stack ItemStatCost row, ctx)

The Ghidra signature is wrong about the first two arguments: ECX and EDX
pass through unchanged into `FUN_00626200` (the value of the key from the
row's +0xe0 op entries; d2d cites it in merc.cpp / strike.cpp), so ECX is the list and EDX the key; the recursive calls
`FUN_006266c0(row, ctx)` push a dependent's row and ctx.

- Row byte +0x51 is 0: the computed value is stored (`FUN_00625150`, a
  set: value 0 with row +0x50 clear removes the entry, a nonzero value
  sets list flag 0x20000000 when row +0x53 is set, and the list's
  change callback (+0x5c) runs when the value changed and row +5 bit 3
  is set). Returns the value.
- Row +0x51 set (the stat has dependents): with a computed 0 it first
  stores 0. It then walks up to 3 dependents (`ushort` ids at row +0x58,
  stops at 0xffff; the matching shorts at +0xde, stride 3 shorts),
  recomputing each through itself and storing it with
  `FUN_00625150(id << 16, ...)`. When a dependent's value is nonzero,
  the row byte +0x54 decides whether the parent's own value may stand:
  0, 8-0xb never block; 2/3 block when the owner type (list +8) is 0 or 1
  and the id at +0xde is not 0xffff and that stat reads positive
  (`FUN_00624f60`); 4/5 the same for the unit at list +4 (when non-null)
  through `FUN_00625480`; 6/7 block when the owner
  type is 0 or 1; 0xd blocks when the owner type is 4; any other value
  blocks (switch default). If nothing blocked and the value is nonzero it
  stores the value again and recomputes the up to 64 stats at row +0x5e.
  The op byte's names are not resolved: cited, read from the decompile.
- It returns the computed value in every path.

d2d: not ported (no stat lists). Re-check: `python3 tools/ghidra/fn.py 006266c0`,
`python3 tools/ghidra/calls.py 006266c0`.

### Push a change up the lists — FUN_00626920 (ECX list, EDX key, stack delta, ctx)

Called by `SetStat` (`FUN_006270b0`, see above) and the other setters
with the change. Does nothing for a delta of 0, a stat id (key >> 16)
out of range or a list with flag 0x2000. Otherwise it starts at the
list itself when flag 0x80000000 is set, else at its parent (+0x34), and
for each list up the parent chain: a stat whose row has +0x51 and +0x52
both 0 gets the delta added (`FUN_006250b0`: add, flag 0x20000000,
removal at 0, callback); any other is refreshed (`FUN_006266c0`). It
stops after a list with 0x2000, and after any list with 0x40000000 when
the stat's row +4 has bit 2.
d2d: not ported. Re-check: `python3 tools/ghidra/fn.py 00626920`,
`python3 tools/ghidra/fn.py 006250b0`.

### Take a list off its parent — FUN_006269f0 (stack list)

Cited, read from the decompile.
1. Parent (+0x34) cleared; when the parent is expanded and its +0x3c or
   +0x40 head is this list, the head moves to this list's next (+0x2c).
   The sibling links (+0x2c / +0x30) are spliced out and the old +4
   (the unit it hung on) is kept and cleared.
2. A list with flag 0x2000 stops here (only unlinked).
3. Otherwise, if expanded and flag 0x20000000: the keys whose
   ItemStatCost row +0x53 is set are collected from its expanded array
   into a 16-int stack buffer with no count check (more than 16 write
   past it, bugs.md #53) and each refreshed (`FUN_006266c0`, ctx 0).
4. If it had a parent: the entries' values are pushed up negated
   (`FUN_00626920(-value, owner)`; with flag 0x40000000 only the rows
   whose +4 bit 2 is clear).
5. If the old +4 was set and the callback (+0x38) exists, it is called
   with the list in ECX and EDX (unit, state): so a state's end hook runs
   only for a list that was attached.

d2d: reads the same, cited from the decompile, no oracle run, as
`components/game/fight/states.cpp:43` and
`components/rules/monsters.hpp:3869` (the callback runs when the stat
list comes off); `weapon-switch.md:142` cites it too. Step 3's buffer
is 16 ints with no count check: bugs.md #53.
Re-check: `python3 tools/ghidra/fn.py 006269f0`.

### Hang a list on a unit — FUN_00626e10 (unit, list, enable)

(d2d name `d2d_rules_socket_props`.) Cited, read from the decompile.
The unit's own list (`unit +0x5c`) must exist and be expanded, else
nothing happens. The new list is first detached (`FUN_006269f0`); the
call returns if the new list is the unit's list or one of its parents
(a loop on +0x34). A new list with flag 4 sets flag 2 on the unit's list.
A 0x2000 list goes to the head of the unit list's +0x40 chain and
stops. Any other goes to the head of its +0x3c chain (+0x34 = unit
list, +4 = the unit). If the new list is expanded its owner (+0x44) is
kept; with flag 0x20000000 the keys whose row +0x53 is set are
collected (no count check, so more than 16 write past the buffer) and
refreshed (`FUN_0045c4f0`, `FUN_006266c0`; same 16-int buffer, bugs.md
#53; the decompile does not show which list's array `FUN_00624f40` reads,
read by analogy with `FUN_006269f0` as the new list's). Then, `enable` 0: flag 0x40000000 is set and the
entries whose row +4 bit 2 is clear are pushed up (`FUN_00626920`);
`enable` nonzero: the flag is cleared and the entries are pushed up.
Callers include `FUN_00643620`, `FUN_00570590`, `FUN_005aae10`,
`FUN_005ab110` and `FUN_00557ab0`, each with enable 1.
d2d: reads the same, cited from the decompile, no oracle run, as `components/rules/rules.hpp:682` (a socketed item's
properties count as the parent's own); d2d has no flag 0x40000000.
Re-check: `python3 tools/ghidra/fn.py 00626e10`, `python3 tools/ghidra/calls.py 00626e10`.

### A new item's base stats — FUN_00557ab0 (ECX ctx, &unit, drop row, made)

(Ghidra names it `d2d_d2s_items_quest_difficulty_of`, after d2d's header
function that only mirrors its last step; the 1071 bytes do much more.)
Cited, read from the decompile; the random draws (`FUN_0045c390`,
`FUN_0045c3e0`) are not checked here.
- Item class `FUN_0062b400(unit) == 4` (gold, per `drops.hpp:114`):
  stat 0xe = `FUN_0045c390() + drop[+0xc]`, at least 1, replaced by
  `drop[+0x54]` when that is positive; set through `FUN_00627260`. A branch
  that zeroes the stat for a unit of type 0 above `FUN_00622e70` cannot
  be taken by an item unit (type 4), so it is dead here.
- Other classes: stat 0x46 (quantity) = draw + `FUN_006296f0` (at least 1,
  `drop[+0x54]` overrides), for a stackable; weapon / armor branch:
  stat 0x14 from base +0x111 (shield block), 0x43 and 0x44 as
  `-base[+0xd8]`, 0x48 durability = draw + (base +0x112 >> 1) capped 255
  and 0x49 max durability = base +0x112 capped 255. Type 0xb also runs
  `FUN_0065e8e0`.
- If `FUN_0062e8d0(unit)` is nonzero, `FUN_00628420(unit, FUN_00472280(it))`
  runs.
- With `made` and a drop row: returns `d2d_drops_made_quality(drop)`, else
  1. For a base whose row has +0x12a and +0x12b set (quest item with a
  difficulty): the item's kind-0x40 list (`FUN_00625790`) or a new one
  (`FUN_006251f0(unit +8, 0x40, 0, 4, unit +0xc)` attached by
  `FUN_00626e10`), stat 0x164 (356, d2d `kQuestItemDifficulty`) = byte
  `ctx +0x6d` (`FUN_00627150`), and item flag 0x10 (`FUN_006280d0`).
d2d: reads the same, cited from the decompile, no oracle run, on the quest difficulty stat (`rules.hpp:1850`) and the
gold draw (`drops.hpp:114`); the other branches are described in
`rules.hpp:1715-1730` and not re-checked here.
Re-check: `python3 tools/ghidra/fn.py 00557ab0`, `git grep -n 00557ab0 components`.

### Switch a state's list on or off — FUN_00643620 (stack on, EDI unit, EBX state)

Cited. Looks the unit's list for `state` (`FUN_006256b0`). `on` = 0:
if found, `FUN_006277e0` (detach through `FUN_006269f0`) then
`FUN_00626cd0` (free it); returns 0. `on` set and none found: a list is
made with `FUN_006251f0(unit +8, 0, 0, unit type, unit +0xc)` (for no
unit: 0, 0, 0, 6, -1), its state word (+0x14) set by `FUN_006252d0`, and
hung on the unit by `FUN_00626e10(unit, list, 1)`; returns the list (the
existing one when found). Its only caller is `FUN_00646d60`.
d2d: not ported (d2d has no per-state stat lists).
Re-check: `python3 tools/ghidra/fn.py 00643620`, `python3 tools/ghidra/calls.py 00643620`.

### Put a skill's state on a unit — FUN_00570590 (ECX game, source, on, state, skill, level)

Cited. The Ghidra parameter names are shifted by the register args;
read by use: `skill` indexes the Skills rows (0x23c bytes at
`tables +0xb98`, count `+0xba0`), else it returns 0. The length is
`FUN_00646ca0(source, row +0x60, skill, level)` (the skill's length
calc); the list is `FUN_006251f0(ECX +0x1c, 2, ECX +0xa8 + length,
source type, source +0xc)`: flags 2, expiry = ECX +0xa8 plus the length
(not clamped to 5, unlike hits 16 and 19 below). Then its state word is
`state` (`FUN_006252d0`), its removal callback (+0x38) `FUN_0056e900`
(`FUN_00625ce0`), it is hung on `on` (`FUN_00626e10(on, list, 1)`), the
state bit is set (`FUN_00639db0(on, state, 1)`), an event 0xc is queued
for the expiry frame (`FUN_005417d0`) and the skill's aurastats are
written to the list (`FUN_005c6cc0`). Returns the list. Only caller
`FUN_005d26f0`. Its decompile has no lookup of a list already holding the state
(hits 16 and 19 do and renew it).
d2d: not ported by name; `skills.md` describes the state side per skill.
Re-check: `python3 tools/ghidra/fn.py 00570590`, `python3 tools/ghidra/calls.py 00570590`.

### Missile hit 16 — FUN_005aae10 (ECX game, EDX missile, stack target)

Cited. Entry 16 of the pSrvHitFunc table (0x73c840, 4-byte entries:
0x73c880 holds this address); `skills.md` lists hits 9-21 but not this
one. Same shape as hit 19 (`FUN_005ab110`, `skills.md:955`): skill id
`FUN_0064a280`, level `FUN_0064a210`, a source unit `FUN_00552fd0`
(returns 1, "not handled", when the skill or the unit is missing), the
skill's auratargetstate (+0x82, must be below `tables +0xc4`), a length
at least 5, the target's list for that state (`FUN_006256b0`) or a new
one (flags 2, expiry = game +0xa8 + length, callback `FUN_0056e900`,
`FUN_00626e10(target, list, 1)`, `FUN_00639db0`), then aurastats
(`FUN_005c6cc0`), `FUN_00639e30(target, state, 1)`, the expiry renewed
(`FUN_006260b0`) and event 0xc queued. Differences from hit 19:
- the length is the skill's calc at **+0x144** (calc4, `skills.md:79`),
  hit 19 uses +0x60 (auralen);
- no target (stack arg 0) returns 0 after the row and unit checks, hit 19
  returns 1;
- it returns 0 when done, hit 19 returns 3 (damage, end).
Which missile record carries hit 16 was not looked up.
d2d: not ported (no mention of 16 in `skills.md`, `ai.hpp`, `fight.hpp`).
Re-check: `python3 tools/ghidra/fn.py 005aae10`, `python3 tools/ghidra/fn.py 005ab110`.

### Missile hit 19 — FUN_005ab110

Entry 19 of the same table (0x73c88c). Read against `skills.md:955`: the
state is skill +0x82, the length the +0x60 calc (`FUN_00646ca0`) with a
floor of 5, the list found or made as for hit 16 (flags 2, callback
`FUN_0056e900`), aurastats by `FUN_005c6cc0`, expiry renewed by
`FUN_006260b0`, event 0xc queued, and it returns 3; a missing skill,
source unit, target or an out-of-range state returns 1. The points read in
`skills.md:955` and `ai.hpp:969` (`foe.marked`, hit 19) read the same, cited from the decompile, no
oracle run. What the
doc leaves out and this adds: the state's stat list is flagged 2, has the
removal callback `FUN_0056e900`, and the new state bit is also set in a
second mask (`FUN_00639e30`) for hit 19 and 16 but not in
`FUN_00570590`.
d2d: reads the same, cited from the decompile, no oracle run.
Re-check: `python3 tools/ghidra/fn.py 005ab110`, `git grep -n FUN_005ab110 components docs/research/re/skills.md`.
