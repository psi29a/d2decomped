# D2Decomp — Plan

Reverse-engineer Diablo II (2000, LoD 2001) into a cross-platform, modern
C++26 engine. Ghidra drives decompilation; reference implementations
(OpenD2, OpenDiablo2, AbyssEngine) guide file-format work.

## Non-goals

- Ship a game with Blizzard assets. Users bring their own ISOs.
- Multiplayer server (Battle.net). Out of scope until singleplayer runs.
- Pixel-perfect renderer. Match behavior, not implementation.

## Phases

Status as of 2026-09-26.

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
   Blood Moor. Left: the other Act 1 levels, waypoint travel, saving.
6. **Combat + AI**: actor state machine, packet-equivalent events,
   monster AI from game.exe. *Implemented:* monsters and their fights,
   gear in combat, drops, experience, the merc, and the skills (phases 0–6
   in `docs/research/re/skills.md`). Left: the per-type AI think
   functions, champions and uniques, the gaps each skill section lists.
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

Game rules stay testable and free of assets and screens; the app wires
them to input, units and drawing.

| Area | Rules (`components/rules`, tested) | App (`apps/d2d`) |
|---|---|---|
| Items, stores, gambling, hiring, potions | `rules.hpp` (`test_rules`) | `items.hpp`, `store.hpp`, `cursor.hpp`, `panels.hpp` |
| Monsters: tables, spawning, stats | `monsters.hpp` (`test_monsters`) | `load.hpp` (`load_monsters`), `ai.hpp` (units, AI, missiles) |
| Combat: Fighter, blows, speed, experience | `combat.hpp` (`test_combat`) | `fight.hpp` (`Fight`) |
| Drops: treasure classes, quality | `drops.hpp` (`test_drops`) | `loot.hpp` (`Loot`) |
| Skills: records, calc VM, levels, mana | `skills.hpp` (`test_skills`) | `skillbar.hpp` (HUD buttons, picker, hotkeys); skill use in `fight.hpp` |
| Level layout, outdoor generator | `components/drlg` (`test_drlg`, `test_outdoor`) | `load.hpp` (`load_wilderness`) |
| Town: input, panels, NPCs, walking, levels | — | `town.hpp` (`Town`) |
| Scripted-test verbs | — | `devctl_verbs.hpp` (+ info/screenshot/quit in `main.cpp`) |
| World sounds | — | `audio.hpp` (`Cues`) |

`Town` owns the shared state (scene, level, character, player, merc, rng)
and hands references to its subsystems (`Fight`, `Loot`); a new system
follows the same pattern rather than growing `town.hpp` or `main.cpp`.

## Toolchain

- C++26 (`-std=c++2c` / `/std:c++latest`), CMake ≥ 3.28, Ninja.
- System packages: SDL3 (window, input, WAV decode), OpenAL (openal-soft),
  FFmpeg (Bink cinematics), zlib, Qt6 ≥ 6.5 (launcher only).
- `FetchContent`: StormLib (MPQ), CLI11.
- License: GPL-3.0-or-later (matches OpenD2). Compatible with StormLib
  (MIT), SDL3 (Zlib), openal-soft (LGPL), FFmpeg (LGPL), Qt6 (LGPL).

## Reference projects (in `../`)

- **OpenD2** — C, closest to original engine layout. Best for file-format
  code (DCC, DC6, DS1, DT1, COF). Reads like a decompilation.
- **OpenDiablo2** — Go. Best for high-level game logic and asset docs.
- **AbyssEngine** — C, successor to OpenDiablo2 by same author. Sabbat-era
  design; uses Lua for game code.

Inspiration only. We do not copy code — we read, understand, cite in
`docs/research/`, and write our own.

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
combat and skills). Next: more of Act 1 (Cold Plains, the Den of Evil).

## Open questions

- Launch page: the plan was for `d2d-launcher` to grow one; today it's two
  binaries (`d2d-launcher` installs, `d2d` runs). Keep it that way unless
  a launch page earns its place.
- Save format: keep 1.14d-compatible or greenfield? Compatible unless it
  hurts.
- Networking: leave off until singleplayer runs.

## Known issues / to investigate

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
   Gaps: preset units and stamp objects (monsters, shrines, waypoints),
   CheckAll stamps, cliff caves / cliff styles / waypoints (other act 1
   levels); the app still draws first-match tiles, not the proven picks.
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

- **Networking**: game.exe's client/server packet tables and handlers
  (single player runs both in one process). Routes and the codebase impact
  in `docs/design/multiplayer.md`.
- **The simulation loop**: the server's 25 Hz tick, the client's frame and
  interpolation, event timers.
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
- Champions and uniques (Nightmare/Hell Blood Moor: MonUMin/Max 4-9):
  FUN_005a43e0 -> FUN_005a09e0 / FUN_005a2120, mod functions at 0x73c008,
  names.
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
  the game seed is the map seed; the +0x20 seed for group counts.

Act 1 levels and rendering
- Built: Rogue Encampment, Blood Moor. Not built: Cold Plains, Stony Field,
  Dark Wood, Black Marsh, Tamoe Highland, the other caves/crypts,
  Tristram, Monastery through the Catacombs.
- Built and proven against game.exe (layout and tiles): the Blood Moor
  and the Den of Evil; click the cave mouth to go in, the stairs to come
  back. Preset units (proven too) put in the shrines, torches, chests,
  Flavie and Corpsefire with his minions. Missing: superunique mods and
  stat bonuses, shrines / chests that do anything; warp arrival spots
  are a guess (LvlWarp ExitWalk).
- Rendering: shadows not blended, no unit shadows, no lighting / day-night
  / rain, cell-granular wall sorting, no item colour tints on composites,
  the neighbour level's NPCs not drawn across the edge.

Town
- Warriv's "go east", waypoint travel, the hire list's widget and offer
  count, Cain's spot in an already-rescued game, trade leftovers (magic
  stock, the real stock roll), saving .d2s.

## Next up (as of 2026-09-26)

1. DRLG: done and proven for the Blood Moor and the Den of Evil (layout,
   room seeds, tiles; docs/research/re/drlg.md "Checking against
   game.exe"), drawn and walkable in d2d, the Den entered by its warp.
   Preset units too (Corpsefire, shrines, Flavie). Champions and uniques
   roll in nightmare / hell (docs/research/re/monsters.md). Left: their
   behaviour mods and names, operable shrines and chests, then Cold Plains.
2. Champions and uniques (FUN_005a43e0).
3. Saving .d2s.
4. Cold Plains and the rest of Act 1's outdoor levels.
5. Skill fidelity: pet AI, the shapeshifted look, the approximations
   listed in skills.md.

Town leftovers (Warriv's "go east", waypoint travel, merc resurrect, the
hire list widget, Cain's spot, trade leftovers, charge recharging) are
in the checkpoint above.
