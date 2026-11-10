# BNGatewayAccess — Battle.net gateway list + private INI parser

Source-file breadcrumb: `.\BNetGW.cpp` (present in every assertion call site).
Address range: `0x00517e40`–`0x00518b55` + globals at
`0x0042e680`/`0x0042e690`.

Most routines have MSVC decorated exports as `BNGatewayAccess` members;
`FUN_00518300` is an unexported helper. The demangled names and signatures
are useful starting points, and the code is the authority for behavior.

`tools/emu/bnetgw.py` executes selected parser, accessor, and selection paths
from stock game.exe 1.14d on synthetic inputs (`D2_MPQ_DIR=<data> uv run
python bnetgw.py` from `tools/emu`). File IO, configuration, live
Battle.net, malformed buffers beyond the cases below, and concurrency are
not exercised. The remaining function descriptions are static traces.

## Class layout — 0x24 bytes

Inferred from `operator=` (copies 9 dwords), `Load` field zeroing, and
callsite offsets.

| off  | type    | field         | notes                                                     |
|------|---------|---------------|-----------------------------------------------------------|
| 0x00 | u32     | ?flag0        | zeroed in `Load`; set to 1 during list rebuild            |
| 0x04 | u32     | dirty         | 1 after `SetCurGateway`; guards write-back                |
| 0x08 | i32     | numGateways   | count of entries                                          |
| 0x0c | i32     | currentIndex  | 1-based, clamped to 99                                    |
| 0x10 | char*   | buffer        | heap: null-separated triples, double-null terminator      |
| 0x14 | i32     | bufferSize    | bytes in `buffer` including double-null                   |
| 0x18 | u32     | version       | `[Server List Version] VER=` from parsed INI; ≥1000 valid |
| 0x1c | u32     | usesOverride  | 1 if `"Override Battle.net gateways"` section used        |
| 0x20 | char*   | realmsBuf     | `Data\Global\Realms.bin` contents                         |

`NumGateways @ 0x0042e680` and `CurGateway @ 0x0042e690` are exported data
symbols, but they're **16 bytes apart** — so they aren't `numGateways` and
`currentIndex` fields of a single instance (those would be 4 bytes apart).
Likely two separate mirror globals updated alongside the class fields, kept
around for legacy external readers. Confirm by xref before trusting.

## Buffer layout

`buffer` (field +0x10) holds the gateway list as a packed triples table:

```
"<version>\0"   "<currentIdx-2digit>\0"       ← 2-line prelude
"east.example\0"  "-5\0"    "USEast\0"        ← 1st gateway: DNS, gmt, display name
"west.example\0"  "-8\0"    "USWest\0"
...
"\0\0"                                        ← double-null terminates
```

Version is written as `%d`, currentIdx as two ASCII digits at
`buffer[strlen(version)+1]`. Assertions in `SaveAndUnload` confirm the
double-null tail — it's the "packet-well-formed" invariant.

## Function map

| addr       | mangled               | signature                                                                                                                                             | notes                                                                                                                       |
|------------|-----------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------|
| `0x517e40` | `??4…@@QAEAAV0@ABV0@` | `BNGatewayAccess& operator=(const BNGatewayAccess&)`                                                                                                  | copies 9 dwords; no deep buffer copy                                                                                        |
| `0x517e60` | `?GetSystemTimeZone…` | `private int GetSystemTimeZone()`                                                                                                                     | returns Windows bias in minutes, except Australian English locale (0x0c09) returns 480                                            |
| `0x517ed0` | `?SaveAndUnload…`     | `public void __stdcall SaveAndUnload()`                                                                                                               | writes `Configuration/Diablo II Battle.net gateways` (or `Override…`) via `FUN_00415000`; frees `buffer` and `realmsBuf`   |
| `0x517fd0` | `?Nth…`               | `private const char* Nth(int)`                                                                                                                        | walks triple-index into buffer (3 strings/entry). Returns start of DNS string; invalid index or an out-of-bounds offset returns `this` |
| `0x518040` | `?DNS…`               | `public const char* __stdcall DNS(int)`                                                                                                               | thin wrapper: returns `Nth(i)` = DNS string                                                                               |
| `0x518060` | `?GMT…`               | `public int __stdcall GMT(int)`                                                                                                                       | steps past DNS string, `strtol` on gmt string                                                                             |
| `0x5180a0` | `?Name…`              | `public const char* __stdcall Name(int)`                                                                                                              | steps past DNS+gmt, returns name pointer                                                                                  |
| `0x5180e0` | `?Realm…`             | `public const char* __stdcall Realm(int)`                                                                                                             | scans `realmsBuf` for a key matching the gateway DNS string; returns its realm string                                          |
| `0x518150` | `?SetCurGateway…`     | `public void __stdcall SetCurGateway(int)`                                                                                                            | validates 1..numGateways, cap 99, sets dirty flag                                                                           |
| `0x518190` | `?GetGatewayList…`    | `private void GetGatewayList(const char*)`                                                                                                            | reads a named section from `Configuration` config; parses version prelude via `strtoul`                                    |
| `0x518270` | `?GetBattlenetGate…`  | `private void GetBattlenetGatewayList()`                                                                                                              | tries `"Override Battle.net gateways"` then falls back to `"Diablo II Battle.net gateways"`; sets `usesOverride`         |
| `0x5182b0` | `?GetBattlenetRealm…` | `private void GetBattlenetRealmsList()`                                                                                                               | slurps `Data\Global\Realms.bin` into `realmsBuf`                                                                            |
| `0x518300` | *(no export)*         | `static int ReadDefaultGateways(char** out)`                                                                                                          | slurps `DATA\GLOBAL\gateways.txt`                                                                                           |
| `0x518360` | `?WriteDefaultGate…`  | `private void WriteDefaultGatewayList()`                                                                                                              | reads `gateways.txt` then calls `UpdateGatewaysFromIni`                                                                     |
| `0x518390` | `?SkipToEOL…`         | `private char* SkipToEOL(char*, char*)`                                                                                                               | walk forward until `\r`, `\n`, `\0`, or end                                                                                 |
| `0x5183c0` | `?SkipEOL…`           | `private char* SkipEOL(char*, char*)`                                                                                                                 | walk forward past `\r`/`\n` run                                                                                             |
| `0x5183f0` | `?FindSection…`       | `private char* FindSection(char*, const char*)`                                                                                                       | scan for `[` anywhere, then compare the name prefix case-insensitively; returns after EOL                                                          |
| `0x5184c0` | `?FindKey…`           | `private char* FindKey(char*, const char*)`                                                                                                           | scan line starts for case-insensitive key prefix, then return one byte past it                                                                |
| `0x518580` | `?PickClosestZone…`   | `private void PickClosestZone(int minutesGMT)`                                                                                                        | scans all entries, picks smallest absolute difference between GMT × 60 and minutes; ties keep the first                                                              |
| `0x5186d0` | `?Load…`              | `public void __stdcall Load()`                                                                                                                        | full init: realms → gateways → default fallback → parse count/current → auto-pick if index invalid                          |
| `0x518850` | `?UpdateGatewaysFrom…`| `public void __stdcall UpdateGatewaysFromIni(char*)`                                                                                                  | parses `[Server List Version] VER=N` + `[Server Gateways] N=<line>` records + `[<name>] ZONE=<gmt> ENU=<realm>` sections   |

