# MSVC C runtime helpers Ghidra did not name — game.exe 1.14d

game.exe links the Visual Studio 2005 C runtime statically, from
`__security_check_cookie` (`FUN_00681a48`) to the import thunks at
`0x006b00de`. Ghidra's signatures name most of it (`__invoke_watson`,
`_fclose`, `_sprintf`, …); the rest keeps `FUN_` names. Nothing in it is
specific to the game; d2d uses its own C++ standard library and ports none
of it. Read from the Ghidra decompile and listing (capstone for the small
ones); no oracle ran, so all of it is **cited**.

The name column is the VS2005 CRT routine each function has the shape of:
game.exe carries no symbols for them, so a name is a reading from the
function's named callers and callees, the Win32 calls it makes and the
fields it touches, not a fact read in game.exe. The description is what the
code does. A few of them are also in `names.tsv`. Each table is in address
order.

## Start-up, `errno` and the invalid-parameter path

| Address | Name | What it does |
|---|---|---|
| `FUN_00688bbd` | `DecodePointer` wrapper (`_decode_pointer`) | takes the CRT's per-thread function table from TLS (`DAT_00749e94` / `DAT_00749e90`, entry +0x1fc), else `GetProcAddress(GetModuleHandleA("KERNEL32.DLL"), "DecodePointer")`; calls it on the argument, or returns the argument unchanged when there is none |
| `FUN_00688d96` | `_getptd_noexit` | saves `GetLastError`, reads the thread's data block from the TLS slot; on first use `__calloc_crt(1, 0x214)`, stores it, initialises it (`FUN_00688cd7`), sets +0 = `GetCurrentThreadId()` and +4 = -1; frees it and returns 0 if the store fails; restores the last error |
| `FUN_00689635` | `_invalid_parameter` | decodes `DAT_0096de70` (the invalid-parameter handler) with `FUN_00688bbd`; non-null: `JMP EAX`, so the caller's five arguments reach the handler as they were pushed; null: `FUN_0069a28d(2)` then a tail jump to `__invoke_watson` (`FUN_00689501`), which raises 0xC000000D and terminates the process |
| `FUN_006896ad` | `_errno` | `FUN_00688d96()`; null returns `0x0074a1d8` (the static fallback), else the block + 8 |
| `FUN_006896c0` | `__doserrno` | the same with + 0xc and fallback `0x0074a1dc` |
| `FUN_0069a28d` | `_crt_debugger_hook` | 8 bytes: writes 0 to `DAT_00993b20`; the 2 pushed by `FUN_00689635` (`_CRT_DEBUGGER_INVALIDPARAMETER`) is not read |

The CRT's argument checks use the first two together. `__get_winmajor`
(`FUN_00681baf`), for one, with a null pointer or before start-up has set
`DAT_0096d674`, runs `*_errno() = 0x16` (`EINVAL`), then
`_invalid_parameter(0, 0, 0, 0, 0)`, and returns 0x16. The same calls are in
`__get_osplatform`, `_fclose`, `_sprintf`, `_fwrite`, `__strlwr` and
`FUN_00688271` (game-calls.tsv).

game.exe was not read for a call to `_set_invalid_parameter_handler`, so
whether the game installs a handler at `DAT_0096de70`, and so whether a bad
CRT argument ends the process, is not traced.

## Locks

`_unlock(n)` is `FUN_006893d6`: `LeaveCriticalSection` on the lock table
entry at `0x00749f50 + n * 8` (`__lock`, `FUN_006894c6`, enters it). Most of
the rest are the `__finally` blocks of named routines: a few bytes that push
a lock number and call `_unlock`, called by their routine on its way out.
The lock numbers, by the routines that take them: 0 `raise`; 1 `flsall` and
`_getstream` (the stream table); 4 the heap (`_free`, `_realloc`,
`__msize`, `__calloc_impl`, `_V6_HeapAlloc`); 5 `___unDName`; 6 `___tzset`
and `__isindst`; 7 `_tzset_nolock` and `__getcwd`; 8 exit (`__onexit`,
`doexit`); 10 `__mtinitlocknum`, `___lock_fhandle` and `__alloc_osfhnd`;
11 `__alloc_osfhnd`; 12 `___updatetlocinfo`, `__freefls@4` and
`FUN_00688cd7`; 13 `__setmbcp`, `___updatetmbcinfo` and `__freefls@4`;
14 `_Type_info_dtor`; 0x10 + i the i-th of the 20 built-in streams.

