# D2Decomp — Plan

Reverse-engineer Diablo II (2000, LoD 2001) into a cross-platform, modern
C++26 engine. game.exe 1.14d is the ground truth: Ghidra and an emulator
of its own code (tools/emu) drive the work; other projects only check our
reading ("Reference projects" below).

## Goals

- **A drop-in replacement for game.exe.** Everything a player or a mod
  passes to Diablo II 1.14d still works: its command-line switches
  (`-w`, `-ns`, `-direct`, `-txt`, `-seed`, `-act`, `-skiptobnet`,
  `-gamma`, `-vsync`, `-lq`, `-nofixaspect`, the class ones like `-ama`,
  ...), with game.exe's meaning and spelling (one dash). d2d's own
  options may add to them, never clash. Research: game.exe's switch table
  (the strings sit together in .rdata) and what each one sets.
- Act 1, then the rest, playable from game.exe's own rules (Road to a
  whole Act 1, below).

## Non-goals

- Ship a game with Blizzard assets. Users bring their own ISOs.
- Realm / Battle.net services for now. The game itself is built
  client/server from here on (docs/design/multiplayer.md).
- Pixel-perfect renderer. Match behavior, not implementation.

## Phases

Status as of 2026-09-28.

1. **Bootstrap** — repo skeleton, CMake root, docs seeded. *Done.*
2. **Launcher (install path)** — one Qt6 app (`apps/launcher/`, target
   `d2d-launcher`) that reads the 4 D2/LoD ISOs directly (single-header
   `iso9660.hpp`) and copies files into a per-user data dir. *Done.* The
   1.14d patch data is read straight from `LODPatch_114d.exe` (d2d.cfg
   `patch = …`), so there is no separate patch step.
3. **Asset formats** — parsers for MPQ (StormLib), DC6, DCC, DS1, DT1,
   COF, TBL, TXT, plus D2S saves, fonts and palettes, tested against real
   assets (`components/`, `tests/`). *Done;* extended as game code needs.
4. **Ghidra pass** — 1.14d `game.exe`, the Blizzard-provided static PE
   (the old D2Common/D2Game/D2Client/… DLLs are linked into it; no CD
   checks; most modding work targets it). *Ongoing, driven by phase 5:*
   notes per subsystem under `docs/research/re/`.

   The launcher's install step drops the PEs into `<dest>/bin/` when it
   sees them in a source (GOG install → v1.14d; disc `crack/` → v1.00), and
   the `Add patch binaries…` / `Fetch patch…` buttons extract them on
   demand from a Blizzard MPQ-appended installer.

   **`bin/` is dev-only.** Nothing our engine ships against Blizzard's PEs;
   they exist so contributors can open them in Ghidra. Once this phase is
   done, the `bin/` import will be dropped from the launcher — redistributing
   Blizzard's binaries is not permitted, so no release will include them.
5. **Core loop**: main menu, character select, load act 1 rogue camp,
   render tiles. *Done for Act 1's start:* frontend, town, NPCs, panels,
   trade and waypoints (no travel yet); leaving camp into a generated
   Blood Moor and the Den of Evil, saving. Left: the other Act 1 levels,
   waypoint travel.
6. **Combat + AI**: actor state machine, packet-equivalent events,
   monster AI from game.exe. *Implemented:* monsters and their fights,
   gear in combat, drops, experience, the merc, and the skills (phases 0–6
   in `docs/research/re/skills.md`). Left: the per-type AI think
   functions, the unique behaviour mods, the gaps each skill section lists.
7. **Cross-platform polish** — Linux/macOS/Windows CI, controller,
   high-DPI, rebindable input.

## Directory layout

```
apps/           end-user apps: launcher (Qt6 installer), d2d (the game)
components/     shared libraries (one subdir per lib, own CMakeLists.txt)
tests/          all tests. one test_*.cpp per subject, ctest-hooked
tools/          decompilation helpers, format converters, Ghidra scripts
docs/           plan, notes
docs/research/  file format specs, RE notes, references
cmake/          shared CMake modules
```

## Where code goes

Rules stay testable and free of assets and screens; the game applies
them to levels and units; the client wires the game to input, drawing
and sound.

