# Item accessors and helpers

These functions take an item unit. They require a non-null unit of type 4
with a non-null item-data pointer at unit `+0x14`; rejected calls do not
write item data. `FUN_0048c060` uses the quality to select item-name
formatting, and the call sites cited below identify fields where a semantic
name is known.

The 16-bit getters write only `AX`, zeroing it on the rejection paths above.
On success the upper `EAX` bits come from the item-data pointer. A wrong-type
call instead retains the unit pointer's upper bits; null-unit and null-data
calls return zero.

| Address | Traced claim |
|---|---|
| `FUN_00627e70` | Read the 32-bit quality at item data `+0x00`; return 2 (normal) for a rejected call. |
| `FUN_00627ea0` | Write the 32-bit quality at item data `+0x00`; do nothing for a rejected call. |
| `FUN_00627ec0` | Read MagicPrefix ID `i` (0..2) from item data `+0x38 + 2*i`. `FUN_0048c060` formats slot 0 through `FUN_00633ee0`; `FUN_0055f5d0` compares slots 0, 1 and 2. |
| `FUN_00627ef0` | Write the low 16 bits of a value to MagicPrefix slot `i` at item data `+0x38 + 2*i`; do nothing for a rejected call. |
| `FUN_00627f20` | Read the 1-based AutoMagic ID at item data `+0x36`. `FUN_0062b5b0` includes it in required-level calculation through `FUN_00633ee0`. |
| `FUN_00627f50` | Write the low 16 bits of a value to the AutoMagic ID at item data `+0x36`; do nothing for a rejected call. |
| `FUN_00627fb0` | Write the low 16 bits of a value to MagicSuffix slot `i` (0..2) at item data `+0x3e + 2*i`; do nothing for a rejected call. `FUN_0055f5d0` compares suffix slots 0, 1 and 2 through `FUN_00627f80`. |
| `FUN_00627fe0` | Read the rare-name prefix ID at item data `+0x32`. `FUN_0048c060` formats rare and crafted names through `FUN_00634260`. |
| `FUN_00628010` | Write the low 16 bits of a value to the rare-name prefix ID at item data `+0x32`; do nothing for a rejected call. |
| `FUN_00628040` | Read the rare-name suffix ID at item data `+0x34`. `FUN_0048c060` formats rare and crafted names through `FUN_00634260`. |
| `FUN_00628070` | Write the low 16 bits of a value to the rare-name suffix ID at item data `+0x34`; do nothing for a rejected call. |
| `FUN_00628110` | Read the 32-bit item flags at item data `+0x18`; return 0 for a rejected call. |
| `FUN_00628170` | For a mask and boolean-like third argument, nonzero sets `item_data[+0x14] \|= mask` and zero applies `item_data[+0x14] &= ~mask`; do nothing for a rejected call. |
| `FUN_006281e0` | Read the signed item level at item data `+0x2c`, overwriting values below 1 with 1; return 1 for a rejected call. `FUN_00628220` is the corresponding item-level setter. |
| `FUN_00628280` | Write the low byte of a value to the inventory page at item data `+0x45`; do nothing for a rejected call. `FUN_00628250` is the corresponding getter; `tools/emu/auto_belt.py` exercises this field as the inventory page. |
| `FUN_00628320` | Write the low byte of a value at item data `+0x47`; do nothing for a rejected call. `FUN_006282f0` is the corresponding getter. |
| `FUN_00628340` | Return `item_data + 0x4a`, the ear-name string pointer; return 0 for a rejected call. |
| `FUN_00628370` | Copy a NUL-terminated source string to item data `+0x4a`, including the terminator; do nothing for a rejected call. `FUN_00567070` copies ear names through this setter. |
| `FUN_006283a0` | Read the ear-class byte at item data `+0x48`, writing only `AL`; rejected calls set `AL=1`. Successful calls retain the item-data pointer's upper 24 bits, wrong-type calls retain the unit pointer's upper 24 bits, and null-unit or null-data calls return 1. |
| `FUN_006283d0` | Write the low byte of a value to the ear-class byte at item data `+0x48`; do nothing for a rejected call. `FUN_00567070` sets the ear class through this function. |
| `FUN_00628420` | Write the low byte of a value to the picture ID at item data `+0x49`; do nothing for a rejected call. `FUN_00557a70` sets it from the item graphics code. |

## Item-record accessors and helpers

`FUN_006335f0` resolves the unit class at unit `+0x04` to the item record used
by the record accessors below. The oracle substitutes a synthetic record.

| Address | Traced claim |
|---|---|
| `FUN_00628500` | Read the 32-bit value at item record `+0xc0`; return 0 for a null unit, a wrong unit type, or a failed class lookup. |
| `FUN_00628590` | Return the 32-bit item code at item record `+0x80` for a valid item unit with a successful class lookup. `FUN_00483c50` compares this field with the Horadric Cube code `box `. |
| `FUN_006286c0` | Copy item record bytes `+0x10f` and `+0x110` to the two output pointers; leave them unchanged for a null unit, a wrong unit type, or a failed class lookup. The function returns with `RET 0x14`, so the oracle supplies the two additional assert arguments used by its call sites. |
| `FUN_00628700` | Return the signed 16-bit item-type index at item record `+0x11e` for the class `FUN_006335f0` resolves. An unknown class (record 0) reports string id `0x68d` through `FUN_00408090` / `FUN_00408a60` and exits with `FUN_00681e09(-1)`; there is no unit-type check. Callers: `FUN_0062b400`, `FUN_00629a90`, `FUN_00629bb0`, `FUN_0055a6d0`, `FUN_00558640`. d2d: reads the same, cited from the decompile, no oracle run (the `type` column of the item table is the ItemTypes index; `rules.hpp:749` reads it for Repair). |
| `FUN_00628740` | Read the base item level at item record `+0xfd`, writing only `AL` and retaining the record pointer's upper 24 bits. `FUN_005c1560` uses it as a floor when selecting an affix level. |
| `FUN_006287a0` | Return the bitwise AND of its two 32-bit arguments. |
| `FUN_006287b0` | Update the 32-bit value at the first argument: a nonzero third argument sets the bits in the second argument with `\|=`, while zero clears them with `&= ~mask`. |
| `FUN_006287d0` | For a valid item unit and class lookup, return item record `normcode` at `+0x84` when nonzero, otherwise `code` at `+0x80`. Invalid inputs take assertion paths. `FUN_0062efb0` uses this fallback when pricing gambled items. |
| `FUN_00628830` | Clamp an affix required level using a custom fastcall convention: `ECX` is the affix record, `EDX` the optional player unit, and `EAX` the initial value. A null affix preserves `EAX`. Select byte `+0x65`, or byte `+0x68` when byte `+0x67` is not `0xff`, the player is nonnull, and that byte equals the player class at `+0x04`; raise a signed initial value to the selected byte, but preserve higher values. `FUN_0062b5b0` calls this helper. |
| `FUN_00628860` | Resolve the data-table state through global pointer `0x744304`; index the item-type table at state `+0xbf8`, with count at `+0xbfc` and record stride `0xe4`. Return the zero-extended byte at item-type record `+0x10`, or 0 for a negative index, an index greater than or equal to the count, or a computed null record. `FUN_0055a6d0` uses the result when rerolling classic drops. |
| `FUN_0062b400` | Return the item-type index of an item unit: `FUN_00628700(unit +0x04)` when the unit type (`+0x00`) is 4, else 0. A null unit reports string id `0x67d` and exits with `FUN_00681e09(-1)`. Note the 0 for a non-item is also ItemTypes row 0. d2d: reads the same, cited from the decompile, no oracle run (`rules.hpp:749` comment, `type_mends`). |
| `FUN_0062e830` | Return the byte at ItemTypes record `+0x12` (table at state `+0xbf8`, count `+0xbfc`, stride `0xe4`, as `FUN_00555c90`) for an item unit's type index (`FUN_0062b400`); 0 for a null or non-item unit, an out-of-range index or a null record. Its only caller `FUN_00580030` (ammo run out, `.\PLAYER\PlrModes.cpp`) saves the answer and, when nonzero, ends by calling `FUN_0057ff70`, which resolves a unit with `FUN_00623410`/`FUN_00552f60` and, if `FUN_0055d710` finds a slot, moves it there (`FUN_00562a30`). The sibling `FUN_0062e7e0` reads `+0x11` the same way and gates the same-code search loop (`FUN_0063cd80`) earlier in the function. Whether the columns are Reload (`+0x11`) and ReEquip (`+0x12`) is an unverified guess from ItemTypes.txt's column order, not read from game.exe. d2d: `out_of_ammo` (`components/game/fight.hpp:862`) models the `+0x11` search branch; the `+0x12` branch (`FUN_0057ff70`) is not ported. Reads the same, cited from the decompile, no oracle run. |

