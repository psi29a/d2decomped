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
- *work around*: do the evidently intended thing, with a ponytail note;
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

## How to add one

Give it a new row: where it is (file / function), what's wrong, the
confidence, what d2d does, and which doc has the details. Raise or lower
the confidence as it's checked. Don't delete a row; mark it "not a bug"
with the reason.
