# Item quality and affix accessors

These functions take an item unit. They require a non-null unit of type 4
with a non-null item-data pointer at unit `+0x14`; rejected calls do not
write item data. `FUN_0048c060` uses the quality to select item-name
formatting, and the other call sites cited below identify the affix fields.

| Address | Traced claim |
|---|---|
| `FUN_00627e70` | Read the 32-bit quality at item data `+0x00`; return 2 (normal) for a rejected call. |

Re-check all claims against game.exe 1.14d:

```sh
cd tools/emu && uv run python item_accessors.py
```