Re-check these claims against game.exe 1.14d:

```sh
cd tools/emu && uv run python item_accessors.py
```

## Clearing transient unit and item flags (`FUN_005979b0`)

`FUN_005979b0` (fastcall, the unit is the second argument, in `EDX`) does three
things in order:

1. A nonnull unit has bits `0x14` cleared: `unit[+0xc8] &= 0xffffffeb`.
2. For each of `DAT_00738cc4` entries of the table at `DAT_00738c70`, it calls
   `FUN_00628170(unit, entry, 0)`: that clears the entry's bits in
   `item_data[+0x14]` (see the accessor table above).
3. For each of `DAT_00738c6c` entries of the table at `DAT_00738c4c`, it calls
   `FUN_006280d0(unit, entry, 0)`: that clears the entry's bits in
   `item_data[+0x18]`, the item flags.

Both setters ignore a null unit, a non-item unit or null item data, so the
loops are harmless for a non-item unit. Step 1 is not guarded by the type.

Callers: `FUN_00597b00` (source file `INVENTORY/InvMode.cpp`, per
`game-srcfiles.tsv`) calls it for each item in a unit's inventory list, and
again for each item in the contents (`unit[+0x60]` list) of an item that has
`unit[+0xc8] & 1`; `FUN_0055a2a0` (item copy) also calls it.

Not read: the entries of the two tables and their counts. Nothing in
game.exe's code writes `DAT_00738cc4`, `DAT_00738c6c` or the tables
(`game_all.c` has no other reference), so they are initialised data, and the
byte values were not dumped. Which flags are cleared is therefore open.
Status: cited, not proven; no oracle was run. d2d: not ported (no mention of
`FUN_005979b0` in `components/` or `apps/`).

## A belt put on or taken off (`FUN_005608c0`)

The server runs it in two cases:

- A belt swapped in from the cursor (C->S 0x1d, `54af50` -> `FUN_00560f00`).
  After the swap passes `FUN_0063de60`, an incoming item of type 0x13
  (`FUN_0062b400`) calls it with the new belt.
- A belt taken to the cursor (C->S 0x1c, `54aec0` -> `FUN_00560cd0`). It
  calls it with no belt, so Belts.txt row 2 (one row) is used.

It reads the belt's row (`FUN_00621ed0`, its Belts.txt index;
`FUN_00660cb0`) for `numboxes`. Then, for box 0 up to 15, each item in a
box at or past `numboxes`:

1. Comes out of the belt (`FUN_0063ad90`), and the client is told
   (`FUN_0053eed0`).
2. Gets inventory page 0 (`FUN_00628280`) and goes to the inventory's
   free spot (`FUN_0063b950`, a player's best fit).
3. With no room, drops at the nearest free spot to the player
   (`FUN_00555da0`, `FUN_00558aa0`).

The swap itself checks nothing about the rows. The one belt check in the
0x1d handler, `FUN_00567840`, refuses the swap only while a trade with
another player is open (interaction type 0, `FUN_00554100`) and the belt
holds an item (`FUN_0063e020` page 2).

d2d: `rules::spill_belt`, called by the World's `cmd::ToBody` (belt
slot) and `cmd::ToCursor` (the worn belt).

| Address | Traced claim | Status |
|---|---|---|
| `FUN_00451f50` | `__fastcall`: return `*(int*)(p + 0xc)`, or `0xffffffff` for a null pointer. A null-safe read of a field at `+0xc` (used by `FUN_0048a990`, `FUN_0055a2a0`, `FUN_0045f190`, `FUN_0049a690`, `FUN_004d6d40`, `FUN_0052c410`; the struct was not identified). `tools/emu/_hire_probe.py:25-26` replaces it with a zero-returning hook. | not verified by this probe; not ported |
| `FUN_00557fd0` | `__fastcall` wrapper: `FUN_005538d0(arg2)` then `FUN_00555600()`; no logic of its own. Called by `FUN_00597b00` when `FUN_00628140(item, 1)` is nonzero, and by `FUN_00535f30`, `FUN_00536580`, `FUN_005466b0`, `FUN_0055c850`, `FUN_0055d0d0`. What the two callees do was not read. | not ported |
| `FUN_00597b00` | Source file `.\INVENTORY\InvMode.cpp` (`game-srcfiles.tsv`). `__fastcall`, the unit is the second argument. Takes `unit[+0x60]` (its inventory); if null, does nothing. Otherwise `FUN_00621000(unit, 0)`, then walks the inventory list (`FUN_0063cbb0` head, `FUN_0063cd20` next, `FUN_0063cd40` payload, `FUN_00552f60` payload to unit; skips null units). Per item: if `FUN_00628140(item, 0x10)` or `(item, 0x4000)` is set, `FUN_00627d70(item, 0)`; if `(item, 0x20)` is set and `FUN_006280a0(item, 0x80, 0x180, ".\\INVENTORY\\InvMode.cpp")` is nonzero, `FUN_00627d70(item, 0)`; then `FUN_005979b0(item)`. If `item[+0xc8] & 1` (it holds a socket inventory): `FUN_00621000(item, 0)`, take `item[+0x60]` (null: log via `FUN_00408090(0x1a4)`/`FUN_00408a60` and exit `FUN_00681e09(-1)`), call `FUN_005979b0` on each child whose payload converts to a non-null unit (null ones are skipped), then free that inner list with `FUN_0063cbd0`. Finally if `FUN_00628140(item, 1)` is set, `FUN_00557fd0()`. After the loop it frees the outer list with `FUN_0063cbd0`. Sibling `FUN_00597890` (`stubs.md:54`) does the same walk with `FUN_005973f0`. The decompiler drops the fastcall arguments of the `FUN_005979b0`/`FUN_00557fd0` calls, so what is passed is inferred, not read from asm. Callers: `FUN_00539760`, `FUN_00553220`. | not ported |
| `FUN_0063cbb0` | Inventory list head: return `inv[+0x2c]` if `inv` is non-null and `inv[0] == 0x01020304` (the inventory magic), else 0. | not ported |
| `FUN_0063cbd0` | Free an inventory's node list: if `inv` is non-null with the magic, walk from `inv[+0x2c]` following `node[+4]`, calling `FUN_0040b480(".\\INVENTORY\\Inventory.cpp", 0xcbb, 0)` per node (a free; the node argument goes in a register the decompiler dropped), then zero `inv[+0x2c]` and `inv[+0x30]`. | not ported |
| `FUN_0063cd20` | List node next: return `node[+4]`, or 0 for a null node. | not ported |
| `FUN_0063cd40` | List node payload: return `node[0]`, or 0 for a null node. | not ported |

## Into a hand (`FUN_0063de60` → `FUN_0063dd90`)

For body slots 4 and 5, `FUN_0063de60` asks `FUN_0063dd90` with the item
on the cursor, the one in the clicked hand and the other hand's. With an
item on the cursor:

