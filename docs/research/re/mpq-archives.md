# MPQ archive management — `.\Src\D2WinArchive.cpp` + statically-linked Storm

D2 1.14d bundles Storm (`.\SOURCE\SFILE.CPP` in the strings dump, plus the
full `SMemAlloc`/`SMemFree`/`SMemHeapCreate`… symbol set at
`0x006ced04–0x006cee1c`) statically inside `game.exe`. Every MPQ call is
therefore an inlined Storm function — matching the disassembly against
public StormLib source is the cheapest port for this whole subsystem.

## The MPQ list

Filename constants live at `0x006dc728–0x006dc7b4`:

| pos | filename        | notes                                                  |
|----:|-----------------|--------------------------------------------------------|
|   0 | `patch_d2.mpq`  | 1.14d patch — overrides everything below it            |
|   1 | `d2kfixup.mpq`  | Korean-locale fixup patch                              |
|   2 | `d2delta.mpq`   | delta / incremental patch (only present on some builds) |
|   3 | `d2speech.mpq`  | voice-over base                                        |
|   4 | `d2sfx.mpq`     | sound effects                                          |
|   5 | `d2data.mpq`    | non-character game data (levels, items, monsters, txt) |
|   6 | `d2char.mpq`    | sprite / animation                                     |
|   7 | `d2Xvideo.mpq`  | LoD Bink cinematics                                    |
|   8 | `d2Xtalk.mpq`   | LoD voice-over                                         |
|   9 | `d2Xmusic.mpq`  | LoD music                                              |
|  10 | `d2music.mpq`   | classic music                                          |
|   — | `d2exp.mpq`     | LoD marker archive                                     |

The list-order in memory is a strong hint at load-priority order but not
proof; confirm by walking `FUN_004fab90` (main init) with real breakpoints
if it matters. StormLib's priority-based lookup will pick the highest
priority MPQ that contains a given filename, so `patch_d2.mpq` always wins.

The three "expansion" archives (`d2X*.mpq`) are conditionally loaded when
LoD is installed — the check is a `GetFileAttributesA` on `d2exp.mpq` at
one of the callers.

## Function map — `D2WinArchive.cpp`

Range `0x004fa9b0–0x004fb100` (thin C++ wrappers around Storm).

| addr       | signature                                                        | notes                                                                                                    |
|------------|------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------|
| `0x4fa9b0` | `HSFILE OpenFile(const char* localName, HANDLE mpq)`             | Copies name to stack, calls `FUN_00517079(0, ".\\Src\\D2WinArchive.cpp", 0x41)` (Storm read wrapper) then `FUN_00601340(handle, &out, __FILE__, __LINE__, -1, 0)`. Returns `out`. This is `SFileOpenFileEx`-shaped. |
| `0x4faa40` | `HSFILE OpenFileWithFlags(const char* name, DWORD flags, HANDLE)` | Same shape as above, extra flags arg forwarded — matches `SFILE_OPEN_FROM_MPQ`/`SFILE_OPEN_PATCHED_FILE`. |
| `0x4faae0` | `void CloseFile(HSFILE)`                                         | `FUN_00601a50(h)`; then `SMemFree` (`FUN_0040b3c0(0x5c, 0)`) if handle non-null.                         |
| `0x4fab90` | `int InitPrimaryMpqs()`                                          | Opens the 7 primary archives with priorities `1000, 1000, 1000, 1000, 1000, 5000, 3000` via `FUN_00517332`. Stores handles at globals `0x007d561c/5608/5624/560c/5620/5630/5634`. |
| `0x4fac90` | `bool IsInstallMissing()`                                        | Probes for `d2char.mpq` via `GetFileAttributesA`, with one fallback path attempt. Returns `true` when missing. |
| `0x4fad10` | `int OpenExpansionMpq(...)`                                      | Opens `d2exp.mpq` under one of two priority slots (`DAT_007d5614` or `DAT_007d562c`) depending on a flag. |
| `0x4fad70` | `void CloseExpansionMpqs()`                                      | Walks 6 handles, closes each via `FUN_005173ac` (Storm close wrapper).                                   |
| `0x4fae00` | `void CloseAllMpqs()`                                            | Closes the 8 primary handles + calls `CloseExpansionMpqs`. Full teardown.                                |
| `0x4faec0` | `int InstallOptionalMpqs(name, expName, ?, callbackTable)`       | Installs `d2exp.mpq` (or `d2delta.mpq` — needs confirmation) with optional callback (VTBL slot `+0x211`). |
| `0x4fb010` | `void InitFilePools()`                                           | Sets up an array of 49 × 256-byte and 13 × 256-byte scratch buffers — used by `SFileReadFile` chunked reads. |
| `0x4fb0c0` | `void* SlotAt(int)`                                              | `return &DAT_007d6468 + i * 256;` — 256-byte slot lookup into the read-pool array.                       |
| `0x4fb0d0` | `void MaybeReleasePool()`                                        | Timed pool release: if 0x7d1 (2001 ms) since last use, decrement refcount and free.                     |

## Storm entry points (already recovered by name)

Grep `game-strings.tsv` around `0x006ced04–0x006cee1c` for the full set.
The ones that matter for a port:

- `SFileOpenArchive`  ≡ `FUN_00601340` (called by MPQ open wrappers)
- `SFileCloseArchive` ≡ `FUN_00601a50`
- `SFileOpenFileEx`   — reached via the file-open wrapper chain
- `SMemAlloc(size, __FILE__, __LINE__, 0)` ≡ `FUN_00413020` (already seen in Gateway walk)
- `SMemFree(ptr, __FILE__, __LINE__, 0)`   ≡ `FUN_00412650`
- `SMemHeapCreate`, `SMemGetSize` etc. — full Storm memory API is present but unused in the paths we've read so far.

## Priority scheme

Storm assigns each opened MPQ a priority; `SFileOpenFileEx` searches
higher-priority MPQs first. `InitPrimaryMpqs` uses:

| priority | archive(s)                    | why                                                        |
|---------:|-------------------------------|------------------------------------------------------------|
| 5000     | `d2music.mpq`                 | rarely overlaps other MPQs — cheap to keep high            |
| 3000     | `d2Xmusic.mpq`                | LoD music, same reason                                     |
| 1000     | data/char/sfx/speech/video    | base game content — all equal so first-registered wins     |

Patch archives (`patch_d2.mpq`, `d2kfixup.mpq`, `d2delta.mpq`) are opened
via a **separate** path — most likely `SFileOpenPatchArchive` layering
over the base MPQs, giving them effective priority ∞ regardless of the
number here. Look at `FUN_0051f14x` and `0x00524xxx` (the two other
clusters that reference patch_d2/d2exp/d2data) to confirm.

## Would we port this?

Yes — this is core. Every other subsystem opens files through Storm. Our
port already links StormLib (used by the launcher), so the port is mostly
adapter code: pick a priority order, open the archives, and expose a
`file_from_mpq(path) -> std::vector<std::byte>` helper. Keep an ordered
`fallback stack` of archives the way Blizzard does, not a monolithic
"patched view", so failures are legible.

## Open threads

- Confirm the patch load path (`SFileOpenPatchArchive` layering vs. plain
  open with high priority). Walk `FUN_0051f14x` next.
- Identify `FUN_00517332`'s exact signature — it's not a direct Storm
  export; likely a D2-side wrapper that adds tracking + priority
  bookkeeping around `SFileOpenArchive`.
- The read pool at `FUN_004fb010` looks like a chunked-read optimization
  for large sequential reads. Confirm before porting (may be avoidable
  with modern IO).
