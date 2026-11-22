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
| 8 | Skills: every class skill works, fidelity varies by skill | exact | maybe | skills.md, each phase's "gaps". The char panel's damage and rating lines follow game.exe's handlers for every classic skill, two hands and throws included (`tools/emu/panel_damage.py`). A two-weapon blow takes its hand's weapon alone (skills.md "Which hand a skill swings"), and the panel's weapon sum is FUN_004e86f0's (run by the same oracle) |
| 14 | Seed stand-ins: d2d's rng where game.exe rolls a unit's or the game's own seed (same odds, different rolls) | exact | no | `git grep -n ponytail: -- components apps \| grep -i seed`, act1-gaps.md. Matters once machines share a game. The client unit seed (FUN_00465fd0) is in: `client_unit_seed` for objects, the ancient death cloud, a shatter's ice, Lightning's sparks and the Horadric Staff; left are the server's (a pet's, a summon's, Cain's spot, the object seed's order) |

## Sounds

`tools/sound_coverage.py <mpq dir>`: 3,416 of the base install's 3,520
sounds play, 104 don't (2026-10-29). All 104 are traced as never played
by stock 1.14d (unit-sounds.md "In d2d"), so in a game d2d hosts it
plays every sound the original does; a joined game takes each monster's
impact from the host's hit class (S->C 0x0c, unit-sounds.md "In d2d").
Sounds start and mix as game.exe's do (sound.md "Starting a sound",
"Solo and the game menu"; rules/sound_start.hpp, apps/d2d/audio.hpp):
Compound, Defer / Stop Inst, one line at a time per unit, the Solo duck
to 70% and the menu's fade-out. Nothing open.

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
a whole, the DS1 layer order, and the .d2s header and item bitstream.

## Joined games

Out of single player's way, listed so they aren't lost: 0x74 corpses,
0x11 leech, 0x90 party spots and 0x99 skill effects are read but not
drawn; tooltip stats leave out auras, states and weapon masteries; live
checks still owed (shrine overheads, leaving a cave, the gold box's
Backspace, other units' states, a Battle Orders cast vs d2d's maxima, a
host monster's impact sounds on GH / KB / DT from S->C 0x0c).
docs/design/net-join-plan.md, PLAN.md "Checkpoint 2026-10-19".

## The long tail

`git grep -n ponytail: -- components apps` lists the `ponytail:` notes;
each names the game.exe function to trace when it matters.
act1-gaps.md sorted them on 2026-10-10 (most don't change single-player
play). Items here that a player could notice belong in a table above.

## Keeping it current

When a row is done, delete it here and note it where its doc says
(unverified.md's "Traced", deviations.md's "matched since", the PLAN
checkpoint). Re-run `tools/sound_coverage.py` and `tools/anim_coverage.py` and update the counts.