| Area | Rules (`components/rules`, tested) | Game (`components/game`) | Client (`apps/d2d`) |
|---|---|---|---|
| Items, stores, gambling, hiring, potions | `rules.hpp` (`test_rules`) | `inventory.hpp`, `item_text.hpp` | `items.hpp`, `store.hpp`, `cursor.hpp`, `panels.hpp` |
| Monsters: tables, spawning, stats | `monsters.hpp` (`test_monsters`) | `ai.hpp` (units, AI, missiles) | `load.hpp` (`load_monsters`) |
| Combat: Fighter, blows, speed, experience | `combat.hpp` (`test_combat`) | `fight.hpp` (`Fight`) | |
| Drops: treasure classes, quality | `drops.hpp` (`test_drops`) | `loot.hpp` (`Loot`) | |
| Skills: records, calc VM, levels, mana | `skills.hpp` (`test_skills`) | skill use in `fight.hpp` | `skillbar.hpp` (HUD buttons, picker, hotkeys) |
| Level layout, outdoor generator | `components/drlg` (`test_drlg`, `test_outdoor`) | `gamedata.hpp` (`build_level`, `build_outdoor`) | |
| The game server: levels, units, commands, the 25 Hz tick | | `world.hpp` (`World`), `protocol.hpp` (`Command`), `replication.hpp` (`View`) | |
| The client: input, panels, camera, drawing | | | `town.hpp` (`Town`), `world_view.hpp` (drawing the level and units) |
| Scripted-test verbs | | | `devctl_verbs.hpp` (+ info/screenshot/quit in `main.cpp`) |
| World sounds | | `cues.hpp` (`Cues`, queued by the World) | `audio.hpp` (`play_cues`) |

**The game and the client.** The game is `components/game`, namespace
`d2d::game`: a library that `test_game` links alone, so nothing there may
reach the client. It holds no SDL, sound, pixels or fonts. Its base is
`game.hpp` (the `fs` alias and the iso geometry);
`gamedata.hpp` (`GameData`, `Level`, `Npc`), `gamedata_load.hpp`
(`load_game_data`), `character.hpp` (`Character`: what a save holds), `world.hpp` (`World`), `protocol.hpp` (`Command`) and
`replication.hpp` (the `View` as bytes) are its main headers. A game
header never includes a client one.

The client is `apps/d2d`, namespace `d2d::client`. It sees the game
through `game_api.hpp`: the game's headers plus a `using game::Name;` for
each game name it uses, so the list is the client's whole view of the
game. `common.hpp` is its base (game_api, screen size, blits),
`platform.hpp` brings SDL and OpenAL to the headers that need them, and
`scene.hpp` holds `Scene : GameData` (sprites, fonts, palettes).

**Files.** A header declares; its bodies live in the `.cpp` of the same
name (`fight.hpp` / `fight.cpp`). A file includes the headers whose names
it uses, not whatever happens to bring them in: every header compiles on
its own (`d2d_check_headers`, cmake/HeaderCheck.cmake, built with the rest;
`-DD2D_CHECK_HEADERS=OFF` skips it). A class's methods are defined out of
line as `auto Class::name(params) -> Ret { ... }` (the trailing return type
resolves the class's own types); short accessors may stay in the class. A
global in a header is `inline`.
The game loads its own data: `load_game_data` (gamedata_load.hpp) reads
the MPQs, strings, tables, Act 1 and the camp; the client's `load_scene`
takes that GameData and adds sprites, fonts and palettes. `test_game`
loads GameData and plays a character with no client code.
Every file includes the standard and component headers it uses itself
(clang-tidy's misc-include-cleaner, checked in CI; apps/d2d/.clang-tidy
exempts the game headers game_api.hpp brings); game.hpp and common.hpp
carry only their own. A level's
tiles decode their pixels only when the client asks (`load_game_data(...,
tile_pixels)`, dt1::Pixels): a headless server keeps their headers and
walk flags.

`World` (world.hpp) owns the game's state (level, player, merc, NPCs,
rng) and hands references to its subsystems (`Fight`, `Loot`); the client
(`Town`) only sends it commands (protocol.hpp) and reacts to its events
(docs/design/multiplayer.md). A new game system goes into the World or a
subsystem it owns; a new panel or input into `Town`.

## Code style