| Clicked hand | The other hand's item | Code | Client sends | Server |
|---|---|---|---|---|
| empty | none, or goes with it (`FUN_0063dbc0`) | 1 | 0x1a | `FUN_005606b0`: equipped |
| empty | doesn't go with it | 2 | 0x1b | `FUN_00563d20`: the other hand's to the cursor, then equipped |
| full, a stack it joins (`FUN_0062c850`) | (any) | 6 | 0x21 | `FUN_0055e7c0` → `FUN_0055e590`: merged |
| full | none, or goes with it | 5 | 0x1d | `FUN_00560f00`: swapped |
| full | doesn't go with it, and fits the inventory (`FUN_0063cb00` → `FUN_0063b850`) | 7 | 0x1e | `FUN_00561220`: the other hand's to the inventory (`FUN_0063b950`), this hand's to the cursor, then equipped |
| full | doesn't go with it, no room | 0 | nothing | the client's class voice (`FUN_004cb9c0`): 0x13 "impossible"; 0x14 "can't use yet" only when a retry without the requirements (`FUN_0063de60` with 1) passes and the item's class is any or the player's (`FUN_0062c0b0`) |

With nothing on the cursor, a hand holding an item gives code 3 (C->S
0x1c, picked up). An empty hand beside a two-hander (Items +0x11c,
`FUN_006289c0`; read from the 1.14d tables: bax, 2hs, sst, sbw, 9gi 1;
axe, ssd, jav, buc 0) gives code 4: the client sends 0x1c with the
other hand's slot, so the two-hander is picked up. d2d:
`rules::two_hander_beside`, used by the cursor.

Code 6 is asked first, after the requirements (`FUN_0063de60`'s
`FUN_0062eaf0`). `FUN_0062c850`: the same class, quality and file index,
a Stackable base (`FUN_006289f0`), both ethereal or neither, plain
quality (`FUN_0062a2f0`), damage stats 21-24 and 159 / 160 equal, no
sockets (d2d `rules::stacks_with`). `FUN_0055e590` merges: past the
hand's most (`FUN_006295b0`) the hand's stack fills and the rest stays
on the cursor; else the hand's takes it all, the lower durability of the
two (stat 0x48, when the cursor's has durability, `FUN_00629930`), and
the cursor's is freed. Two items of ItemTypes 0x12 also move a skill's
count (`FUN_0055c070`), which a hand's stack never is. d2d:
`rules::hand_stack`, `rules::stack_onto`.

`FUN_0063dbc0` (d2d `rules::hands_ok`) passes:
- a bow beside its quiver;
- not a quiver beside anything else;
- a two-hander alone, unless the wearer holds it in one hand
  (`FUN_0062a1e0`);
- two weapons (ItemTypes 0x2d) for a Barbarian, or for an Assassin with
  two claws (0x43).

The client's hand handlers are `FUN_00490780` (right, slot 4) and
`FUN_00490ba0` (left, 5). Each sends the packet with `FUN_004786a0`, and
puts the moving items in state 0x36 (`FUN_004c21f0`) until the server
answers.

d2d: `rules::equip`, called by the World's `cmd::ToBody` with the class's
inventory grid. A joined client sends 0x1b / 0x1e the same way
(`c2s::equip`'s `other_off`), and nothing when the move is refused.

## Requirements kept (`FUN_0055dbc0`, flag 0x4000)

`FUN_0055dbc0(player, …)` (ItemMode.cpp, the item's stats redone) runs
at the end of ItemMode.cpp's server moves: a body item off
(`FUN_00560cd0`) or on (`FUN_005606b0`, `FUN_00563d20`, `FUN_00560f00`,
`FUN_00561220`), into the inventory (`FUN_00560200`), a pickup
(`FUN_00563560`), the weapon switch (`FUN_005616a0`), a break
(`FUN_0055f850`), a mend (`FUN_0055f900`), a stack refill
(`FUN_00562a30`), the corpse (`FUN_00562f30`); and on a join
(`FUN_0052d440` → `FUN_0055df00`, client states 3 and 5). In order:

1. The inventory list's items with item data `+0x69` 1 (`FUN_0063e020`)
   that `FUN_0062ff70` takes (a charm, ItemTypes 0xd, not broken nor
   flagged, item data `+0x45` 0, its requirements met) whose stats
   aren't on (`FUN_00625820`) and that pass `FUN_0055db00`: flag 0x4000
   cleared, `FUN_0055d970` (stats on).
2. Body slots 1..10 (`FUN_0063bde0`; the switch's 0xb / 0xc aren't
   walked), in order: an item not broken nor flagged (or with its stats
   on) that fails `FUN_0055db00` gets 0x4000 (`FUN_006280d0`), then
   `FUN_0063d2b0`, and in item mode 1 its stats come off
   (`FUN_0055c730`). The checks see the stats as they are then: an item
   flagged earlier in the pass no longer counts, one still unflagged
   counts toward its own requirements.
3. Repeated while one changed: an item in slots 1..10, not broken,
   flagged (or its stats off), that passes `FUN_0055db00`: 0x4000
   cleared, `FUN_0063d1d0` (not for 0xb / 0xc), its stats on
   (`FUN_0055c2c0`). One item's stats can let another pass.

`FUN_0055db00(player, item)`: `FUN_0062eaf0`'s requirements (identified
flag 0x10; strength, dexterity and level, with the player's stats as
they stand; the class), and when the item's type has a Quiver
(`FUN_0062e740`: ItemTypes `+0xe`, the 1.14d rows `bowq` / `mboq`
`bow`, `xboq` / `mxbq` `xbow`), the right hand's item (or the left's,
when the quiver is the right's) must be of that type (`FUN_00629bb0`).
A quiver with no bow beside it is flagged.

d2d: the flag in the item's flag word (`Item::flags`, written back to
the d2s as read). `rules::requirements_hold` (`FUN_0055db00`),
`rules::settle_requirements` (passes 2 and 3), run by
`game::settle_requirements` with `game::wearer` after each of the
World's item commands (`cmd::ToBody`, `ToCursor`, `ToGrid`,
`SwapWeapons`, `Buy`, `Sell`, `Repair`), a pickup, `Fight`'s breaks and
refill, the corpse and `World::take_character`. A flagged item's stats
are off like a broken one's (`game::gear_active`, `panel_stats`,

## Item bitstream loader (`FUN_00558cb0` chain)

Traced from the seed `FUN_00558cb0` (callers `FUN_005335e0`, `FUN_00541990`,
`FUN_0055a2a0`, `FUN_0056ace0`). Cited (decompile only, no oracle run).
Registers are hidden in the decompile, so the argument order below is the
decompile's.

### The bit reader (`FUN_00410e40`, `FUN_00410e90`)

- `bitreader_init(reader, buffer, bytes)` fills a 5-word struct: +0 the
  buffer, +4 the length in bits (bytes * 8), +8, +0xc, +0x10 zero. A null
  reader hits the assert-id path (`FUN_00408090(0x13c)`, `FUN_00681e09`).
- `bitreader_bytes_used(reader)` is word +8, plus one when word +0xc is
  nonzero: the bytes consumed, a partly read byte counted whole.
- `FUN_00411020(reader, n)` reads n bits; `FUN_0062e430` reads the `JM` mark
  (16 bits) through `FUN_00411030(reader, 0x10)`, a second reader entry whose
  difference from `FUN_00411020` is not traced here.
- d2d: not ported by name; `d2s::Bits` (components/d2s/d2s_items.hpp) is the
  same idea. Agrees: its `read(n)` is bit by bit and the loader's `JM` check
  is a 16-bit read.
  same idea. It reads the same: its `read(n)` is bit by bit and the loader's
  `JM` check is a 16-bit read (cited from the decompile, no oracle run).

### Arming the replenish events (`FUN_00558530`, `FUN_00558580`)

Two twins, the same body but for the stat. Cited.

| Function | Stat read (`FUN_00625480(item, stat, 0)`) | Meaning |
|---|---|---|
| `FUN_00558530` | 0xfc | item_replenish_durability |
| `FUN_00558580` | 0xfd | item_replenish_quantity |

With a nonzero rate N and no pending event of type 3 (`FUN_005415a0(3)`
returns 0), each queues one event of type 3 at `game +0xa8` (the frame)
`+ 2500 / N + 1` (`FUN_005417d0`). 2500 is 100 seconds at 25 frames a
second: a rate of N means one point every 100 / N seconds. The stat ids
are named after d2d's `kReplenishDurability` / `kReplenishQuantity`
(components/rules/rules.hpp:526); that reads the same, cited from the decompile, no oracle run.

