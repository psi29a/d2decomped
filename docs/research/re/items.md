# Item data accessors

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

Re-check these claims against game.exe 1.14d:

```sh
cd tools/emu && uv run python item_accessors.py
```
