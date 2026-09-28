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
| 2 | Skills.txt, Bone Wall calc2 | `par34` isn't a calc token (a typo for `par3`/`par4`?). What game.exe's calc parser makes of it isn't traced. | high (typo), low (effect) | 565 of 566 calcs compile; this one doesn't | skills.md |
| 3 | MonStats, Trap-poisoncloud / Trap-nova | Their MissA1 / MissS1 columns (trappoisonjavcloud; firebolt, nova) are never used: the AI casts Skill1, and srvmissilea decides. Dead data that misleads. | high | n/a (d2d follows the skill) | objects.md "Trap monsters" |
| 4 | Chest traps 5 / 7 (fires) | The fires deal no damage: objects.txt Damage is only read by the gas and exploding traps and the exploding barrel. Fire that doesn't burn looks unintended. | medium | match | objects.md "Fires" |
| 5 | Missiles.txt row names | Misspelled ids: `chokinggaspoition`, `rancidgasepotion`. Harmless, since rows are found by these exact names. | high | n/a (d2d uses them verbatim) | 2026-09-27, shrines |
| 6 | a1q1.cpp entering the Den (`FUN_00590470`) | The log state is set to 2 before `LAB_0058fc90` marks the flags, so that path always sets bit 4. Bit 3 can only come from the other branch (param +0x14 == 1). | low | match | quests.md |
| 7 | Sounds.txt `druid_death_2` | Uses the same file as `druid_death_1` (combat\player\druid\death1.wav), where every other class has distinct death sounds. | low | match | 2026-09-27, sound |
| 8 | d2data: Act1/Town/townN1.ds1 | Its DT1 file list names `.tg1` files (treegroups.tg1, floor.tg1, …) that don't exist; the other three camps list `.dt1`s. Harmless in game.exe, which takes a preset's DT1s from LvlTypes by the LvlPrest Dt1Mask, never from the DS1. | high | n/a (d2d now does the same) | drlg.md "The game's map seed" |
| 9 | Env.cpp day step (`FUN_0061bee0`) | A phase moves on when `next.start × scale < time`. Phase 2 starts at 0°, so phase 1 (dawn at 340°) moves on at its first frame and the clock snaps to sunrise: the 340°–360° dawn (its (208, 184, 131) colour) never shows, and a day is 24 minutes instead of 31. | medium | match | lighting.md "The day" |
| 10 | Rain splashes (`FUN_00472da0`) | The spot is the tile's top corner x + rand(the row's width), with no half-width back: splashes land up to half a tile right of the water they belong to. | medium | match | weather.md "Splashes" |
| 11 | The light grid (0x7b0e68, `FUN_00475800`) | Only 48 × 48 subtiles round the player: a light more than 24 subtiles off lights nothing, and the view's corners at 800 × 600 (about 31 subtiles out) read the grid's clamped edge. Likely sized for 640 × 480. | medium | work around (deviations.md #1) | lighting.md |
| 12 | A composite layer's tint byte (d2s +0x98) | Holds Transform x 32 + 1 + colour in a byte, so Transform 8 items (tower shields, some armour) wrap to Transform 0's range: uld white saves 0x01, uth dark purple 0x13. How the drawing reads it back isn't traced; it may lose the colormap. | low | n/a (tints not built) | compcode.md "Tints" |

## How to add one

Give it a new row: where it is (file / function), what's wrong, the
confidence, what d2d does, and which doc has the details. Raise or lower
the confidence as it's checked. Don't delete a row; mark it "not a bug"
with the reason.
