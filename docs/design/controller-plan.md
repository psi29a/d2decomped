# Controller support — Steam Deck first, Android ready, Switch later

Status: the first cut is in (PR #36, 2026-10-19): SDL3 gamepads with one
default layout by button position (`apps/d2d/gamepad.*`, `gamepad_map.hpp`;
the layout is in [apps/d2d/README.md](../../apps/d2d/README.md)), stick
walking by direction (run follows the run toggle), soft targeting in the field, menus by cursor
and d-pad; panels hop the cursor item to item on the d-pad, and a store
trades on X with the shoulders for tabs; the right stick's click opens the
skill picker, and the left trigger swaps weapons. Since #38: the right
stick aims in the field and what A would hit or use is hovered with an
"A  Talk" prompt; the d-pad hops through every panel; a menu's shoulders
step its buttons; buttons are named per pad family; every action is
rebindable (`pad::Bindings`, d2d.cfg `pad_*`) on Configure Controls'
gamepad page, which also ports game.exe's keyboard config
(docs/research/re/controls.md). Not yet: Steam Input, touch.
The plan below was written 2026-10-15, before any controller code.

## Goal

Make d2d comfortable to play from a couch on Steam Deck, while keeping the
input design usable on Android devices with a controller and portable to a
future Nintendo Switch build. Preserve mouse and keyboard play on desktop.

The goal is a shared set of **game actions**—move, primary action, secondary
skill, interact, select target, open panel—fed by platform input adapters.
Steam Input layouts can help prototype bindings, but Steam-specific bindings
must not become the only place where gameplay behavior lives.

## Preliminary review: useful starting points

These are references and APIs to study, not code to copy wholesale.

### DevilutionX: gameplay and UX reference

[DevilutionX](https://github.com/diasurgical/devilutionX) is a modern Diablo I
port, not a Diablo II engine and not a drop-in dependency. Its
[controller scheme](https://github.com/diasurgical/devilutionX/wiki/Game-Controller-Scheme)
is useful because it demonstrates the same kinds of decisions we need to make:
direct movement on the left stick, contextual face-button actions, a separate
spell action, menu controls, belt shortcuts, and a right stick that can act as
a mouse. Its source also has configurable pad actions and dead-zone options.

Use it to compare interaction flows and identify failure cases (inventory,
shops, spell selection, Android controls). Do not transplant code or assets
without checking license compatibility: DevilutionX's repository currently
uses its own non-commercial Sustainable Use License.

### SDL3 Gamepad API: cross-platform input foundation

d2d already uses SDL3 for video, mouse, keyboard, and audio. It currently
initializes only `SDL_INIT_VIDEO` for the windowed path and has no gamepad
handling. SDL3 provides normalized gamepad buttons, axes, device labels,
device-added/removed events, and mapping support. Use this layer for physical
controllers on Steam Deck, desktop Linux, and Android; keep SDL types out of
the game simulation and command model.

References:

- [SDL3 gamepad API](https://wiki.libsdl.org/SDL3/CategoryGamepad)
- [SDL3 initialization flags](https://wiki.libsdl.org/SDL3/SDL_InitFlags)
- [SDL3 Android project setup](https://wiki.libsdl.org/SDL3/README-android)
- [SDL3 Nintendo Switch port notes](https://wiki.libsdl.org/SDL3/README-switch)

SDL's Switch page says the SDL3 Switch port is separate and only available to
developers under the appropriate Nintendo NDA. Treat Switch as a later
platform gate requiring Nintendo development access, not as a routine Linux
build target.

### Steam Input: rapid Deck layout experiments

Steam Input's **legacy mode** can map Deck controls to keyboard and mouse
without the Steam Input API. It is useful for trying an initial layout and
for players who want to customize controls. It can also provide mouse regions,
radial menus, and stick outer-ring bindings. These bindings produce emulated
low-level inputs; d2d does not receive semantic actions or know which physical
button produced them.

Steam Input's **native mode** exposes named actions through Steamworks and can
show controller-specific glyphs. Keep it optional and out of the core input
design: SDL3 is the common path for Android and a future console port.

References:

- [Steam Input concepts: legacy and native modes](https://partner.steamgames.com/doc/features/steam_controller/concepts)
- [Steam Input source modes](https://partner.steamgames.com/doc/features/steam_controller/input_source_modes)
- [Steam Input action manifests](https://partner.steamgames.com/doc/features/steam_controller/action_manifest_file)

### Current d2d input and command path

- `apps/d2d/window.cpp` converts SDL mouse, keyboard, and text events into a
  `Mouse` snapshot and a list of pressed keys. The main window initializes
  video only.
- `Town::input` converts the cursor's 800×600 screen position to an
  isometric world point. A left press attacks a hovered monster, picks up a
  hovered item, interacts with a hovered NPC/object, or moves to the clicked
  point. Holding left mouse repeats the attack or re-aims movement. Right
  mouse uses the currently selected right-hand skill.
- `R` toggles running. The game command model already has `Move`, `UseSkill`,
  `Pickup`, `Interact`, and `Run`, which are useful endpoints for a controller
  action layer.
- `Town::update` currently owns a large part of in-game UI interaction. A
  controller implementation must account for inventory, skills, belt, store,
  dialogs, trade, quest log, automap, and game menus, not only combat.

Relevant code: [window event translation](../../apps/d2d/window.cpp),
[Town input and actions](../../apps/d2d/town.cpp),
[game commands](../../components/game/protocol.hpp).

## Steam Deck installation and update path

Controller support alone does not make the Deck experience easy. The release
needs a discoverable install, a first-run path for the user's game data, a
Gaming Mode launch entry, and a reliable update path. Keep the legal
bring-your-own-game-data rule: no Diablo II ISOs, MPQs, or Blizzard binaries
go into a d2d package.

The repository already makes a Linux `.tar.gz` containing the launcher, game,
and required libraries. The Qt launcher can import from the four CD ISOs and
detect selected existing installs. This is a useful preview channel, but a
Deck owner must currently extract the archive, start the launcher, and add a
Steam shortcut. Valve's [Deck instructions](https://help.steampowered.com/en/faqs/view/671A-4453-E8D2-323C/)
describe adding installed desktop apps to Gaming Mode, while the [non-Steam
game instructions](https://help.steampowered.com/en/faqs/view/4B8B-9697-2338-40EC)
clarify that this adds a library shortcut and does not deliver updates through
Steam.

Do not assume the existing Linux archive runs on SteamOS just because both
are Linux. The package bundles many libraries but deliberately relies on host
glibc and graphics/audio system libraries; its CI build environment and ABI
must be checked against SteamOS before advertising it for Deck.

| Route | What the player does | Effort and tradeoffs |
|---|---|---|
| Current Linux archive, improved for Deck | Download/extract in Desktop Mode, run `d2d-launcher`, import/select their own game data, add the launcher to Steam as a non-Steam game | Fastest route for testing and early releases. Needs a Deck quick-start, desktop entry/icon, and a well-tested launcher. The Steam shortcut is manual and updates come from project releases. |
| Flatpak through Discover/Flathub | Install in Desktop Mode, run the launcher, grant access to a selected install folder or ISO through a file chooser, then add the exported app to Steam | Best open-source app-store candidate: discoverable installation and normal Flatpak updates. Sandbox rules mean broad host-folder scanning cannot be assumed; data import must work with portal-granted access and saves need a predictable app-data location. Flathub requires source-built packages, minimal permissions, and maintainers to own updates; acceptance is not automatic. |
| AppImage | Download one file, mark executable/run it, then add it to Steam | A useful optional direct-download artifact for Linux users who do not use Flathub. It is not an install/update store; FUSE and host ABI compatibility need testing, including on Deck. Do this only if the project can maintain a second Linux release path. |
| Steam store | Install from the Steam library in Gaming Mode | Lowest-friction install and update experience. Requires Steamworks onboarding, a store/release operation, legal and branding review, and the [Steam Direct fee](https://partner.steamgames.com/doc/gettingstarted/appfee) for a new app. It still needs an explicit flow for the player's separately owned game data. Treat as a later product decision, not a prerequisite for Deck support. |

**Recommended sequence:** keep the existing archive as the engineering preview
path and make it Deck-friendly first; in parallel, do a time-boxed Flatpak
feasibility spike. If the launcher can import from user-selected sources
without broad filesystem access and the project can commit to package
maintenance, prefer Discover/Flathub for a polished community release. Consider
AppImage later only if a direct-download audience justifies another release
and update path. Revisit a Steam store release only with a sustained
release/support plan. See the [Linux packaging plan](linux-packaging-plan.md)
for the build, sandbox, Flathub, and AppImage work packages.
For either non-Steam route, document that Steam's shortcut must be added once
from Desktop Mode and that it does not install updates.

The current launcher mostly solves the user-data question already: it can
import from original media and configure a data directory. Audit it on Deck
for file-picker behavior, Steam/Proton and external-drive paths, full-screen
launch/return, persistent saves, and operation without a keyboard/mouse. A
sandboxed build must use an explicit picker or portal-granted access rather
than silently scanning all host directories. Never tell users to disable
SteamOS read-only protections or install system packages with `sudo`.

Flathub's current policy requires maintainers to follow its AI and submission
rules; check that policy at the time of packaging. In particular, its current
submission guidance disallows AI-generated manifests and AI-agent submission
pull requests, so a project maintainer must own those files and interactions.
This policy applies to the packaging workflow, not to ordinary research or
planning. See [Flathub requirements](https://docs.flathub.org/docs/for-app-authors/requirements)
and [submission guidance](https://docs.flathub.org/docs/for-app-authors/submission).

## Proposed input model

Keep three layers distinct:

1. **Device input** — SDL gamepad buttons/axes, mouse, keyboard, and later
   Android touch controls. Steam Input may emulate one of these on Deck.
2. **Mapped actions** — named, remappable actions with pressed/held/released
   state and analog values. Examples: `move`, `aim`, `primary`, `secondary`,
   `interact`, `target_next`, `inventory`, `belt_1`.
3. **Screen/game behavior** — menu navigation, targeting, and conversion into
   existing game commands. The game simulation should not depend on SDL,
   Steamworks, Android, or Nintendo types.

Bindings need contexts (front end, gameplay, inventory/store/dialog, text
entry). Keep simultaneous keyboard/mouse and gamepad input working. Use SDL's
positional gamepad buttons internally and obtain physical labels for prompts;
allow accept/cancel swapping and user remapping because button labels and
locations vary by controller and region.

## Candidate controller layout to prototype

Treat this as a starting profile, not a locked design. Make every action
remappable.

| Input | Candidate action |
|---|---|
| Left stick | Directional movement; walk/run follows the run toggle (left-stick click) |
| A / south face button | Primary action: attack with active left skill, otherwise contextual move/interact/pickup |
| B / east face button | Cast active right-hand skill; cancel/back in menus |
| Right stick | Aim/soft-target adjustment in the world; cursor movement in pointer-heavy panels |
| Deck right trackpad | Precise mouse-style cursor for inventory, stores, and legacy UI |
| L/R shoulder | Cycle/select skills or skill sets; prototype against a radial skill selector |
| D-pad | Belt slots and menu navigation |
| Start / View | Game menu, character, inventory, map, and quest panels by context |

The face-button mapping should follow the physical labels shown to the player;
“A” is a useful Deck default, not a universal internal button name. The active
left/right skill model should remain understandable for mouse players too.

## Console-feel behavior to investigate

### Directional movement and walk/run

The current model sends a point destination and has a separate run toggle. A
left-stick implementation needs to turn a direction vector into a stable
world-space destination or movement intent, then keep updating it while the
stick is held. Use a configurable dead zone. Decide whether
stamina exhaustion overrides the requested run state, and expose that state to
the HUD.

Do not try to express continuous walking through repeated `R` key toggles.
Tried and dropped: running by stick magnitude. Every push started as a walk
until the stick reached the run threshold (about 80% of its travel), and it
overrode an always-run toggle, which made movement feel late. The stick now
gives direction only; walk/run is the run toggle (left-stick click), as with
the mouse.

### Soft targeting and contextual primary action

Prototype a **soft target resolver** that runs when a gamepad action needs a
target. It should not change monster AI or combat rules; it chooses which
existing unit/item/object the player's action refers to.

Questions for the research spike:

- What is the authoritative player facing direction after mouse movement,
  click-to-move, melee attacks, ranged skills, and knockback? Should controller
  aim use the left stick, right stick, last movement direction, or a priority
  among them?
- Which skills target a unit, a ground point, a direction, or self? Can one
  selected target sensibly serve both primary and secondary actions?
- What distance and line-of-sight checks are already represented in the
  simulation, and which should the controller resolver add?
- How should the same selection map to commands when joining an original
  game.exe host, where the host owns authoritative units and attacks?

Initial resolver rules to test:

1. Build candidates from living hostile units in the current area and within
   the action's valid reach. Interactables and ground items use separate
   candidate rules.
2. Prefer candidates close to the aim/facing direction; use distance as a
   secondary score. Keep the current target sticky until it dies, leaves range,
   or the aim moves clearly toward another candidate.
3. If no valid hostile is in the facing cone/range, primary uses the normal
   directional move or contextual interact behavior. Do not silently attack
   behind the player.
4. Let the player cycle targets and provide an on-screen target cue. Add
   settings for assist strength or off; enable a sensible default only for
   gamepad input so desktop mouse precision is preserved.
5. Do not auto-pick up arbitrary loot during an attack. Use an explicit
   interact/pickup action or a configurable hold/modifier, with item labels
   and a way to cycle nearby items.

The cone angle, range, target-stickiness timing, and action priorities are
open tuning decisions. Prototype with varied scenes rather than baking
unvalidated constants into the first implementation.

### Other high-value QoL

- Contextual interact with nearby NPCs, doors, chests, shrines, and portals;
  show the selected target name before activation.
- Radial skill selection plus quick-cast; keep skill assignment, selection,
  and casting distinct so a button does not unexpectedly change meaning.
- Direct belt-slot shortcuts and hold-to-repeat behavior where appropriate.
- Controller navigation in every menu and panel; avoid requiring cursor
  precision for common inventory moves. Keep a cursor/trackpad path for exact
  grid placement and item comparison.
- Clear focus/highlight, target reticle, and button glyphs; support pause,
  reconnect/hotplug, configurable dead zones, sensitivity, and button swap.
- Test text fields and Steam/Android on-screen keyboards for character names,
  chat, trade gold, and join addresses.
- Consider rumble for hits and important events only after basic input and
  settings are solid; make it configurable.

## Agent-sized work packages

The first four are research-only and can proceed independently. Each should
return a short note with findings, open questions, file/API links, and a
recommended next step; do not edit gameplay code during this phase.

### R0 — DevilutionX controller UX audit

Trace the current controller scheme and relevant source for movement,
contextual actions, spell selection, inventory, target selection, remapping,
dead zones, and Android/touch behavior. Compare its choices against the
candidate layout above. Return a d2d-specific interaction recommendation;
do not port code or assets.

### R1 — SDL3 and platform API audit

Check the SDL3 version used by this repo and document exact APIs/events for
gamepad initialization, hotplug, axes/buttons, labels, mappings, and Android
build integration. Separately summarize SDL's Switch port availability and
its Nintendo NDA gate. Recommend the smallest input adapter that keeps the
game layer platform-neutral.

### R2 — Steam Input prototype plan

Specify a Deck legacy-mode layout for today's mouse/keyboard interface and a
future gamepad-native layout. Explain what can be configured without
Steamworks, which behaviors require game code, how radial/trackpad/outer-ring
features help, and where keyboard/mouse plus native gamepad inputs can
conflict. Do not make Steam Input the Android/Switch dependency.

### R3 — D2 targeting and command audit

Trace how player position/facing, hovered units, skill target types, melee
range, interact range, ground items, and run/stamina are represented. Review
single-player and original-host join paths. Propose target scoring, target
stability, command conversion, and a set of reproducible scenarios for later
verification. Use the repo's reverse-engineering workflow only for questions
that need confirmation against game.exe; mark unverified assumptions.

### D0 — Steam Deck distribution decision

Compare the existing Linux archive, a Flatpak/Discover release, AppImage, and
a Steam store release against the user's install/update journey, project
maintenance, build dependencies, game-data licensing, save persistence, and
Steam Gaming Mode launch. Confirm the current Steam Deck non-Steam shortcut
flow and Flathub's current requirements. Return a recommendation and a
minimal Deck setup journey; do not create a Flathub manifest or submission.

### D1 — Deck-ready archive and first-run journey

On a real Deck, install the current Linux package using a clean user account.
Check its glibc and host-library requirements against SteamOS; adjust the
release build environment if needed. Write the shortest verified Desktop Mode
→ Gaming Mode instructions; add a desktop entry/icon or installer support if
it makes the app show in Steam's non-Steam picker. Verify launcher start,
game-data import or selection, correct data/save paths, controller launch,
close/return behavior, and updating from a later release. Never require
disabling SteamOS protections.

### D2 — Sandboxed package feasibility

After D0 recommends a Flatpak prototype, validate a local source-built package
with the launcher's ISO/folder selection and the game's file access. Determine
whether Qt's file chooser uses the portal on SteamOS, what permissions are
actually needed, where saves/config live, how SDL/OpenAL/FFmpeg/StormLib and
Qt are supplied, and how the installed app is added to Steam. Record a go/no-go
and maintenance estimate. A project maintainer must author and submit any
Flathub manifest under the policy current at submission time.

### I1 — Shared action and mapping design

After R0–R3, define the action enum/state, screen contexts, defaults, remapping
format, physical button labels, and settings. Keep SDL types in the app layer
and map the resulting actions to existing `game::Command` values. Deliver a
small design note and API sketch before implementation.

### I2 — SDL3 gamepad adapter and Deck baseline

Initialize SDL gamepad input, support hotplug, normalize axes/buttons, and
preserve keyboard/mouse input. Add front-end/menu navigation and a basic
Deck layout. Validate launch, focus, suspend/resume, cursor behavior, and
button prompts on actual Deck hardware. Keep Steam Input API integration out
of this milestone; use Steam Input legacy mappings only as a fallback or
prototype.

### I3 — Gameplay movement, actions, and soft targeting

Implement left-stick movement, walk/run threshold, target selection, primary
and secondary actions, target cycling, and contextual interaction. Translate
results through existing commands. Preserve click-to-move and exact mouse
targeting. Verify local play and the supported original-host join path.

### I4 — Controller-first panels and QoL

Add radial skill selection/quick-cast, belt actions, focus navigation,
controller-friendly inventory/store/dialog flows, target and item cues,
settings, prompts, and text input. Remove mouse-only blockers from the most
common tasks, while retaining pointer precision for grid placement.

### A1 — Android platform spike (can start during I1/I2)

Build a minimal SDL3 Android shell using the documented activity/Gradle/NDK
path; audit native dependencies, file access, save locations, screen sizes,
and application lifecycle. Decide how users select their own Diablo II data
without bundling it and without assuming desktop filesystem paths. Confirm
physical controller input reaches the shared action layer. Keep the Deck
gameplay action code shared; implement touch controls as another adapter,
not a second gameplay implementation.

### A2 — Android package and touch adapter

Once A1's dependency/data decisions are proven, produce an installable test
build, controller support, virtual controls, touch-friendly settings, and
rotation/background/resume handling. Verify licensed user data remains
external to the app package.

### S1 — Switch feasibility gate

Before platform code, confirm project access to the Nintendo developer
program, required NDAs/devkit, and SDL3 Switch port. Estimate dependency,
asset storage, performance, suspend/resume, controller glyph, and certification
work. Stop at a written feasibility decision until access exists. Reuse the
shared action layer only after a legal build environment is available.

## Suggested order and dependencies

```text
D0 distribution decision ──> D1 Deck install flow ─┐
                       └────> D2 Flatpak spike*   │
R0 DevilutionX UX ─┐
R1 SDL/platform ───┼──> I1 shared actions ──> I2 Deck input ──> I3 gameplay/targeting ──> I4 full QoL
R2 Steam Input ────┤                          │
R3 D2 targeting ───┘                          └──────────────> A1 Android spike ──> A2 Android build
                                                                                  S1 Switch feasibility is gated on Nintendo access
```

`*` Start D2 only if the D0 recommendation supports Flatpak.

R0–R3, D0, and the Android dependency audit can run in parallel. I1 is the
architecture gate: avoid parallel feature work until the action/context
contract is agreed. I2 and the non-gameplay parts of A1 can then proceed in
parallel. S1 is an external-access decision, not a coding milestone.
Run D1 alongside Deck input work; start D2 only if D0 supports pursuing
Flatpak. Do not make any one distribution channel block controller
development.

## Acceptance gates

### Deck baseline

- d2d launches from Steam in Gaming Mode using the Linux build and the user's
  own game data.
- The title flow, character select/create, gameplay, core panels, and exit
  path are usable without a physical keyboard or external mouse.
- Keyboard/mouse still works when connected; switching between it and a
  controller does not duplicate clicks or lose focus.
- Controls are remappable and button prompts reflect the active controller.
- A new Deck owner can follow a verified install guide, select/import their
  own game data without a shell command, add the game to the Steam library,
  launch it in Gaming Mode, and find the documented update path.
- The release bundle contains only d2d/project files and dependencies; it
  never includes Diablo II data or Blizzard binaries.

### Gameplay feel

- Left-stick direction reliably moves the character; dead-zone and
  walk/run threshold are adjustable and stable around the threshold.
- Primary attacks the intended facing target within configured reach without
  cursor placement. Secondary casts at that target or the correct aim point.
- Target selection is predictable with several enemies, enemies behind the
  player, doors/NPCs/items in the same area, and moving targets; the user can
  change target and disable assist.
- Existing mouse selection, pickup, interact, skill assignment, stamina, and
  save behavior remain correct.

### Portability

- The normalized action logic has no Steamworks, Android Java, or Nintendo
  types in the game/simulation layer.
- An Android test build can use a controller through the same actions and
  touch controls through a separate adapter; data import and saves work within
  Android's storage model.
- Switch is only called supported after building/running with approved access
  and checking the relevant platform requirements.

## Open decisions

- Does controller aim follow left stick, right stick, last movement direction,
  or a configurable combination?
- Should target assist default on only for gamepad, or be an explicit global
  gameplay option?
- How much target lock is appropriate for melee versus ranged/ground-target
  skills? Should a failed target automatically fall back to a directional
  ground aim?
- Which actions deserve dedicated buttons versus modifier/radial combinations?
- Should controller bindings be stored in `d2d.cfg`, a separate controls file,
  or both (global defaults plus per-device profiles)?
- What is the Android data import flow, and can existing MPQ readers operate
  efficiently over the chosen private-copy or document-provider model?
- Is Steam Input's native API worth maintaining in addition to SDL3 for a
  future Steam distribution, or is SDL plus legacy mapping sufficient?
