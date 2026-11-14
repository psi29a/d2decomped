# MSVC C runtime helpers Ghidra did not name — game.exe 1.14d

game.exe links the Visual Studio 2005 C runtime statically. Ghidra's
signatures name most of it (`__invoke_watson`, `_fclose`, `_sprintf`, …),
but a few small helpers keep `FUN_` names, and because named CRT routines
call them, the gap map listed them as frontier. Nothing in them is specific
to the game; d2d uses its own C++ standard library and ports none of them.
Read from the Ghidra decompile and listing; no oracle ran, so all of it is
**cited**. Names are in `names.tsv`. In address order:

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
