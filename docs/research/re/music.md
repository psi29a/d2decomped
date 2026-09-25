# Level music — game.exe 1.14d

`.\Sound\SoundHdr.cpp` loads Sounds.txt (0x8e-byte records at
`DAT_007bc9a0`; Volume +0x3c, Fade In +0x3f, Fade Out +0x40, Defer Inst
+0x41, Stop Inst +0x42, Cache +0x4c, Priority +0x4e, Stream +0x4f, Music
Vol +0x53, Block 1..3 +0x54/+0x58/+0x5c) and SoundEnviron.txt (0x58-byte
records at `DAT_007bc9a8`, Song +0, Day / Night Ambience +4 / +8, Day /
Night Event +0xc / +0x10, Event Delay +0x14, Indoors +0x18); a level
names its environment with Levels.txt SoundEnv.

**The sound tick** (FUN_00482c20, from the game loop): ambience
(FUN_004e42e0), music (FUN_004dcaa0), sounds (FUN_004ba020), then the
tick counter `DAT_007bc9bc` (FUN_00481820) goes up by one. Fades and
delays count these ticks (the loop runs at 25 Hz: 40 ms).

**Music** (FUN_004dcaa0), each tick:

- the player's level changed → remember when;
- the environment's Song (checked against the music index range) differs
  from the playing one and 75 ticks (3 s) have passed since the level
  changed (or music is off, or nothing plays) → it becomes the song;
- a song that isn't playing starts (FUN_004b9a00) from the position
  saved for it, after the previous one is told to stop (FUN_004ba8f0);
- every 125 ticks the playing song's position is saved, snapped to its
  Block 1..3 sample offsets — songs resume at a cue point.

**Fades** (FUN_004b9ef0): stopping a sound with a Fade Out, or starting
one with a Fade In (FUN_004ba020), ramps its volume linearly from now to
now + fade ticks. Act music is Fade In / Out 125 (5 s), the wilderness
ambience 12 — so changing songs is a 5 s cross-fade.

d2d: `Audio::crossfade_music`, the 3 s rule in `Town::update`.
ponytail: no resume positions, no ambience fades.

# Automap layers

Levels.txt `Layer`: levels on one layer share an automap. Act 1's town,
Blood Moor, Cold Plains, Stony Field and Burial Grounds are all layer 0
(the Den of Evil is 1), so the map shows the camp and the wilderness
together. d2d: `Automap` per layer, cells in act coordinates.