**Names say what they hold.** A variable, parameter or field is named for
its meaning: `framebuffer`, `now_ms`, `monster`, `row`, `scene`,
`game_data`, not `fb`, `ms`, `m`, `r`, `s`. The few short names that stay
are the conventional ones: `i`, `j`, `k` as loop counters, `x`, `y`,
`dx`, `dy` for coordinates, `id`, `ok`; and a colour's `r`, `g`, `b`, `a`
(`palette::Rgba`). Constants are `kCamelCase`. `.clang-tidy`
(readability-identifier-length) enforces this for variables and
parameters, and misc-include-cleaner keeps every file including what it
uses; CI runs both on Linux (clang-tidy -p build on every source). Fields
are held to the same rule by review. A header must not name a
platform-private header (`<_string.h>`, `<bits/...>`): include the
standard one. Don't name anything `near` or `far`: windows.h defines them
as empty macros. Code under `#ifdef _WIN32` / `__linux__` isn't seen by the
macOS build or Linux clang-tidy; CI's other jobs are its only check.

**Namespaces say which part.** The file formats are `d2d::mpq`, `d2d::dcc`,
`d2d::ds1` ... (components/<format>), the game's rules `d2d::rules`, the
level generator `d2d::drlg`, the game server `d2d::game`
(components/game) and the client `d2d::client` (apps/d2d).

## Toolchain

- C++26 (`-std=c++2c` / `/std:c++latest`), CMake ≥ 3.28, Ninja.
- System packages: SDL3 (window, input, WAV decode), OpenAL (openal-soft),
  FFmpeg (Bink cinematics), zlib, Qt6 ≥ 6.5 (launcher only).
- StormLib (MPQ): the system package, else `FetchContent`; CLI11: `FetchContent`.
- License: GPL-3.0-or-later (matches OpenD2). Compatible with StormLib
  (MIT), SDL3 (Zlib), openal-soft (LGPL), FFmpeg (LGPL), Qt6 (LGPL).

## Reference projects (in `../`)

- **OpenD2** — C, closest to original engine layout. Best for file-format
  code (DCC, DC6, DS1, DT1, COF). Reads like a decompilation.
- **OpenDiablo2** — Go. Best for high-level game logic and asset docs.
- **AbyssEngine** — C, successor to OpenDiablo2 by same author. Sabbat-era
  design; uses Lua for game code.

Verification only. game.exe 1.14d is the ground truth: behaviour comes
from tracing it. We do not copy code, and we don't take behaviour from
these projects on trust; we use them to check our reading. Anything not
yet traced in game.exe is marked `// unverified (source: ...)` where it's
used and listed in `docs/research/re/unverified.md` until it is.

## Ghidra workflow

1. Extract 1.14d PEs (Game.exe + D2*.dll) into a work dir outside the repo.
2. Ghidra project shared under `tools/ghidra/` as scripts + `.gpr` metadata
   (never the raw binaries).
3. Symbol names, structs, enums live in Ghidra Data Type archives (`.gdt`)
   committed under `tools/ghidra/gdt/`.
4. One markdown per function group in `docs/research/re/` with:
   address, signature, callers/callees, prose walkthrough, our C++ port.

## Milestone: "camera pans the rogue camp"

Success = launch our binary, log in with an imported save, camera moves
around a rendered act 1 town. No NPCs interactive. *Reached,* and passed:
NPCs talk and trade. Leaving town: *reached* (the Blood Moor, with
combat and skills) and the Den of Evil. Next: Cold Plains and the rest
of Act 1.

## Open questions

- Launch page: the plan was for `d2d-launcher` to grow one; today it's two
  binaries (`d2d-launcher` installs, `d2d` runs). Keep it that way unless
  a launch page earns its place.
- Save format: keep 1.14d-compatible or greenfield? Compatible unless it
  hurts.
- Networking: decided 2026-09-27. Listen server, single player as a
  one-player in-process session (docs/design/multiplayer.md).

## Known issues / to investigate

Bugs in the original (game.exe, the MPQs) are kept apart, with a
confidence each: docs/research/re/bugs.md.

Noted while playing the dev build (2026-09-24):

- **Collision.** Now built like game.exe's room grid (DT1 subtile rows
  were read upside down) with the plus-shaped unit test, and click-to-walk
  paths round obstacles (docs/research/re/collision.md). Left: the tile
  entry flag bits, units blocking each other, doors, and D2's own
  pathing in place of our A*.