Callers (each re-arms after an item is created or moved): `FUN_00558cb0`
(a new item), `FUN_00558d90`, `FUN_00562660` (after a socketed runeword is
made, `FUN_006600a0`), `FUN_00562f30` (an item put on a corpse-take pass,
twice) and `FUN_0056c310`; `FUN_00559e30` calls the durability twin only.

d2d: not ported. d2d reads the two stats for prices (`rules.hpp:557`,
`:818`) but nothing ticks them: no event 3 for an item, so a replenishing
belt potion / javelin / durability is not regrown (no d2d code does it). Not a
disagreement, a gap: it needs the event-3 handler for items (not traced:
the handler is whatever event 3 calls on an item unit).

### The item maker (`FUN_00558cb0`)

Cited. Fastcall: ecx the game, edx the second argument `FUN_00555230`
takes; the stack carries the bitstream (buffer, length, flag), `param_6` the
record ref, `param_7` an out word and `param_8` the version.

1. No game (ecx 0): return 0.
2. The record ref `*param_6` must be >= 0 and below the item-table count
   (`FUN_00633590` returns `&DAT_0096ca58`, whose first word is the count).
   Otherwise `*param_7 = 0` and the result is 0.
3. `FUN_00555230(short ref[1], short ref[+6], game, edx, 1, char ref[2], 0)`
   makes the item unit; 0 returns 0.
4. `FUN_0062e430(unit, buffer, length, flag, 0, version, &err)` fills it from
   the bitstream; its result (bytes used) goes to `*param_7`.
5. If it used 0 bytes or `err` is set, the unit is freed (`FUN_00555600`)
   and 0 returned.
6. Else item flag 0x80000 is set and 0x2000 cleared (`FUN_006280d0`, item
   data +0x18), the replenish events are armed (`FUN_00558530`,
   `FUN_00558580`) and the unit is returned.

Note the decompile reuses `param_6` as the error word after the call (its
address is passed as `&err`); the ref struct is read before that.

d2d: not ported by name. The nearest is `d2s::item` / `parse_net_item`,
which parse the stream into an `Item` value, not a unit. Agrees on the
stream; the replenish arming in step 6 has no d2d counterpart (see above).

### The stream entry (`FUN_0062e430`)

Cited. `item_read_bitstream(unit, buffer, bytes, check_jm, sockets_out,
version, err_out)`:

1. `FUN_00410e40` starts a reader. With `check_jm` set, 16 bits must be
   `0x4d4a` ("JM"), else `*err = 1` and 0 is returned. `*err = 0` otherwise.
2. 32 flag bits are read. Bit 0x2000000 (the header-only flag) is kept apart
   and stripped from the flags.
3. For an item unit (type 4 with data), item data +0x18 (the flags) is
   cleared, the stream flags OR-ed in, then bit 0x80000 cleared; +0x2c is
   set to 1 and the halfwords +0x38, +0x3e, +0x3a, +0x40, +0x3c, +0x42,
   +0x32, +0x34 zeroed.
4. Flag 0x200000 clear: `FUN_0062cbe0(unit, reader, check_jm, header_only,
   sockets_out, version)`. Set: `FUN_0062a970(check_jm, version)` (the
   reader is passed in a register) and `*sockets_out = 0` when given.
5. A result below 1 sets `*err = 1`. The result is `FUN_00410e90(reader)`,
   the bytes used.

d2d: reads the same as `d2s::item` (components/d2s/d2s_items.hpp:249; cited from the decompile, no oracle run) and
net-packets.md "Items": JM only for saves, header-only stops after the code,
compact has no socket count. The `check_jm` argument is the d2d `net == null`
case; `FUN_00558cb0` passes its own flag word there. Not recorded in d2d:
the game also zeroes the unit-side fields in step 3 (d2d builds a fresh
`Item`).

### The stream entry (`FUN_0062e430`)

Cited. `item_read_bitstream(unit, buffer, bytes, check_jm, sockets_out,
version, err_out)`:

1. `FUN_00410e40` starts a reader. With `check_jm` set, 16 bits must be
   `0x4d4a` ("JM"), else `*err = 1` and 0 is returned. `*err = 0` otherwise.
2. 32 flag bits are read. Bit 0x2000000 (the header-only flag) is kept apart
   and stripped from the flags.
3. For an item unit (type 4 with data), item data +0x18 (the flags) is
   cleared, the stream flags OR-ed in, then bit 0x80000 cleared; +0x2c is
   set to 1 and the halfwords +0x38, +0x3e, +0x3a, +0x40, +0x3c, +0x42,
   +0x32, +0x34 zeroed.
4. Flag 0x200000 clear: `FUN_0062cbe0(unit, reader, check_jm, header_only,
   sockets_out, version)`. Set: `FUN_0062a970(check_jm, version)` (the
   reader is passed in a register) and `*sockets_out = 0` when given.
5. A result below 1 sets `*err = 1`. The result is `FUN_00410e90(reader)`,
   the bytes used.

d2d: reads the same as `d2s::item` (components/d2s/d2s_items.hpp:249; cited from the decompile, no oracle run) and
net-packets.md "Items": JM only for saves, header-only stops after the code,
compact has no socket count. The `check_jm` argument is the d2d `net == null`
case; `FUN_00558cb0` passes its own flag word there. Not recorded in d2d:
the game also zeroes the unit-side fields in step 3 (d2d builds a fresh
`Item`).

### The compact reader (`FUN_0062a970`)

