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

## Exit and the heap

| Address | Name | What it does |
|---|---|---|
| `FUN_00681ac8` | `_initterm` | calls each non-null function pointer from `ESI` up to (not including) its stack argument. Caller `FUN_00681d27` (`doexit`, unnamed-frontier.md) |
| `FUN_00683700` | `__onexit_nolock` | decodes the atexit table's start (`DAT_00994c98`) and end (`DAT_00994c94`) with `FUN_00688bbd`; when the block (`__msize`) is full, grows it with `__realloc_crt` by its own size up to 0x800 bytes, or else by 0x10, and stores the new start encoded (`FUN_00688b46`); writes the encoded function at the end and the new end; returns the function, or 0 when the table can't grow. Caller `__onexit` |
| `FUN_00683dc0` | `_recalloc` | count × size (over 0xffffffe0 / size: `errno` 0xc (`ENOMEM`), `_invalid_parameter`, 0); `_realloc` to it and zero the bytes past the old `__msize` |
| `FUN_0068fd4c` | the allocators' retry wait | `Sleep(n)`; returns n + 1000, or -1 when that passes `DAT_0096dfbc` (the longest wait). No caller Ghidra lists |
| `FUN_0069004a` | `__sbh_find_block` | the small-block heap's region headers from `DAT_00993b34`, 0x14 bytes each, `DAT_00993b30` of them: returns the first whose +0xc base is within 0x100000 bytes below the argument, else 0. The loop is at `0x0069005c`; Ghidra makes a separate function of its tail. Callers `_realloc`, `_free`, `__msize` |
| `FUN_0069006e` | `__sbh_find_block`'s loop tail | the end test of `FUN_0069004a`'s loop: back to `0x0069005c` while below the end, else 0 (its only caller is Ghidra's thunk of the same tail) |

## Streams

A stream is the CRT's 0x20-byte `FILE` (`_ptr` +0, `_cnt` +4, `_base` +8,
`_flag` +0xc, `_file` +0x10, `_bufsiz` +0x18, `_tmpfname` +0x1c); one the
CRT allocates is 0x38 bytes, a critical section at +0x20 after it. The
"text mode" check several of them make reads the stream's handle block
(`DAT_00993b60`, 0x40 bytes a handle, or `DAT_0074a510` for handles -1 and
-2): byte +0x24 bits 0..6 (the Unicode modes) or bit 7 set on a stream
without `_IOSTRG` (0x40) is `EINVAL` (0x16) and `_invalid_parameter`.

