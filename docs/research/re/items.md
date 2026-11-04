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

Re-check these claims against game.exe 1.14d:

```sh
cd tools/emu && uv run python item_accessors.py
```