- **NPC menu entries.** Not every menu item does something yet:
  - trade/repair, gamble, healing, hire and identify work (see Next up
    for what's approximate);
  - quest topics and the extra entries (Kashya's hire, Warriv's "go
    east") come from the game server (docs/research/re/npc-talk.md).

Noted in a playthrough (2026-09-27), to follow up:

- Fixed: **crossing camp → Blood Moor popped** — the neighbour's objects
  and NPCs draw across the edge now (NPCs at their start), and the
  camera's slide carries across (it snapped).
- Fixed: **Charged Bolt's black box** — Missiles.txt Trans 1 / 2 are draw
  modes 3 / 4, the PL2 additive / multiply tables (world_view.hpp
  blit_dcc_frame; in RGB, not the tables).

Noted in a playthrough (2026-09-28), fixed:

- **Click on an NPC's menu walked the player there**: a press that
  starts on the UI stays the UI's until the button comes up.
- **The weapon drew over the torso facing north**: the COF's draw-order
  row is picked through the compass order, not the DCC's
  (docs/research/re/cof-draw-order.md).
- **Gaps in the camp's fence, wagons and trees**: a corner (orientation 3)
  is two tiles, the second picked with orientation 4 (FUN_0066e9b0), and
  a wall tile's block x counts from its cell's left corner whatever the
  tile's width; narrower tiles drew shifted right.

## Wilderness plan (from 2026-09-25)

Goal: walk out of the Rogue Encampment into a Blood Moor laid out the way
game.exe lays it out, then fill it with monsters.

A. **Act layout** — done: `components/drlg` `act1_layout`, `test_drlg`;
   the town DS1 comes from it (`--seed`, default 3 = townE1).
B. **Outdoor DRLG** — done for the Blood Moor (what's proven identical to
   game.exe and what isn't: docs/research/re/drlg.md "Checking against
   game.exe"): `components/drlg/outdoor.hpp`
   (borders, border substitution, river/bridge, Den of Evil, roads,
   shrines markers, fills, plain rooms with LvlSub stamps, preset rooms),
   `test_outdoor` over 60 seeds; layout, room seeds and every room's
   tiles (`room_tiles.hpp`) proven identical to game.exe with tools/emu.
   Preset units are proven too, and the app draws the proven picks.
   Gaps: CheckAll stamps, cliff caves / cliff styles / waypoints (other
   act 1 levels).
C. **Levels from stamps** — done: the generator builds one `ds1::Map`
   for the level; `finish_level` gives it the town's lookup + collision.
D. **Leaving camp** — done: levels know their neighbours (`Level::near`),
   so collision, pathing and drawing carry on across the edge; walk over
   the town's bridge and `Town::cross_level` hands the player (path,
   merc, automap, music) to the Blood Moor. Walking toward a level that
   isn't built (Cold Plains) logs "not implemented". ponytail: the
   neighbour's NPCs aren't drawn across the edge.
E. **Monsters** — spawning done: game.exe's monster region and room
   population (docs/research/re/monsters.md, `components/rules/monsters.hpp`,
   `test_monsters`); the Blood Moor's monsters draw with rolled components
   and wander (MonWndr). Combat: click a monster to walk up and swing
   (the hit on AnimData's event frame, hit chance and damage from
   `components/rules`), monsters notice, chase, surround and hit back,
   get-hit / death / corpses, Fallen scatter, experience and level-ups,
   the player's death and respawn in camp. Drops: TreasureClassEx (with
   the auto weapN/armoN classes) and ItemRatio quality rolls, gold and
   items on the ground (flippy DC6s, labels), click to pick up; monsters
   follow the character's difficulty. Quill rats shoot spikes
   (Missiles.txt, 32-direction DCCs); the merc fights (hireling.txt stats
   at its level; rogues shoot arrows) and monsters go for whichever of
   player and merc is nearer; a dead merc stays dead. MonSounds.txt
   attack/weapon/hit/death sounds, drop and pickup sounds (fading with
   distance); keys 1-4 drink belt potions. The fight uses the gear:
   block (and monster block), damage reduced % / flat / magic,
   resistances, crushing blow, deadly strike, open wounds, elemental and
   poison damage both ways, chill, knockback, life/mana leech (MonStats
   Drain), thorns, IAS/WSM swing speed, FHR/FBR/FRW, replenish life and
   mana regeneration (components/rules Fighter, player_blow,
   monster_blow; docs/research/re/combat.md). Poison traced: it can't
   take a player below 1 life, it kills monsters (credited to the
   poisoner), one poison at a time. Skills researched
   (docs/research/re/skills.md), not built.

## Later research (noted, not scheduled)

- **Render paths**: the DirectDraw, Direct3D, Glide and OpenGL back ends
  (lighting, blends, shadows, perspective). What d2d could do with them in
  `docs/design/rendering.md`.

## Skills: where they stand (2026-09-26)

Committed:
- Phase 0–2: combat corrections, skill rows, the calc language, the skill
  bar and picker; melee skills, kicks, charge-ups, sequences, moving
  skills.
- Phase 3 (`ba82a88`): passives and masteries.
- Phase 4 (`4c17cde`, `3420f7a`, `c1a0ff1`): missile spells, fans, novas,
  Poison Dagger.
- Phase 5 (`eec2efd`..`a679193`): auras, charge-up releases, summons,
  sentries.
- Phase 6, part 1 (`26506da`): do-function missiles (Guided Arrow,
  Strafe, Chain Lightning, Meteor, Blizzard, Fire Wall, Inferno).
- Phase 6, part 2 (`f28d84f`): pets' stats and masteries, sumskills,
  Hydra, Revive, Decoy, Shadow Warrior, pets following across levels.
- Phase 6, part 3 (`1631376`): missile hit functions and spot spells
  (Frozen Orb, Glacial Spike, Holy Bolt, Fist of the Heavens, Blessed
  Hammer, the arrows, the Druid's elementals, the Assassin's traps,
  Static Field, Corpse Explosion, Teleport).
- Phase 6, part 4 (`61feb63`): buffs, curses and war cries.

- Phase 6, part 5 (`a1763c4`): Royal Strike's and Claws of Thunder's
  releases, Bone Wall and Prison, Werewolf / Werebear (stats only) with
  Feral Rage, Maul, Fire Claws, Hunger and Rabies, Armageddon and
  Hurricane, Blade Sentinel and Shield, Wake of Fire / Inferno and Death
  Sentry's corpse blast, the vines, the Druid's spirits, Double Throw.
  Every class skill now has an implementation path (devctl `debug
  unbuilt` lists none). That isn't the same as complete or exact:
  fidelity varies, and the approximations and gaps are per part in
  skills.md.

## Checkpoint 2026-09-25: what's still open

*(Superseded for skills by the section above; the rest still stands.)*

Combat
- Skills: none usable yet. skills.md has the traced machinery (records,
  level brackets, mana cost, the calc VM and its operands, how a melee
  skill builds its damage, where the save keeps the chosen skills) and a
  phased plan. Phase 0 (combat corrections) and phase 1 (skill rows, the
  calc language, levels with item bonuses, the skill bar, picker and
  hotkeys from the save) are done; phase 2 has started: the Bash family
  and Dragon Talon's kicks work (mana, animation, damage, knockback), and
  the Assassin charge-ups (Tiger / Cobra Strike, Fists of Fire, Claws of
  Thunder, Blades of Ice, Royal Strike) charge and release through Attack,
  Talon and Dragon Tail (a kick with a fire splash). Stun stands monsters,
  Concentrate raises defense while swinging and converts to magic.
  Power Strike, Berserk and Vengeance swing as game.exe's starts do;
  Zeal chains its hits, Sacrifice costs life, Smite bashes with the shield.
  SQ skills play game.exe's sequence table (Jab, Frenzy, Double Swing,
  Dragon Claw, the claw charge-ups); Fend, Impale, Holy Shield (a
  right-click self cast) too; Whirlwind, Leap Attack and Charge move the
  player. Phase 2 is done (skills.md); Double Throw waits for missiles,
  the Druid's shape-shifted skills for shapeshifting. Their release
  missiles (srvprgfunc) wait for the missile phase.
  Phase 3 (passives and masteries) is done: the passives' stats are on
  the player (masteries by weapon type, Weapon Block with two claws, Iron
  Skin, Natural Resistance, Increased Speed, Critical Strike, Dodge /
  Avoid / Evade, Penetrate, Warmth, the elemental masteries on gear).
  Phase 4 has started: missile skills whose missile carries the skill's
  damage cast and fly (Magic Arrow, Fire Bolt, Ice Bolt, Ice Blast,
  Lightning, Bone Spear, Fire Ball's explosion, the Amazon's elemental
  arrows and javelins), fans (Teeth, Multiple Shot), Charged Bolt and the
  novas, with synergies, masteries, pierce and FCR; Poison Dagger. Phase
  4 is done; the do-function missiles (Guided Arrow, Strafe, Chain
  Lightning, Meteor, Blizzard, walls) are listed in skills.md.
  Phase 5 has started: the Paladin's auras (on the right button: the
  friendly ones' stats, Prayer's healing, Holy Fire / Shock / Freeze
  pulses, Conviction on monsters in range) and the charge-up releases
  (bursts, novas, scattered fire and ice; Royal Strike's not yet),
  and summons (golems, skeletons from corpses, Valkyrie, the Druid's
  wolves / bear / raven) fighting at the player's side in the Blood Moor, and the Assassin's sentries (Lightning, Charged Bolt, Death) shooting
  the player's skill. Phase 5 is done; what's left of each part is listed
  in skills.md.
  Phase 6 (2026-09-26) rounds every class skill off: the do-function
  missiles (Guided Arrow, Strafe, Chain Lightning, Meteor, Blizzard, Fire
  Wall, Inferno), pets (the summon's stats and masteries, sumskills,
  Hydra, Revive, Decoy, Shadow Warrior, following across levels), the
  missile hit functions and spot spells (Frozen Orb, Glacial Spike, FoH,
  Blessed Hammer, the arrows, the Druid's elementals, Static Field, Corpse
  Explosion, Teleport), buffs / curses / war cries, and the rest (Royal
  Strike, Bone Wall / Prison, vines, shapeshifting, Armageddon, Blade
  Sentinel / Shield, the sentries). devctl `debug unbuilt` lists no class
  skill without a path; what each part approximates is in skills.md.
- Champion / unique behaviour mods (the mod functions at 0x73c008 past
  the stat ones): enchanted, cursed, mana burn, teleport, spectral hit,
  multishot, aura enchanted. Rolls, stats and names are built.
- Formulas taken from the published rules, not traced (combat.md): hit
  chance, the damage order, crushing blow divisors, block, hit recovery
  thresholds, FHR/FBR breakpoint tables, mana regen base.
- Monster life regeneration (MonStats DamageRegen), cold slowing the
  player, poison length reduction, set bonuses and the weapon swap in the
  stat sums.
- Mercs: resurrection at Kashya, their skills and gear, their share of
  experience.
- Monster AI: one melee/shooter think for every AI type; the per-type think
  functions (aip1..8) aren't traced.
- Class get-hit and attack sounds; Alt to show all ground item labels; the
  potion drink visuals.

Spawning
- Rooms populate at load in cell order (game.exe: on first activation);
  the +0x20 seed for group counts; which region component set a monster
  takes. (The game seed, regions and object seed match game.exe.)

Act 1 levels and rendering
- Built: Rogue Encampment, Blood Moor. Not built: Cold Plains, Stony Field,
  Dark Wood, Black Marsh, Tamoe Highland, the other caves/crypts,
  Tristram, Monastery through the Catacombs.
- Built and proven against game.exe (layout and tiles): the Blood Moor
  and the Den of Evil; click the cave mouth to go in, the stairs to come
  back. Preset units (proven too) put in the shrines, torches, chests,
  Flavie and Corpsefire with his minions. Shrines work (recharges and
  boosts, skill, gem, warping; the other magic shrines only log) and
  chests open, lock (keys) and drop their act chest TC; traps fire
  firebolts and poison (docs/research/re/objects.md). Missing: the unique
  behaviour mods, the other traps; warp arrival spots are a guess
  (LvlWarp ExitWalk).
- Rendering: cell-granular wall sorting, no item colour tints on composites,
  the neighbour level's NPCs drawn standing at their start.

Town
- Warriv's "go east", waypoint travel, the hire list's widget and offer
  count, Cain's spot in an already-rescued game, trade leftovers (magic
  stock, the real stock roll), saving .d2s.