Cited. Args: `param_1` the is-save word (`check_jm` above), `param_2` the
version the caller passes (0x60 in `FUN_00558cb0`'s callers); the item unit
comes in a register. Read in this order:

| Bits | Into |
|---|---|
| 10 | item version (`FUN_0062a6c0`, item data +0x30) |
| 3 | location: 3 or 5 (ground / socketed) read x, y as 16 + 16 bits (`FUN_00620470`, `FUN_006204c0`); else 4 bits to data +0x44, x 4, y 4, 3 bits - 1 to data +0x45 |
| (ear flag 0x10000 set) | 3 bits ear class, 7 bits level to +0x48, then 7-bit name characters to +0x4a until a zero |
| 32 | otherwise the item code, then `FUN_00633680` finds the base row into unit +4 |
| 1, then 32 or 12 | if the item is of type 4 (`FUN_00629bb0(unit, 4)`, gold): the amount, stat 0xe through `FUN_00627260` |
| (type 0x16, codes `tsc ` / `isc `) | `tsc `: data +0x3e = 0; `isc `: `FUN_00627fb0(unit, 1, 0)` |
| `stat bits` | version > 0x5c, item row +0x12a and +0x12b nonzero (quest difficulty check): ItemStatCost row 0x164's save bits (row +0x19), minus its save add (+0x1c), shifted by the row's +0x18 & 31, stored as stat 0x164. Fewer than 0x165 rows exits with assert id 0x1607 |
| 1 (+ 32, 32, and a third 32 when version > 0x5d) | is-save only and version > 0x56: when the bit is set, realm words via `FUN_00629ea0` |

It ends with unit +0x28 = 0, `FUN_00650e40`, item data +0x2c = 1 and data
+0 = 2, and returns 1 (-1 when the base row isn't found).

d2d: `d2s::item`'s simple branch (components/d2s/d2s_items.hpp:284) reads the same, cited from the decompile, no oracle run,
on the version gates (> 92 quest difficulty, > 86 realm, > 93 third word), the
32/12 gold read and the ear fields. Two differences, neither one a save
bug: d2d reads gold only on the wire (`net`) and only for code `gld`, where
the game tests the item type (saves never hold gold), and d2d applies no
`<< (row +0x18)` shift to the quest difficulty, which is right only while
that column of row 0x164 is 0 (not checked here; needs Itemstatcost.txt).
The `tsc ` / `isc ` fix-ups are not ported.

### The compact reader (`FUN_0062a970`)

Cited. Args: `param_1` the is-save word (`check_jm` above), `param_2` the
version the caller passes (0x60 in `FUN_00558cb0`'s callers); the item unit
comes in a register. Read in this order:

| Bits | Into |
|---|---|
| 10 | item version (`FUN_0062a6c0`, item data +0x30) |
| 3 | location: 3 or 5 (ground / socketed) read x, y as 16 + 16 bits (`FUN_00620470`, `FUN_006204c0`); else 4 bits to data +0x44, x 4, y 4, 3 bits - 1 to data +0x45 |
| (ear flag 0x10000 set) | 3 bits ear class, 7 bits level to +0x48, then 7-bit name characters to +0x4a until a zero |
| 32 | otherwise the item code, then `FUN_00633680` finds the base row into unit +4 |
| 1, then 32 or 12 | if the item is of type 4 (`FUN_00629bb0(unit, 4)`, gold): the amount, stat 0xe through `FUN_00627260` |
| (type 0x16, codes `tsc ` / `isc `) | `tsc `: data +0x3e = 0; `isc `: `FUN_00627fb0(unit, 1, 0)` |
| `stat bits` | version > 0x5c, item row +0x12a and +0x12b nonzero (quest difficulty check): ItemStatCost row 0x164's save bits (row +0x19), minus its save add (+0x1c), shifted by the row's +0x18 & 31, stored as stat 0x164. Fewer than 0x165 rows exits with assert id 0x1607 |
| 1 (+ 32, 32, and a third 32 when version > 0x5d) | is-save only and version > 0x56: when the bit is set, realm words via `FUN_00629ea0` |

It ends with unit +0x28 = 0, `FUN_00650e40`, item data +0x2c = 1 and data
+0 = 2, and returns 1 (-1 when the base row isn't found).

d2d: `d2s::item`'s simple branch (components/d2s/d2s_items.hpp:284) reads the same, cited from the decompile, no oracle run,
on the version gates (> 92 quest difficulty, > 86 realm, > 93 third word), the
32/12 gold read and the ear fields. Two differences, neither one a save
bug: d2d reads gold only on the wire (`net`) and only for code `gld`, where
the game tests the item type (a save is not expected to hold gold; unchecked), and d2d applies no
`<< (row +0x18)` shift to the quest difficulty, which is right only while
that column of row 0x164 is 0 (not checked here; needs Itemstatcost.txt).
The `tsc ` / `isc ` fix-ups are not ported.

### Recharging charged skills (`FUN_0055fe80`)

Cited. `(owner in ecx, edx unused, item)`; 0 unless the item is an item unit.

1. `FUN_006261d0(item, 0xcc, buf, 0x40)` lists the item's stat 0xcc
   (item_charged_skill) entries: each a param (skill id and level, split by
   the shift / mask at tables +0xc6c / +0xc70) and a value.
2. Per entry: current = value & 0xff, max = value >> 8. If current < max,
   `FUN_0065c940(item, param >> shift, param & mask, max)` writes
   `max << 8 | min(max, request)`, i.e. max / max, to the entry (it returns
   0 and writes nothing when max is not 1..255, and tries the item's
   socketed items when the item itself has no such entry).
3. The result is 1 if any entry was below max (set whether or not
   `FUN_0065c940` wrote it).
4. When the owner is a player (type 0) with a client (`FUN_005531c0`), the
   new value is also sent: `FUN_0053d130(1, 0xcc, ...)`.

Callers: `FUN_00562660` (when its flag argument is set, after a socket),
`FUN_00565ab0` (the cube's recharge flag 0x400), `FUN_005761c0` (repair),
`FUN_005e0070` (the Torch / Standard drop).

d2d: reads the same, cited from the decompile, no oracle run. `rules::recharge` (components/rules/rules.hpp:731) sets the
stats it checks that are 204 below max to `max << 8 | max` and returns whether any was; the
cube (cube.hpp:362) and the repair use it. d2d omits the client stat packet
(step 4) and the 1..255 guard, neither visible in play.

### Recharging charged skills (`FUN_0055fe80`)

Cited. `(owner in ecx, edx unused, item)`; 0 unless the item is an item unit.

1. `FUN_006261d0(item, 0xcc, buf, 0x40)` lists the item's stat 0xcc
   (item_charged_skill) entries: each a param (skill id and level, split by
   the shift / mask at tables +0xc6c / +0xc70) and a value.
2. Per entry: current = value & 0xff, max = value >> 8. If current < max,
   `FUN_0065c940(item, param >> shift, param & mask, max)` writes
   `max << 8 | min(max, request)`, i.e. max / max, to the entry (it returns
   0 and writes nothing when max is not 1..255, and tries the item's
   socketed items when the item itself has no such entry).
3. The result is 1 if any entry was below max (set whether or not
   `FUN_0065c940` wrote it).
4. When the owner is a player (type 0) with a client (`FUN_005531c0`), the
   new value is also sent: `FUN_0053d130(1, 0xcc, ...)`.

Callers: `FUN_00562660` (when its flag argument is set, after a socket),
`FUN_00565ab0` (the cube's recharge flag 0x400), `FUN_005761c0` (repair),
`FUN_005e0070` (the Torch / Standard drop).

d2d: reads the same, cited from the decompile, no oracle run. `rules::recharge` (components/rules/rules.hpp:731) sets the
stats it checks that are 204 below max to `max << 8 | max` and returns whether any was; the
cube (cube.hpp:362) and the repair use it. d2d omits the client stat packet
(step 4) and the 1..255 guard, neither visible in play.

### Does the item take sockets (`FUN_00629900`)

Cited. For an item unit (type 4), returns the Items record's byte +0x137
(`FUN_006335f0(unit +4)`), the `hasinv` column; 0 for no unit, another unit
type or no record. Callers include `FUN_00562660` (a filler goes only into
an item with hasinv, with a free socket) and the item-text builders
(`FUN_00483ff0`, `FUN_004843e0`, `FUN_004845a0`, `FUN_0048b540`,
`FUN_0048dd90`).

d2d: reads the same, cited from the decompile, no oracle run. `rules.hpp:99` names the same field (`hasinv (+0x137)`) and
item_text.hpp:430 cites `FUN_00629900` for the rune-letter line.

### Does the item take sockets (`FUN_00629900`)

Cited. For an item unit (type 4), returns the Items record's byte +0x137
(`FUN_006335f0(unit +4)`), the `hasinv` column; 0 for no unit, another unit
type or no record. Callers include `FUN_00562660` (a filler goes only into
an item with hasinv, with a free socket) and the item-text builders
(`FUN_00483ff0`, `FUN_004843e0`, `FUN_004845a0`, `FUN_0048b540`,
`FUN_0048dd90`).

d2d: reads the same, cited from the decompile, no oracle run. `rules.hpp:99` names the same field (`hasinv (+0x137)`) and
item_text.hpp:430 cites `FUN_00629900` for the rune-letter line.

## Ground items sent to a client (`FUN_0055bed0` chain)

In call order, callee first. Status: cited, not proven; no oracle was run.
The decompile drops fastcall register arguments, so which register holds
which argument below is read from the callers' shapes, not from the asm.

### The player unit of a client record (`FUN_00537860`)

`FUN_00537860(client, force)`: a null client ends in the assert exit
(`FUN_00408090(0xfb)`, `FUN_00408a60`, `FUN_00681e09(-1)`). Otherwise, when
`client[+0x3d4] & 1` or `force != 0`, it returns `client[+0x174]` as is.
When neither holds, it returns 0 if `client[+0x174]` is 0, else it looks the
player up again with `FUN_00552f60` (the unit hash lookup `(game, type, id)`,
shared with ai.cpp and missiles.cpp), returns the result and zeroes
`client[+0x174]` when the lookup finds nothing. The decompile shows only
`client[+0x170]` as the call's first (ECX) argument, which for that lookup is
the game pointer, so `+0x170` is most likely the client's game; the type and
id arguments are in registers the decompile hides (net-join.md has the
player unit id at `+0x1a8`), and were not read from the asm. So `+0x174` is a
cached player unit pointer that a client without flag `+0x3d4 & 1`
revalidates on every call. Callers include `FUN_0052c410`, `FUN_0052c500`, `FUN_0053eae0`
(the 0x9c builder, net-packets.md), `FUN_0054da10` and `FUN_00571740`.
Evidence: `python3 tools/ghidra/fn.py 00537860`.
d2d: not ported (no mention in `components/` or `apps/`); act1-end.md already
names it as "client =" in the act-change walk, which agrees.

## Ground items sent to a client (`FUN_0055bed0` chain)

In call order, callee first. Status: cited, not proven; no oracle was run.
The decompile drops fastcall register arguments, so which register holds
which argument below is read from the callers' shapes, not from the asm.

### The player unit of a client record (`FUN_00537860`)

`FUN_00537860(client, force)`: a null client ends in the assert exit
(`FUN_00408090(0xfb)`, `FUN_00408a60`, `FUN_00681e09(-1)`). Otherwise, when
`client[+0x3d4] & 1` or `force != 0`, it returns `client[+0x174]` as is.
When neither holds, it returns 0 if `client[+0x174]` is 0, else it looks the
player up again with `FUN_00552f60` (the unit hash lookup `(game, type, id)`,
shared with ai.cpp and missiles.cpp), returns the result and zeroes
`client[+0x174]` when the lookup finds nothing. The decompile shows only
`client[+0x170]` as the call's first (ECX) argument, which for that lookup is
the game pointer, so `+0x170` is most likely the client's game; the type and
id arguments are in registers the decompile hides (net-join.md has the
player unit id at `+0x1a8`), and were not read from the asm. So `+0x174` is a
cached player unit pointer that a client without flag `+0x3d4 & 1`
revalidates when called without `force`. Callers include `FUN_0052c410`, `FUN_0052c500`, `FUN_0053eae0`
(the 0x9c builder, net-packets.md), `FUN_0054da10` and `FUN_00571740`.
Evidence: `python3 tools/ghidra/fn.py 00537860`.
d2d: not ported (no mention in `components/` or `apps/`); act1-end.md already
names it as "client =" in the act-change walk, which agrees.

### The MonStats row by id (`FUN_00451f80`)

`FUN_00451f80(id)` (fastcall, ECX): returns 0 when `id < 0` or
`id >= [DAT_00744304 + 0xa80]` (the row count), else
`[DAT_00744304 + 0xa78] + id * 0x1a8`. That is the MonStats row accessor of
monsters.md "Tables" (rows of 0x1a8 bytes at `+0xa78`, count at `+0xa80`).
It does not assert on a bad id. Callers include `FUN_00454ad0`, `FUN_00461dc0`
and `FUN_00571f90`, which is in this work package.
Evidence: `python3 tools/ghidra/fn.py 00451f80`.
d2d: reads the same as monsters.md (cited from the decompile, no oracle run); no d2d function names it (the oracles
`tools/emu/freeze_rules.py` and `freeze_resume.py` call it).

### The MonStats row by id (`FUN_00451f80`)

`FUN_00451f80(id)` (fastcall, ECX): returns 0 when `id < 0` or
`id >= [DAT_00744304 + 0xa80]` (the row count), else
`[DAT_00744304 + 0xa78] + id * 0x1a8`. That is the MonStats row accessor of
monsters.md "Tables" (rows of 0x1a8 bytes at `+0xa78`, count at `+0xa80`).
It does not assert on a bad id. Callers include `FUN_00454ad0`, `FUN_00461dc0`
and `FUN_00571f90`, which is in this work package.
Evidence: `python3 tools/ghidra/fn.py 00451f80`.
d2d: reads the same as monsters.md (cited from the decompile, no oracle run); no d2d function names it (the oracles
`tools/emu/freeze_rules.py` and `freeze_resume.py` call it).

### A ground item sent to a client (`FUN_0055bed0`, `FUN_0053ec90`, `FUN_0053ecf0`)

`FUN_0055bed0(unit, client)` (fastcall; the client is the second argument, as
the decompile's `FUN_00537860(param_2, 0)` shows) does nothing unless all of:
the unit is nonnull, `unit[+0xc4] & 0x10` is clear, and `unit[+0x10] == 3`
(the mode field, structs.tsv). It then resolves the client's player unit with
`FUN_00537860(client, 0)` (the result is not used in the decompile) and
branches on `unit[+0xc4] & 0x1000`: set calls `FUN_0053ec90(unit, 0)`, clear
calls `FUN_0053ecf0(unit, 0)`.

Both callees have the same shape: `FUN_005531c0` is checked against the unit,
and a mismatch exits through the assert path with id `0x7dd`
(`FUN_0053ec90`) or `0x7e6` (`FUN_0053ecf0`); otherwise they call the 0x9c
builder `FUN_0053eae0` with a literal 2 (`FUN_0053ec90`) or 3
(`FUN_0053ecf0`). In net-packets.md the 0x9c action 2 is "dropped to the
ground" and action 3 is "a ground item comes into view", so the pair reads as:
a ground item (mode 3) whose flag 0x1000 is set is announced as dropped, one
whose flag is clear as coming into view. What flag 0x1000 means in `+0xc4` is
not traced; only the two-way split is read.

Not resolved from the decompile (the asm was not read): which registers carry
the client and item into `FUN_0053eae0`, whose first parameter reaches
`FUN_00537860` as a client, so the literal 2 / 3 may sit in another slot than
the action byte. Treat the action mapping as cited from net-packets.md's
table, not from this chain.
Evidence: `python3 tools/ghidra/fn.py 0055bed0`, `... 0053ec90`, `... 0053ecf0`.
d2d: not ported; `git grep -n -E "0055bed0|0053ec90|0053ecf0" -- components apps`
finds nothing.

### The flag-1 wrapper (`FUN_0055bf30`)

`FUN_0055bf30(unit)` calls `FUN_0055bed0` only when the unit is nonnull and
`unit[+0xc4] & 1` is set; it passes no second argument of its own in the
decompile, so the client reaches `FUN_0055bed0` in a register this function
does not set (the asm was not read). Its one caller is `FUN_0053a500`.
Evidence: `python3 tools/ghidra/fn.py 0055bf30`, `python3 tools/ghidra/calls.py 0055bf30`.
d2d: not ported; no mention in `components/` or `apps/`.

### A ground item sent to a client (`FUN_0055bed0`, `FUN_0053ec90`, `FUN_0053ecf0`)

`FUN_0055bed0(unit, client)` (fastcall; the client is the second argument, as
the decompile's `FUN_00537860(param_2, 0)` shows) does nothing unless all of:
the unit is nonnull, `unit[+0xc4] & 0x10` is clear, and `unit[+0x10] == 3`
(the mode field, structs.tsv). It then resolves the client's player unit with
`FUN_00537860(client, 0)` (the result is not used in the decompile) and
branches on `unit[+0xc4] & 0x1000`: set calls `FUN_0053ec90(unit, 0)`, clear
calls `FUN_0053ecf0(unit, 0)`.

Both callees have the same shape: `FUN_005531c0` is checked against the unit,
and a mismatch exits through the assert path with id `0x7dd`
(`FUN_0053ec90`) or `0x7e6` (`FUN_0053ecf0`); otherwise they call the 0x9c
builder `FUN_0053eae0` with a literal 2 (`FUN_0053ec90`) or 3
(`FUN_0053ecf0`). In net-packets.md the 0x9c action 2 is "dropped to the
ground" and action 3 is "a ground item comes into view", so the pair reads as:
a ground item (mode 3) whose flag 0x1000 is set is announced as dropped, one
whose flag is clear as coming into view. What flag 0x1000 means in `+0xc4` is
not traced; only the two-way split is read.

Not resolved from the decompile (the asm was not read): which registers carry
the client and item into `FUN_0053eae0`, whose first parameter reaches
`FUN_00537860` as a client, so the literal 2 / 3 may sit in another slot than
the action byte. Treat the action mapping as cited from net-packets.md's
table, not from this chain.
Evidence: `python3 tools/ghidra/fn.py 0055bed0`, `... 0053ec90`, `... 0053ecf0`.
d2d: not ported; `git grep -n -E "0055bed0|0053ec90|0053ecf0" -- components apps`
finds nothing.

### The flag-1 wrapper (`FUN_0055bf30`)

`FUN_0055bf30(unit)` calls `FUN_0055bed0` only when the unit is nonnull and
`unit[+0xc4] & 1` is set; it passes no second argument of its own in the
decompile, so the client reaches `FUN_0055bed0` in a register this function
does not set (the asm was not read). Its one caller is `FUN_0053a500`.
Evidence: `python3 tools/ghidra/fn.py 0055bf30`, `python3 tools/ghidra/calls.py 0055bf30`.
d2d: not ported; no mention in `components/` or `apps/`.

## Is-a by type index and the equipment look (`FUN_00629b50` chain)

Traced in call order, callee first. The claims here are cited from the
decompile, not proven (no oracle run). The state is the data-tables block at `[0x744304]`.

### Is-a between two ItemTypes indices (`FUN_00629b50`)

`FUN_00629b50(type, want)` is true when ItemTypes row `type` is `want` or
derives from it. It reads one bit of a precomputed matrix: 0 unless both
indices are in `0 .. [state+0xbfc]` (the ItemTypes count, structs.tsv
`item_type_count`), else the dword at `[state+0xc04] + ([state+0xc00] * type +
(want >> 5)) * 4` masked with `1 << (want & 31)` (`DAT_006ce268`); the row
stride is `[state+0xc00]` dwords. `FUN_00629bb0` is the same test on an item
unit (its type from `FUN_00628700`), and `FUN_00504a20` is the Equiv1/Equiv2
walk (compcode.md) that answers the same question without the matrix.
Callers: `FUN_0055c560`, `FUN_0063d710`, `FUN_0063e510`, `FUN_00645720`. How
the matrix is filled was not read.
Evidence: `python3 tools/ghidra/fn.py 00629b50`, `python3 tools/ghidra/fn.py 00629bb0`.
d2d: reads the same on the answer (cited from the decompile, no oracle run; `d2d::rules::type_is` walks the Equiv chain); the
matrix itself is not ported.

### An item's colour and tint byte (`FUN_0062c100`)

`FUN_0062c100(owner, item, tint_out, inv)` returns the colormap pointer from
`FUN_00600c20(Transform, colour)` (Transform from the item class record
`+0x141`, or `+0x142` when `inv`), or 0 for no colour. compcode.md "Tints"
has the colour rules; checked here against the decompile:
- Order: a null item gives 0. With an owner, each state of the list at
  `[state+0x18c]` (`[state+400]` shorts) that the owner has (`FUN_00639df0`) and
  whose record (stride 0x3c at `[state+0xbc]`) has colour `+0x2c` under 21 and an
  itemtype (`+0x2a`) the item is (`FUN_00629bb0`) returns that colour at once
  and leaves `*tint_out` unwritten (the States.txt itemtrans; d2d keeps this in
  `gamedata.hpp:493`, and `view.cpp:201` notes the unwritten tint).
- Then the item data quality (`item+0x14` → `+0`): 7 unique (record `+0x38`
  worn, `+0x39` inv, negative gives 0), 5 set (`+0x40` / `+0x41`), 4 and 6 the
  affix lists (suffixes `+0x3e..+0x43`, prefixes `+0x38..+0x3d`, then the
  automod `+0x36`, each through `FUN_00633ee0`, first with colour `+0x56` not
  -1), anything else the socketed-gem rule then the automod.
- The gem rule needs the class's hasinv byte (`+0x137`) and `FUN_0062bc20`
  (sockets) nonzero, flag `0x800`, the first socketed item an ItemTypes 0x14
  (`FUN_00629bb0`), colour = its gems record `+0x2f`.
- `*tint_out` = `FUN_0062a250(Transform, colour)`: `Transform * 32 + (colour &
  31)` for Transform 1..8 and colour under 21, else 0. This is the in-memory
  byte, with no +1 and 0 for none; its callers `FUN_0063e510` / `FUN_0063da70`
  add 1 on a colour and store 0xff on none, which is the d2s header's
  `(Transform * 32 + colour + 1)`, 0xff for none (compcode.md).
Evidence: `python3 tools/ghidra/fn.py 0062c100 0062a250`.
d2d: reads the same on the colour rules traced here, cited from the decompile, no oracle run (`compcode::Colours::of`,
`components/compcode/compcode.hpp:310`); the gem and the hasinv/socket gate are
approximated by the caller's socketed-item code. Cited, not proven.

### A 0x34-byte row check (`FUN_0065b620`)

`FUN_0065b620(index)` computes `index * 0x34 + DAT_0096d4e8` (a table of 0x34
byte rows whose base is the pointer in `DAT_0096d4e8`) and, when that sum is 0,
reports string id 0x150 (`FUN_00408090` / `FUN_00408a60`) and exits with
`FUN_00681e09(-1)`. It tests the row's address, not the base or the row's
content, so it fires only if the address wraps to 0 (bugs.md #54). The decompile
content, so it fires only when the address is 0: base 0 with index 0, or a
wrap (bugs.md #59); base 0 with another index passes. The decompile
wrap (bugs.md #60); base 0 with another index passes. The decompile
content, so it fires only when the address is 0: base 0 with index 0, or a
wrap (bugs.md #54); base 0 with another index passes. The decompile
returns nothing; the asm was not read, so whether the row address stays in EAX
for the caller is unverified: `FUN_0063e510` calls `FUN_00451f60` right after it
with no argument of its own, which fits the row (its `+4` read, > 0 to go on).
Callers: `FUN_004da790`, `FUN_0063e510`. What the table is was not traced.
Evidence: `python3 tools/ghidra/fn.py 0065b620`, `python3 tools/ghidra/calls.py 0065b620`.
d2d: not ported; no mention in `components/` or `apps/`.

### The worn look: codes and tints (`FUN_0063e510`)

`FUN_0063e510(unit, codes, tints)` fills two 16-byte arrays, one byte per
look layer, from what the unit wears. Both are pre-set to 0xff by its
wrappers (below). Steps, all from the decompile:
- Needs the unit's inventory (`unit+0x60`, magic `0x1020304`), then walks its
  item list from `+0xc` along each item's data `+0x64`. It takes only items
  of unit type 4 in mode 1 (`piVar6[4] == 1`, worn). A class with no Items
  record reports string 0x1446 and exits. It stops at a non-item or an item
  without data (a data pointer of -0x5c also stops it, an odd guard).
- The worn spot is `FUN_00627d40(item)` (the byte at item data `+0x44`,
  weapon-switch.md). Spots 1, 4 and 5 (when the item is not ItemTypes row
  0x4b, `FUN_00629b50`) go to `FUN_0063da70`: the layer is `FUN_00628660(item)`,
  forced to 5 or 6 when the item is what `FUN_0063c050` gives for 5 or 6;
  the code is `FUN_0063d900`'s, 0xff (both arrays) when it has none; the tint
  is `FUN_0062c100(unit, item, &tint, 0)` + 1, or 0xff when that returns 0.
- Spot 3 (the torso) fills layers 0..15 that `FUN_0064f420` accepts (1, 2, 3,
  4, 8, 9): `FUN_0063d690` copies six bytes at the armor record `+0x116..0x11b`
  and `FUN_0064f500` picks one per layer (1 → byte 2, 2 → 3, 3 → 0, 4 → 1,
  8 → 4, 9 → 5; another layer under 16 asserts string 0x274, 16 or more
  0x265). That byte goes through `FUN_0065b620` and `FUN_00451f60`, and a
  result over 0 asks `FUN_0063d900` for the code; the tint is as above. A
  layer without one gets 0xff in both arrays.
`FUN_0063d900` takes its two arguments in registers (not read) and searches
the table at `DAT_0096cc68` (pairs, 255 at most) built once by `FUN_0063d710`
from the Items records (code `+0x90`, else `+0x80`; first three rows
`lit `, `med `, `hvy `); it returns the pair index, 0 for none.
Evidence: `python3 tools/ghidra/fn.py 0063e510 0063da70 0063d900 0063d710`,
`python3 tools/ghidra/calls.py 0063e510`.
d2d: reads the same on the result encoding (cited from the decompile, no oracle run; tint = Transform * 32 + colour + 1, 0xff for
none: `compcode::tints`); d2d builds the look from the worn list
(`GameData::look_of`) and does not run this walk. Cited, not proven.

### The look codes of a unit (`FUN_005379d0`)

`FUN_005379d0(_, unit, out, size)` (fastcall: unit in EDX, `out` and `size`
on the stack) asserts (string 0x3d4) when `size < 0x10` or the unit is null,
sets the 16 bytes at `out` to 0xff, and calls `FUN_0063e510(unit, out,
scratch)`: `out` gets the per-layer codes and the tints go to a stack buffer.
No caller in the call table (reached through a table or a callback).
Evidence: `python3 tools/ghidra/fn.py 005379d0`, `python3 tools/ghidra/calls.py 005379d0`.
d2d: not ported; no mention in `components/` or `apps/`.

## Is-a by type index and the equipment look (`FUN_00629b50` chain)

Traced in call order, callee first. The claims here are cited from the
decompile, not proven (no oracle run). The state is the data-tables block at `[0x744304]`.

### Is-a between two ItemTypes indices (`FUN_00629b50`)

`FUN_00629b50(type, want)` is true when ItemTypes row `type` is `want` or
derives from it. It reads one bit of a precomputed matrix: 0 unless both
indices are in `0 .. [state+0xbfc]` (the ItemTypes count, structs.tsv
`item_type_count`), else the dword at `[state+0xc04] + ([state+0xc00] * type +
(want >> 5)) * 4` masked with `1 << (want & 31)` (`DAT_006ce268`); the row
stride is `[state+0xc00]` dwords. `FUN_00629bb0` is the same test on an item
unit (its type from `FUN_00628700`), and `FUN_00504a20` is the Equiv1/Equiv2
walk (compcode.md) that answers the same question without the matrix.
Callers: `FUN_0055c560`, `FUN_0063d710`, `FUN_0063e510`, `FUN_00645720`. How
the matrix is filled was not read.
Evidence: `python3 tools/ghidra/fn.py 00629b50`, `python3 tools/ghidra/fn.py 00629bb0`.
d2d: reads the same on the answer (cited from the decompile, no oracle run; `d2d::rules::type_is` walks the Equiv chain); the
matrix itself is not ported.

### An item's colour and tint byte (`FUN_0062c100`)

`FUN_0062c100(owner, item, tint_out, inv)` returns the colormap pointer from
`FUN_00600c20(Transform, colour)` (Transform from the item class record
`+0x141`, or `+0x142` when `inv`), or 0 for no colour. compcode.md "Tints"
has the colour rules; checked here against the decompile:
- Order: a null item gives 0. With an owner, each state of the list at
  `[state+0x18c]` (`[state+400]` shorts) that the owner has (`FUN_00639df0`) and
  whose record (stride 0x3c at `[state+0xbc]`) has colour `+0x2c` under 21 and an
  itemtype (`+0x2a`) the item is (`FUN_00629bb0`) returns that colour at once
  and leaves `*tint_out` unwritten (the States.txt itemtrans; d2d keeps this in
  `gamedata.hpp:493`, and `view.cpp:201` notes the unwritten tint).
- Then the item data quality (`item+0x14` → `+0`): 7 unique (record `+0x38`
  worn, `+0x39` inv, negative gives 0), 5 set (`+0x40` / `+0x41`), 4 and 6 the
  affix lists (suffixes `+0x3e..+0x43`, prefixes `+0x38..+0x3d`, then the
  automod `+0x36`, each through `FUN_00633ee0`, first with colour `+0x56` not
  -1), anything else the socketed-gem rule then the automod.
- The gem rule needs the class's hasinv byte (`+0x137`) and `FUN_0062bc20`
  (sockets) nonzero, flag `0x800`, the first socketed item an ItemTypes 0x14
  (`FUN_00629bb0`), colour = its gems record `+0x2f`.
- `*tint_out` = `FUN_0062a250(Transform, colour)`: `Transform * 32 + (colour &
  31)` for Transform 1..8 and colour under 21, else 0. This is the in-memory
  byte, with no +1 and 0 for none; the d2s header's `(Transform * 32 + colour +
  1)`, 0xff for none (compcode.md) is made from it elsewhere, not traced here.
Evidence: `python3 tools/ghidra/fn.py 0062c100 0062a250`.
d2d: reads the same on the colour rules traced here, cited from the decompile, no oracle run (`compcode::Colours::of`,
`components/compcode/compcode.hpp:310`); the gem and the hasinv/socket gate are
approximated by the caller's socketed-item code. Cited, not proven.

### A unit's class id (`FUN_00451f60`)

`FUN_00451f60(unit)` (ECX) returns `-1` for a null unit, else the dword at
unit `+4` (the class id: Items row, MonStats id, player class). A getter with
no checks; callers `FUN_00470ec0`, `FUN_0048a990`, `FUN_00497e00`,
`FUN_004a7d00`, `FUN_004abf10`, `FUN_004ac4d0`, `FUN_004ed480`, and
`FUN_0062c100` and `FUN_0063e510` call it (game-calls.tsv).
Evidence: `python3 tools/ghidra/fn.py 00451f60`, `python3 tools/ghidra/calls.py 00451f60`.
d2d: not ported as a function (a unit's class is a plain field); the pet-ai.md
mention at line 285 uses it for the owner's class.

### A unit's look tints (`FUN_00537a40`)

`FUN_00537a40` is `FUN_005379d0` with the two arrays swapped: assert string
0x3df, `out` pre-set to 0xff and passed as `FUN_0063e510`'s tints (third)
argument, the codes going to the scratch buffer. No caller in the call table.
Evidence: `python3 tools/ghidra/fn.py 00537a40`, `python3 tools/ghidra/calls.py 00537a40`.
d2d: not ported; its result is what `compcode::tints` builds from the worn
list, in the same encoding.

### A unit's class id (`FUN_00451f60`)

`FUN_00451f60(unit)` (ECX) returns `-1` for a null unit, else the dword at
unit `+4` (the class id: Items row, MonStats id, player class). A getter with
no checks; callers `FUN_00470ec0`, `FUN_0048a990`, `FUN_00497e00`,
`FUN_004a7d00`, `FUN_004abf10`, `FUN_004ac4d0`, `FUN_004ed480`, and
`FUN_0062c100` and `FUN_0063e510` call it (game-calls.tsv).
Evidence: `python3 tools/ghidra/fn.py 00451f60`, `python3 tools/ghidra/calls.py 00451f60`.
d2d: not ported as a function (a unit's class is a plain field); the pet-ai.md
mention at line 285 uses it for the owner's class.
belt potion / javelin / durability never regrows in play. Not a
disagreement, a gap: it needs the event-3 handler for items (not traced:
the handler is whatever event 3 calls on an item unit).
`rules::make_fighter`'s weapon).

Pass 1 sets no 0x4000: a charm short of its requirements just gets no
`FUN_0055d970`, and `FUN_0062ff70` refuses it at the other attach sites
(`FUN_00560200` into the inventory, and the rest of its 29 callers). None
of `FUN_0055dbc0`'s 22 callers is a level up; `FUN_005380d0` calls it
(through `FUN_0055df00`, 0x538146) only with unit flag `+0xc8 & 0x200000`,
which only `FUN_00539760` (the join's player, 0x539939) sets. So a charm
whose level the player reaches counts from the next item move or join,
not at the level up. ItemTypes row 0xd is `char` (Charm).

d2d: pass 1 is `rules::settle_requirements`' first loop. An inventory
charm (`rules::inventory_charm`) failing `requirements_hold` gets
`Item::charm_short` (not saved; sent in the replicated items' byte with
`wielded`), and `game::gear_active` leaves its stats out.

The server's other item moves (C→S 0x19, 0x1f, 0x22, 0x24, 0x25, 0x2a) and the
small Items.cpp getters are in [itemmode.md](itemmode.md); the inventory record in
[inventory.md](inventory.md).
