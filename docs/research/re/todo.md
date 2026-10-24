# What's left for classic (Acts 1–4)

One list of what d2d still doesn't do, or doesn't do the way game.exe
1.14d does, in the base game (no expansion). Act 5 and the LoD-only
classes and items are out of scope here. The details stay in the docs
each row points to; this file is the index. As of 2026-10-23.

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
| 2 | MonProp.txt isn't read: Mephisto's +cast / +swing speed (NM 15, Hell 30), the Council's extra fire (NM 40, Hell 100), Blunderbore's crushing blow, Blood Raven's knockback in Hell, quillrat6..8's thorns, the Iron Golem's row | build | maybe | Nightmare and Hell only, apart from the golem. Trace where game.exe applies it to a new monster |
| 3 | Missiles' EDmgSymPerCalc in the fight: Meteor's fire, Immolation Arrow and Fist of the Heavens' bolts miss their synergies (the tooltips have them) | build | yes | PLAN.md "Checkpoint 2026-10-22 (2)" |
| 4 | Item charges as skills | build | yes | fight.hpp:116, item_text.hpp:237 (the tooltip has no charges branch either) |
| 5 | MonEquip.txt is hardcoded to Blood Raven's bow; the Valkyrie's gear by skill level (rows by `level`) isn't rolled | build | maybe | objgroups.cpp:552; pets.hpp |
| 6 | MonItemPercent.txt (TreasureClassPercent 20: a monster's own gear dropping) isn't read | check | maybe | Matters once #5 gives monsters gear |
| 7 | The Council's S1 shot: its mode event isn't traced (fires MissS1 at once) | trace | maybe | quests-act3.md, PLAN.md "Checkpoint 2026-10-14" |
| 8 | Skills: every class skill works, fidelity varies by skill | exact | maybe | skills.md, each phase's "gaps" |
| 9 | Unique monster mods still untraced: the thief, the act bosses' deaths | trace | maybe | PLAN.md phase 6, monster-ai.md |
| 10 | Throw's level 1 only while a throwable is held | trace | no | unverified.md |
| 11 | Charsi's Imbue pick: the click path after the menu | trace | no | unverified.md, act1-end.md |
| 12 | The character-create name filter | trace | maybe | unverified.md |
| 13 | Quests keep one player's lists | build | no | Single player is fine; matters once d2d hosts. `ponytail: one player` in quests.hpp |
| 14 | Seed stand-ins: d2d's rng where game.exe rolls a unit's or the game's own seed (same odds, different rolls) | exact | no | ~32 notes, act1-gaps.md. Matters once machines share a game |
| 15 | A town portal opens 0.6 cells south of its caster, not at game.exe's nearest free spot (collision 0x3e01, size 3) | exact | hardly | deviations.md approximation 8 |

## Sounds

`tools/sound_coverage.py <mpq dir>`: 3,346 of the base install's 3,520
sounds play, 174 don't (2026-10-24).

| # | What | Count | Kind | Seen | Notes |
|---|---|---|---|---|---|
| 16 | The hero's quest lines: each quest's "find" and "complete" voice, five classes | 125 | build | yes | Triggers include Cain rescue, Seven Tombs, Act 3 completions and prior Act 1 / 2 cues. 25 remaining lines per classic class (125 sounds) have no server dispatch in stock 1.14d; includes find hints and unsupported quest event lines. |
| 17 | The quest music jingles (Andariel, Blood Raven, Den, Forge, Horadric …) | 10 | build | yes | music.md |
| 18 | Code-played one-offs: Diablo's death hit / schwing, Andariel's quake loop and fire end, the Hellforge smash, shrine overlays, corpse loot, trap trigger, sewer water, Wanderer's fissure, monster casts, steal impacts | ~30 | build | yes | sound_coverage.py's "code:" buckets; Energy Shield, Barbarian Leap, town-restricted skill, leech, and missile ProgSound cues are hooked too. |
| 19 | NPC lines left: Alkor / Asheara / Hratli / Ormus act 3 q4, Cain's act 1 help and act 4 greeting, Tyrael after a4q2, Warriv's act 2 gossip, guard greeting, rogue / Iron Wolf hireling confirms | ~20 | build | yes | npc-talk.md |
| 20 | Skills' dosound a / b, prgsound; block and kick sounds; monsters' own skill stsound | — | build | yes | unit-sounds.md |
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
| 26 | Run / walk animation isn't sped by faster run/walk | exact | yes | world.cpp:2439 |
| 27 | Overlays: their light (Radius) isn't stamped, LoopWaitTime isn't applied | exact | yes | town.hpp:61, world_view.cpp:415 |
| 28 | An NPC's act lasts 600 ms, not its animation; the balloon starts wherever d2d's loop is | exact | maybe | town.cpp:811, town.cpp:2732 |
| 29 | COF transparent layers: which of 25 / 50 / 75 % draw effects 0..2 are | trace | maybe | unverified.md, deviations.md approximation 9 |
| 30 | Palette blends (additive / multiply missiles, blended shadows, rain) done in RGB, not through the PL2 tables | exact | slightly | deviations.md approximation 1 |
| 31 | Static lights' shadows (type 2) not built | build | maybe | lighting.md, deviations.md approximation 5 |
| 32 | Light Quality always high; unit shadows darken once; lit tiles per pixel, not per 32-pixel block | exact | hardly | deviations.md approximations 2–4 |
| 33 | Act 4's clock: its 15 more a frame and eclipse phases (0x744438) | build | maybe | light.hpp:28 |
| 34 | Objects.txt CycleAnim: 0 read as "play once, hold the last frame" | trace | no | unverified.md |
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

456 `ponytail:` notes in components/ and apps/ (2026-10-23), most in
ai.cpp (48), fight.cpp (45), world.cpp (44) and fight.hpp (42). Each names
the game.exe function to trace when it matters. act1-gaps.md sorted them
on 2026-10-10 (most don't change single-player play); `git grep -n
ponytail:` lists them. Items here that a player could notice belong in a
table above.

## Keeping it current

When a row is done, delete it here and note it where its doc says
(unverified.md's "Traced", deviations.md's "matched since", the PLAN
checkpoint). Re-run `tools/sound_coverage.py` and `tools/anim_coverage.py` and update the counts.