## Networking-shaped core (from 2026-09-27; before more features)

Decided: every game is a server-authoritative `GameSession`; single
player is one client on an in-process transport
(docs/design/multiplayer.md "Decision"). Done before the feature list
below, so new features land on the new structure:

1. Research: done, `docs/research/re/network.md` (C → S sizes
   0x730dc0 and handlers 0x6e0d18; S → C table 0x7114d0; game frames
   at 1000 / fps in FUN_0052fc20 → FUN_0052d870, flush every 40 ms).
2. The split: done (2026-09-27). `World` (world.hpp) owns the game and
   steps at 25 Hz; `Town` is its client: input → `Command` (protocol.hpp:
   Move, UseSkill, Interact, Pickup, Resurrect), `Event`s back (level
   changed, open UI); monsters have unit ids; the camera slides between
   ticks; devctl `cmd` sends commands and the smoke test walks and
   attacks through them. Still on the client side of the line: item moves,
   stat / skill points, the store, potions and select-skill edit the
   character or Fight directly; objects and ground items are named by
   index; one shared rng.
3. Saving: done (2026-09-27). `CharacterStore` (character_store.hpp) writes
   the .d2s (components/d2s/d2s_write.hpp) when the player leaves the game
   or quits: parse-back check, temp file + rename, a one-time .d2s.bak; the
   19 real saves write back byte for byte (test_d2s). New characters get
   CharStats.txt's stats, start skill and items and are saved at once.
