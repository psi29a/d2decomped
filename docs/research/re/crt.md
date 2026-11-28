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
| `FUN_00682f3d` | the thread FP hook | a bare `ret`. The pointers at `0x006f2878` and `0x006f287c` hold it; `__endthreadex` calls through the second, and the thread start code at `0x006880f7` (no Ghidra function; it then calls `__callthreadstartex`) through the first, each when `__IsNonwritableInCurrentImage` allows |
| `FUN_00688bbd` | `DecodePointer` wrapper (`_decode_pointer`) | takes the CRT's per-thread function table from TLS (`DAT_00749e94` / `DAT_00749e90`, entry +0x1fc), else `GetProcAddress(GetModuleHandleA("KERNEL32.DLL"), "DecodePointer")`; calls it on the argument, or returns the argument unchanged when there is none |
| `FUN_00688c49` | the TLS index | returns `DAT_00749e90`. Caller `__callthreadstartex` |
| `FUN_00688c81` | the FLS setter call | decodes the pointer `DAT_0096d9fc` (`FUN_00688bbd`) and calls it with its two arguments (`ret 8`). Caller `__callthreadstartex` |
| `FUN_00688d96` | `_getptd_noexit` | saves `GetLastError`, reads the thread's data block from the TLS slot; on first use `__calloc_crt(1, 0x214)`, stores it, initialises it (`FUN_00688cd7`), sets +0 = `GetCurrentThreadId()` and +4 = -1; frees it and returns 0 if the store fails; restores the last error |
| `FUN_006894f7` | `__initp_misc_invarg` | stores its argument in `DAT_0096de70`, the invalid-parameter handler. Caller `__init_pointers` |
| `FUN_0068962c` | a forwarder | a frame pushed and popped, then a tail jump to `__invoke_watson`. No caller |
| `FUN_00689635` | `_invalid_parameter` | decodes `DAT_0096de70` (the invalid-parameter handler) with `FUN_00688bbd`; non-null: `JMP EAX`, so the caller's five arguments reach the handler as they were pushed; null: `FUN_0069a28d(2)` then a tail jump to `__invoke_watson` (`FUN_00689501`), which raises 0xC000000D and terminates the process |
| `FUN_00689669` | a forwarder | the same to `FUN_00689635`. No caller |
| `FUN_00689672` | `_get_errno_from_oserr` | a Windows error to an `errno`: the 45 pairs at `0x0074a070`; else 19..36 → 13 (`EACCES`), 188..202 → 8 (`ENOEXEC`), the rest 22 (`EINVAL`) |
| `FUN_006896ad` | `_errno` | `FUN_00688d96()`; null returns `0x0074a1d8` (the static fallback), else the block + 8 |
| `FUN_006896c0` | `__doserrno` | the same with + 0xc and fallback `0x0074a1dc` |
| `FUN_006896d3` | `_dosmaperr` | `_doserrno` = the Windows error, `errno` = `FUN_00689672` of it. Callers include `__fullpath`, `__beginthreadex` and the low-level I/O routines |
| `FUN_006899bf` | an empty start-up hook | a bare `ret`. Caller `__init_pointers` |
| `FUN_006899f2` | `_XcptFilter` | the thread's exception-action table (`FUN_00688d96()` +0x5c, `DAT_0074a28c` entries of 12 bytes): no entry or no action: 0; action 5: cleared, 1; action 1: -1; else the handler is called, with the exception pointers in +0x60 meanwhile: for signal 8 (`SIGFPE`) the FP entries are cleared and the floating-point code (+0x64) set from the exception (0x81..0x8a), else the entry is cleared first; -1. Caller `__callthreadstartex` |
| `FUN_0068a05f` | a start-up pointer setter | stores its argument in `DAT_0096de8c`. Caller `__init_pointers` |
| `FUN_0068a2c7` | the same | `DAT_0096de98` |
| `FUN_0068a2d1` | the same | `DAT_0096de9c` |
| `FUN_0068a3b0` | the same | `DAT_0096dea0` |
| `FUN_0069a28d` | `_crt_debugger_hook` | 8 bytes: writes 0 to `DAT_00993b20`; the 2 pushed by `FUN_00689635` (`_CRT_DEBUGGER_INVALIDPARAMETER`) is not read |

