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

Status as of 2026-10-25.

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
   render tiles. *Done for Acts 1–5:* frontend, the five towns, NPCs,
   panels, trade, hiring, waypoints across acts, all 39 + 35 + 28 + 6 + 28
   levels (maps, monsters, objects and drops match game.exe, but for the uber
   levels 133–136; monsters and objects on all three difficulties), the twenty-seven quests, both
   endings, saving. Left: the uber levels' monsters and object groups.
6. **Combat + AI**: actor state machine, packet-equivalent events,
   monster AI from game.exe. *Implemented:* monsters and their fights,
   gear in combat, drops, experience, the merc, and the skills (phases 0–6
   in `docs/research/re/skills.md`); Acts 1–4's MonAI thinks, Diablo's
   included, run against game.exe's own (`tools/emu/mon_ais.py`, `npcs.py`,
   `jarjar.py`, `diablo_score.py`), and Act 5's, Baal and the ubers
   included. Left: the unique mods still untraced (thief, the act
   bosses' deaths), the gaps each skill section lists.
7. **Cross-platform polish** — Linux/macOS/Windows CI, controller and
   rebindable keys and pad (game.exe's Configure Controls) done; left:
   high-DPI.

## Directory layout

```
apps/           end-user apps: launcher (Qt6 installer), d2d (the game)
assets/         not code: the icon and banner (assets/icon), Flatpak's desktop entry and app info (assets/flatpak)
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
combat and skills), the Den of Evil, every Act 1 outdoor level and the
caves (9–16). Next: the rest of Act 1.

## Open questions

- Launch page: settled 2026-10-07. `d2d-launcher` finds or installs the game,
  holds the Fullscreen setting, and its Launch button saves `d2d.cfg`, starts
  `d2d` and closes. There's no separate launch page.
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
  entry flag bits, units blocking each other, doors. Every walker takes
  game.exe's pathers (collision.md "Path types"): the player type 7,
  monsters, NPCs, Cain, the merc and pets 0xd then 0xf.
- Open, by act and in joined games: the newest checkpoint below.
  (The September playthrough notes here were all fixed; git history has them.)

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
   isn't built (Stony Field) logs "not implemented". ponytail: the
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
  multishot, aura enchanted; the champion kinds (ghostly, fanatic,
  possessed, berserker; 2026-09-29). Rolls, stats and names are built.
  Left: the ghostly see-through draw (client flag 0x40).
- Formulas taken from the published rules, not traced (combat.md): hit
  chance, the damage order, crushing blow divisors, block, hit recovery
  thresholds, FHR/FBR breakpoint tables, mana regen base.
- Monster life regeneration (MonStats DamageRegen), cold slowing the
  player, poison length reduction, set bonuses in the stat sums.
- Mercs: resurrection at Kashya, their skills and gear, their share of
  experience.
- Monster AI: one melee/shooter think for every AI type; the per-type think
  functions (aip1..8) aren't traced.
- Class get-hit and attack sounds; Alt to show all ground item labels; the
  potion drink visuals.

Spawning
- Rooms populate as they come into play, on the room1 seed (done
  2026-09-28, monsters.md "When a room populates"); left: tiles still
  come up at load, the camp's room order; the +0x20 seed for group counts.
  A monster's look is game.exe's pick from its unit seed (done 2026-09-28).
  Random object groups (FUN_00552610) are built and match game.exe (2026-09-30).

Act 1 levels and rendering
- Built: Rogue Encampment, Blood Moor, Cold Plains, Stony Field, Dark
  Wood, Black Marsh, Tamoe Highland, Burial Grounds, Moo Moo Farm, the
  caves (9–16). Not built: the crypts, Tristram, Monastery through the
  Catacombs.
- Built and proven against game.exe (layout and tiles): the Blood Moor
  and the Den of Evil; click the cave mouth to go in, the stairs to come
  back. Preset units (proven too) put in the shrines, torches, chests,
  Flavie and Corpsefire with his minions. Shrines work (recharges and
  boosts, skill, gem, warping; the other magic shrines only log) and
  chests open, lock (keys) and drop their act chest TC; traps fire
  firebolts and poison (docs/research/re/objects.md). Missing: the unique
  behaviour mods. Warp arrival traced (drlg.md "Taking a warp").
- Rendering: cell-granular wall sorting, no item colour tints on composites.
  The neighbour level's NPCs patrol and are drawn where they are (2026-09-29).

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
7. The N-player World: done (2026-10-19, local co-op, PR #57, #34). `World`
   holds up to four `World::Player`s (each its character, unit, merc and
   Fight state), commands come tagged with their player (`Order`),
   `World::join` / `leave` come after one `new_game`, and player-count
   scaling, party experience, gold and quest credit follow game.exe
   (docs/design/local-coop.md, net-host.md items 1, 4, 5). Still to do:
   `TcpTransport` (a second d2d joins) and a standalone `d2ds` server.

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
monsters.md); the object seed is game.exe's, one across the game as rooms
come up (objects.md). Left: levels 2 and 8 are
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
2. Outdoor levels: Stony Field, Dark Wood, Black Marsh,
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
`diff_drlg.py 1-10 <level>` over Act 1: the Den and every outdoor level
(2–7, 17, 39), the caves (9–16) and the preset levels (20, 25–27,
32, 33, 37, 38) match game.exe on every seed, grids through tiles and
units. Next: the other maze level types on the Den's generator, each
joining `kBuiltLevels` once `diff_drlg` matches — crypts (LevelType 4:
18, 19, 21–24), Barracks (28), Jail (29–31), Catacombs (34–36).
**Checkpoint 2026-09-30.** Every Act 1 level now builds and matches
game.exe on `diff_drlg.py` (grids, tiles, units): crypts (18, 19, 21–24),
Jail (29–31), Catacombs (34–36) and the Barracks (28, placed beside the
courtyard). `kBuiltLevels` holds 2–39. Next: the rest of Act 1's play —
quests 2–6, Andariel, preset specials (superunique
rules, quest objects), random object groups; research in
docs/research/re/quests-act1.md, act1-end.md, preset-specials.md.
Waypoint travel within Act 1 is in (C→S 0x49, dark wilderness waypoints).
Sisters to the Slaughter (quest 6) is in: talk, alerts, Andariel's death
hook (gems, progression, the lair portal), Warriv's Go East (Act 2 itself
not yet).
**Checkpoint 2026-09-30, later.** All six Act 1 quests are in (Den, Burial
Grounds + Kashya's merc, Tools of the Trade + Charsi's imbue, Search for
Cain + Tristram, Forgotten Tower, Sisters to the Slaughter); doors and the
dungeon objects (containers, stands, wells, bookshelves, exploding
barrels); 17 Act 1 MonAI thinks including Blood Raven; walk-through level
links (Levels.txt Vis with Warp -1: Outer Cloister ↔ Barracks, Inner
Cloister ↔ Cathedral); lone warp units (the Forgotten Tower's stairs).
Open: bit-exact object groups, dungeon monster population, drops, the Countess's
AI and superunique specials.

**Checkpoint 2026-09-30, placement.** Random object groups and monster
placement match game.exe on levels 2–39 (`diff_drlg.py 1-50 <level>
monsters|objgroups`, 1-50: all pass).
Collision is built per room as it comes up (FUN_0064c900), with shared-edge
patches (FUN_0064c860), in the order the player brings rooms up (relevel;
`diff_drlg.py 1-10 <level> collision`, shuffled order, levels 2–39 pass);
ponytail: units, warps and object groups stay as list order made them. Still open: object drops on the shared rng, drop
scatter, the Tristram portal position, the HC smoke's
extra life. Netplay: plan in docs/design/net-join-plan.md (built since: PRs #8–#12).
Quest log (2026-10-03): all six Act 1 records, and the panel as game.exe
draws it (tabs by act reached, held icons and the questdone plate, the
pick on opening, hover texts, FontFormal11) and the Quest Log button a
quest's news brings up (UI 0x11, S->C 0x5d); quests.md.

**Checkpoint 2026-10-01.** Full sweep (`sweep.sh 1-20 monsters objgroups
drops collision tiles`): 190/190, every level 2–39. Object drops roll off
their own seeds and land as FUN_00555da0 finds room (drops.md); collision
follows the order rooms come up (relevel); monsters notice as FUN_005dd7f0
(monster-ai.md: outdoor no sight, indoor sight then spawn-area alert);
Cain's portal at game.exe's spot. Still approximate (ponytail): object
seed per level, not game-wide (both closed by the next checkpoint). (search_target was already FUN_005dd7f0's; what a monster then hits
now follows its target: monster-ai.md "The attack's target".)

**Checkpoint 2026-10-01, one object seed.** The object seed (game +0x10f0)
is one per game (`Spawning::objects`): every room's presets and groups and
every container opened step it in the order they happen, and a room's
objects come with it (`room_objects`, the level's npcs grow, footprints
stamped as made). relevel takes the room1 seeds from the live order too;
units and warps come out the same in any order (`diff_drlg.py <level>
units` brings game.exe's rooms up in $ORDER), the spawn areas too (the game
seed through the monsters matches). `diff_drlg.py 1-20 0 game` (Blood Moor, Den, Stony Field,
Cave 1 in one game, rooms shuffled, every container opened): 20/20; reverse
and list order, and touching levels (3,2,17,18,5,6,7) 20/20. Still
approximate (ponytail): relevel laying the whole level (same result, only
slower), and rooms never freed. Units off every room turned out never to be
made by game.exe either (2026-10-03, drlg.md "A room's life on the server":
the near-list walk, the 5 s freeing, the storage and restore). Since: the camp populates as game.exe (town-start.md), and its
NPCs walk on game.exe's think (MonAI Npc, town-npcs.md: `diff_drlg.py 1-40 0
npcs` 40/40); the MonWndr wanderer (FUN_0054ef50) is written up there, its
draw stepped, the monster not made (1 room in 1.1 million).

**Checkpoint 2026-10-03, research pass.** Nine topics researched in
game.exe and built where Act 1 needs them:
- The quest log panel and its Quest Log button (quests.md); the chain,
  "!" alerts and done flash were already in.
- Monster hits follow the think's target; no idle wander; Cursed's seed
  and radius; Gargoyle Trap shots (monster-ai.md).
- The calc compiler ported (FUN_006c1ae0, all 1,650 calcs match):
  Bone Wall's `par34` reads as `par3` (skills.md, bugs.md #2).
- Superuniques: boss sound sets, life-bar name colours
  (superuniques.md). Open: Andariel and Blood Raven spawn without the
  unique flag (gold name, Blood Raven's half freeze).
- Monster stats as FUN_00573cb0 (noRatio rows; monstats.py 2202/2202);
  poison and cold conversion (monsters.md, combat.md).
- Town NPCs walk on game.exe's think (town-npcs.md).
- A room's life on the server traced; act bosses count one unique
  (drlg.md, bugs.md #15).
- Chest traps fire 35 frames on, Act 1's swap to firebolt; object
  blasts rolled and resisted; the Moldy Tome speaks (objects.md).
- Stun guards, exact mana regen, the need-mana voice, Find Potion by
  act (combat.md, skills.md).
Shortcut comments were sorted per file into Act 1 work / networking /
later acts / another area; most Act 1 ones left are small and invisible.
Open and noticed: missiles fly 4/3 too fast (every game.exe missile is
velocity x 75/100, FUN_0059fa30; the stepper isn't traced). The sweep
(`tools/emu/sweep.sh`) now runs a Release drlg-dump, one emulator boot
per level, with live progress. Full sweep after the merges (1-20, six kinds,
levels 2–39): 228/228; ctest 31/31.

**Missiles (2026-10-04), branch act1-missiles:** missiles fly as game.exe.
`rules/missiles.hpp` (FUN_0059fa30's x75/100 velocity, the 16.16 step,
halved walk, rooms, Charged Bolt's wiggle FUN_005c9290); every missile
(player, merc, pets, monsters, traps) flies on `rules::MissileFlight`, hits
by footprint on entered subtiles; `tools/emu/missiles.py` checks against
game.exe's own (ok); write-up in docs/research/re/missiles.md. ctest 31/31,
clang-tidy 0, sweep 228/228 at 20/20; played live (Bret), missiles land and kill.

**Found in play (2026-10-04, Bret), not yet looked into:**
- Fixed: the farm's cow (AI Idle, untraced) took the untraced-AI chase,
  followed the player and went invisible in an attack mode it has no
  graphics for; Idle now stands (unverified, unverified.md).
- Fixed: an opened chest replayed its lid forever; objects now hold the
  last frame in a mode whose CycleAnim is 0 (unverified, unverified.md).
- Fixed: Scroll / Tome of Identify: right-click picks it, a click on an
  item identifies it (cmd::IdentifyWith, FUN_00561ed0), with the
  identify cursor drawn ("Identify cursor" below).
- Fixed (4a882fb): a magic Bolts quiver (cqv) with neither prefix nor
  suffix landed in "Bob Bitchen.d2s"; with no affix game.exe remakes it
  superior, then normal (FUN_005565e0 → FUN_00557320 / FUN_005572a0).
- Fixed: the Esc menu's pentagrams stepped every 40 ms with catch-up;
  FUN_00454850 steps once a draw past 50 ms (menu.md).

**Bosses and NPCs (2026-10-04), branch act1-bosses-npcs:** act bosses
spawn as FUN_005b1cf0 (Andariel mod 22; Blood Raven 12 + 22 and half
freeze; the Smith 22, which the sweep caught; flag 8: gold bar name, Demon
label, TreasureClass3). Town NPCs walk up to a player they have a "!" for
within 16, greet 2 off, go home past 16, and stand while talked to
(FUN_005e68f0, bugs.md 16). The smoke test's monster wait is longer (rooms by the camp
come up empty since the traced room life). ctest 31/31, clang-tidy 0,
-Wshadow 0, sweep 228/228 at 10/10.

**Identify cursor (2026-10-04).** game.exe's cursors are `Gaunt, grasp,
ohand, orotate, ppress, protate, buysell` (rows at 0x712010, 28 bytes, name
last; state DAT_007a6adc / DAT_007a6af0). Right-clicking a scroll or tome
runs its item-skill pre-use (pSpell 1, table 0x741790: FUN_005be130).
That sends S→C 0x3f with code = FUN_0062bb20 (a scroll / tome: Books.txt
SpellIcon, Identify 0, TP -1 so no packet; any other item: ItemsTxt +0x134
spellicon) and the book's skill. The client's FUN_00468010(item, code)
sets state 6, which is identify mode: an item click then sends C→S 0x27
(FUN_0048ffe0). FUN_00468460 draws buysell frame = code, bottom 0x21 below
the hotspot. d2d draws that (main.cpp). Checked live (Bret, 1.14d): the
hooked arrow, frame 0. Frame 8, a magnifier, is used by nothing in
Books.txt or Misc.txt.

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
Blizzard's Skills.txt (game.exe reads it as `par3`, skills.md; closed
2026-10-02); a Debug build loads ~2.5x slower than RelWithDebInfo.

## Road to Act 2 (from 2026-10-10, branch `act2`)

Goal: Warriv's caravan lands in Lut Gholein and Act 2 plays from there
to Duriel and Jerhyn's way east (Meshif), each piece from game.exe and
diffed where the emulator can. It follows Act 1's road; Act 1's
leftovers (act1-gaps.md) and hosting a game wait.

**Step 1 — the act's levels, checked with `diff_drlg.py`:**
1. The layout (FUN_00677790) and the tomb rolls. *Done (2026-10-10):*
   `act2_layout`, `roll_act2_tombs`, `lut_gholein_file`
   (components/drlg/drlg.hpp, drlg.md "Act 2's layout"); `diff_drlg.py
   1-2000 40 layout`: 2000/2000.
2. Lut Gholein (DrlgType 2, LvlPrest 301, LutW / LutN by the Rocky
   Waste's side) as a town: its DS1, tiles, warps, NPCs and their
   MonAI, waypoint. *Built (2026-10-10):* level 40 builds on demand
   like any level (`kBuiltLevels`), acts 1 and 2's presets load;
   `diff_drlg.py 1-20 40` / `tiles` 20/20, `units` 10/10. Its start and
   portal spots come from the rooms' spawn tiles (BuiltRoom::spawns).
   Its NPCs stand where the DS1 puts them with game.exe's menus (0x726c48);
   Drognan, Elzix, Fara and Lysander trade, Greiz hires Act 2's desert
   mercenaries (`World::hire_lists`, one per act), Warriv's Go West lands
   by him in the camp (FUN_0054b830(1, 5): spawn index 5, `Level::spawn_at`),
   vendors restock on an act change. The NPCs walk their DS1 paths (Npc
   AI), the townsfolk and guards wander (Towner), the traders idle
   (Vendor; town-npcs.md "Act 2's town AIs"). Jerhyn and the palace
   guard come from a2q4's two marker objects (quests-act2.md); the palace
   door leads into Harem Level 1 (level 50: tiles 10/10, units 10/10).
   The town's thinks match game.exe (`diff_drlg.py 1-20 40 npcs`, 20/20).
   The hire lists are game.exe's (npc-menu.md "Hire": entries on the game
   seed, ten offered, the offers off each entry's seed; checked by
   tools/emu/hire.py). The vendors' stock is game.exe's fill (store.md;
   tools/emu/store.py 10200 / 10200) with game.exe's magic affixes
   (drops.md "Magic", 9994 / 9995). Lut Gholein's container drops are
   game.exe's (a container rolls its level's act's chest class, FUN_00585b90;
   `LEVELS=40 diff_drlg.py 1-5 0 game` 5/5). JarJar's think (the palace
   guard) is game.exe's too (tools/emu/jarjar.py, town-npcs.md).
3. The desert (LevelType 16: 41–46): the outdoor generator with act 2's
   border and exit rules (drlg.md: exits in pairs from 0x6f112c, columns
   2 / 3 of 0x6f0630), its LvlSub / presets. *Built (2026-10-10):*
   drlg.md "Act 2's desert"; 10/10 tiles and units on every level; the
   town walks out into the Rocky Waste.
4. The mazes: Sewers (47–49), Harem 2 / Palace Cellar (51–54), the tombs
   (55–61, 66–72, the boss tomb ×3 and staff tomb ×2 rooms), the Maggot
   Lair (62–64), Ancient Tunnels (65), the Arcane Sanctuary (74); presets:
   Harem 1 (50), Duriel's Lair (73). *Built (2026-10-10):* 47–73 match
   game.exe (rooms, tiles, units with doors; drlg.md "Act 2's mazes") and
   build in play. The Arcane Sanctuary (74) too (2026-10-11): 20/20, tiles
   and units 10/10.
5. The act 2 / act 4 MonPreset remaps and type-4 units (NPCs by name).

**Step 2 — moving between acts:** *Done first (2026-10-10), ahead of the
town:* Warriv's Go East lands in Lut Gholein (`World::arrive`, the save's
act byte set), a save left in act 2 starts there, town portals and
respawns go to the act's town (`GameData::town_of`), every town counts
as town (`in_town`), the palette follows the act (`load_act_palettes`),
the quest log opens on the act's tab; `tests/quests_d2d.py` rides the
caravan; Warriv takes the player back west; waypoints cross acts; Meshif
sails to Kurast Docks and back. The acts stay as they were left: game.exe's
server builds an act once and frees its acts only as the game ends
(FUN_0052c7f0); the client swaps its one act on LoadAct (FUN_0044e100).
d2d keeps the earlier acts' levels built, as the server does.

**Step 3 — Act 2's content:** the town (Atma, Drognan, Elzix, Fara,
Geglash, Greiz' hire list: act 2 mercs and their auras, Jerhyn, Lysander,
Meshif, Warriv), the six quests (Radament, the Horadric Staff and Cube,
Tainted Sun, Arcane Sanctuary, the Summoner, Seven Tombs), the desert's
monsters and MonAI, Duriel.
*Started (2026-10-11):* Act 2's opening (Jerhyn's greeting, the game's
flag that moves him into the palace) and Radament's Lair (Atma, the kill,
the Book of Skill, the log's Act 2 tab) are game.exe's records
(quests-act2.md), checked live. Next: the Horadric Staff (record 9),
then Tainted Sun, Arcane Sanctuary, the Summoner, the Seven Tombs (all five in by 2026-10-11, quests-act2.md).
*Done (2026-10-12, PRs #17–#19):* Act 2's MonAI thinks (17, run against
game.exe's own), MonSeq skill sequences row by row, Act 2's objgroups,
monster curses on the player, Smite's stun, buried maggots and aloft
vultures, the Maggot Queen's poison hit, Radament's corpse reach, the
Seven Tombs' log, Meshif taking the staff's parts, and in a joined game the
host's quests, travel and every unit's states.

## Road to Act 3 (from 2026-10-12; steps 1.1–1.3 in PR #22)

Same road as Act 2's: the levels diffed against game.exe, then the town,
the quests, the thinks.

**Step 1 — the act's levels, checked with `diff_drlg.py`:**
1. The layout (FUN_006789b0): the docks, the jungle's three pieces and
   their cells, Kurast stacked above. *Done (2026-10-12):* `act3_layout`
   (drlg.md "Act 3's layout"); `diff_drlg.py 1-2000 75 layout`: 2000/2000.
2. The Kurast Docks (75, LvlPrest 529) as a town. *Rooms, tiles and
   units already matched (20/20, units 10/10).* Its NPCs think as
   game.exe's (`diff_drlg.py 1-20 75 npcs` 20/20); Hratli's and
   Natalya's markers make them as the game's quest words stand
   (town-npcs.md "The Kurast Docks"); the act palette follows any act.
3. The jungle (76–78) built from its cells. *Done (2026-10-12):* drlg.md
   "Act 3's jungle"; tiles 10/10, units 5/5 on all three. Builds in
   play; the docks walk into the Spider Forest and back. MonPlace
   fetish / fetish shaman / tentacle heads (FUN_0054e600) placed.
   *Population exact (2026-10-13):* `diff_drlg.py 76|77|78 monsters`
   10/10 / 5/5, `LEVELS=75,76,77,78 game` 5/5. The collision grid
   stamps shadows' subtile flags too (fixed act 2's desert as well);
   spawnCol 1 types spawn on water (FUN_005b2700), a tentacle head
   spreads its tentacles (FUN_005b2570), the Dark Wanderer's marker
   (a3q7) makes him (monsters.md "Act 3's jungle").
4. Kurast (79–83). *Done (2026-10-13):* drlg.md "Kurast and Travincal"
   (FUN_0067f190 walls, inner presets and scatter; FUN_0067f3b0);
   `diff_drlg.py 79..83 tiles` 20/20 each, units 5/5, monsters 5/5.
   Travincal's compelling orb (InitFn 60) made as game.exe's (monsters.md
   "Kurast and Travincal"); all five build in play. The whole act as one
   game: `LEVELS=75,..,83 diff_drlg.py 1-20 75 game` 20/20 (floor lists and
   areas follow the rooms' order; water packs search the room they stand
   in; tiles shared with the level next door). Spawn area ids as game.exe's,
   merged across rooms (`area_ids.py` every tile 5/5 on 79-81, 3, 6); mode
   5's search and the pet / merc thinks compare them (FUN_0061b130).
5. The dungeons, sewers, temples and the Durance (84–102). *Done
   (2026-10-13):* drlg.md "Act 3's mazes" (LevelTypes 22–25: the ring,
   the spider caves' def table, the sewers' ring of 5, the specials);
   the presets matched already. `diff_drlg.py 84..102`: rooms 20/20,
   tiles, units, monsters 5/5. All build in play.
6. The Dark Wanderer's walk and afterglow. *Done (2026-10-13):* MonAI
   91 walks him off; FUN_005bd390 leaves object 131 round where he went
   (monsters.md "Act 3's jungle"). Step 1 done; a store now opens on its
   first full tab in the app too (it showed tab 0).

**Step 2 — the act's monsters (2026-10-13):** 14 of 15 Act 3 MonAI thinks
as game.exe's (monster-ai.md "Act 3's thinks"; `mon_ais.py`, 5000 runs
each): Mosquito, ThornHulk, FrogDemon, the tentacles, FetishShaman,
FetishBlowgun, the zealots and priests, the Council, Mephisto, his hydras;
in play through `ai.cpp`, Submerge / Emerge as the maggots' burrow. Later
the same day: WillOWisp, the plain S1 casts (ZakarumHeal / Lightning,
MonTeleport, Hydra), MonFrenzy's state and Inferno's breath.

**Step 3 — Mephisto (2026-10-13):** a3q6's death hook (quests-act3.md):
word 0x16, progression to Act 4, the soulstone, the hellgate open (its DC6
parts drawn) and the bridge up. Left: Act 4 itself (the hellgate says so),
Act 3's other quests.

**Step 4 — the quests (2026-10-13):** all of a3q0..a3q6 as game.exe's
(quests-act3.md, `rules/quests_act3.hpp`; message tables by
`tools/emu/quest_msgs.py`): Hratli's greeting, Lam Esen's Tome, the Golden
Bird, the Blade of the Old Religion, Khalim's Will (chests, the sewers'
lever, the Cube), the Blackened Temple (the Council, the orb) and the
Guardian's report. `tests/quests_d2d.py` plays them all. The plain casts
(S1 / A1 / A2: heals, lightning, teleport, Hydra) and the will-o-wisps'
dance are in too (monster-ai.md). Act 3 is done; next: Act 4.

## Road to Act 4 (from 2026-10-14, branch `act4`)

Traced first, all of it, before any port (2026-10-14). Same road as Acts 2
and 3: the levels diffed against game.exe, the town, the monsters, the quests.

**Step 1 — the levels** (drlg.md "Act 4's layout", "Act 4's outdoors",
"Act 4's River of Flame"; the oracle already builds 103..108):
1. The layout (FUN_00678a20): two chains as act 2's; the Outer Steppes'
   own placer (FUN_00676c00) and its 0x400000 / 0x800000 flag.
2. Pandemonium Fortress (103): one preset (797 Fortress.ds1); d2d just
   doesn't load act 4's presets yet.
3. The Mesa (104..106, LevelType 27): borders in column 3 (799..), the
   FortMesa entry, border LvlSub base 799, FUN_0067e6a0's anywhere list.
4. The Chaos Sanctuary (108): a fixed 5x5 of 24x24 presets.
5. The River of Flame (107, maze type 28): grow, then FUN_00673320 (Warp
   Mesa below, BridgeLava above, two bridges, the forge swap, the LavaX fill
   on eight sides, the shift under the sanctuary).
6. Then population (`diff_drlg.py monsters`, `game`), act 4's NPC thinks
   (`npcs.py` level 103), `ACT=4 sweep.sh`.

*Done (2026-10-16):* the layout 2000/2000; 103..108 rooms, tiles, units,
monsters, object groups as game.exe's (`ACT=4 sweep.sh`). New on the way:
animated presets keep every frame (DrlgAnim.cpp), FillBlanks is the row's
flag, outdoor preset units roll on the room's whole seed, the River of
Flame's pass (FUN_00673320) and its file rotation, the knights' variant
draw, InitFns 34 / 46 (trapped souls) / 58. The hellgate takes the player
to the fortress (its start: the first spawn tile, FUN_0066ac40);
`quests_d2d.py ... 4`; the fortress's NPCs think as game.exe's
(`diff_drlg.py 1-20 103 npcs` 20/20). Nightmare / hell population: done
2026-10-17 (monsters.md "Nightmare and Hell").

**Step 2 — the monsters** (monster-ai.md "Act 4's MonAIs", skills.md "Act
4's monster skills"): VileMother, VileDog, FingerMage, Regurgitator, the
three knights, Megademon, Izual, Diablo (his 17-way chooser). Seven already
run in `mon_ais.py`'s harness; VileMother (search type 0xc, the lay),
Regurgitator and OblivionKnight (their search callbacks) and Diablo (his
target / action choosers) need hooks. Skills: srvst 49 / 53 / 54, srvdo 91,
100..104, 108, 112, 148, 152.

*Done (2026-10-16):* all twelve thinks (TrappedSoul too: the souls InitFn
46 makes) as game.exe's, `mon_ais.py` → `tests/mon_ais4_cases.inc`, wired
in `ai.cpp`; a live run walks, swings and casts on 104..108. Skills cast:
MonCurseCast, DiabCold, DiabLight (as the breath), the spider / knight /
bone spirit missiles, RegurgitatorEat, Bone Armor, Nest (the lay), and
roughly: DiabRun (as Charge), DiabWall (a fan), DiabPrison (four
boneprison1). Left as notes: armor absorb, the knights' missile variant,
Diablo's target score, the target's skills / states — all done by
2026-10-17.

**Step 3 — the quests** (quests-act4.md): the opening, Izual (his ghost),
the Hellforge (Hephasto's hammer, the soulstone, the gems and rune), the
seals and their three bosses, Diablo's release (the sanctuary cleared) and
death; the end: classic 90 s on, the game ends; LoD Tyrael's portal (Act 5
isn't built: it stops there). The hellgate opens the fortress.

*Done (2026-10-16):* `rules/quests_act4.hpp` (Act4Opening, IzualQuest,
HellforgeQuest, TerrorQuest), `world_quests_act4.cpp` at the World's hooks
and the quest objects (the forge, the five seals and their three
superuniques, Diablo's release and spawn at object 255, Tyrael's last
portal 566). `tests/quests_d2d.py ... 4` plays all four from a new
character. Left as notes: the quest log's +0xb values (inferred), the
classic ending (90 s, the credits), the seal flash's event (the boss comes
at once), the gems at once on loot's rng, the burning souls' chest drop,
Act 5 past the portal.

## Checkpoint 2026-10-24: Act 5 (Lord of Destruction)

Branch `act5`:

- Levels: Act 5's layout and all 28 levels (Harrogath to the Worldstone
  Chamber) match game.exe room for room, tile for tile, in units, object
  groups and monsters (docs/research/re/drlg.md "Act 5's layout"); the
  uber levels 133..136 build but their monsters / object groups differ.
- Harrogath's NPCs as game.exe places them; its town portals open at its
  start tile (no index-11 tile: `FUN_0066ac40` takes the first).
- Thinks: 22 Act 5 MonAIs verified against game.exe in tools/emu
  (`tests/mon_ais5_cases.inc`), plus the quest-driven Wussie, BaalThrone
  and BaalToStairs (ai.cpp). MonStats Align at spawn (`FUN_005b2a00`): the
  defenders and prisoners are on the player's side.
- Quests (docs/research/re/quests-act5.md): the introductions (the save's
  NPC section kept), Siege on Harrogath, Rescue on Mount Arreat, Prison of
  Ice, Betrayal of Harrogath, Rite of Passage, Eve of Destruction —
  `tests/quests_d2d.py ... 5` plays all six from a new character, with
  Larzuk's sockets and Anya's personalizing (Charsi's imbue at their NPCs).
- Lower walls (Act 5's cliffs) draw under the floors, as game.exe.

- Baal's fight (monster-ai.md "Baal"): his think and his clone's proven on
  game.exe (`tests/mon_ais_baal_cases.inc`), the Vile Effigy, Festering
  Appendages, the cold missiles' maker.

- AncientStatue traced: it never spawns in 1.14d. The ubers as game.exe has
  them (monster-ai.md "AncientStatue and the ubers' MonAIs"): UberIzual's
  think proven in tools/emu; uber Mephisto / Diablo / Baal a bare `ret 4`;
  the uber levels' presets matching (201 seeds); the Hellfire Torch when
  the three fall. The Cube's Cow Portal opens; the Pandemonium portals
  return 0 in 1.14d (realm-only), so the keys and organs stay.

- The imps' skills and ride (monster-ai.md "The imps' ride and skills"):
  Teleport (do 129) on and off a siege beast or barricade tower, the ride
  think (special 16) proven in tools/emu, its Fireball and Inferno (srvst
  59 / do 126), Fire Missile (do 132). Left: imp6..8's sequence Inferno.

- The quest leftovers (quests-act5.md, from "The Ancients' deaths" on): an
  Ancient's death missile and its clouds (the client's do 55), the statues'
  return, Baal's gold for a first killer and the save, Baal Corpse Explode
  (it spends the corpses; it does no damage in 1.14d, bugs.md #27), Baal
  Taunt's prgsound, the last portal's save and D2X_OUT (S->C 0x61 {7}, also
  from a host). InitFns 66 / 67 / 69 were already in; 70 is empty.

- Act 5's monster skills (monster-ai.md "Act 5's monster skills"): the
  overseers' Cry Help (its minions sent at the unit) and Whip (Bloodlust, or
  the suicide turn), umod 33's blast, DeathMaul, the putrid defilers' find
  and Impregnate (a painworm at the death), the Ancients' Shout, Whirlwind
  and Leap Attack, imp6..8's sequence Inferno. Siege Beast Stomp does
  nothing in 1.14d (bugs.md #28). Each seen live in headless probes but
  imp6..8's, which is probably unreachable: only a riding imp casts it,
  and their levels have no mounts (stubs.md).
- The act-change cinematics (video.md): Warriv {2}, Meshif {3}, the hellgate
  {4}, Tyrael's portal {5} (LoD: ACT04END then D2X_INTRO), the last portal {7}.

Stubs and dead code found so far: docs/research/re/stubs.md.

Still open for Act 5:

- Live checks: the Cow Portal (devctl can't fill the Cube or set Act 5
  quests yet) and Act 5 against the game.exe host.
- ~~`debug difficulty 1` spawns Normal's rows~~: not a bug (2026-10-25). The
  verb and real Nightmare / Hell saves spawn each difficulty's rows and
  levels; seed 3's region just never picks imp6..8 (seed 8 does), and the
  probe's level-1 character died at once, so nothing found a target.

## Checkpoint 2026-10-22 (2): Sentry and tester fixes; skill tooltips exact

PR #66 (`sentry-fixes`):

- Sentry D2D-3: SDL's base path was freed twice (heap corruption at exit).
- Tester fixes: delete character, stash gold, sockets on worn items, tomes,
  the stamina potion, pad B; the gold box (drop, trade offer, deposit,
  withdraw) with game.exe's spinner rate (`FUN_004bbfb0`).
- Skill tooltips (docs/research/re/skill-tooltips.md): the tree's and the
  skill buttons', every SkillDesc line format SkillDesc uses, checked
  against game.exe's own `FUN_004edea0` in tools/emu
  (`skill_tips.py` vs `test_game --skill-tips`): all lines at levels
  1..20 match on all three difficulties. The calcs gained the descmissile,
  len / rng / pets, sklvl() and miss() operands.
- Item-granted skills (`FUN_0055b800`): oskills (97) and +single skills
  (107, own class) give a skill at 0 points, castable and shown " (item)";
  the item bonus follows `FUN_00644180` (+all skills on every skill,
  own-class oskills capped at 3, +elemental skills 126).
- Flavie's lines traced (record 0x1d's table 0x738778, message = string).
- `coop_d2d` no longer walks Alpha into the town wall (a flake under load).

Still open: missiles' EDmgSymPerCalc in combat (tooltips only so far:
Meteor's fire, Immolation Arrow, Fist of the Heavens bolts miss their
synergies in the fight); tooltip stats in a joined game leave out auras,
states and weapon masteries; item charges as skills.

Next: missile synergies in the fight; then the hero's quest lines.

## Checkpoint 2026-10-22: sounds; the Deck's A on waypoints and portals

Branch `sounds`, after Bret's Steam Deck playtest (no hit or miss sounds,
A did nothing on a waypoint or a town portal):

- The pad: an open waypoint counted as the field (`Town::pad_field`), so A
  never reached its rows; the cursor now starts on its first row. A also
  takes town portals and corpses. `tests/pad_use_d2d.py`.
- Sounds, traced in game.exe's client (UnitSnd.cpp, Skills.cpp) and
  ported (docs/research/re/unit-sounds.md): the weapon's swing and the
  struck unit's impact by hit class; skills' start, do and target sounds;
  missiles' flight (a looping one stops with its missile) and hit; states
  on and off; the hero's own hit and death cries; footsteps by floor
  material; monsters' idle Neutral, Init, Skill1..4 and footsteps; every
  object's mode and looping sounds (the whole 0x7295f8 table,
  `rules/object_sounds.hpp`); town NPCs' greetings with their time of day.
- `tools/sound_coverage.py` lists the base install's sounds d2d can't play:
  3,288 of 3,520 play now (2,181 before).

Still open (the tool's list): some hero quest completions (each quest's
server code sends its id to FUN_00553380); the "find" lines are done
(2026-10-25: the client's level table, FUN_004cc270; nine have no trigger,
stubs.md); their own skill stsound; dosound a / b,
prgsound, block and kick sounds; the quest music jingles; camp floors (DS1
tiles aren't in `room_floors`: always dirt).

Next: the hero's quest lines, act by act; then a real-Deck check.

## Checkpoint 2026-10-19: Coverity scans our code; max mana %

PR #52 (`coverity-mana`), on top of the gamepad (#36):

- Coverity Scan uploads again: the C++23 scan's tarball hit 545M and
  scan.coverity.com answered 413. FFmpeg, Sentry/Crashpad and the
  header-check stubs now build outside cov-build (545M → 470M before the stubs went), so only
  our code is captured. Coverity's 2024.12 parser couldn't deduce
  `for (... : { std::pair{..}, {..} })` and dropped 8 functions
  (`load_scene`, `Crowd::at`, `boss_events`, ...): those lists are named
  and typed now.
- Max life / mana % (item_maxhp_percent 76, item_maxmana_percent 77):
  game.exe's op 11 (FUN_00626200) is raw × (items' % + states' %) / 100,
  raw = own + the items' flat, without the attributes' op 8 share
  (char-panel.md). d2d ignored the items' % (stats ≥ 64 were dropped) and
  took Battle Orders' % of the whole maximum. Miriam: 929 → 1296 mana,
  the host's figure.
- Joined games: the host's maxima (7 / 9 / 11) are the character's own;
  our gear and Battle Orders shares stay on top. Walks past the host's
  reach (snaps from ~45 subtiles) go in 40-subtile legs.

Still open in Acts I–IV (none blocking):

- Nobody has played an act end to end in d2d: the quest tests set up
  each quest with debug verbs. A scripted play-through (Den of Evil →
  Andariel, by play) would give a real gap list.
- Quests keep one player's lists (`ponytail: one player` in quests.hpp);
  they matter once d2d hosts.
- The Council's S1 shot (Act 3): its mode event isn't traced.
- Joined games, live: the netplay fixes above and a Battle Orders cast vs
  d2d's maxima; 0x74 corpses, 0x11 leech, 0x90 party spots, 0x99 skill
  effects are read but not drawn (docs/design/net-join-plan.md).
  Shrine overheads, leaving a cave, the trade gold box's Backspace and
  other units' states haven't been seen live yet.

Next: those live checks with the host, or Act 5.

## Checkpoint 2026-10-17: Act 4 done, v0.4.0

PR #32 (`act4`, merged) and `act4-tidy`: Act 4 plays from the hellgate to
Diablo's death and the ending (classic: the game ends; LoD: Tyrael's
portal, Act 5 not built). Since v0.3.0 also: opt-in crash reports
(Sentry, apps/README.md), the Act 3 quest-state crash fixed, the
Expansion Character box. The tidy-up:

- Monsters think on while the player is dead (the dead player isn't a
  target: FUN_005dd7f0); before, every monster froze mid-mode.
- The player's states the thinks read: a monster's curse and the finger
  mage's spider mark (hit 19, FUN_005ab110: fingermagecurse, 0x54).
- The knights fire undeadmissile1..4 by their variant (FUN_005cdfb0);
  monster Bone Spirit homes (missile do 7, FUN_005ae780).
- Nightmare and Hell populate through objgroups.cpp's port: the mazes'
  Rooms(N) / Rooms(H), the unique pick off the region, superunique extras.
  `DIFFICULTY=0..2 ACT=1..4 sweep.sh 1-10 monsters objgroups`: every level
  10/10 on every difficulty.

Fidelity pass (2026-10-17): the seals' flash (object 131) at
FUN_00545340's free spot and the boss at its event 7, 27 frames on; the
Hellforge's gems a round each 20 frames on the quest seed; DiabRun's
velocity (calc1 × velocitypercent) and its hit on the event-1 row only;
Diablo's target lists (pets, list 9's fallback); the quest log's +0xb
moves traced (quests-act4.md "The quest log"); the 75 s step is a realm
save (nothing in single player); Diablo comes wherever the player is;
devctl `debug difficulty` rebuilds the levels.

Next: Act 5.

## Checkpoint 2026-10-14 (later): housekeeping, fast act checks

Fast checks, one act at a time or all three side by side:

- `ctest --test-dir build -R quests_act -j3`: `tests/quests_d2d.py <d2d>
  <act>` plays each act's quests from a new character (Act 2's six are new:
  Jerhyn to Meshif's boat, ~70 s; all three acts ~1:45).
- `ACT=2 tools/emu/sweep.sh <seeds> <kinds>` (ACT is 1, 2 or 3): every
  level of the act against game.exe (tiles, units, monsters: ~15 s for 2
  seeds).

Planned: a play bot (`tests/play_d2d.py`), a headless player that plays
by clicks alone (`cmd move` / `cmd skill` / `cmd interact`, routes on
`debug grid 5`, no `debug goto` / `kill`): spends stat and skill points,
buys potions, goes back to town to heal, and plays an act's quests in
order. First a gap finder (what breaks in real play), later speed runs
(how fast an act goes, run against run). A scratch probe (2026-10-14) got
from the camp through the Blood Moor and over the whole Den of Evil, then
died at level 3 to Corpsefire with no points spent: no d2d bug on the way.

What they found, all fixed on the same branch:

- Act 2's monster population had never been diffed (only rooms, tiles,
  units). The Canyon of the Magi (46): a Position level's entrances take
  its own type 0xb tile, not its waypoint's too. Maggot Lair 3 (64):
  Coldworm, a superunique and an act boss, was counted as two uniques.
  Duriel's Lair (73): the oracle left out Flavie only, d2d every MonStats
  npc (Tyrael); now both do.
- The whole-game diff (`diff_drlg.py game`) on every level: the tombs
  (55–60, 66–72) took their doors from the first list-order build;
  `relevel` makes the units again in the rooms' order. The Arcane
  Sanctuary's six tomb symbols (DS1 582, FUN_0059d720) all came out 307;
  now the six that aren't the true tomb's. The Jail's bookshelves: the
  oracle read the book's code off its own stand-in, not the unit.
- Coldworm the Burrower (SandMaggotQueen, MonAI 66, search type 0) is
  traced: S1 where she stands, a sand maggot rising 8 subtiles east, a
  rest, up to aip1 (`mon_ais.py`, 50 000 runs). Every acts 1–3 MonStats
  AI is now game.exe's.
- Missiles of Size 2 and 3 strike by FUN_00641cb0's full table (the
  unravelers', Sand Leapers', Zakarum priests', the wisps'); a blank Size
  strikes no unit.
- Travincal's `objgroups` diff crashed in the emulator (a quest flag
  read); now quest-stubbed as `monsters` was.

## Checkpoint 2026-10-14: Act 3 done, what's still open

PR #23 (`act3-kurast`): Kurast, Travincal and the mazes down to the
Durance (75–102) are game.exe's (`diff_drlg.py` rooms, tiles, units,
monsters; the whole act as one game), all six quests play
(`tests/quests_d2d.py`), and Act 3's MonAI thinks match `mon_ais.py`.
Known gaps, none blocking:

- Act 4: the hellgate opens and says it isn't built.
- The Council's S1 shot fires MissS1 with S1's damage and to-hit; the
  mode's event isn't traced in game.exe (the A1 / A2 shots' pattern).
- Waiting on a live host: the joined game's checklist, now Act 3 too.

Next: Act 4, the same road (levels diffed, the town, the quests, the thinks).

## Checkpoint 2026-10-13: Act 3's docks and jungle done

PR #22 (merged): Meshif's boat lands in a Kurast Docks that's game.exe's
(rooms, tiles, units, NPC thinks, trade, repair, hire, gamble), and the
Spider Forest, Great Marsh and Flayer Jungle are game.exe's down to
every monster's spot (`diff_drlg.py 76|77|78 monsters`, `LEVELS=75,76,77,78
game`). The collision fix (shadows block) holds for every outdoor level
of acts 1–3.

Open, by the road above:

- Step 1.4: Kurast (79–83): FUN_0067f190's bazaar / upper Kurast /
  causeway presets, FUN_0067f3b0's Travincal.
- Step 1.5: the dungeons, sewers, temples and the Durance (84–102):
  act 3's maze types (FUN_00673b30) and presets.
- The Dark Wanderer stands (a3q7's walk, its quest record's think).
- Act 3's quests, MonAI thinks (rows 51, 52, 61, 65 …) and Mephisto's
  hooks (FUN_005b1cf0): step 2 and 3 of the road.
- A store opens on its first tab even when that one's empty (the World
  picks the first full tab; the app shows tab 0): all acts.

## Checkpoint 2026-10-12: Act 2 done, what's still open

Act 2 plays from Lut Gholein to Duriel and Meshif's boat. Known gaps, none
blocking:

- Monster other thinks of Acts 3–5 (MonAI rows 51, 52, 61, 65, 128, 135,
  140, 145, 147, 152): `rules::other_think` has Act 2's two.
- Act bosses of Acts 3–5 (FUN_005b1cf0 by BaseId: Mephisto, Diablo, Izual,
  Baal, the Council's quest hooks): not in `rules::act_boss` yet.
- Waiting on a live host: the joined game's checklist (quests, travel,
  other units' states).

Next: Act 3, the same road as Act 2's (levels on demand and diffed, the
town, the quests, the thinks).

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
7. The rest of Act 1's caves, crypts and presets.
8. Skill fidelity: pet AI, the shapeshifted look, the approximations
   listed in skills.md.

Town leftovers (Warriv's "go east", waypoint travel, merc resurrect, the
hire list widget, Cain's spot, trade leftovers, charge recharging) are
in the checkpoint above.

## Act 4 prep (2026-10-14, branch `act4-prep`)

Preparation only: Act 4 isn't built or playable. The act boundaries now
come from one table, `level_ids::kActTowns` (the five towns; each act's
levels follow its town's Id). `act_of`, `is_town`, `GameData::town_of`,
`layout_of`, the save's start town (`World`), the act in objgroups
(FUN_006427f0) and its town-room flag (FUN_006426a0) read it. Before, the
last two chains and `town_of` gave Kurast for any level past 74.

Already generic, nothing to do: DS1 units map through MonPreset.txt for
all five acts (`drlg::ds1_units`, Act 5's monster objects too); monster
stats and level monster pools come from the tables. Still gated:
`kBuiltLevels` (Act 4 isn't in it), `layout_of` (no Act 4 layout), the
outdoor dispatch in `build_outdoor` (Act 3's jungle its own generator).
Act 4 work: its layout (not traced yet), its levels in `kBuiltLevels`, the town,
its quests and Diablo.

Each act's quests now sit in their own file, `world_quests_actN.cpp`, at
ten World hooks (`actN_log`, `_join`, `_enter`, `_talk`, `_talk_closed`,
`_said`, `_tick`, `_alert`, `_kill`, `_update`; world.hpp lists them). An
act needs only the hooks it uses; Act 4 adds `world_quests_act4.cpp` and a
call at each. Within an act the order is as before; across acts it now
runs act by act (each quest reads and writes its own records). Still in
world.cpp: the quest objects' operate hooks (the `operate_fn` cases) and
a few level-arrival bits (Tyrael gone, camp Cain, the cairn stones).
The tools take every act too: `ACT=1..5 tools/emu/sweep.sh` (ranges
from `drlg.TOWNS`).
A new act's monster AIs: append them to `AIS` in `tools/emu/mon_ais.py`
and re-run its `--dump`; it writes the cases and `tests/mon_ais_names.inc`
(the table test_monsters reads). Its quest playthrough: the act's number
in `tests/CMakeLists.txt` and an `actN()` in `tests/quests_d2d.py`.
