# What's left for classic (Acts 1–4)

One list of what d2d still doesn't do, or doesn't do the way game.exe
1.14d does, in the base game (no expansion). Act 5 and the LoD-only
classes and items are out of scope here: what's open there is in PLAN.md
"Checkpoint 2026-10-24: Act 5" ("Still open for Act 5"). The details stay
in the docs each row points to; this file is the index. Rows as of
2026-10-23; the sound and `ponytail:` counts as of 2026-10-29.

**Kind:**
- *build*: game.exe does it and d2d doesn't yet.
- *trace*: d2d does it, but not on game.exe's word (from another
  project, or a guess); it needs reading in Ghidra or the emulator.
- *exact*: d2d does it, close but not exact (a `ponytail:` or an
  approximation in deviations.md).
- *check*: nobody has looked yet; it may already be right.

**Seen:** would a player notice? *yes*, *maybe* (in some games or on some
difficulties), *no* (only a test or a diff would).

Ranked by Seen, then by how much play it touches.

## Play and rules

| # | What | Kind | Seen | Where / notes |
|---|---|---|---|---|
| 1 | Nobody has played an act end to end by play: the quest tests set each quest up with debug verbs. A play bot (clicks only) would give the real gap list | check | yes | PLAN.md "Checkpoint 2026-10-14 (later)", `tests/play_d2d.py` (planned) |
| 5 | MonEquip.txt is hardcoded to Blood Raven's bow; the Valkyrie's gear by skill level (rows by `level`) isn't rolled. Traced: `FUN_005d6b60` (skills.md), called by `FUN_005c4470` with the skill level | build | maybe | objgroups.cpp:552; pets.hpp |
| 6 | MonItemPercent.txt (TreasureClassPercent 20: a monster's own gear dropping) isn't read | check | maybe | Matters once #5 gives monsters gear |
| 7 | Council S1: compare built d2d timing, including speed modifiers. The original event is traced; the current source already waits for the action frame. | check | maybe | [council-s1.md](council-s1.md): stock original scheduler/dispatch checked; no built-d2d differential yet |
| 8 | Skills: every class skill works, fidelity varies by skill | exact | maybe | skills.md, each phase's "gaps" |
| 9 | Unique monster mods still untraced: the act bosses' deaths (the thief is traced: never applied, monsters.md) | trace | maybe | PLAN.md phase 6, monster-ai.md |
| 13 | Quests keep one player's lists | build | no | Single player is fine; matters once d2d hosts. Left: quests-act2.md's "one player" ponytails. The Den's clear now reaches the killer's party (quests.md "A monster dies") |
| 14 | Seed stand-ins: d2d's rng where game.exe rolls a unit's or the game's own seed (same odds, different rolls) | exact | no | ~32 notes, act1-gaps.md. Matters once machines share a game |
| 37 | The get-hit oracles `hit_absorb.py` and `hit_render.py` were cited but never committed: absorb and get-hit frames are cited, not re-checkable (`hit_rule.py` is in) | trace | no | combat.md, combat.hpp `absorb_blows`, `test_combat --get-hit-frames` |
| 39 | A walk to an NPC ends at the server's reach (under 7, no line test), not the client's melee range with a clear line (FUN_00480c80 → FUN_00622c40) | exact | yes | collision.md "How near the client walks"; world/tick.cpp `in_reach` |
| 40 | One click on the ground re-plans after a short path while it moves; game.exe's walk to a spot ends there | exact | maybe | collision.md "A walk to a spot just ends"; world/tick.cpp "Short of the target with a step made" |
| 41 | `follow_path` tests the end of a step short of its next point, its own subtile too, and not a point it reaches; game.exe tests only a new subtile and every one crossed (FUN_00650150 from LAB_006501c0), so with a blocker come onto its footprint d2d's unit can't make a short step inside its subtile and game.exe's can | exact | hardly | collision.md "A walk's stop"; ai.cpp `follow_path` |
| 47 | A walk to a warp has no two-walk limit (FUN_00548a50, FUN_00580c20): it re-plans while it steps | exact | hardly | collision.md "The server's pending interact"; world/tick.cpp, world/travel.cpp `use_warp` |
| 48 | `use_warp` takes a warp mid-walk at reach; a single click in game.exe tests when the walk ends (LAB_00580e73), up to 4 subtiles later | exact | hardly | collision.md "The server's pending interact"; world/travel.cpp `use_warp` |
| 49 | A held button over a warp forgets it after a stop; game.exe clicks again after a stop (LAB_00461fb7; FUN_00462930 stops a held frame while it walks), and refuses warp clicks inside that time | exact | hardly | collision.md "The server's pending interact"; world/commands.cpp `walk_to` |
| 50 | A held button re-aims each tick, and a joined game sends C->S 0x01 / 0x03 each drawn frame the aim moves to a new subtile (`NetGame::send` drops a repeat within 200 ms, as FUN_00478350); game.exe's walking player sends every 7 client frames, 280 ms (FUN_004623c0) | exact | hardly | collision.md "What a held frame sends"; town/input.cpp, netgame.cpp `move_to` |
| 51 | A click nearer than 3 subtiles (held 4, a run 5) walks only that far; game.exe's spot goes out to that distance (FUN_004623c0) | exact | hardly | collision.md "What a held frame sends"; world/commands.cpp `walk_to` |

## Sounds

`tools/sound_coverage.py <mpq dir>`: 3,416 of the base install's 3,520
sounds play, 104 don't (2026-10-29). 97 of the 104 are traced as never
played by stock 1.14d (rows 16, 18 and 19), so d2d plays 3,416 of the 3,423
the original does (unit-sounds.md "In d2d").
Sounds start and mix as game.exe's do (sound.md "Starting a sound",
"Solo and the game menu"; rules/sound_start.hpp, apps/d2d/audio.hpp):
Compound, Defer / Stop Inst, one line at a time per unit, the Solo duck
to 70% and the menu's fade-out.

