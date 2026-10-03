# Bugs in the original — game.exe 1.14d and its MPQ data

game.exe and its data aren't assumed to be perfect. This file lists what
looks wrong in the original, so it can be checked again, reported to
Blizzard, and either matched or worked around on purpose.

**Confidence:**
- **high**: seen in the bytes or code, and there's no other reading.
- **medium**: the code clearly does it, but it may be intended.
- **low**: a suspicion that needs another look.

**d2d** says what we do about it:
- *match*: do what game.exe does;
- *work around*: do the evidently intended thing, listed in
  deviations.md;
- *n/a*: it has no effect.

| # | Where | What | Confidence | d2d | Found in |
|---|---|---|---|---|---|
| 1 | d2data: Act1/Outdoors/Trees.ds1 (v12) | It declares 14 substitution groups but ends 12 bytes into the 14th. `FUN_00665950` reads past its buffer: zeros in the emulator, whatever follows the allocation on real hardware. | high | match (a 0×0 group stamps nothing); the emulator pads files with 64 zero bytes | drlg.md "Checking against game.exe" |
| 2 | Skills.txt, Bone Wall calc2 | `par34` isn't a calc name (a typo for `par3` or `par4`). game.exe's compiler looks names up by their first four characters (`FUN_006119f0`), so it reads `par3`: calc2 ("# of walls - 1") is Param3 = 8 at every level. game.exe's own compiler emits `04 0a 00` for it (tools/emu, 2026-10-02). | high | match | skills.md "The calc-text compiler" |
| 3 | MonStats, Trap-poisoncloud / Trap-nova | Their MissA1 / MissS1 columns (trappoisonjavcloud; firebolt, nova) are never used: the AI casts Skill1, and srvmissilea decides. Dead data that misleads. | high | n/a (d2d follows the skill) | objects.md "Trap monsters" |
| 4 | Chest traps 5 / 7 (fires) | The fires deal no damage: objects.txt Damage is only read by the gas and exploding traps and the exploding barrel. Fire that doesn't burn looks unintended. | medium | match | objects.md "Fires" |
| 5 | Missiles.txt row names | Misspelled ids: `chokinggaspoition`, `rancidgasepotion`. Harmless, since rows are found by these exact names. | high | n/a (d2d uses them verbatim) | 2026-09-27, shrines |
| 6 | a1q1.cpp entering the Den (`FUN_00590470`) | The log state is set to 2 before `LAB_0058fc90` marks the flags, so that path always sets bit 4. Bit 3 can only come from the other branch (param +0x14 == 1). | low | match | quests.md |
| 7 | Sounds.txt `druid_death_2` | Uses the same file as `druid_death_1` (combat\player\druid\death1.wav), where every other class has distinct death sounds. | low | match | 2026-09-27, sound |
| 8 | d2data: Act1/Town/townN1.ds1 | Its DT1 file list names `.tg1` files (treegroups.tg1, floor.tg1, …) that don't exist; the other three camps list `.dt1`s. Harmless in game.exe, which takes a preset's DT1s from LvlTypes by the LvlPrest Dt1Mask, never from the DS1. | high | n/a (d2d now does the same) | drlg.md "The game's map seed" |
| 9 | Env.cpp day step (`FUN_0061bee0`) | A phase moves on when `next.start × scale < time`. Phase 2 starts at 0°, so phase 1 (dawn at 340°) moves on at its first frame and the clock snaps to sunrise: the 340°–360° dawn (its (208, 184, 131) colour) never shows, and a day is 24 minutes instead of 31. | medium | match | lighting.md "The day" |
| 10 | Rain splashes (`FUN_00472da0`) | The spot is the tile's top corner x + rand(the row's width), with no half-width back: splashes land up to half a tile right of the water they belong to. | medium | match | weather.md "Splashes" |
| 11 | The light grid (0x7b0e68, `FUN_00475800`) | Only 48 × 48 subtiles round the player: a light more than 24 subtiles off lights nothing, and the view's corners at 800 × 600 (about 31 subtiles out) read the grid's clamped edge. Likely sized for 640 × 480. | medium | work around (deviations.md #1) | lighting.md |
| 12 | A composite layer's tint byte (d2s +0x98) | **Not a bug.** It holds Transform x 32 + 1 + colour in a byte, so Transform 8 wraps to 0x01..0x15 (uld white 0x01, uth dark purple 0x13); but the reader, `FUN_005038d0`, takes the byte − 1 and sends Transform 0 to the Transform 8 colormaps (slot 0x348 = 8 x 105), and 0 is never a tint's Transform. | not a bug | match (`tint_of`) | compcode.md "Tints" |
| 13 | MonStats.txt `cr_lancer8` | The Id is on two rows (hcIdx 617 and 723). game.exe resolves names by their place among the distinct Ids, so every Id after the second one lands a row early: Pandemonium 3's (level 135) `overseer6` is row 723, the second `cr_lancer8`. Checked on all 125 levels' lists. | high | match (`Monsters::by_id`) | monsters.md "Monster region" |
| 14 | Gargoyle Trap shot (`FUN_005cc050`, srvdofunc 93) | The missile starts at a sixth of the way to its aim point **less 1 on both axes**, whatever the direction, so a trap shooting toward −x / −y starts nearer itself and one shooting +x / +y starts a subtile short of the sixth. Likely meant to step back toward the trap. | low | match (`rules::gargoyle_shot`) | monster-ai.md "The Gargoyle Trap's shot" |
| 16 | A town NPC talked to (`FUN_005e68f0`) | While AI control +0x14 (40 at the talk's start, FUN_00548b00) is over 36, the NPC walks to (+0x18, +0x1c) when over 2 off it (FUN_005dc5c0, FUN_005ded90). +0x18 is the greeting count (0..60) and nothing seen sets +0x1c, so the spot is near the map's corner, over 100 subtiles off: no path, the walk can't set off and the think comes aidel (15) on. Looks like a spot meant to be stored there. | medium | match (stand 15, `rules::npc_think`) | town-npcs.md "The visitor" |
| 15 | A superunique restored (`FUN_005a4440`, from FUN_005424f0 when a freed room comes back) | It re-applies the stored mods, superunique index and quest bindings, but not the special AI 0xd that FUN_005a49b0 gives the Countess at spawn. So a Countess whose room was freed (no player within a room of it for ~5 s) comes back without it. | medium | doesn't arise: d2d never frees a room (drlg.md "A room's life on the server") | superuniques.md "FUN_005a4440" |

## How to add one

Give it a new row: where it is (file / function), what's wrong, the
confidence, what d2d does, and which doc has the details. Raise or lower
the confidence as it's checked. Don't delete a row; mark it "not a bug"
with the reason.