| Address | Name | What it does |
|---|---|---|
| `FUN_00681ab6` | `_lockexit` | `__lock(8)`. Caller `__onexit` |
| `FUN_00681abf` | `_unlockexit` | `_unlock(8)`. Caller `FUN_0068381e` |
| `FUN_00681df4` | `doexit`'s `__finally` | `_unlock(8)` when its frame's `[ebp+0x10]` (the return-to-caller flag) is set. Caller `FUN_00681d27` |
| `FUN_00682050` | `_fclose`'s `__finally` | `_unlock_file(ESI)` (`FUN_0068ab4c`) |
| `FUN_0068218f` | `FUN_00682058`'s `__finally` | the same |
| `FUN_00682390` | `__fsopen`'s `__finally` | `_unlock_file` of its frame's stream |
| `FUN_0068381e` | `__onexit`'s `__finally` | calls `FUN_00681abf` |
| `FUN_00683ce9` | `_realloc`'s `__finally` | `_unlock(4)` |
| `FUN_00683e7f` | `_V6_HeapAlloc`'s `__finally` | `_unlock(4)` |
| `FUN_00684016` | `_free`'s `__finally` | `_unlock(4)` |
| `FUN_006844c1` | `_fwrite`'s `__finally` | `_unlock_file` of its frame's stream |
| `FUN_00684778` | `_fread_s`'s `__finally` | the same |
| `FUN_00684999` | `_ftell`'s `__finally` | the same |
| `FUN_00684aa5` | `_fseek`'s `__finally` | the same |
| `FUN_00684dd9` | `FUN_00684c94`'s `__finally` | the same |
| `FUN_006855d3` | `FUN_0068548f`'s `__finally` | the same |
| `FUN_00685ee3` | `__wfsopen`'s `__finally` | the same |
| `FUN_006883f0` | `FUN_00688271`'s `__finally` | `_unlock_file(EDI)` |
| `FUN_00688494` | `printf`'s `__finally` (Ghidra: `FID_conflict:_wprintf`) | `_unlock_file2(1, __iob_func() + 0x20)`, stream 1 (`stdout`) |
| `FUN_006888ef` | `flsall`'s inner `__finally` | `_unlock_file2(ESI, __piob[ESI])` (`DAT_00993c60`) |
| `FUN_0068891e` | `flsall`'s `__finally` | `_unlock(1)` |
| `FUN_00688970` | `_fflush`'s `__finally` | `_unlock_file` of its frame's stream |
| `FUN_00688d8d` | `FUN_00688cd7`'s `__finally` | `_unlock(0xc)` |
| `FUN_00688f31` | `__freefls@4`'s first `__finally` | `_unlock(0xd)` |
| `FUN_00688f3d` | `__freefls@4`'s second `__finally` | `_unlock(0xc)` |
| `FUN_006893d6` | `_unlock` | `LeaveCriticalSection(*(0x00749f50 + n * 8))` |
| `FUN_006894bd` | `__mtinitlocknum`'s `__finally` | `_unlock(0xa)` |
| `FUN_0068a023` | `raise`'s `__finally` | `_unlock(0)` when its frame's `[ebp-0x1c]` (lock taken) is set |
| `FUN_0068a9a8` | `FUN_0068a8e5`'s `__finally` | `_unlock_fhandle` (`FUN_0069aadc`) of its frame's handle |
| `FUN_0068aae2` | `_lock_file` | a `FILE *` inside the built-in table `_iob` (`0x0074a290`..`0x0074a4f0`, 0x20 bytes each): `__lock(0x10 + index)` and flag 0x8000 set; any other stream: `EnterCriticalSection` at the stream + 0x20 |
| `FUN_0068ab1e` | `_lock_file2` | the same by index: under 20, `__lock(0x10 + index)` and flag 0x8000; else the stream's own section |
| `FUN_0068ab4c` | `_unlock_file` | the reverse of `FUN_0068aae2`: flag 0x8000 cleared and `_unlock(0x10 + index)`, else `LeaveCriticalSection` |
| `FUN_0068ab82` | `_unlock_file2` | the reverse of `FUN_0068ab1e` |
| `FUN_0068b379` | `FUN_0068b24c`'s `__finally` | `_unlock(1)` |
| `FUN_0068ff56` | `__msize`'s `__finally` | `_unlock(4)` |
| `FUN_00691201` | `___updatetmbcinfo`'s `__finally` | `_unlock(0xd)` |
| `FUN_006915fa` | `__setmbcp`'s `__finally` | `_unlock(0xd)` |
| `FUN_0069197b` | `___updatetlocinfo`'s `__finally` | `_unlock(0xc)`, then reloads `ESI` from its frame |
| `FUN_00692ea2` | `FUN_00692dd0`'s `__finally` | `_unlock_fhandle` of its frame's handle |
| `FUN_00693680` | `FUN_0069358d`'s `__finally` | the same |
| `FUN_00693849` | `FUN_00693777`'s `__finally` | the same |
| `FUN_00695504` | `_Type_info_dtor`'s `__finally` | `_unlock(0xe)` |
| `FUN_00696ce7` | `FUN_00696a38`'s `__finally` | `_unlock(7)` |
| `FUN_00697192` | `___tzset`'s `__finally` | `_unlock(6)` |
| `FUN_0069720b` | `__isindst`'s `__finally` | `_unlock(6)` |
| `FUN_00697c7c` | `__getcwd`'s `__finally` | `_unlock(7)` |
| `FUN_00697dcb` | `__calloc_impl`'s `__finally` | `_unlock(4)` |
| `FUN_0069a283` | `FUN_0069a1ac`'s `__finally` | `_unlock_fhandle` of its frame's handle |
| `FUN_0069aad3` | `___lock_fhandle`'s `__finally` | `_unlock(0xa)` |
| `FUN_0069aadc` | `_unlock_fhandle` | `LeaveCriticalSection` at `__pioinfo[fh >> 5] + (fh & 0x1f) * 0x40 + 0xc` (the handle blocks `DAT_00993b60`) |
| `FUN_0069abd1` | `__alloc_osfhnd`'s first `__finally` | `_unlock(0xa)` |
| `FUN_0069ac8f` | `__alloc_osfhnd`'s second `__finally` | `_unlock(0xb)` |
| `FUN_0069afde` | `FUN_0069aecf`'s `__finally` | `_unlock_fhandle` of its frame's handle |
| `FUN_006a7e26` | `___unDName`'s `__finally` | `_unlock(5)` |
