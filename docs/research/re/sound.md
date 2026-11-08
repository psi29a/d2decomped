# Sound — game.exe 1.14d

d2d plays sound through OpenAL (openal-soft, `cmake/FindOpenAL.cmake`
from ../thirdeye). WAVs are decoded with SDL; headless runs use
`ALSOFT_DRIVERS=null`.

## Sounds.txt

Rows are keyed by `Index`. d2d resolves `FileName` relative to:
- `data\global\music\` when `Music Vol` is 1, tried there first;
- otherwise `data\global\sfx\`, or `data\local\sfx\` for speech
  (d2speech/d2xtalk).
A name missing from those sfx folders is tried in the music archive
too. The Den's jingle is Music Vol off and lives only in d2music.

`FUN_00482710` builds an original path from a row's filename: sound ids
0xb76..0x1230 use `DATA\\LOCAL\\SFX\\`, ids 0x1231..0x125a use
`DATA\\GLOBAL\\MUSIC\\`, and other ids use `DATA\\GLOBAL\\SFX\\`.
It terminates with a fatal diagnostic (0x3ab) if the prefix and filename
are 80 bytes or more. `tools/emu/sound_hdr_paths.py` checks the boundaries
of both id ranges and the 79/80-byte boundary with synthetic rows; it hooks
the fatal diagnostic and does not open files. The music-id range here is
independent of a row's Music Vol flag.

`Volume` runs 0..255, and `Loop` marks looping sounds.

The loaded record is 0x8e bytes (`FUN_00481950`'s field table): Volume
+0x3c, Group Size +0x3d, Loop +0x3e, Fade In +0x3f, Fade Out +0x40, Defer
Inst +0x41, Stop Inst +0x42, Duration +0x43 (word), Compound +0x45 (word),
Falloff +0x47, Reverb +0x4b, Cache +0x4c, Async Only +0x4d, Priority +0x4e,
Stream +0x4f, Stereo +0x50, Tracking +0x51, Solo +0x52, Music Vol +0x53,
Block 1..3 +0x54 / +0x58 / +0x5c; +0x60 is the group's first row, set
at load. Times below are sound ticks: `DAT_007bc9bc`, +1 per
`FUN_00482c20` (40 ms).

`FUN_00481950` loads Sounds.txt and SoundEnviron.txt, allocating rows of
0x8e and 0x58 bytes respectively. Starting at sound row 1, it sets each
group member's +0x60 to its group's first row; a zero Group Size becomes 1
after that assignment. It also stores at +0x80 the number of leading Block
entries before the first -1, capped at three. It scans positive Song values
in the environment table for a minimum (initially the sound-row count) and
maximum (initially 0), then allocates four bytes per id in that inclusive
range. `tools/emu/sound_hdr_tables.py` executes the loader on one synthetic
pair of tables and checks these fields, sizes and requested paths. File reads
and parsing are hooked; those checks do not establish parser correctness or
cover empty and malformed tables.

### Falloff bounds (`FUN_004825b0`, `FUN_00482610`)

The minimum and maximum distance getters read the dword at record +0x47.
For falloff values 0, 1, 2, 3 and 4 their respective bounds are 60/400,
60/700, 200/1000, 400/1500 and 2000/2000 client pixels. Other values use
60/700. Each getter indexes the table at `DAT_007bc9a0` using 0x8e-byte
records and the count at `DAT_007bc9a4`; an invalid index or absent table
leads to a null dereference, not a default. `tools/emu/sound_hdr_ranges.py`
runs both stock getters on three synthetic rows and seven falloff values,
including an out-of-range and a negative value (42 getter calls). The
invalid-index path is read from the instructions, not run by this oracle.
The corresponding d2d bounds are `rules::falloff_range` in `sound_mix.hpp`.

### Group reference counts (`FUN_004822b0`, `FUN_00482370`)

`FUN_004822b0` takes the sound id in ECX and an adjustment in EDX. For a
valid nonzero id it uses that row's group start (+0x60) and byte group size
(+0x3d), adding the adjustment to the count at +0x8a of each group member.
If the selected row has count zero and the adjustment is -1, it does nothing.
Otherwise, decrementing a group member whose count is zero reaches a fatal
diagnostic (0x1dc), after earlier group members have already been changed.
Other adjustments use 32-bit arithmetic. `FUN_00482370` clears those group
counts. Both ignore sound id zero, an absent table and an out-of-range outer
id; neither checks group-member bounds before dereferencing them.

`tools/emu/sound_hdr_refs.py` checks grouped increments, decrements, clear,
outer-id guards and the partial decrement before an underflow diagnostic;
the diagnostic is intercepted, and malformed group membership is not tested.
d2d records each group's first row (`gamedata_load.cpp`) but does not port
this original grouped lifetime operation.

### Lifecycle and cache touch

`FUN_00482260` checks `FUN_00513b50` before initializing the sound table and
five sound subsystems in order, then clears the sound tick and three adjacent
counters. It passes its ECX argument to `FUN_004e0050` and 255 to
`FUN_00515ce0`. `FUN_00482940` performs four shutdown calls and then
`FUN_004828b0`, also only when that getter is nonzero.

`FUN_00482860` stores the current sound tick at record +0x7c, removes the id
from the list at `DAT_007bc9b8`, then adds it back. A missing table or bad id
leads to a null write. `FUN_004828b0` frees the environment table if present,
calls `FUN_004823e0` on sound ids 1 through count - 1, frees the sound table,
clears its pointer and count, then clears two range globals. It is safe to
call again after that cleanup.

`tools/emu/sound_hdr_lifecycle.py` runs these four routines, with their
subsystem, list, individual record release and allocator calls hooked. It
checks the getter guard, call order, timestamp and repeated cleanup; it does
not test the hooked subsystem internals or invalid touch ids.

`FUN_004823e0` releases a loaded sound record: it refuses an active sound
with a fatal diagnostic (0x259), subtracts the recorded size from the cache
total, removes the id from the list, and frees either its asynchronous request
or synchronous sample, then clears the relevant pointers, timestamp and load
state. An unloaded row is left alone. `tools/emu/sound_hdr_release.py` checks
the unloaded, synchronous, asynchronous and active paths with the active
query, removal and free operations hooked.

`FUN_004824a0` tries to free a requested number of cached bytes. Its first
pass skips records with a nonzero reference count or Cache flag; an optional
second pass considers those records too. An optional age check skips records
used fewer than 750 sound ticks ago. In either pass an active sound is skipped;
an eligible sound is released and the search starts again until the target
is met or no more can be freed. `tools/emu/sound_hdr_evict.py` checks these
choices using three synthetic list nodes, with the active query and release
hooked; the release hook updates the list and byte total. The target uses
unsigned subtraction: requesting more than the current byte count wraps the
target and returns success without evicting anything.

`FUN_00482970` dispatches a row's sample load. A missing-file flag or load
state 2 returns immediately. State 1 waits only for a synchronous request.
State 0 builds the path, probes the file and size, and requests eviction if
the new bytes exceed the cache limit. A missing file marks +0x81; unsuccessful
eviction records the current tick at `DAT_007bc9c4`. A successful synchronous
read stores the buffer and size, changes state to 2, touches the row and
invokes audio setup; asynchronous creation stores a request and size, changes
state to 1 and increments the pending count. `tools/emu/sound_hdr_load.py`
checks those paths and the synchronous read failure diagnostic with file,
eviction, touch and audio boundaries hooked; it does not play or decode audio.

`FUN_00482680` chooses within a group. Sizes 0 and 1 return the supplied id;
otherwise it chooses an offset below the size and retries when the chosen id
matches one of the active history entries at +0x64 and +0x68. For groups of
two or three a preliminary random draw can reduce the number of entries it
avoids; larger groups avoid two. An absent table returns the supplied id.
`tools/emu/sound_hdr_group.py` checks these choices with scripted random
values, including retries. It hooks the RNG and does not test RNG quality or
the caller that updates the history.

## Starting a sound: Volume, Compound, Defer / Stop Inst

`FUN_004b9a00` (ecx row, edx unit, then delay, ...) makes an instance:
- Volume 0: nothing plays.
- **Compound** ≠ 0: `FUN_004b9760` looks for a live instance of the same
  group (record +0x60) that isn't stopping (instance +0x3d). Compound < 0
  takes any; otherwise one started (instance +0x34) at most Compound ticks
  ago. If there is one, no new instance is made: the play returns that
  one, with the new unit added to its unit list (+0x28). That list is
  what "on a unit" means below: the speech test (`FUN_004b9700`, 0x4b9721)
  and Defer / Stop Inst's same-unit search (`FUN_004b9690`) both ask
  `FUN_005169d0(instance +0x28, unit)`, a walk of the list, so a merged
  sound counts as playing on every unit that asked for it. 590 rows have
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

The sound tick (`FUN_00482c20`: `FUN_004e42e0`, `FUN_004dcaa0`,
`FUN_00482b40`, `FUN_004ba020`, `FUN_004df890`, then `DAT_007bc9bc` + 1
and `Sleep(0)`) has no clock of its own: `FUN_004df870` is only the
sound-on flag (`DAT_007c8c78`). `FUN_004ba020` steps the ducks once per
new `DAT_007bc9bc`, so they move once per call. In play the 40 ms frame
paces the calls; under the pause (menu.md "The pause") the client calls
it every pass of its message loop, whose `Sleep(10)` is skipped while
the window is active, so the menu's duck (20 steps of 5) runs out in 20
passes, a few ms on a fast machine.

Both are applied in `FUN_004dfc20`, on the instance's 0..255 volume
(+0x1c), in this order, each a whole percent with the product truncated
(`* 0x51eb851f, sar 5`): Music Vol rows x music / 100 (0x4dfc84), every
row x master / 100 (0x4dfca1), the menu (0x4dfcb2), Solo (0x4dfd0b). The
speaker-mode step and the distance falloff (from 0x4dfeb5) then work on
that volume. At 255 under menu 50 and Solo 70 that is 255 -> 127 -> 88,
not 255 x 0.5 x 0.7 = 89.25.

The master volume reaches nothing else. Its getter (`FUN_00514cc0`) has
five callers: the game menu's slider (`FUN_0047cdc0`), the speech box
(`FUN_004a0770`) and the two sound ticks (`FUN_004ba020` 0x4ba141,
`FUN_004dcaa0` 0x4dcaf8), which only test it for > 0, and `FUN_004dfc20`
above. The cinematics play through Bink on the game's DirectSound
(`_BinkSetSoundSystem` / `_BinkOpenDirectSound`), and game.exe imports
no Bink volume call (no `BinkSetVolume`, game-imports.tsv), so a video's
sound plays at Bink's own level whatever the master slider says.

d2d: rules/sound_start.hpp has the rules and the ducks; `ducked_mix`
ducks a world sound's volume before its distance mix, as above. Sounds
not placed in the world (UI, voice, ambience, music) carry a float gain
with their fades and take the ducks, master and music as multipliers,
not this chain's integer steps (a placed sound takes master in its
integer volume). audio.hpp keeps
a sound tick per 40 ms and, per channel, the group's first row and the
unit it plays on (`unit_key` in game/cues.hpp: the hero, a monster, an
object or a missile). Compound joins the playing instance and adds the
unit to its list (`Channel::merged`).

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
