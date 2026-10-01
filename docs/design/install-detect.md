# Finding the user's Diablo II — install detection plan

Status: plan, 2026-10-01. Nothing built. Goal: the launcher finds an
existing, lawfully installed classic Diablo II (1.14d, with or without Lord
of Destruction) and reads it in place, so the user doesn't have to browse
for the MPQ folder. Diablo II: Resurrected is recognised and refused, with
a reason.

Sources: `components/game/gamedata_load.cpp` (`load_game_data`: what d2d
needs), `apps/d2d/common.cpp` (`default_data_dir`), `apps/d2d/main.cpp`
(d2d.cfg, save dir), `apps/launcher/main.cpp` (`game_dir_valid`,
`pe_file_version`, QSettings `game/dataPath`), LEGAL.md, and the public
pages cited inline. Anything marked **(unverified)** was not confirmed
from a public source or a real install; check it on a machine before relying on it.

## What d2d needs from an install

From `load_game_data` (the loader, today):

| File | Need | Why |
|---|---|---|
| `d2data.mpq` | **required** | no game data without it (falls back to a test pattern) |
| 1.14d patch layer: `patch_d2.mpq`, else `LODPatch_114d.exe` (d2d.cfg `patch =`, or next to the MPQs) | **required for correct tables** | without it the loader warns and uses CD-era tables and strings |
| `d2exp.mpq` | needed for LoD | expansion data; the classic-only path is untested in d2d |
| `d2char.mpq` | strongly wanted | character animations (a miss is skipped silently) |
| `d2sfx`, `d2speech`, `d2xtalk`, `d2music`/`d2xmusic`, `d2video`/`d2xvideo` | optional | sound, music, cinematics |

d2d never writes to the data dir. Saves go to `userdir::user_dir("d2d")/save`
and MPQs open with `MPQ_OPEN_READ_ONLY | STREAM_FLAG_READ_ONLY`
(components/mpq/mpq.hpp). So reading an install in place already works;
what's missing is finding it.

Two loader gaps that detection exposes (follow-ups, not part of the detector):
1. **A stale `patch_d2.mpq` wins.** On a 1.13c or 1.14b install the loader
   picks that install's `patch_d2.mpq` over a supplied `LODPatch_114d.exe`,
   which gives the wrong tables. Fix: an explicit `patch =` outranks
   `patch_d2.mpq`, and the launcher sets it when the install isn't 1.14d.