The CRT's argument checks use the first two together. `__get_winmajor`
(`FUN_00681baf`), for one, with a null pointer or before start-up has set
`DAT_0096d674`, runs `*_errno() = 0x16` (`EINVAL`), then
`_invalid_parameter(0, 0, 0, 0, 0)`, and returns 0x16. The same calls are in
`__get_osplatform`, `_fclose`, `_sprintf`, `_fwrite`, `__strlwr` and
`FUN_00688271` (game-calls.tsv).

`__init_pointers` (`FUN_00681e49`) passes `__encoded_null()` to
`FUN_006894f7`, the only code that writes `DAT_0096de70`: Ghidra's
references and a byte scan of game.exe agree. `_set_invalid_parameter_handler`
is at `0x006895fd` (it decodes the old handler, stores the new one encoded
and returns the old) and `_get_invalid_parameter_handler` at `0x0068961f`,
neither a Ghidra function, and no call, jump or pointer in game.exe reaches
either. So the handler stays null, and a CRT argument check that calls
`_invalid_parameter` always ends the process through `__invoke_watson`.

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

## Math

The game calls five math wrappers Ghidra didn't name (unnamed-frontier.md):
`FUN_00687ac0` (`log`), `FUN_00687c10` (`pow`), `FUN_00687ea0` (`log10`),
`FUN_00688590` (`sin`) and `FUN_006886c0` (`cos`). Each takes the SSE2 body
when `DAT_00994c84` is set and the FPU and MXCSR mask all exceptions, else
the x87 one. The x87 bodies are told apart by their instruction:
`fldln2; fyl2x` is `log`, `fldlg2; fyl2x` `log10`, `fsin` and `fcos` the two
others, `fyl2x` then `f2xm1` `pow`; the SSE2 body each one reaches is named
after it. An x87 body ends through `__math_exit`, or, on an error, through
`__startOneArgErrorHandling` with the error type in `EAX` (1 domain, 2 a
pole, 7 a quiet NaN), unless `DAT_0096d6c4` is set.

