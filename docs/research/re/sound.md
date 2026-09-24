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