## INI dialect

`FindSection` and `FindKey` use private, permissive parsing rather than the
Windows INI API. The stock-executable cases in `bnetgw.py` demonstrate:

- `FindSection` scans for `[` anywhere, including mid-line. It compares only
  the requested name's length, case-insensitively: `[ALPHAbeta]` matches
  `alpha`. It does not validate a closing `]` before skipping the line.
- `FindKey` compares a case-insensitive prefix at each line start, then skips
  one byte without checking for `=`. Thus `KEYXvalue` for key `KEY` returns
  `value`; `KEY =value` returns `=value`. The first match wins.
- Key scanning stops on NUL or `]` at a line start, not `[`; it can find a
  key in a later section. Line scanning treats CR and LF as terminators and
  skips runs of both. No whitespace trimming or comment processing is done.
- Section and key routines determine their end from the first NUL. Their
  behavior on malformed or unterminated input is not generally validated;
  a failed section scan can enter a fatal assertion.

The gateway data consumer assumes a well-formed input; the examples below
show intended input rather than enforcing a strict parser grammar.

`gateways.txt` shape (deduced from `UpdateGatewaysFromIni`):

```
[Server List Version]
VER=1234

[Server Gateways]
1=useast.battle.net
2=uswest.battle.net

[useast.battle.net]
ZONE=-5
ENU=USEast

[uswest.battle.net]
ZONE=-8
ENU=USWest
...
```

## What isn't here (yet)

- **General D2 config API.** `FUN_00414de0("Configuration", ...)` is the
  actual read entry point; `FUN_00415000("Configuration", ...)` writes it.
  These live outside this range and use different plumbing (registry?
  `D2.ini`?). Grep for the string `"Configuration"` in `game-strings.tsv`
  and walk the callers — likely 100–200 lines of code.
- **Debug/logger.** `FUN_00518d40("Recording gateway list selection")`,
  `FUN_00518d40("Find gateway closest to %02d.%02d")` — small varargs
  logger in the same segment, worth its own 30-line doc.
- **Assertion helpers.** `FUN_00408090` / `FUN_00408a60` / `FUN_00681e09` —
  the `__debugbreak`+message+exit trio, seen in every `SaveAndUnload`-style
  guard. Third one is `[[noreturn]]` (`WARNING: Subroutine does not return`).
- **Allocator.** `FUN_00413020(size, ".\\BNetGW.cpp", line, 0)` allocates,
  `FUN_00412650(ptr, ".\\BNetGW.cpp", line, 0)` frees. The `__FILE__`/
  `__LINE__` pair is preserved — this is Blizzard's Storm-style tracked
  allocator (`SMemAlloc`/`SMemFree`). Confirms Storm was statically linked
  into the 1.14d monolith, as we already suspected from the exported
  `SMemFree()` and `SErrSetBlizzardErrorFunction` strings.

## Would we port this?

Not yet. `BNGatewayAccess` reads a Battle.net server list that our
singleplayer-first engine has no use for. Documenting it is enough to prove
the tooling flow works end-to-end. If/when networking lands, revisit and
port then — file IO, error paths and malformed inputs need further
verification before treating this as a complete specification.