| Address | Name | What it does |
|---|---|---|
| `FUN_00683080` | `floor` | SSE2 when allowed, else `__floor_default`: under 1 in size, 0 or -1; under 2^52, the integer part, less 1 for a negative with a fraction; a NaN goes to `___libm_error_support` (0x3ed). Called from game code (game-calls.tsv) |
| `FUN_006879dd` | `sqrt`'s x87 body | `fsqrt`; a negative argument (not -0) is a domain error. Caller `__CIsqrt` |
| `FUN_00687b18` | `log`'s x87 body | `fldln2; fyl2x`; 0 is a pole error, a negative a domain error, a NaN `FUN_0069877c`. Caller `FUN_00687ac0` |
| `FUN_00687c6d` | `pow`'s x87 body | x^y as 2^(y·log2 x) (`fyl2x`, `FUN_00698750`); a negative x needs an integer y (`FUN_00687e32`), the sign from its parity; infinite operands through `FUN_00699924`; the result checked by `FUN_00698839`; errors to `FUN_006986f0`. Caller `FUN_00687c10` |
| `FUN_00687e32` | `pow`'s integer test | `CL` = 0 when `ST0` is not an integer, 1 an odd integer, 2 an even one (half of it, ×0.5 from `DAT_00749e50`, also an integer) |
| `FUN_00687ef8` | `log10`'s x87 body | `fldlg2; fyl2x`, the same checks as `FUN_00687b18`. Caller `FUN_00687ea0` |
| `FUN_006885df` | `sin`'s x87 entry from the SSE2 body | loads the stack argument (`__fload_withFB`) and runs on into `FUN_006885e8`. Caller `FUN_00699e4e` |
| `FUN_006885e8` | `sin`'s x87 body | `fsin`; when the FPU reports the argument out of range (C2), reduces it by the constant at `DAT_006f333a` until it isn't and runs `fsin` again; Inf or NaN through `FUN_0069877c`. Caller `FUN_00688590` |
| `FUN_0068870f` | `cos`'s x87 entry from the SSE2 body | the same into `FUN_00688718` (`fcos`, cited). Caller `FUN_0069a01e` |
| `FUN_006986f0` | the two-argument error start | the error type and the stack arguments into its frame, then joins `__startOneArgErrorHandling`'s body. Callers `FUN_00687c6d`, `FUN_00698839` |
| `FUN_00698750` | 2^x | `ST0` split by `frndint`, `f2xm1` of the fraction, `fscale` by the integer part. Caller `FUN_00687c6d` |
| `FUN_00698765` | the control-word load | loads a control word of the argument's precision and rounding bits (0x300) with every exception masked (0x7f) |
| `FUN_0069877c` | the NaN case | a quiet NaN (bit 19 of `EAX`) returns 7; else adds 1.0 (`DAT_006f3330`) to `ST0` and returns 1 |
| `FUN_006987d8` | the exponent test | the double's high word when its exponent is all ones (Inf or NaN), else its exponent bits. Callers `__CIsqrt` and the wrappers but `pow` |
| `FUN_006987ee` | the x87 exit | reloads the saved control word unless it is the default 0x27f, pops it and returns |
| `FUN_00698839` | `pow`'s range check | a result with a zero exponent is scaled by 2^1536, an all-ones one by 2^-1536 (`fscale`), then error handling (error 0x1d through `FUN_006986f0`); else returns unless a precision exception is unmasked and raised |
| `FUN_006988e0` | `log`'s SSE2 bridge | aligns the stack, `ST0` into `XMM0`, calls `FUN_006988fe` |
| `FUN_006988fe` | `log`'s SSE2 body | table-driven (`0x006f3380`..`0x006f3870`); errors to `___libm_error_support` |
| `FUN_00698d60` | `pow`'s SSE2 bridge | `ST1` and `ST0` onto the aligned stack, calls `FUN_00698d79` |
| `FUN_00698d79` | `pow`'s SSE2 body | (2,872 bytes, not followed) errors to `___libm_error_support` |
| `FUN_00699924` | `pow`'s infinite cases | for an infinite x or y, the result: 0, 1, +Inf (`DAT_0074af20`) or -0 (`DAT_0074af40`), an odd integer y (`__d_inttype`) keeping a negative x's sign; y = -Inf with \|x\| = 1 gives NaN (`DAT_0074af28`) and returns 1, else 0. Caller `FUN_00687c6d` |
| `FUN_00699a70` | `log10`'s SSE2 bridge | as `FUN_006988e0`, calls `FUN_00699a8e` |
| `FUN_00699a8e` | `log10`'s SSE2 body | errors to `___libm_error_support` |
| `FUN_00699e30` | `sin`'s SSE2 bridge | as `FUN_006988e0`, calls `FUN_00699e4e` |
| `FUN_00699e4e` | `sin`'s SSE2 body | its hard cases go to the x87 body through `FUN_006885df` |
| `FUN_0069a000` | `cos`'s SSE2 bridge | as `FUN_006988e0`, calls `FUN_0069a01e` |
| `FUN_0069a01e` | `cos`'s SSE2 body | its hard cases go to the x87 body through `FUN_0068870f` |
| `FUN_0069a810` | the SSE2 probe | runs `movapd xmm0, xmm1` inside an SEH frame: 1, or 0 when it raises 0xC0000005 or 0xC000001D. Caller `__get_sse2_info` |
| `FUN_0069c68c` | the default `_matherr` | returns 0. Referenced by `___libm_error_support`, called by `__umatherr` and `__87except` |
| `FUN_0069cfd1` | `_decomp` | splits a double: the mantissa with its exponent set for [0.5, 1) (`__set_exp`), the exponent to `*out` (denormals normalised first; 0 gives 0, 0). Callers `__handle_exc`, `_ldexp`, `FUN_006aa6f2`, `FUN_006aa7d5` |
| `FUN_0069d0be` | the exception raiser | for each flag bit of its argument, an x87 operation that raises it: 1 `fistp` of the long double at `0x0074af48`, 8 and 0x10 `fstp` of the ones at `0x0074af48` and `0x0074af54` to a double, 4 1/0, 0x20 `fldpi` stored to a double. Caller `__handle_exc` |
| `FUN_006aa6f2` | `_logb` | Inf or NaN through `__sptype` / `__handle_qnan1`; 0 gives -Inf through `__except1` (operation 0x25); else `_decomp`'s exponent less 1. No caller |
| `FUN_006aa7d5` | `_nextafter` | two doubles; Inf, NaN and the error case through `__handle_qnan2` / `__except2` (operation 0x26); the step through `_decomp` and `__set_exp`. No caller |

