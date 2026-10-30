# What's left for classic (Acts 1–4)

One list of what d2d still doesn't do, or doesn't do the way game.exe
1.14d does, in the base game (no expansion). Act 5 and the LoD-only
classes and items are out of scope here: what's open there is in PLAN.md
"Checkpoint 2026-10-24: Act 5" ("Still open for Act 5"). The details stay
in the docs each row points to; this file is the index. Rows as of
2026-10-23; the sound count as of 2026-10-28, `ponytail:` as of 2026-10-29.

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
| 5 | MonEquip.txt is hardcoded to Blood Raven's bow; the Valkyrie's gear by skill level (rows by `level`) isn't rolled | build | maybe | objgroups.cpp:552; pets.hpp |
| 6 | MonItemPercent.txt (TreasureClassPercent 20: a monster's own gear dropping) isn't read | check | maybe | Matters once #5 gives monsters gear |
| 7 | The Council's S1 shot: its mode event isn't traced (fires MissS1 at once) | trace | maybe | quests-act3.md, PLAN.md "Checkpoint 2026-10-14" |
| 8 | Skills: every class skill works, fidelity varies by skill | exact | maybe | skills.md, each phase's "gaps" |
| 9 | Unique monster mods still untraced: the thief, the act bosses' deaths | trace | maybe | PLAN.md phase 6, monster-ai.md |
| 11 | Charsi's Imbue pick: the click path after the menu | trace | no | unverified.md, act1-end.md |
| 13 | Quests keep one player's lists | build | no | Single player is fine; matters once d2d hosts. `ponytail: one player` in quests.hpp |
| 14 | Seed stand-ins: d2d's rng where game.exe rolls a unit's or the game's own seed (same odds, different rolls) | exact | no | ~32 notes, act1-gaps.md. Matters once machines share a game |
| 15 | A town portal opens 0.6 cells south of its caster, not at game.exe's nearest free spot (collision 0x3e01, size 3) | exact | hardly | deviations.md approximation 8 |

## Sounds

`tools/sound_coverage.py <mpq dir>`: 3,414 of the base install's 3,520
sounds play, 106 don't (2026-10-28). 97 of the 106 are traced as never
played by stock 1.14d (rows 16, 18 and 19), so d2d plays 3,414 of the 3,423
the original does (unit-sounds.md "In d2d").
Sounds start and mix as game.exe's do (sound.md "Starting a sound",
"Solo and the game menu"; rules/sound_start.hpp, apps/d2d/audio.hpp):
Compound, Defer / Stop Inst, one line at a time per unit, the Solo duck
to 70% and the menu's fade-out.

| # | What | Count | Kind | Seen | Notes |
|---|---|---|---|---|---|
| 16 | The hero's quest lines: each quest's "find" and "complete" voice, five classes | 70 | build | yes | Triggers include Cain rescue, Seven Tombs, Act 3 completions and prior Act 1 / 2 cues. 14 remaining per classic class (70 sounds: act 2's begin_tainted, complete_tightspot and four find lines, events 0x31, 0x35, 0x39..0x3c) have no server dispatch in stock 1.14d (unit-sounds.md). |
| 17 | The classic quest music jingles (Andariel, Blood Raven, Den, Forge, Horadric …) | 10 | build | yes | Wired at the quest completion/object event; music.md |
| 18 | Code-played one-offs: Andariel's quake loop and fire end, the Hellforge smash, shrine overlays, corpse loot, trap trigger, sewer water, Wanderer's fissure, monster casts, steal impacts | ~30 | build | yes | sound_coverage.py's "code:" buckets; Energy Shield, Barbarian Leap, town-restricted skill, leech, missile ProgSound, classic quest music cues and Diablo's death schwing / hit are wired. The monster cast / skeleton cast / Izual taunt_2 rows are never played (unit-sounds.md). |
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
| 25 | Timings from the COF, where AnimData.d2 is game.exe's | exact | maybe | world_view.hpp:198, fight.hpp:498, fight.hpp:729 |
| 27 | Kind 8's visible state overlays aren't rotated after 50 ticks; active states draw together | exact | maybe | states.md, town.hpp `StateClock`; Radius is stamped and LoopWaitTime applies only to kind 6 |
| 28 | A joined monster's act lasts 600 ms, not its animation | exact | maybe | town.cpp `net_overlay`; balloon starts and rate now traced (quests.md) |
| 29 | COF transparent layers: which of 25 / 50 / 75 % draw effects 0..2 are | trace | maybe | unverified.md, deviations.md approximation 9 |
| 30 | Palette blends (additive / multiply missiles, blended shadows, rain) done in RGB, not through the PL2 tables | exact | slightly | deviations.md approximation 1 |
| 31 | Static lights' shadows (type 2) not built | build | maybe | lighting.md, deviations.md approximation 5 |
| 32 | Light Quality always high; unit shadows darken once; lit tiles per pixel, not per 32-pixel block | exact | hardly | deviations.md approximations 2–4 |
| 33 | Act 4's clock: its 15 more a frame and eclipse phases (0x744438) | build | maybe | light.hpp:28 |
| 35 | DC6 / DCC frame anchor; DT1 block y-shift | trace | no | unverified.md (they draw right) |
| 36 | The play tests reach a quarter of the animations; a run (or devctl tour) that gives each class every weapon class, casts every skill, opens every object type and fights every monster would show which ones d2d really never asks for | check | — | tools/anim_coverage.py; `tests/play_d2d.py` (planned, #1) |

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
a whole, the DS1 layer order, pal.dat as BGR, TBL as Latin-1, the PL2
layout, the .d2s header and item bitstream.

## Joined games

Out of single player's way, listed so they aren't lost: 0x74 corpses,
0x11 leech, 0x90 party spots and 0x99 skill effects are read but not
drawn; tooltip stats leave out auras, states and weapon masteries; live
checks still owed (shrine overheads, leaving a cave, the gold box's
Backspace, other units' states, a Battle Orders cast vs d2d's maxima).
docs/design/net-join-plan.md, PLAN.md "Checkpoint 2026-10-19".

## The long tail

496 `ponytail:` notes in components/ and apps/ (2026-10-29), most in
ai.cpp (60), fight.cpp (48), world.cpp (44) and fight.hpp (42). Each names
the game.exe function to trace when it matters. act1-gaps.md sorted them
on 2026-10-10 (most don't change single-player play); `git grep -n
ponytail:` lists them. Items here that a player could notice belong in a
table above.

## Keeping it current

When a row is done, delete it here and note it where its doc says
(unverified.md's "Traced", deviations.md's "matched since", the PLAN
checkpoint). Re-run `tools/sound_coverage.py` and `tools/anim_coverage.py` and update the counts.
