# Common item routines: remaining static traces

Read from the cached Ghidra export of stock game.exe 1.14d and checked
against `research/game114d.asm` where register arguments, narrow returns or
an omitted return affect the claim. These are static observations, not
oracle results or proof that d2d follows the same paths.

`D` below is item data at unit `+0x14`; `R` is the Items record resolved
from unit class `+0x04` by `FUN_006335f0`. A valid-item guard means nonnull
unit, type 4, and (when accessing D) nonnull D. A fatal guard logs the
Items.cpp assertion and exits; it is not a zero-return fallback. Type rows
are at `[0x744304]+0xbf8`, count at `+0xbfc`, stride `0xe4`.
Narrow byte/word return descriptions refer to AL/AX unless explicitly
zero/sign extended; callers must not assume a clean full EAX.

| Address | Static trace |
|---|---|
| `FUN_00627c90` | With pool and item as two stack arguments, ignore null/non-item; allocate and zero `0x74` bytes, set D `+0x0c` to −1 and install D. Allocation failure is fatal. There is no existing-D check. |
| `FUN_00627d00` | With pool and item as two stack arguments, ignore null/non-item; detach from inventory through `FUN_0063aaf0`, then free nonnull D through the pool. Does not clear unit `+0x14` afterward. |
| `FUN_00627d40` | Valid-item getter for byte D `+0x44`, otherwise AL=0. Successful and wrong-type returns preserve upper pointer bits. |
| `FUN_00627d90` | Return D `+0x04`, the item's RNG-state address, for a valid item; otherwise null. |
| `FUN_00627dc0` | Valid items pass D `+0x04` in ECX to `FUN_00650e30`: initialize its two words to `1, 0x29a`. Invalid inputs do nothing. |
| `FUN_00627e20` | Store the second argument as the 32-bit word at D `+0x10`. Null unit, wrong type or null D is fatal. |
| `FUN_00628140` | Valid item: return D `+0x14` AND caller mask; otherwise zero. This is the transient flag word, distinct from persistent flags at D `+0x18`. |
| `FUN_006281b0` | Valid-item getter for the full transient flag word D `+0x14`; otherwise zero. |
| `FUN_006282a0` | Valid-item getter for byte D `+0x46`; otherwise AL=`0xff`. |
| `FUN_006282d0` | Set byte D `+0x46` from the low byte of the second argument; invalid items do nothing. |
| `FUN_006282f0` | Valid-item getter for byte D `+0x47`; otherwise AL=`0xff`. Paired setter is `FUN_00628320`. |
| `FUN_006283f0` | Valid-item getter for picture byte D `+0x49`; otherwise AL=0. Upper EAX bits retain pointer residue. |
| `FUN_00628440` | For a signed type index in range and a nonnull computed row, sign-extend the short at type row `+0x0c`; otherwise return zero. |
| `FUN_006284c0` | For a signed type index in range and a nonnull computed row, read byte `+0x13`; otherwise AL=0. |
| `FUN_00628530` | Read the 32-bit word at R `+0xc4`. Null unit, wrong type or missing row is fatal. |
| `FUN_006285f0` | Read R `+0x90`, falling back to item code R `+0x80` when zero. Null unit, wrong type or missing row is fatal. |
| `FUN_00628660` | Read byte R `+0x115`; null unit, wrong type or missing row is fatal. |
| `FUN_006288a0` | Nonnull type-4 unit: zero-extend ushort R `+0xfa`. Otherwise return −1. The successful path does not check R for null. |
| `FUN_00628900` | Resolve a class-ID row and compare its 32-bit R `+0xc0` with the second argument. Missing row is fatal. |
| `FUN_00628950` | Nonnull type-4 unit: forward its class ID and the second argument to `FUN_00628900`; otherwise zero. |
| `FUN_00628980` | Class-ID getter for byte R `+0x11c`; missing row is fatal. |
| `FUN_00629620` | Class-ID getter for 32-bit R `+0xec`; missing row is fatal. |
| `FUN_00629660` | Require nonnull type-4 unit, pass its class to `FUN_00629620`, and preserve its EAX return. Ghidra's void declaration drops this result. |
| `FUN_006296b0` | Class-ID getter for 32-bit R `+0xe4`; missing row is fatal. |
| `FUN_006296f0` | Require nonnull type-4 unit, pass its class to `FUN_006296b0`, and preserve its EAX return despite Ghidra's void declaration. |
| `FUN_00629740` | Read R `+0xdc` AND `8`, preserving the mask value; null unit, wrong type or missing row is fatal. |
| `FUN_006297a0` | Read byte R `+0x135`; null unit, wrong type or missing row is fatal. |
| `FUN_00629800` | Read byte R `+0x136`; null unit, wrong type or missing row is fatal. |
| `FUN_00629860` | Return short R `+0x106` except that a stored value of 1 gives 100. Null unit, wrong type or missing row also gives 100; zero and negative stored shorts are retained. |
| `FUN_006298a0` | As the preceding helper for R `+0x108`: value 1 or rejected input gives 100, other shorts are retained. |
| `FUN_006298e0` | Class-ID getter for byte R `+0x137`; missing row gives AL=0. |
| `FUN_00629990` | Class-ID getter for byte R `+0x138`; missing row gives AL=0. |
| `FUN_00629a00` | Class-ID getter for byte R `+0x13e`; missing row is fatal. |
| `FUN_00629a40` | Require nonnull type-4 unit; forward its class to `FUN_00629a00`, preserving its byte result despite Ghidra's void declaration. |
| `FUN_00629b50` | Bounds-check both signed type IDs. In the inheritance matrix at table-state `+0xc04`, use row stride in words at `+0xc00`, select column `second >> 5`, AND with the bit-mask table `0x6ce268[second & 31]`. Invalid IDs give zero; the result is a mask, not normalized boolean. |
| `FUN_00629d00` | Class-ID getter for R `+0xdc` AND `4`; missing row is fatal. |
| `FUN_00629e40` | Read 32-bit words D `+0x1c` and `+0x20` to two outputs; invalid-item paths write zero to both outputs. The output pointers themselves are not checked. |
| `FUN_00629ea0` | Store the two arguments in D `+0x1c` and `+0x20`; invalid items do nothing. |
| `FUN_00629ed0` | Set 32-bit word D `+0x0c`; null unit, wrong type or null D is fatal. Inventory detach resets this field to −1. |
| `FUN_00629f20` | Get 32-bit word D `+0x0c`; null unit, wrong type or null D is fatal. |
| `FUN_00629fe0` | Resolve the unit class (−1 for null), assert on missing R, then search `0x7446e0` eight-byte entries at `0x7446a0` for R `+0xc0`. Return the matched entry's ushort `+4` in AX; no match gives AX=0. Upper EAX is not a defined result. |
| `FUN_0062a060` | Class-ID predicate R byte `+0x139` nonzero; missing row is fatal. |
| `FUN_0062a0a0` | Require nonnull type-4 unit; forward its class to `FUN_0062a060` and retain EAX despite Ghidra's void declaration. `tools/emu/items_objects_helpers.py` runs it with `FUN_0062a060` hooked: the class is forwarded and EAX returned whole, and a null unit (assert 0x1162) or another type (0x1163) reaches the hooked fatal path. d2d is not compared. |
| `FUN_0062a180` | Read byte R `+0x13c`; null unit, wrong type or missing row is fatal. |
| `FUN_0062a250` | Register helper: EAX carries a group in 1..8 and ECX an unsigned value in 0..20. Return their packed byte `(group << 5) + value`; invalid input gives AL=0. Group 8 wraps in the byte result. |
| `FUN_0062a2b0` | Unit in EAX: resolve its class (−1 when null), assert on missing R, then zero-extend byte R `+0x143`. There is no unit-type guard. |
| `FUN_0062a310` | Null unit gives 0. Positive stat `0x46` gives 1. Otherwise nonzero stat `0x7d` read through `FUN_00625500` writes stat `0x46` to zero and gives 1. Without that stat, a positive `FUN_006295b0` result gives 0, zero/negative gives 1. |
| `FUN_0062a360` | No-argument stub: return zero, with no state change. |
| `FUN_0062a490` | Require a valid item with quality 5; obtain its ID through `FUN_00629da0`, bounds-check against table-state `+0xc1c`, then return the SetItems row at `+0xc18 + id*0x1b8`. Otherwise null. |
| `FUN_0062a670` | Require nonnull type-4 unit with D, then return the ushort at D `+0x30` in AX. All invalid paths are fatal; upper EAX retains pointer bits. |
| `FUN_0062a900` | Item in EAX: valid type/row, nonzero R `+0x137` and nonnull unit inventory `+0x60` permit counting every node from `FUN_0063b2c0`/`FUN_0063dfa0`. Otherwise return zero. It does not filter node unit mode. |
| `FUN_0062ac80` | Decode one stat: ECX=file version, EDX=stat ID, EAX=save-column index, ESI=ItemStatCost row; stack holds bit reader and destination stat list. Read optional param bits, then value bits; subtract Save Add and shift by row `+0x18`, then set the stat. Before version `0x5a`, stats `0x17/0x18/0x4a/0x4f/0x50` override widths (5/6/5/8/7), with adds 10/20/20 for the last three. Before `0x5d`, remap `0x5c,0x5e→0x5d`, `0x5f,0x61→0x60`, `0x62,0x64→0x63`, `0x65,0x67→0x66`, `0x68,0x6a→0x69`; selected skill stats use six bits before `0x58`. |
| `FUN_0062ae20` | Parse an item header from a bit reader; when requested, require the 16-bit JM marker, returning 0 on mismatch. Save flags, skip ten version bits, read mode; modes 3/5 read 16-bit coordinates, other modes read 4-bit body slot/x/y and 3-bit page minus one. Ear flag `0x10000` supplies code `ear `; otherwise read code, remapping `nec ` to `neg ` for file versions below `0x5d`. Resolve class and read three socket bits unless flags `0x2200000` suppress them; return 1. |
| `FUN_0062af50` | ECX=width, EDX=unsigned value, one stack argument=bit writer. For widths other than 32, saturate value to `(1<<width)-1`; width 32 passes it unchanged. Forward to `FUN_00410eb0`. No guard makes arbitrary widths outside the caller contract safe. |
| `FUN_0062af80` | Compact-item serializer: item in EAX, bit writer in EBX, stack save-mode flag. Write 10-bit D `+0x30`, 3-bit unit mode and either 16-bit world coordinates (modes 3/5) or 4-bit body/x/y and 3-bit page+1. Write item code except ears: ears encode FUN_00629da0(item) in 3 bits, item data +0x48 in 7 and NUL-terminated name in 7-bit characters. Gold (type 4) writes a quantity-width flag then 12 or 32 quantity bits at threshold 4096. R `+0x12a` and `+0x12b` jointly enable stat `0x164`, encoded via ItemStatCost. Save mode appends a presence bit for D `+0x20`, then D `+0x1c`, D `+0x20` and a zero third word when present. Values use the serializer's saturation rules. Missing required item/stat row is fatal. |
| `FUN_0062b3a0` | Valid item of quality 5: bounds-check its ID, resolve SetItems, and sign-extend short row `+0x2e`; otherwise return −1. |
| `FUN_0062b450` | Parent item in EBX, stat ID and output pointer on stack. Zero output; require type 4, a row and nonzero R `+0x137`, then iterate its socket inventory. Add each child's requested stat only when the parent item's mode is 1. Assembly at `LAB_0062b496` sets ECX=EBX before the mode call: it does not test the child's mode. |
| `FUN_0062b560` | Require nonnull type-4 unit; forward its class to `FUN_0062b4d0` and preserve EAX despite the void decompilation. That predicate checks armour/weapon type ancestry, exceptional/elite code equality, excludes type `0x26`, and requires R `+0x12a` zero. |
| `FUN_0062ba60` | Two stack arguments are forwarded in ECX/EDX to `FUN_0062b5b0`, the item required-level calculation; return its EAX and pop eight argument bytes. |
| `FUN_0062bb20` | Require nonnull type-4 unit. Scroll/tome type ancestry (`0x16` or `0x12`) selects Books row using D suffix slot 0 and returns byte Books `+2`, or −1 if lookup fails. Other items return zero-extended byte R `+0x134`; missing row is fatal. |
| `FUN_0062bbd0` | For nonnull type-4 unit with valid primary type index and computed row, read byte type row `+0x1f`; rejected inputs return AL=7. |
| `FUN_0062be00` | For nonnull type-4 unit, compute inventory area from R bytes `+0x10f`/`+0x110`; missing/zero area does nothing. Limit capacity to min(area,6,low-byte `FUN_0062bc20`). Clamp requested signed count first to at least 1, then to capacity. Set persistent flag `0x800` when D exists and write stat `0xc2` to that count. A zero capacity can therefore write zero while setting the flag. |
| `FUN_0062beb0` | Forward its stack item argument to the type-ancestry predicate `FUN_00629bb0` with type `0x35`; preserve the callee's EAX return. |
| `FUN_0062c0b0` | Resolve the primary type of a nonnull type-4 item and validate its row; return byte type row `+0x21` only below 7, otherwise AL=7. |
| `FUN_0062c9f0` | Item in ESI, triggering stat on stack: resolve a row through item code. For stats `0x10/0x1f`, armour type `0x32` with nonzero row `+0xd0` raises base defence to max(current+1,row+1). For `0x11/0x16`, weapon type `0x2d` floors stats `0x16/0x18` at nonzero row bytes `+0xff/+0x103`; throwable items also floor stat `0xa0` at `+0x101`. For `0x12/0x15`, similarly floor `0x15/0x17` at `+0xfe/+0x102` and throwable `0x9f` at `+0x100`. Other triggers or missing rows do nothing. |
| `FUN_0062e410` | Forward four stack arguments to header parser `FUN_0062ae20`, supplying file version `0x60`; return its success value and pop 16 argument bytes. |
| `FUN_0062e7e0` | For a nonnull type-4 item with valid primary type row, return byte `+0x11`; rejected inputs give AL=0. |
| `FUN_0062e830` | For a nonnull type-4 item with valid primary type row, return byte `+0x12`; rejected inputs give AL=0. |
| `FUN_0062e8d0` | For a nonnull type-4 item with valid primary type row, return byte `+0x23` in AL; rejected inputs clear AL. Upper EAX is not defined as zero. |
| `FUN_0062e920` | Resolve primary type row; signed requested index is clamped to at least 0 and at most row byte `+0x23` minus one, then return `row+0x24+32*index`. Missing item/type row uses the fallback pointer at `0x7446e4`. There is no zero-count guard: count zero selects index −1, hence row `+4`, rather than the fallback. |
| `FUN_0062ea80` | Resolve primary type row of a nonnull type-4 item and copy bytes `+0x0a/+0x0b` to two outputs; rejected inputs write zero to both. Output pointers themselves are not checked. |

## Limits

The numeric fields and branches above are observed statically. Where names
remain numeric, the semantic table-column meaning has not been established
in this pass. No oracle was run, invalid pointers were not exercised, and
all side effects behind a called subsystem remain bounded by that call.
The serializer trace covers its explicit branches and bit widths; it does
not establish save/network round-trip parity. The unguarded zero-count
row selection and parent-mode socket filter are observations, not new
claims of port defects.