## Time

The time-zone state: `_timezone` at `0x0074ace8` (seconds west of UTC),
`_daylight` at `0x0074acec`, `_dstbias` at `0x0074acf0`, `_tzname` at
`0x0074ad78` (two pointers to 64-byte names).

| Address | Name | What it does |
|---|---|---|
| `FUN_006856e9` | `_localtime64_s` | null `tm`: `EINVAL`; fills it with 0xff; a time below 0 or past `0x793406fff` is `EINVAL`; `___tzset`, then the three settings through their getters (a failure is `__invoke_watson`); past 3 days (0x3f480 s), `_gmtime64_s` of the time less `_timezone`, and again less `_dstbias` with `tm_isdst` = 1 when `__isindst`; nearer 1970, `_gmtime64_s` of the time itself and the offset carried through seconds, minutes, hours and days by hand. Called from game code (game-calls.tsv) |
| `FUN_00696a38` | `_tzset_nolock` | under lock 7: no `TZ` variable (`__getenv_helper_nolock`): `GetTimeZoneInformation` into `0x0096e020`, `_timezone` = the bias × 60, plus the standard bias × 60 when a standard date is set, `_daylight` and `_dstbias` from the daylight date and bias, the two names through `WideCharToMultiByte` in the locale's code page; a `TZ` like `PST8PDT`: a copy kept in `DAT_0096e0d0` (an unchanged one is not parsed again), the first 3 letters the standard name, the offset as hours`:`minutes`:`seconds with a sign, and 3 more letters the daylight name, `_daylight` = whether there are any. Caller `___tzset` |
| `FUN_00696f76` | `_isindst_nolock` | with `_daylight` set: for a new year (`tm_year` +0x14), the start and end of daylight time from the `GetTimeZoneInformation` dates (`cvtdate`), or a built-in rule with one set of dates before `tm_year` 0x6b (2007) and another from it; then whether `tm_yday` (and the time of day in ms on the change days) falls between them. Caller `__isindst` |
| `FUN_00697474` | `_get_daylight` | null: `EINVAL`; else `*out = _daylight`, 0 |
| `FUN_006974dc` | `_get_timezone` | the same for `_timezone` |
| `FUN_006975b2` | `__daylight` | returns `&_daylight` |
| `FUN_006975b8` | `__dstbias` | returns `&_dstbias` |
| `FUN_006975be` | `__timezone` | returns `&_timezone` |
| `FUN_006975c4` | `__tzname` | returns `&_tzname` |
| `FUN_0069edda` | `_store_str` | copies the string at `EDX` to `*ECX` while the count at `*EAX` lasts, advancing both. Caller `FUN_0069eea9` |
| `FUN_0069eea9` | `_expandtime` | one `strftime` conversion in `AL`: checks the `tm` field's range (`EINVAL` and `_invalid_parameter` out of it) and writes it with `_store_num` (`FID_conflict:__store_num`) or `_store_str`; `%` itself; `c`, `x` and `X` through `FUN_0069f28f`; `z` and `Z` the time-zone name after `___tzset`; codes 4 and 13 return without writing. Callers `__Strftime_l`, `FUN_0069f28f` |
| `FUN_0069f28f` | `_store_winword` | (from its calls; the 1,117-byte body was not followed) a date or time picture expanded with `GetDateFormatA` / `GetTimeFormatA`, with `_expandtime` for its pieces. Caller `FUN_0069eea9` |