4. The client's side of the line, emptied (2026-09-27): character edits,
   item moves and NPC deals are commands; ground items and the
   character's items have unit ids; the World works out skill levels;
   commands cross as bytes through a `LocalTransport` (protocol.hpp,
   test_protocol).
5. Replication: done (2026-09-27). The World fills a `View` after each
   tick; it crosses as bytes (replication.hpp) and the client draws and
   clicks from it alone; the World owns its own character, the client's
   is the View's; run / walk, talking, sounds and events go over too.
6. Views as deltas: done (2026-09-27; about 0.4 KB a tick).
7. Deferred until Act 1 plays through (see "Road to a whole Act 1"): a
   World with N players (Fight's player state per player), `TcpTransport`
   (a second d2d joins), a standalone `d2ds` server, player-count scaling
   and party.

## Road to a whole Act 1 (from 2026-09-27)

Goal: Act 1 playable from the camp to Andariel and the way to Act 2, each
piece from game.exe (no lost bits: checked against it where the emulator
can, tools/emu). In this order:

**Step 1 — levels on demand; game data apart from graphics.** *Done
(2026-09-27):* `GameData::level(id)` builds a level from the map seed the
first time it's wanted, on one builder thread with its own MPQ handles and
DRLG tables; the levels next to the player's build ahead (`want_nearby`),
the Blood Moor while the menus run; monsters populate per level and
difficulty when first played (`level_spawns`). `Scene : GameData`: the
World holds a `const GameData*`, so it can't reach sprites, fonts or
palettes; animation timings come from the COF alone (`npc_timing`,
`composite_timing`); missile sprites live in `Scene::missile_cels`. Debug
start 3.8 s → 1.6 s. Since: a server's levels hold no tile pixels; the
monster regions match game.exe (every level's at game start on one seed,
monsters.md); the object seed is game.exe's (objects.md; d2d restarts it
per level, game.exe draws it across the game). Left: levels 2 and 8 are
the only ones built (`kBuiltLevels`).

