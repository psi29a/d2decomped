# Sound — game.exe 1.14d

d2d plays sound through OpenAL (openal-soft, `cmake/FindOpenAL.cmake`
from ../thirdeye). WAVs are decoded with SDL; headless runs use
`ALSOFT_DRIVERS=null`.

## Sounds.txt

Rows are keyed by `Index`. `FileName` is relative to:
- `data\global\music\` when `Music Vol` is 1, tried there first;
- otherwise `data\global\sfx\`, or `data\local\sfx\` for speech
  (d2speech/d2xtalk).
A name missing from those sfx folders is tried in the music archive
too. The Den's jingle is Music Vol off and lives only in d2music.

`Volume` runs 0..255, and `Loop` marks looping sounds.

The loaded record is 0x8e bytes (`FUN_00481950`'s field table): Volume
+0x3c, Group Size +0x3d, Loop +0x3e, Fade In +0x3f, Fade Out +0x40, Defer
Inst +0x41, Stop Inst +0x42, Duration +0x43 (word), Compound +0x45 (word),
Falloff +0x47, Reverb +0x4b, Cache +0x4c, Async Only +0x4d, Priority +0x4e,
Stream +0x4f, Stereo +0x50, Tracking +0x51, Solo +0x52, Music Vol +0x53,
Block 1..3 +0x54 / +0x58 / +0x5c; +0x60 is the group's first row, set
at load. Times below are sound ticks: `DAT_007bc9bc`, +1 per
`FUN_00482c20` (40 ms).

## Starting a sound: Volume, Compound, Defer / Stop Inst

`FUN_004b9a00` (ecx row, edx unit, then delay, ...) makes an instance:
- Volume 0: nothing plays.
- **Compound** ≠ 0: `FUN_004b9760` looks for a live instance of the same
  group (record +0x60) that isn't stopping (instance +0x3d). Compound < 0
  takes any; otherwise one started (instance +0x34) at most Compound ticks
  ago. If there is one, no new instance is made: the play returns that
  one, with the new unit added to its unit list (+0x28). 590 rows have
  one (8 ticks for 380 of them, −1 for 49).
- The instance's priority is Priority (+0x4e), +0x50 when the unit is the
  local player.

When it starts (`FUN_004ba020`, 0x4ba47a), the instance it clashes with is
- for Defer or Stop Inst, the oldest other playing instance of its group:
  on the same unit when it has one, anywhere when not (`FUN_004b9690`);
- for a speech row (0xb76..0x1230) on a unit, any other speech playing on
  that unit (`FUN_004b9700`), whatever its flags.

With a clash, Stop Inst stops the old one and the new one plays; otherwise
the new one is dropped. So a unit says one line at a time. A Defer Inst
row with no unit and a position skips the clash and plays over whatever
is playing. 2,049 rows defer and 1,444 stop.

## Solo and the game menu: ducking

Each sound tick `FUN_004ba020` (0x4ba68f) steps two volume percentages:
- **Solo**: while any instance whose row is Solo (+0x52) plays and isn't
  fading out, `DAT_00727568` goes 2 a tick down to 70, else 2 up to 100.
  0x4dfd0b scales every non-Solo row's volume by it. The 1,273 Solo rows
  are the speech: under a quest line or an NPC's, the rest of the game
  plays at 70%.
- **The game menu**: in game mode 0 / 1 (`DAT_007a0610`) with UI 9 or 0xb
  up (the pause, menu.md), `DAT_00727564` goes 5 a tick down to 0, else 5
  up to 100. 0x4dfcb2 scales every row by it except 1..0xf (cursor),
  0x34..0x47 (`scene_*` ambience) and 0x1231..0x125a (music). The
  instances keep running: under the pause the world's sounds fade out
  while the ambience and the music play on, and fade back in after.

Both are applied in `FUN_004dfc20`, on the instance's 0..255 volume
(+0x1c), in this order, each a whole percent with the product truncated
(`* 0x51eb851f, sar 5`): Music Vol rows x music / 100 (0x4dfc84), every
row x master / 100 (0x4dfca1), the menu (0x4dfcb2), Solo (0x4dfd0b). The
speaker-mode step and the distance falloff (from 0x4dfeb5) then work on
that volume. At 255 under menu 50 and Solo 70 that is 255 -> 127 -> 88,
not 255 x 0.5 x 0.7 = 89.25.

d2d: rules/sound_start.hpp has the rules and the ducks; `ducked_mix`
ducks a world sound's volume before its distance mix, as above. Sounds
not placed in the world (UI, voice, ambience, music) carry a float gain
with their fades and take the ducks as a multiplier, and master / music
are the listener's and the music channel's gains, not this chain's
integer steps. audio.hpp keeps
a sound tick per 40 ms and, per channel, the group's first row and the
unit it plays on (`unit_key` in game/cues.hpp: the hero, a monster, an
object or a missile). Compound joins the playing instance without adding
the unit to a list.

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
+0x18 Indoors. Day is time-of-day phases 1..3 (`FUN_0061c220` returns the
phase index), 0° to 180°; else night. d2d plays both (town.hpp).

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

Screen shake uses its own unplaced quake loop and instance-volume envelope;
see [unit-sounds.md](unit-sounds.md#screen-shake-and-quest-control-missiles).
Andariel death and Diablo arrival start it; Diablo also plays its arrival
taunt. Other shake triggers and the control missiles' visuals remain open.