2. **Case.** The loader looks for lowercase `d2data.mpq` etc. On Linux and
   in Wine prefixes the names can be `D2DATA.MPQ` or `D2Data.mpq`.
   (The launcher's `game_dir_valid` already accepts two spellings.) Since
   we read in place we can't rename. Fix: look names up case-insensitively.

## Where installs live

Classic D2 does **not** install through the Battle.net app. Blizzard ships
standalone installers (1.14b) from the account's "Classic Games" page, and
the game patches itself to 1.14d on first login.
Default folder: `C:\Program Files (x86)\Diablo II` (64-bit Windows) or
`C:\Program Files\Diablo II` (32-bit)
([Blizzard support 000013867](https://us.support.blizzard.com/en/article/000013867)).
So for classic installs the **registry and the default folders** are what
matter. The Battle.net app's `product.db` mainly finds **D2R**, so we can
name it and turn it down.

### Windows

| # | Probe | Notes |
|---|---|---|
| W1 | `HKCU\Software\Blizzard Entertainment\Diablo II` → `InstallPath` (also `Save Path`) | [Phrozen Keep KB 333](https://d2mods.info/forum/kb/viewarticle?a=333); Blizzard says it may be in HKCU, HKLM or both, and that **stale values from an old CD install are common** ([Blizzard support 41129](https://eu.battle.net/support/en/article/41129)), so validate every path |
| W2 | `HKLM\SOFTWARE\Blizzard Entertainment\Diablo II` → `InstallPath`, read through the **32-bit view** (`KEY_WOW64_32KEY`), which on 64-bit Windows is `HKLM\SOFTWARE\WOW6432Node\...` | D2 is a 32-bit program; registry redirection: [MS registry redirector](https://learn.microsoft.com/en-us/windows/win32/winprog64/registry-redirector), [alternate view](https://learn.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view). Also read the 64-bit view once; it's cheap |
| W3 | Uninstall keys: `HK{LM,CU}\SOFTWARE\(WOW6432Node\)Microsoft\Windows\CurrentVersion\Uninstall\*` whose `DisplayName` starts with `Diablo II` → `InstallLocation`, else the folder of `DisplayIcon` / `UninstallString` | key name and which values the D2 installer writes: **(unverified)**. Also finds D2R (`Diablo II Resurrected`), which we refuse |
| W4 | Default folders: `%ProgramFiles(x86)%\Diablo II`, `%ProgramFiles%\Diablo II` (env vars, not a hard-coded `C:`) | Blizzard support article above |
| W5 | Battle.net agent `%ProgramData%\Battle.net\Agent\product.db` | path: [blizzard-product-parser settings.js](https://github.com/TinkoLiu/blizzard-product-parser/blob/master/src/js/settings.js); format below. Classify every `install_path` regardless of product code |
| W6 | D2R default folders: `%ProgramFiles(x86)%\Diablo II Resurrected`, `%ProgramFiles%\Battle.net\Diablo II Resurrected`, Steam `steamapps\common\Diablo II Resurrected` | to refuse with a reason ([D2R Reimagined install guide](https://wiki.d2r-reimagined.com/en/Installs)); D2R is also sold on Steam ([Steam app 2536520](https://steamcommunity.com/app/2536520/discussions/0/807973228118103848/)) |

**UAC VirtualStore.** Old installs patched without elevation may have
`patch_d2.mpq` / `Game.exe` redirected to
`%LOCALAPPDATA%\VirtualStore\Program Files (x86)\Diablo II`, which is what the game
then saw ([UAC virtualization](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/user-account-control/how-it-works#virtualization)).
Overlay it: where a file exists in VirtualStore, use that one. Whether this happens with
1.14d installs is **(unverified)**; checking costs one directory test per install.

`Battle.net.config` (`%APPDATA%\Battle.net\Battle.net.config`, JSON) holds
the app's default install path and per-game settings **(unverified layout)**.
It adds nothing over product.db, so skip it.

In the launcher, Qt reads the registry directly:
`QSettings(key, QSettings::Registry32Format)` / `Registry64Format`
([QSettings](https://doc.qt.io/qt-6/qsettings.html#Format-enum)). The
component uses `RegGetValueW` with `KEY_WOW64_32KEY` behind `#ifdef _WIN32`,
behind an injected callback (see API) so it can be tested.

### macOS

| # | Probe | Value |
|---|---|---|
| M1 | `/Applications/Diablo II/` and `~/Applications/Diablo II/` (MPQs next to `Diablo II.app`) | medium. The native 1.14 Mac client ran on 10.10–10.14; Catalina dropped 32-bit apps, so the folder survives on upgraded Macs but the game no longer runs ([MacRumors guide](https://forums.macrumors.com/threads/diablo-ii-installation-version-guide-for-mac-powerpc-and-intel.2358323/)). Whether the MPQs sit beside or inside the bundle is **(unverified)**; probe both `…/Diablo II/` and `Diablo II.app/Contents/Resources/` |
| M2 | Wine-wrapper bottles, treated like a Wine prefix (below): CrossOver `~/Library/Application Support/CrossOver/Bottles/*/`, Whisky `~/Library/Containers/com.isaacmarovitz.Whisky/Bottles/*/`, Porting Kit / Wineskin `~/Applications/Wineskin/*.app/Contents/SharedSupport/prefix/` | **high** on modern macOS, where the Windows build in a bottle is how D2 runs today. Paths **(unverified)** |
| M3 | `/Users/Shared/Battle.net/Agent/product.db` | low. D2R has no Mac release, and the path is [blizzard-product-parser settings.js](https://github.com/TinkoLiu/blizzard-product-parser/blob/master/src/js/settings.js). Read it only for completeness |
| M4 | The launcher's own previous copy (QSettings `game/dataPath`, default `AppLocalDataLocation/game`) | already done today |

Windows and Mac MPQs are interchangeable (MacRumors guide), so a Mac user
pointing at MPQs copied from a PC is normal. M1 installs have no
`Game.exe`, so their version is "unknown" (see classification). Any Mac
plist the classic installer wrote is **(unverified)**; we don't need one.

**TCC risk:** on macOS 14+, reading another app's `~/Library/Containers/…`
triggers a "would like to access data from other apps" prompt. Probe
Whisky's container only on an explicit "Search more places" click, not on
first launch.

### Linux (Wine)

One routine handles every prefix (`prefix/` containing `drive_c/`,
`system.reg` and `user.reg`):
1. `user.reg` (HKCU) `[Software\\Blizzard Entertainment\\Diablo II]` and
   `system.reg` (HKLM) `[Software\\Wow6432Node\\Blizzard Entertainment\\Diablo II]`
   (64-bit prefix) or `[Software\\Blizzard Entertainment\\Diablo II]`
   (32-bit) → `"InstallPath"="C:\\…"`. Format: plain text, `[Key\\Path] <timestamp>`
   then `"name"="value"` lines, system.reg = HKLM, user.reg = HKCU
   ([Wine User Guide, registry](https://manualzz.com/doc/o/1smns/wine-user-guide-using-the-registry-and-regedit),
   [wine server/registry.c](https://github.com/wine-mirror/wine/blob/master/server/registry.c)).
   Unescape `\\` and `\"`. Non-ASCII is written as `\x` escapes **(unverified)**:
   decode, or skip the value and fall back to step 3.
2. The same Uninstall-key scan as W3, from the .reg text.
3. Default folders `drive_c/Program Files (x86)/Diablo II`, `drive_c/Program Files/Diablo II`.
4. `drive_c/ProgramData/Battle.net/Agent/product.db` (Lutris reads exactly this,
   [lutris services/battlenet.py](https://github.com/lutris/lutris/blob/master/lutris/services/battlenet.py)).

Mapping `C:\X\Y` to a host path: drive `c:` maps to `prefix/drive_c`. Other letters go through
the `prefix/dosdevices/<letter>:` symlinks (`z:` → `/`). Resolve each
component case-insensitively, as Wine does.

Prefixes, ranked by value:

| Rank | Where | Why |
|---|---|---|
| 1 | `$WINEPREFIX`, else `~/.wine` | the default; most hand installs |
| 2 | Lutris: `~/Games/*/` (Lutris' default game dir) | Lutris has a D2 LoD script (Blizzard's installer + 1.14d). Default dir **(unverified)**; Lutris' `pga.db` (sqlite) knows exact paths, but that means a sqlite dependency, so skip it |
| 3 | Bottles: `~/.local/share/bottles/bottles/*/`, Flatpak `~/.var/app/com.usebottles.bottles/data/bottles/bottles/*/` | common on Steam Deck/Fedora; paths **(unverified)** |
| 4 | Steam/Proton: `~/.steam/steam/steamapps/compatdata/*/pfx/` (+ extra libraries from `libraryfolders.vdf`) | classic D2 isn't on Steam; only a user who added it as a non-Steam game. Mostly finds D2R (Steam) to refuse |

Every probe is a bounded set of `stat`s and two small file reads per prefix.
**No recursive disk scans.**

## Telling classic 1.14d from D2R and from older versions

Classify a candidate folder (after VirtualStore overlay, case-insensitive):

| Kind | Markers | Launcher treatment |
|---|---|---|
| **D2R** | `D2R.exe`, or `.build.info` + `Data/data/` (CASC), or product code starting `osi` | listed greyed out, not selectable: "Diablo II: Resurrected stores its data differently; d2d needs classic Diablo II (2000/2001)" |
| **Classic 1.14d** | `d2data.mpq` (MPQ magic `MPQ\x1A` at 0), `Game.exe` file version **1.14.3.71**, no `D2Client.dll` | selectable, preferred |
| **Classic 1.14a–c** | `Game.exe` 1.14.x other than 3.71 | selectable with a warning: "patch to 1.14d (log in once with the original), or add LODPatch_114d.exe"; launcher sets `patch =` |
| **Classic ≤1.13** | `D2Client.dll` / `D2Common.dll` present (1.14 linked them all into `Game.exe`, [Phrozen Keep, 1.14a](https://d2mods.info/forum/viewtopic.php?t=62805)), or `Game.exe` < 1.14 | same as 1.14a–c. Base MPQs are fine; only the patch layer is stale (loader gap 1) |
| **Classic, version unknown** | MPQs, no `Game.exe` (Mac native, CD copy, launcher copy) | selectable. A `patch_d2.mpq` is trusted as-is, else "add LODPatch_114d.exe" |
| **Incomplete** | no `d2data.mpq` | dropped (stale registry value) |

Each row also records `expansion` (`d2exp.mpq`), `d2char.mpq` presence and
the optional MPQs that are missing (pre-1.12 "minimal" CD installs left
music/video on the disc).

**Version string.** `Game.exe` 1.14d: FileVersion **1.14.3.71**,
ProductVersion "1, 14, 3, 257 (79b7ae8)". These differ, so compare the
file version ([Median XL forum, IndirectSound log](https://forum.median-xl.com/viewtopic.php?t=59898)). 1.14a/b/c file versions
1.14.0.64 / 1.14.1.68 / 1.14.2.70 are **(unverified)**; the rule only needs
"1.14.3.71 or not". Read `VS_FIXEDFILEINFO`: scan for signature `0xFEEF04BD`,
then `dwFileVersionMS/LS`
([MS VS_FIXEDFILEINFO](https://learn.microsoft.com/en-us/windows/win32/api/verrsrc/ns-verrsrc-vs_fixedfileinfo)).
That is sturdier than the launcher's current UTF-16 `"FileVersion"` string
scan, which it replaces.

**Product codes.** D2R's Battle.net (TACT/agent) code is `osi`, with `osib` for
beta and `osit` for test ([jaenster/d2r-cdn](https://github.com/jaenster/d2r-cdn)) and
`osidev` ([BlizzTrack](https://blizztrack.com/config/osidev/pc/0d9d921545dd5ae3c938ae156ce4ff72)).
`D2DV` / `D2XP` are classic D2's **BNCS** client IDs sent in SID_AUTH_INFO
([BNETDocs product identification](https://bnetdocs.org/document/12/product-identification)),
not agent product codes. Classic D2 isn't expected in `product.db` at all,
so the detector never decides on the code alone; it classifies the folder
on disk. (D2R's `.build.info` has a `Product` column **(unverified)**; the
D2R.exe/CASC markers are enough.)

**Mods.** A 1.14d `Game.exe` next to a modded `patch_d2.mpq` (or PlugY, or
`-direct -txt` data) looks like 1.14d. Detection can't tell without checking
the contents. See open questions.

## product.db

It's protobuf (`Database` message). Only five fields matter, confirmed in two
independent public schemas,
[TACTLib ProtoDatabase.cs](https://github.com/overtools/TACTLib/blob/master/TACTLib/Agent/Protobuf/ProtoDatabase.cs)
and [lutris product_db.py](https://github.com/lutris/lutris/blob/master/lutris/util/battlenet/product_db.py):

```
Database          1: repeated ProductInstall product_install
ProductInstall    1: string uid   2: string product_code   3: UserSettings settings
UserSettings      1: string install_path   2: string play_region
```

No protobuf dependency. A hand-rolled reader (~50 lines) using the
[wire format](https://protobuf.dev/programming-guides/encoding/):
read a varint tag, `field = tag >> 3`, `wire = tag & 7`, then skip by wire
type (0 varint, 1 eight bytes, 2 length-prefixed, 5 four bytes; anything
else means stop, bad file). Descend into the field-1 entries; inside each, take field 2
(code) and field 3 (descend, field 1 = path). Bounds-check every length
against the remaining span, and cap the file at a few MB.

**Is scanning paths enough?** For classic: yes. Classic never appears in
product.db, so W1–W4 find it. product.db only adds D2R installs in custom
folders, which we refuse anyway. It's worth having (2 h with tests)
because "we found Diablo II: Resurrected at X; it can't be used" answers the most likely
user question. It's a lower priority than the rest, and the first thing to drop if the
schema changes. Never talk to the agent's local HTTP API
([wowdev Agent](https://wowdev.wiki/Agent)); read the file only.

## Design

### Where it lives

`components/install/` (lib `install`, plain C++ / std::filesystem, no Qt,
no StormLib), like `userdir`. The launcher links it for the UI. d2d can link
it later so `default_data_dir` tries detection before its hard-coded
`~/Workspace/private/diablo2` guess. The net-join plan's "user's own
game.exe next to the MPQs" also gets its `Game.exe` path from here.

```
components/install/
  install.hpp       Install, Environment, detect(), classify()
  install.cpp       probes, classification, VS_FIXEDFILEINFO, path mapping
  wine_reg.cpp      .reg text lookup + unescape
  product_db.cpp    varint reader
  registry_win.cpp  RegGetValueW (+ uninstall enumeration), _WIN32 only
tests/test_install.cpp
```

### API

```cpp
namespace d2d::install {

enum class Kind    { classic, resurrected };
enum class Version { v114d, v114_other, legacy, unknown };

struct Install {
    std::filesystem::path dir;        // where d2data.mpq is; read in place
    std::filesystem::path game_exe;   // empty if none
    Kind        kind = Kind::classic;
    Version     version = Version::unknown;
    std::string file_version;         // "1.14.3.71", empty if no Game.exe
    bool        expansion = false;    // d2exp.mpq
    bool        patch_mpq = false;    // patch_d2.mpq
    std::vector<std::string> missing; // optional MPQs not found
    std::string source;               // "registry HKCU", "Wine ~/.wine", "product.db", ...
};

enum class Hive { current_user, local_machine };

// Everything the probes touch, so tests can fake a whole machine.
struct Environment {
    std::filesystem::path home, program_data, local_app_data;
    std::vector<std::filesystem::path> program_files;   // (x86) first
    std::vector<std::filesystem::path> wine_prefixes;   // already discovered
    std::function<std::optional<std::string>(Hive, std::string_view key,
                                             std::string_view value)> registry;
    std::function<std::vector<std::string>(Hive, std::string_view key)> subkeys;
};

auto system_environment(bool search_more = false) -> Environment;
// Unique by canonical dir; classic 1.14d first, then other classic, then D2R.
auto detect(const Environment&) -> std::vector<Install>;
auto classify(const std::filesystem::path& dir, std::string source) -> std::optional<Install>;

// Pieces, public for tests.
auto file_version(const std::filesystem::path& pe) -> std::optional<std::array<std::uint16_t, 4>>;
auto wine_reg_value(std::string_view reg_text, std::string_view key,
                    std::string_view name) -> std::optional<std::string>;
auto wine_to_host(const std::filesystem::path& prefix, std::string_view windows_path)
    -> std::optional<std::filesystem::path>;
struct ProductInstall { std::string code, path; };
auto read_product_db(std::span<const std::byte>) -> std::vector<ProductInstall>;

}  // namespace d2d::install
```

### Probe order

The order doesn't change the result, because all hits get classified and sorted. It
sets the `source` label and which duplicate is kept:
1. Saved choice (QSettings `game/dataPath`), still valid? Then done, no scan.
2. Windows: W1 HKCU, W2 HKLM (32, then 64), W3 uninstall, W4 defaults, W5 product.db, W6 D2R defaults.
3. macOS: M1, M4, M2 (CrossOver, Porting Kit; Whisky only with `search_more`), M3.
4. Linux: Wine routine over `$WINEPREFIX`/`~/.wine`, Lutris, Bottles, Proton.
5. Each prefix: user.reg, system.reg, uninstall, defaults, product.db.

Run it in the launcher's `QThreadPool` (already used) with a 2 s budget. A
slow network drive mustn't freeze the window.

### UI flow

- **First run** (no saved path, or it no longer classifies): run detection.
  - Exactly one classic 1.14d: fill the path, status "Found Diablo II: Lord
    of Destruction 1.14d (registry)", Launch enabled. No dialog.
  - Several, or only non-1.14d: a "Found installations" list
    (`QListWidget`), one row each: name, version, path, source, plus a warning
    icon for "needs 1.14d patch" or "no expansion". D2R rows are greyed, not
    selectable, and carry the reason as a tooltip. Buttons: **Use selected**,
    **Browse…**, **Install from discs…** (the existing wizard),
    **Search more places** (TCC-prompting probes), **Rescan**.
  - None: today's screen plus the line "No Diablo II install found. Browse to
    yours, or install from the discs."
- **Browse** accepts a folder, `Game.exe` or `Diablo II.app`, normalises to
  the folder and runs `classify()`, so manual picks get the same messages and
  the D2R refusal.
- Non-1.14d picks show "Add LODPatch_114d.exe…" (the existing
  `addPatchBinaries` file dialog, but recording the path, not extracting).
- The existing copy-install wizard stays for discs/ISOs. For a found install
  the default is **use in place**, so no copy.

### Persistence

- QSettings (existing keys): `game/dataPath` (the chosen dir), plus new
  `game/patchPath` and `game/source` (`detected:<source>` or `manual`).
- The launcher writes `data = …` and `patch = …` into
  `user_dir("d2d")/d2d.cfg`, touching only those two lines, so d2d finds the
  choice on every OS. Today only macOS reads the launcher's plist, via
  `defaults read`.
- Re-validate on every launcher start. If the install vanished, go back to
  first-run detection. Never re-pick silently when a valid saved choice exists.

### At rest

The detector and d2d only **read**: no copy, no rename, no case-fixing,
no `patch_d2.mpq` edits, no registry writes, nothing written into the
install's `Save` dir. The registry is read for `InstallPath` (and the
uninstall location values) only. Other values under the Blizzard key are
never enumerated or logged.

### Tests (`test_install`, no Blizzard bytes)

Temp-dir fixtures built by the test:
- folders holding empty files with the MPQ magic (`MPQ\x1A` + zero padding;
  a public format constant), in mixed case;
- a synthetic PE-like blob we write ourselves: `MZ`, padding, `0xFEEF04BD`,
  then version words for 1.14.3.71 / 1.14.1.68 / 1.0.13.60;
- a 1.13-style folder with an empty `D2Client.dll`; a D2R-style folder
  with `D2R.exe`, `.build.info`, `Data/data/`;
- hand-written `user.reg` / `system.reg` text (escapes, both arch layouts,
  a stale path), a fake prefix with `drive_c/` and a `dosdevices/d:` symlink;
- a hand-encoded product.db (an `osi` entry and an unknown code pointing at
  a classic folder; a truncated file; an over-long length);
- an `Environment` with a lambda registry (HKCU stale, HKLM good).

Assertions cover classification, ordering, dedupe, the stale-path drop, D2R refusal,
and that the fixtures' mtimes and sizes don't change (proving read-only).
Optional: when `D2_MPQ_DIR` is set, `classify(D2_MPQ_DIR)` is expected to
return classic (skipped otherwise, like the other real-data tests).

## Legal fit

This matches LEGAL.md: "You supply the game from your own installation or
media, and D2Decomp reads it at run time." Reading the user's own lawful copy
where it sits is less than what the copy-install wizard does today. The
detector only looks for files and reads version numbers; it contains no
Blizzard code or data. Avoid:
- copying or extracting Blizzard executables as part of detection (the dev-only
  `bin/` import stays separate and is slated for removal, PLAN.md phase 4);
- reading CD keys, Battle.net credentials, tokens or `Battle.net.config`
  account fields, or anything in the registry besides install locations;
- the Battle.net agent's local API, or any network traffic: detection is
  offline, and no telemetry is sent about what was found;
- shipping real `product.db`, `.reg` exports, `Game.exe` excerpts or MPQ
  bytes as fixtures. All fixtures are synthetic;
- making D2R usable: it's a different product with its own terms. We only
  name it to explain the refusal;
- committing hashes of Blizzard files without a decision first (open question 3).

## Estimate

| Piece | Hours |
|---|---|
| Core component: classify, VS_FIXEDFILEINFO, Wine .reg reader, path mapping, product.db reader, detect/sort/dedupe + test_install | 8–10 |
| Windows: registry (32/64 views), uninstall scan, VirtualStore, check on a real Windows box | 3–4 |
| macOS: /Applications, CrossOver/Whisky/Porting Kit globs, TCC behaviour check | 2–3 |
| Linux: `~/.wine`/`$WINEPREFIX`, Lutris, Bottles, Proton globs, check under real Wine | 2–3 |
| Launcher UI: background detect, found-list, browse→classify, d2d.cfg write, persistence | 5–6 |
| Loader follow-ups: `patch =` outranks stale `patch_d2.mpq`; case-insensitive MPQ names | 2–3 |
| **Total** | **22–29 (3–4 days)** |

## Risks

- **Registry facts are partly unverified.** Which hive the 1.14b installer
  writes, and its uninstall key. Mitigation: probe both hives and both views,
  validate everything, and keep the default folders as a backstop.
- **Stale registry values** from old CD installs are common (Blizzard 41129).
  Classification drops paths without `d2data.mpq`.
- **product.db schema drift.** Unknown fields are skipped, a parse failure
  means "no entries", and the feature is optional.
- **Modded installs** look like 1.14d (see Mods); the tables are wrong and
  the cause is hard to see. A visible "patch layer: patch_d2.mpq / installer"
  line in the launcher status helps.
- **Stale `patch_d2.mpq` on older installs** gives silently wrong tables until loader gap 1
  is fixed. Ship that fix with the detector.
- **Case and non-ASCII paths:** `fs::path` from UTF-8 on POSIX and wide APIs on
  Windows. Wine `\x` escapes are unverified.
- **macOS TCC prompts** for container paths. Gated behind "Search more places".
- **Several installs** (classic + D2R + an old CD copy). Sorting and a list
  instead of guessing.

## Decisions (Bret, 2026-10-01)

1. **Classic without LoD:** listed, marked "Lord of Destruction needed",
   for now. Classic-only support may come later.
2. **Not 1.14d** (older 1.14, 1.13 and before): the launcher warns and
   offers a button that fixes it (points the install at `LODPatch_114d.exe`).
3. **Patch-layer check:** structural, at load (e.g. one 1.14d table's row
   count). No known-hash list in the repo.
4. **Classic save dir:** not read. Skip.
5. **d2d without the launcher:** no `detect()`. d2d checks its own folder
   first (drop-in replacement: `d2data.mpq` beside the binary or in the
   working directory), then its config (`data =` in `d2d.cfg`, `--data`,
   env). Nothing found: fail fast with a clear error. The
   `~/Workspace/private/diablo2` fallback goes. The copy-install wizard
   stays as it is.