| Address | Name | What it does |
|---|---|---|
| `FUN_00681f6a` | `_fclose_nolock` | null: `EINVAL`, -1; a stream in use (`_flag & 0x83`): `__flush`, `__freebuf`, `_close(_fileno)` (`FUN_0068a8e5`; below 0: -1), else frees `_tmpfname`; `_flag` = 0. Caller `_fclose` |
| `FUN_00682058` | `fputc` | null stream: `EINVAL`, -1; `_lock_file`; the text-mode check; `--_cnt` below 0 flushes the character with `FUN_0068abac` (`_flsbuf`, unnamed-frontier.md's `crt_fputc`), else stores it at `_ptr++` and returns it as a byte; `_unlock_file`. Called from game code (game-calls.tsv) |
| `FUN_00682526` | `_snprintf_c_l` (4 fixed arguments) | `__vsnprintf_c_l(arg 1, arg 2, arg 3, arg 4, &arg 5)`. No caller |
| `FUN_006842ea` | `_fwrite_nolock` | size × count bytes (either 0: 0; null buffer or stream, or an overflow: `EINVAL`, 0): copies into the buffer while it has room; a whole number of buffers (`_bufsiz`, else 0x1000) straight to `_write` (`FUN_00692dd0`) after `__flush`; a byte at a time through `_flsbuf` otherwise; an error sets `_IOERR` (0x20) and returns the whole items written. Caller `_fwrite` |
| `FUN_006844cb` | `_fread_nolock_s` | the reading twin of `FUN_006842ea` with a buffer size: from the buffer, then whole buffers with `_read` (`FUN_0069358d`), else `_filbuf` (`FUN_00692eac`); more than the buffer holds clears it (unless the size is -1), `errno` 0x22 (`ERANGE`) and `_invalid_parameter`; end of file sets `_IOEOF` (0x10). Caller `_fread_s` |
| `FUN_0068479d` | `_ftell_nolock` | `_lseek(fh, 0, SEEK_CUR)` (`FUN_00693777`), less what is still buffered for reading or plus what is buffered for writing; in text mode (handle flag 0x80) each buffered `\n` counts twice, re-reading the end with `_lseek(SEEK_END)` when the buffer reached it. Callers `_ftell`, `__fseek_nolock` |
| `FUN_00684bf4` | a `scanf`-family entry | `ESI` = arg 1, then `vscan_fn(0x006939ac, arg 2, arg 3, &arg 4)`. No caller |
| `FUN_00684c94` | `fprintf` | null stream or format: `EINVAL`, -1; `_lock_file`, the text-mode check, `__stbuf`, `FUN_0068b5a3(stream, format, 0, &args)` (`_output_l`, unnamed-frontier.md's `crt_output`), `__ftbuf`, `_unlock_file`. Called from game code (game-calls.tsv) |
| `FUN_0068548f` | `_vfprintf_helper` | the same as `FUN_00684c94` through a given output function: `fn(stream, format, locale, args)` |
| `FUN_00685637` | `vfprintf` | `FUN_0068548f(FUN_0068b5a3, stream, format, 0, args)`. Called from game code (game-calls.tsv) |
| `FUN_00688536` | `_get_printf_count_output` | 1 when `DAT_0096d6cc` equals the security cookie (`DAT_00749de0`) with bit 0 set, else 0: whether `%n` is allowed. Caller `FUN_0068b5a3` |
| `FUN_0068aa0b` | `__iob_func` | returns `0x0074a290`, the 20 built-in streams |
| `FUN_0068afac` | `_openfile` | skips spaces, then the mode: `r`, `w` (0x301), `a` (0x109), and the modifiers `+`, `b`, `t`, `c`, `n`, `S`, `R`, `T`, `D`, `N`, and `, ccs=UTF-8` / `UTF-16LE` / `UNICODE`; a repeat or unknown letter: `EINVAL`, 0; `__sopen_s(name, flags, share, 0x180)`; counts the stream in `DAT_0096dea4` and fills it (`_flag`, `_file`, the rest 0). Caller `__fsopen` |
| `FUN_0068b24c` | `_getstream` | under lock 1: the first free slot of `__piob` (`DAT_00993c60`, `DAT_00994c80` of them), allocating a 0x38-byte stream and its section (spin count 4000) for an empty slot; the stream comes back locked; its `_flag` keeps only 0x8000, `_file` -1. Callers `__fsopen`, `__wfsopen` |
| `FUN_00692eac` | `_filbuf` | refills the read buffer with `_read`: needs a stream open for reading (`_flag & 0x83`, not `_IOSTRG`, not only write); gets a buffer (`__getbuf`) when it has none; 0 bytes sets `_IOEOF`, -1 `_IOERR`; a 0x200-byte buffer of a stream the CRT buffered grows to 0x1000; returns the first byte. Callers `FUN_006844cb`, `FUN_00688271` |
| `FUN_006a2f76` | `_ungetc_nolock` | the text-mode check; -1 or a stream not open for reading returns -1; steps `_ptr` back (onto a fresh buffer when there is none) and stores the byte, or on an `_IOSTRG` stream only checks it is the same; clears `_IOEOF`. No caller |
| `FUN_006aaefc` | `_get_fmode` | null: `EINVAL`; else `*out = DAT_0096e268`, 0. Callers `FUN_0069b08a`, `FUN_006a8b91` |

## Low-level I/O

A file handle `fh` indexes the handle blocks `__pioinfo` (`DAT_00993b60`):
block `fh >> 5`, entry `(fh & 0x1f) * 0x40`, the OS handle at +0, the flags
at +4 (bit 0 open, bit 1 cleared by a successful seek, bit 6 a device, bit 7 text), the
lock at +0xc. `DAT_00993b50` is the handle count. The checked entry points
share a shape: -2 sets `_doserrno` 0 and `errno` 9 (`EBADF`); a handle out of
range or not open does the same and calls `_invalid_parameter`; both return
-1. Otherwise `___lock_fhandle`, the open bit checked again, the `_nolock`
routine, and its `__finally` (Locks).

| Address | Name | What it does |
|---|---|---|
| `FUN_0068a84e` | `_close_nolock` | `CloseHandle` of the OS handle, except for handle 1 or 2 when both are open and share one OS handle; `_free_osfhnd` (`FUN_0069a94a`), the flags 0; a failure maps through `_dosmaperr` and returns -1. Callers `FUN_0068a8e5`, `FUN_0069b08a`, `FUN_006a8b91` |
| `FUN_0068a8e5` | `_close` | the checked entry for `FUN_0068a84e`. Caller `FUN_00681f6a` |
| `FUN_006927ae` | `_write_nolock` | (from its calls; the 1,570-byte body was not followed) writes with `WriteFile`; in text mode through `_mbtowc` / `WideCharToMultiByte`, and to a console through `GetConsoleMode`, `GetConsoleCP` and `__putwch_nolock`. Callers `FUN_00692dd0`, `__chsize_nolock` |
| `FUN_00692dd0` | `_write` | the checked entry for `FUN_006927ae`. Callers `FUN_006842ea`, `__flush`, `FUN_0068abac`, `FUN_0069b08a`, `FUN_006a8b91` |
| `FUN_00692fcc` | `_read_nolock` | (from its calls; the 1,473-byte body was not followed) reads with `ReadFile`, a `__malloc_crt` buffer and `MultiByteToWideChar` for the Unicode modes. Callers `FUN_0069358d`, `FUN_0069b08a`, `FUN_006a8b91` |
| `FUN_0069358d` | `_read` | the checked entry for `FUN_00692fcc`; a count of 0x80000000 or more is `EINVAL`. Callers `FUN_006844cb`, `FUN_00692eac` |
| `FUN_00693705` | `_lseek_nolock` | `SetFilePointer(handle, offset, NULL, origin)`; success clears flag bit 1, failure maps through `_dosmaperr` and returns -1. Callers `FUN_00693777`, `FUN_0069b08a`, `FUN_006a8b91` |
| `FUN_00693777` | `_lseek` | the checked entry for `FUN_00693705`. Callers `FUN_0068479d`, `__fseek_nolock` |
| `FUN_0069a1ac` | `_commit` | the checked entry: `FlushFileBuffers`; a failure sets `_doserrno` to it and `errno` 9. Caller `__fflush_nolock` |
| `FUN_0069a8cd` | `_set_osfhnd` | an open-range handle whose OS handle is still -1 takes the new one; when `DAT_00749df0` is 1, handles 0, 1, 2 also `SetStdHandle(STD_INPUT/OUTPUT/ERROR_HANDLE)`. Callers `FUN_0069b08a`, `FUN_006a8b91` |
| `FUN_0069a94a` | `_free_osfhnd` | the reverse: an open handle's OS handle back to -1, the standard handle set to null the same way. Callers `FUN_0068a84e`, `FUN_0069b08a`, `FUN_006a8b91` |
| `FUN_0069a9cb` | `_get_osfhandle` | an open handle's OS handle, else `EBADF`, `_invalid_parameter`, -1. Callers `FUN_0068a84e`, `FUN_00693705`, `FUN_0069a1ac`, `FUN_0069ae4c`, `__chsize_nolock` |
| `FUN_0069ae4c` | `_lseeki64_nolock` | `SetFilePointer` with the high half; returns the 64-bit position, -1 on failure; clears flag bit 1. Callers `FUN_006927ae`, `FUN_00692fcc`, `FUN_0069aecf`, `FUN_0069b08a`, `FUN_006a8b91`, `__chsize_nolock` |
| `FUN_0069aecf` | `_lseeki64` | the checked entry for `FUN_0069ae4c`. Caller `FUN_0068abac` |
| `FUN_0069b02c` | `_isatty` | flag bit 6 of the handle (-2: `EBADF`, 0; out of range: also `_invalid_parameter`). Callers `FUN_0068abac`, `FUN_006927ae`, `__stbuf` |
| `FUN_0069b08a` | `_sopen_nolock` | (from its calls; the 1,875-byte body was not followed) `__alloc_osfhnd`, `CreateFileA`, `GetFileType`, `_set_osfhnd`, `_get_fmode`, `__get_osplatform`; it also calls `_read_nolock`, `_lseek_nolock`, `_lseeki64_nolock`, `_write`, `__chsize_nolock`, `_close_nolock` and `_free_osfhnd`, what for not followed. Caller `__sopen_helper` (Ghidra: `FID_conflict:__sopen_helper`) |
| `FUN_0069b937` | the open helper's `__finally` | when its frame shows the open failed after a handle was taken (`[ebp-0x1c]`, `[ebp-0x20]`), clears the handle's open bit; then `_unlock_fhandle`. Caller `__sopen_helper` |
| `FUN_006a8b91` | `_wsopen_nolock` | the same as `FUN_0069b08a` with `CreateFileW`. Caller the wide `__sopen_helper` |
| `FUN_006a943f` | the wide open helper's `__finally` | the same as `FUN_0069b937` |
| `FUN_006aadfa` | `_setmode_nolock` | sets the handle's mode: 0x4000 text (bit 7, Unicode bits off), 0x8000 binary (bit 7 off), 0x10000 / 0x20000 UTF-16 (+0x24 = 2), 0x40000 UTF-8 (+0x24 = 1); returns the old mode, 0x4000, 0x8000 or 0x10000. Caller `__chsize_nolock` |
