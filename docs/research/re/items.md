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

## Into a hand (`FUN_0063de60` → `FUN_0063dd90`)

For body slots 4 and 5, `FUN_0063de60` asks `FUN_0063dd90` with the item
on the cursor, the one in the clicked hand and the other hand's. With an
item on the cursor:

| Clicked hand | The other hand's item | Code | Client sends | Server |
|---|---|---|---|---|
| empty | none, or goes with it (`FUN_0063dbc0`) | 1 | 0x1a | `FUN_005606b0`: equipped |
| empty | doesn't go with it | 2 | 0x1b | `FUN_00563d20`: the other hand's to the cursor, then equipped |
| full | stacks with it (`FUN_0062c850`) | 6 | 0x21 | stack |
| full | none, or goes with it | 5 | 0x1d | `FUN_00560f00`: swapped |
| full | doesn't go with it, and fits the inventory (`FUN_0063cb00` → `FUN_0063b850`) | 7 | 0x1e | `FUN_00561220`: the other hand's to the inventory (`FUN_0063b950`), this hand's to the cursor, then equipped |
| full | doesn't go with it, no room | 0 | nothing | the client's class voice (`FUN_004cb9c0`): 0x13 "impossible"; 0x14 "can't use yet" only when a retry without the requirements (`FUN_0063de60` with 1) passes and the item's class is any or the player's (`FUN_0062c0b0`) |

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