## Exceptions, RTTI and the name undecorator

The `std::exception` classes are named by the RTTI locator before each
vtable (the type descriptor's name at +8): `exception` `0x006f911c`,
`bad_exception` `0x006f310c`, `bad_cast` `0x006f913c`, `bad_typeid`
`0x006f9148`, `__non_rtti_object` `0x006f9154`; slot 0 is the deleting
destructor, slot 1 `what`. The undecorator's nodes the same way: `charNode`
`0x006fa98c`, `DNameStatusNode` `0x006fa9ac`, `pcharNode` `0x006fa9bc`
(slot 0 `length`, 1 `getLastChar`, 2 `getString`).
Only the vtables reach these.

| Address | Name | What it does |
|---|---|---|
| `FUN_00686a49` | `` `eh vector constructor iterator' ``'s `__finally` | when the loop did not finish (`[ebp-0x20]` 0), `__ArrayUnwind` of the elements built so far |
| `FUN_00686b1c` | `` `eh vector destructor iterator' ``'s `__finally` | the same for the destructor loop (`[ebp-0x1c]`) |
| `FUN_0068799e` | `_unwind_handler` | an SEH handler: `EBP` from the registration record, then `__local_unwind2(record +0x18, +0x1c)`; `ret 4`. No caller |
| `FUN_0068b45a` | `_unwind_handler4` | the same with `__local_unwind4(record +0x28, +0x18, +0x1c)`. No caller |
| `FUN_0068c263` | the C++ unhandled-exception filter | an exception record with code 0xE06D7363 (a C++ throw), 3 parameters and magic 0x19930520, 0x19930521, 0x19930522 or 0x01994000: `terminate`; else 0 |
| `FUN_0068eb66` | `bad_exception`'s deleting destructor | stores its vtable, `~exception`, then `FUN_004053f0` on itself when bit 0 of the flag is set |
| `FUN_0068ecee` | `___FrameUnwindToState`'s `__finally` | decrements `__getptd()` +0x90 (the throw-in-progress count) when above 0 |
| `FUN_0068f382` | `CallCatchBlock`'s `__finally` | unlinks the catch frame (`__FindAndUnlinkFrame`), restores the thread's current exception and context (`__getptd()`), and destroys a C++ exception object no longer referenced (`__IsExceptionObjectToBeDestroyed`, `___DestructExceptionObject`) |
| `FUN_006980a8` | `_NLG_Call` | `call eax; ret`: the local-unwind call of a `__finally` block. Callers `__local_unwind4`, `__local_unwind2` |
| `FUN_0069d36a` | `exception::what` | the stored message (+4), else `"Unknown exception"` (`0x006f9124`) |
| `FUN_0069d42a` | `exception`'s deleting destructor | `~exception`, then `FUN_004053f0` when flagged |
| `FUN_0069d446` | `bad_cast`'s deleting destructor | as `FUN_0068eb66` with vtable `0x006f913c` |
| `FUN_0069d468` | `bad_typeid`'s deleting destructor | the same with `0x006f9148` |
| `FUN_0069d48a` | `__non_rtti_object`'s deleting destructor | the same, resetting to `bad_typeid`'s vtable `0x006f9148`, its base |
| `FUN_006a39c3` | `charNode::length` | 1 |
| `FUN_006a39c7` | `charNode::getLastChar` | the character (+8) |
| `FUN_006a39cb` | `charNode::getString` | with a buffer and a length, writes the character and returns the buffer; else 0 |
| `FUN_006a39e6` | `pcharNode::length` | +0xc |
| `FUN_006a3a3e` | `DNameStatusNode::length` | +0xc |
| `FUN_006a3a42` | `DNameStatusNode::getLastChar` | `' '` (0x20) when the status (+8) is 2, else 0 |
| `FUN_006a3eea` | `pcharNode::getLastChar` | the last character of the string (+8, length +0xc), 0 when empty |
| `FUN_006a4363` | `DName::DName(__int64)` | the number in decimal, a `-` for a negative, through `doPchar`. Caller `getDimension` |
| `FUN_006a4489` | `DName::operator+=(DName *)` | a null argument changes nothing; an empty name takes the argument; an argument whose status is neither 0 nor 2 adds that status; else a `pDNameNode` (from the undecorator's heap `0x0096e228`) appended to a clone of the node list; out of memory sets the status to 3. Caller `FUN_006a4688` |
| `FUN_006a4688` | `DName::operator+(DName *) const` | a copy, then `FUN_006a4489`, or the argument when the copy is empty. No caller Ghidra lists |
| `FUN_006a7ed2` | an `UnDecorator` type step | (866 bytes, not followed) builds `DName`s; callers `getArrayType`, `getPtrRefDataType`, `getPrimaryDataType` and itself |

## Everything else

| Address | Name | What it does |
|---|---|---|
| `FUN_00687454` | `srand` | `__getptd()` +0x14 = the seed: the field `_rand` (`0x00687461`) steps as seed × 0x343fd + 0x269ec3, returning bits 16..30. Called from game code (game-calls.tsv), so the game seeds the CRT's `rand` |
| `FUN_0068c939` | `__VEC_memcpy`'s loop | copies size / 128 blocks of 128 bytes with `movdqa` (both pointers 16-byte aligned). Caller `__VEC_memcpy` |
| `FUN_00690f58` | `CPtoLCID` | the code page in `EAX`: 932 → 0x411, 936 → 0x804, 949 → 0x412, 950 → 0x404, else 0. Caller `FUN_00691284` |
| `FUN_00691284` | `_setmbcp_nolock` | the code page (`getSystemCP`); one of the 5 in the table at `0x0074a9d0` (0x30 bytes each) takes its lead-byte ranges from there; else, unless 65000, 65001 or not `IsValidCodePage`, `GetCPInfo`: a multibyte page marks its lead-byte ranges 4 and bytes 1..0xfe 8; the LCID from `FUN_00690f58`; then `setSBUpLow`. A page it can't use falls back to `setSBCS` when `DAT_0096dfc8` is set. Caller `__setmbcp` |
| `FUN_006ac5fb` | `__multtenpow12` | multiplies a 12-byte float in place by 10^n: n's octal digits pick factors from the tables at `0x0074b2f8` (n above 0) and `0x0074b458` (below), 0x54 bytes a digit position, 12 bytes a factor; too large gives Inf, too small 0. No caller Ghidra lists |
| `FUN_006ac904` | `__strgtold12_l` | (1,680 bytes, not followed) text to a 12-byte float, with `___mtold12`. Callers `__atoflt_l`, `__atoldbl_l` (both `FID_conflict:__atoflt_l` in Ghidra), `FUN_006acfc4` |
| `FUN_006acfc4` | a text-to-long-double entry | `FUN_006ac904` with its three middle flags 0, then `__ld12told`. No caller |
| `FUN_006ad08a` | `$I10_OUTPUT` | (2,240 bytes, not followed) a 10-byte float to decimal digits, with `_strcpy_s`. Caller `__fltout2` |
| `FUN_006afa8d` | `__crtsetenv` | (from its calls) a `NAME=value` string into the CRT's environment (`findenv`, `copy_environ`, `__mbschr`, `__malloc_crt` / `__recalloc_crt`) and into Windows (`SetEnvironmentVariableA`). Caller `___wtomb_environ` |