| # | What | Count | Kind | Seen | Notes |
|---|---|---|---|---|---|
| 16 | The hero's quest lines: each quest's "find" and "complete" voice, five classes | 70 | build | yes | Triggers include Cain rescue, Diablo (0x4b, with the quest music's delays: unit-sounds.md "Quest music"), Seven Tombs, Act 3 completions and prior Act 1 / 2 cues. 14 remaining per classic class (70 sounds: act 2's begin_tainted, complete_tightspot and four find lines, events 0x31, 0x35, 0x39..0x3c) have no server dispatch in stock 1.14d (unit-sounds.md). |
| 18 | Code-played one-offs: Andariel's quake loop and fire end, the Hellforge smash, shrine overlays, corpse loot, trap trigger, sewer water, Wanderer's fissure, monster casts, steal impacts | ~30 | build | yes | sound_coverage.py's "code:" buckets; Energy Shield, Barbarian Leap, town-restricted skill, leech, missile ProgSound, classic quest music cues, Andariel's quake, Diablo's arrival taunt and Diablo's death schwing / hit are wired. The monster cast / skeleton cast / Izual taunt_2 rows are never played (unit-sounds.md). |
| 19 | NPC lines left: Alkor / Asheara / Hratli / Ormus act 3 q4, Cain's act 4 greeting, Tyrael after a4q2, Warriv's act 2 gossip, guard greeting, rogue / Iron Wolf hireling confirms | 21 | build | yes | Cain's act 1 help is wired (the Gibbet's call). As traced, stock 1.14d plays none of the rest: not in the speech table, no client code plays them, and the rogue confirm's C→S 0x46 is never sent (unit-sounds.md). |
| 20 | Skills' prgsound; monsters' own skill stsound | — | build | yes | unit-sounds.md; dosound a / b are in (only Jab's is ever played); the player's block and kick sounds are in |
| 21 | Camp floors: DS1 tiles aren't in `room_floors`, so footsteps there are always dirt | — | exact | yes | unit-sounds.md |

## Animation and drawing

`tools/anim_coverage.py <mpq dir> <anim log>...`: the base install's
data names 2,885 animations (player COFs 524, monsters 788, objects
1,044, missiles 184, overlays 163, item drops 182). d2d logs each one it
draws when `D2D_ANIM_LOG=<file>` is set. After the play tests
(`smoke_d2d`, `coop_d2d`, `pad_use_d2d`, `quests_act1..4`; 2026-10-23)
it had drawn 707 and not yet 2,178. That count is what those runs reached,
not what d2d can draw: they pick few weapon classes, open few objects
and cast few skills. The one mode d2d has no code for is a stray
`amwk1hs` COF (WK isn't a player mode).

Known differences in the ones it draws, and what's left to check:

| # | What | Kind | Seen | Where |
|---|---|---|---|---|
| 52 | Video Options' Gamma and Contrast move but do nothing. game.exe's default driver (DirectDraw) shows the palette through `FUN_006be190`'s ramp at gamma 155: every colour 1.55 times, clipped; `-w` (GDI) has no gamma | build | yes | video-drivers.md "Gamma and Contrast", design/rendering.md item 1 |
| 25 | Timings from the COF, where AnimData.d2 is game.exe's | exact | maybe | world_view.hpp:140, fight.hpp:498, fight.hpp:729 |
| 27 | Kind 8's visible state overlays aren't rotated after 50 ticks; active states draw together | exact | maybe | states.md, town.hpp `StateClock`; Radius is stamped and LoopWaitTime applies only to kind 6 |
| 28 | A joined monster's act lasts 600 ms, not its animation | exact | maybe | town/net.cpp `net_overlay`; balloon starts and rate now traced (quests.md) |
| 29 | COF draw effect 6 (the max-component table) draws opaque | build | maybe | cof-draw-order.md "Draw effects", deviations.md approximation 9 |
| 30 | Palette blends (additive / multiply missiles, blended shadows, rain) done in RGB, not through the PL2 tables | exact | slightly | deviations.md approximation 1 |
| 31 | Static lights' shadows (type 2) are read each frame; game.exe caches them (FUN_004750f0), so a later change to the map doesn't re-shade them | exact | hardly | lighting.md, deviations.md approximation 5 |
| 32 | Light Quality always high; unit shadows darken once; lit tiles per pixel, not per 32-pixel block | exact | hardly | deviations.md approximations 2–4 |
| 36 | The play tests reach a quarter of the animations; a run (or devctl tour) that gives each class every weapon class, casts every skill, opens every object type and fights every monster would show which ones d2d really never asks for | check | — | tools/anim_coverage.py; `tests/play_d2d.py` (planned, #1) |
| 45 | Rooms over a level's edge take the player's level's ambient, not their own: at least three Act 4 borders differ (140 / 120, 120 / 80, 80 / 40; the listing leaves out River of Flame, 107) | exact | hardly | lighting.md "What fills it"; `FUN_00474610`; `level_ambient.py borders` |

## Tables

Every excel table that drives classic play is read, apart from #2, #5
and #6 above. The others the code doesn't name:

- **Code lists** (d2d has the values as constants, as game.exe mostly
  does): ObjMode, ObjType, PlrMode, PlrType, MonMode, Composit, ArmType,
  WeaponClass, BodyLocs, ElemTypes, PlayerClass, HireDesc.
- **Not read by game.exe at runtime**: Arena, CharTemplate, Events,
  CubeMod.

## Formats read on another project's word

They all parse every file in the MPQs and draw or load right; tracing
them would only confirm it (unverified.md has each row): the DCC format as
a whole, the DS1 layer order, and the .d2s header and item bitstream.

## Joined games

Out of single player's way, listed so they aren't lost: 0x74 corpses,
0x11 leech, 0x90 party spots and 0x99 skill effects are read but not
drawn; tooltip stats leave out auras, states and weapon masteries; live
checks still owed (shrine overheads, leaving a cave, the gold box's
Backspace, other units' states, a Battle Orders cast vs d2d's maxima).
docs/design/net-join-plan.md, PLAN.md "Checkpoint 2026-10-19".

## The long tail

437 `ponytail:` notes in components/ and apps/ (2026-10-30), most in
ai.cpp (58), fight.cpp (45), world.cpp (41) and fight.hpp (40)
(fight.cpp and world.cpp were split into `components/game/fight/` and
`components/game/world/` since). Each names
the game.exe function to trace when it matters. act1-gaps.md sorted them
on 2026-10-10 (most don't change single-player play); `git grep -n
ponytail:` lists them. Items here that a player could notice belong in a
table above.

## Keeping it current

When a row is done, delete it here and note it where its doc says
(unverified.md's "Traced", deviations.md's "matched since", the PLAN
checkpoint). Re-run `tools/sound_coverage.py` and `tools/anim_coverage.py` and update the counts.
