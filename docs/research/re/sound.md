# Sound — game.exe 1.14d

d2d plays sound through OpenAL (openal-soft, `cmake/FindOpenAL.cmake`
from ../thirdeye). WAVs are decoded with SDL; headless runs use
`ALSOFT_DRIVERS=null`.

## Sounds.txt

Rows are keyed by `Index`. `FileName` is relative to:
- `data\global\music\` when `Music Vol` is 1;
- otherwise `data\global\sfx\`, or `data\local\sfx\` for speech
  (d2speech/d2xtalk).

`Volume` runs 0..255, and `Loop` marks looping sounds.

## Level music and ambience

Levels.txt `SoundEnv` → SoundEnviron.txt row (`Index`):
- `Song` → a Sounds.txt entry;
- `Day Ambience` / `Night Ambience` → Sounds.txt entries.

Rogue Encampment (level 1): env 1, song 4673 `music_town_1`
(`Act1\town1.wav`, volume 110, loop), day ambience 70. Songs are ~20 MB
WAVs, so d2d decodes them on a worker with its own MPQ handles.

## Frontend music

`FUN_00516250` is the music thread; it wakes every 50 ms. While playlist
mode is on (`DAT_00881798`, set by `FUN_005148f0` from the frontend
screens) and nothing is playing, `FUN_00514990` plays a random
not-yet-played track of an 8-track list. Once all 8 have played,
`FUN_00514860` clears the played flags.

The list depends on the launch config's expansion flag (`FUN_00514530`
→ `DAT_00881790`):
- LoD, at `0x72f8b8`: introedit, act5\icecaves, act5\xtemple,
  act2\desert, act2\sewer, act3\kurast, act3\kurastsewer, act4\diablo;
- classic, at `0x72f878`: common\options, act1\caves, act1\monastery,
  act1\crypt, act2\harem, act2\tombs, act3\spider, act3\kurastsewer.

Tracks play by path; there's no Sounds.txt entry, so no volume from it.
Starting a game stops the playlist (`FUN_00514930`).

## UI

D2Win buttons (control type 6) play `data\global\sfx\cursor\button.wav`
on press unless flagged silent (`FUN_00501090`). The frontend preloads
it together with the class select/deselect sounds (`FUN_004359d0`).

## NPC speech

See npc-talk.md: string → sound table at `0x72b0e0` (`FUN_004e0650`).

## Ambient events (FUN_004e42e0)

SoundEnviron.txt's record is 0x58 bytes (`FUN_00481920`): +0 Song, +4 / +8
Day / Night Ambience, +0xc / +0x10 Day / Night Event, +0x14 Event Delay,
+0x18 Indoors. Day is time-of-day phases 1..3 (`FUN_0061c220`), else night.

In sound ticks (25 Hz), with the client's rng (`FUN_004e40a0`;
`spread(n)` = rand(2n+1) − n, `range(lo, hi)` = rand(hi−lo+1) + lo):
- when the level's event changes: gap = delay + spread(delay / 3), last =
  now − rand(gap);
- once now − last ≥ gap: play the event (a random one of its group) at
  x = ±range(450, 750), y = spread(100) (`FUN_004b99a0`), then last = now
  and a new gap.

The Blood Moor (env 2): event 192 `event_wild_day_1` (5 birds), delay 250
(10 s ± 3.3 s); night 197 (crickets).

## Songs resume (FUN_004dcaa0)

Every 125 sound ticks of a song, its resume point is set to the next
Sounds.txt Block past the play position: [0, Block 1) → Block 1, [1, 2) →
2, [2, 3) → 3, else 0. Blocks are sample frames (act1\wild.wav: 479 s at
22050 Hz, blocks 3457024 and 6755328 = 157 s and 306 s). The song starts
there next time (`FUN_004b9a00`'s offset).

d2d: town.hpp plays the events (pan = x / 750), audio.hpp keeps
`song_resume` and seeks with AL_SAMPLE_OFFSET.
