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
| `FUN_00628740` | Read the base item level at item record `+0xfd`, writing only `AL` and retaining the record pointer's upper 24 bits. `FUN_005c1560` uses it as a floor when selecting an affix level. |
| `FUN_006287a0` | Return the bitwise AND of its two 32-bit arguments. |
| `FUN_006287b0` | Update the 32-bit value at the first argument: a nonzero third argument sets the bits in the second argument with `\|=`, while zero clears them with `&= ~mask`. |
| `FUN_006287d0` | For a valid item unit and class lookup, return item record `normcode` at `+0x84` when nonzero, otherwise `code` at `+0x80`. Invalid inputs take assertion paths. `FUN_0062efb0` uses this fallback when pricing gambled items. |
| `FUN_00628830` | Clamp an affix required level using a custom fastcall convention: `ECX` is the affix record, `EDX` the optional player unit, and `EAX` the initial value. A null affix preserves `EAX`. Select byte `+0x65`, or byte `+0x68` when byte `+0x67` is not `0xff`, the player is nonnull, and that byte equals the player class at `+0x04`; raise a signed initial value to the selected byte, but preserve higher values. `FUN_0062b5b0` calls this helper. |
| `FUN_00628860` | Resolve the data-table state through global pointer `0x744304`; index the item-type table at state `+0xbf8`, with count at `+0xbfc` and record stride `0xe4`. Return the zero-extended byte at item-type record `+0x10`, or 0 for a negative index, an index greater than or equal to the count, or a computed null record. `FUN_0055a6d0` uses the result when rerolling classic drops. |

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