Planned as:
Every level is built at start today (0.1–0.4 s each optimised, monsters
for all three difficulties); Act 1 has ~30. The same cut serves the
standalone server later.
- `GameData` (the World's): tables, levels (layout, collision, rooms,
  warps, objects), monster populations, animation timings (COF +
  animdata). `Assets` (the client's): DT1 / DCC / DC6 pixels, fonts,
  sounds. The World never touches `Assets`.
- A level is built from the map seed the first time it's needed
  (deterministic, so client and server agree), only at the game's
  difficulty; the levels next to the player's build in the background.
- Checks: start time flat as levels are added; the Blood Moor and Den
  still match game.exe (test_drlg / emulator); smoke green.

**Step 2 — Act 1's content, researched then built:**
1. Preset units (`FUN_00667620`): the monsters and objects a level's
   DS1s place — superuniques, quest objects, special chests. *Researched
   and built (2026-09-27):* the roll-to-stay outdoors; MonPlace markers
   (Fallen / shamans at their spots, champion and unique packs, Blood
   Raven); superunique minions MinGrp..MaxGrp (+ difficulty) at radius 3.
   Left: a maze's roll, the superunique specials (Countess, Smith, ...),
   quest objects' behaviour.
2. Outdoor levels: Cold Plains, Stony Field, Dark Wood, Black Marsh,
   Tamoe Highland, the Burial Grounds, Tristram; each checked against
   game.exe like the Blood Moor.
3. Dungeons: the caves and holes (Cave, Underground Passage, Hole, Pit),
   Crypt / Mausoleum, the Forgotten Tower, the Monastery (Gate, Outer
   Cloister, Barracks, Jail, Inner Cloister, Cathedral), Catacombs 1–4.
4. Waypoint travel; level warps between all of them.
5. The six quests: states, NPC talk, rewards (Den of Evil, Sisters' Burial
   Grounds, Search for Cain, The Forgotten Tower, Tools of the Trade,
   Sisters to the Slaughter), and their superuniques.
   *Den of Evil built (2026-09-27, docs/research/re/quests.md):* Akara
   gives it, the Den counts down, clearing it is announced in the class's
   voice, Akara's reward is a skill point; the flags are game.exe's; the
   quest log. Left: the "!" marker, Akara's respec (quest 41).
6. Andariel (her AI, poison) and Warriv's way east: the end of Act 1.
7. Alongside: the remaining unique mods (Cursed, thief, poison hit,
   teleport, auras), town portals, the corpse on death — all built.

**Checkpoint 2026-09-28.** The groundwork for the rest is in: the game
library (`d2d::game`) builds and plays without the client; descriptive
names and per-file includes, checked in CI; the monster regions, game
seed and object seed traced and matching game.exe (monsters.md,
objects.md); MonStats' duplicate Id quirk matched (bugs.md #13).
`diff_drlg.py 1-10 <level>` over Act 1: the Blood Moor and the Den match
game.exe on every seed, the 36 other levels on none yet (our generator
covers only the paths those two take). Next, in order:
1. Cold Plains (level 3), then the other outdoor levels that share its
   generator (Stony Field, Dark Wood, Black Marsh, Tamoe Highland); each
   joins `kBuiltLevels` once `diff_drlg` matches.
2. The caves and crypts on the Den's maze generator, with the cave theme
   rooms (FUN_006735f0).
3. The presets: Tristram, the Monastery, the Catacombs.
Still open in the Blood Moor: rooms populate at load, not on first
activation; which region component set a monster takes; the Portal
Shrine; warp arrival spots (a guess); the camp's hidden river-edge walls
(game.exe keeps them as wall tiles; drawn or only blocking isn't traced).

**Step 3 — networking** (the deferred item 7 above).

Blood Moor polish (2026-09-27): ambient events and song resume done
(sound.md); Act 1's unique mods (Cursed, teleport, auras; monsters.md).
Storm, Exploding and Poison Shrines (objects.md); town portals and the
Portal Shrine. The quest log; the time of day, the light grid and lit
drawing, night sounds (lighting.md). Each character keeps its map seed
(drlg.md). Blended tile shadows and unit shadows; rain (weather.md).
Death (the corpse, penalties); the look follows what's worn, item colour
tints (compcode.md); the dead-hardcore ghost (char-select.md).

deviations.md lists where d2d differs from game.exe on purpose (the
companion of bugs.md).

Loose ends noted 2026-09-27: `par34` in Bone Wall's calc2 is a typo in
Blizzard's Skills.txt (what game.exe's calc parser makes of it isn't
traced); a Debug build loads ~2.5x slower than RelWithDebInfo.

## Earlier list (as of 2026-09-26; superseded by "Road to a whole Act 1")

Done and proven: the Blood Moor and the Den of Evil (layout, room seeds,
tiles, preset units; docs/research/re/drlg.md "Checking against
game.exe"). Built from game.exe but not emulator-checked: champions and
uniques (docs/research/re/monsters.md), shrines and chests
(docs/research/re/objects.md "Shrines and chests").

Done from the half-day list (docs/research/re/objects.md, monsters.md):
the skill, gem and warping shrines; locked chests (keys, two rounds), the
empty quarter; all eight chest traps (trap monsters' one shot at
level 1 / 4 / 8, PrimePoisonNova's rings, Trap Nova, fires, trap 8's
undead); the chest item level (the chest
unit's, the area level); the boss label under the name (the 0x725188
table).

Left for those, ranked:

1. Done: "Champion" is a champion's name word ("Champion Zombie", FUN_004ac870); the label's joiner is a space; labels are for uniques and minions, led by Demon / Undead (monsters.md).
2. Unique mods: built (2026-09-27): fire / lightning / cold enchanted,
   spectral hit, multishot, mana burn, the difficulty bonus (monsters.md
   "Boss mods in the fight"). Left: Cursed (a curse on the player), thief,
   poison hit, teleport (the AI), auras, Charged Bolt's wander.
3. Storm, exploding and poison shrines: thrown potions and fireballs.
   Chest 397's own drop table.
4. Seed emulation (2+ days, research): the object seeds and the
   monster's own seed, so shrine picks and boss rolls can be checked
   against game.exe per room. game.exe populates a room on activation,
   so the order follows the player.
5. Portal shrine: waits for town portals.

Then:

6. Saving .d2s.
7. Cold Plains and the rest of Act 1's outdoor levels.
8. Skill fidelity: pet AI, the shapeshifted look, the approximations
   listed in skills.md.

Town leftovers (Warriv's "go east", waypoint travel, merc resurrect, the
hire list widget, Cain's spot, trade leftovers, charge recharging) are
in the checkpoint above.
