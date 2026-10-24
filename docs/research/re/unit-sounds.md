# Unit sounds: swings and impacts (UnitSnd.cpp)

Traced 2026-10-21 from game.exe 1.14d (client side, `.\UNIT\UnitSnd.cpp`).

## The mode dispatcher, FUN_004cc5b0 (unit, mode)

A unit starting a mode plays its sound by unit type (FUN_004ca320):

- **Player**: DT (0), GH (4), KB (0x13) → FUN_004cc410 (struck);
  A1 / A2 (7 / 8) → FUN_004cb6a0 (the swing); BL (9) → FUN_004cb860;
  KK (0xc) a kick sound by class; S3 (0xf) a Barbarian's swing, S4 (0x10)
  an Assassin's.
- **Monster** (MonSounds.txt): DT (0), GH (3), KB (0xd) → FUN_004cc410;
  A1 / A2 (4 / 5) → FUN_004cb6a0 (Attack1/2 at its probability, Weapon1/2);
  S1..S4 → FUN_004cb890; DD (0xc) → FUN_004cb2c0. A mode MonSounds
  remaps (+0x7c / +0x84 / +0x8c with a negative +0x80 / +0x88 / +0x90)
  plays as the other.

FUN_004b9a00 (sound ECX, unit EDX, delay, ...) queues a Sounds.txt row,
`delay` ticks on (entry +0x34 = now + delay). The row's Group Size picks
a random member.

## The swing, FUN_004cb6a0

The player's weapon hit class (FUN_00623c20: the weapon's
`Weapons.txt hit class` via FUN_0062a180, none: 1 hand to hand) indexes
the 8-byte table at **0x727ed8** {sound, frames}:

| hit class | sound | frames |
|---|---|---|
| 1 hth | 251 weapon_punch | 6 |
| 2 1hss | 262 weapon_1hs_small | 6 |
| 3 1hsl | 268 weapon_1hs_large | 6 |
| 4 2hss | 274 weapon_2hs_small | 8 |
| 5 2hsl | 280 weapon_2hs_large | 8 |
| 6 1ht | 286 weapon_1ht | 6 |
| 7 2ht | 292 weapon_2ht | 8 |
| 8 club | 268 weapon_1hs_large | 6 |
| 9 staf | 298 weapon_staff | 6 |
| 0, 10..13 | none | |

Its delay (FUN_004ca500) is `(frames x 256 + 128) / speed` ticks, speed
being the animation's rate (unit +0x4c, 256 = a frame a tick; IAS in it).
A swing plays hit or miss.

## Struck, FUN_004cc410 → FUN_004ca560

The struck unit's hit class (unit +0xb0, the last hit's) gives two
sounds, both at once:

- **Impact**, the low nibble: 1 → 321 impact_punch; 2..5 → 341
  impact_blade_swing; 6, 7 → 335 impact_blade_thrust; 8, 9 → 329
  impact_blunt; 10, 11 → 347 impact_arrow; 12 → 325 impact_claw.
- **Element layer**, the high nibble (a skill's HitClass, OR-ed into the
  hit): 0x10 → 0x17b with an impact; 0x20 → 0x164 with one, else 0x161
  (impact_fire); 0x30 → 0x167 without one, else 0x16a unless dying (the
  mode passed is DT, 0); 0x40 → 0x174 / 0x170; 0x50 → 0x178 unless an
  impact and dying; 0x60 → 0x17e; 0x70 → 0x18b; 0x80 → 0x956; 0xb0 → 0x187.
- A monster whose MonStats2 flags have **bit 13, `critter`** (FUN_004638a0
  over the record at +0xa90, +4; checked under tools/emu: exactly the 10
  critters) plays neither. Zombies and Fallen (`soft`) do.
- Then a monster's MonSounds HitSound / DeathSound; a monster in state
  0x92 plays nothing (not ported).

The hit class: Weapons.txt `hit class` (hitclass.txt codes: hth 1, 1hss 2,
1hsl 3, 2hss 4, 2hsl 5, 1ht 6, 2ht 7, club 8, staf 9, bow 10, xbow 11,
claw 12, over 13), Missiles.txt HitClass for a missile, Skills.txt
HitClass (+0x134, a number) OR-ed in (Bash 112 = 0x70).

## Skills (Skills.cpp, client)

Skill record (`[0x744304]+0xb98`, 0x23c bytes): +0xfc stsound, +0xfe
stsoundclass, +0x100 dosound, +0x102 / +0x104 dosound a / b, +0x10a tgtsound,
+0x10e prgsound, +0x122 ItemCastSound; flags (+4): bit 13 stsounddelay, bit 14
weaponsnd.

- **Start** (FUN_004c6140, any unit starting a skill): stsound, or
  ItemCastSound when cast from an item; stsounddelay holds it back the swing's
  frames less 3 ticks (FUN_004ca500 − 3); weaponsnd plays the weapon's swing
  (the 0x727ed8 table) at the swing's delay; stsoundclass when the caster is a
  player of the skill's charclass (+0xc), unless an item cast has its own.
- **Do** (FUN_004c6680, its client do-function done): dosound on the caster,
  tgtsound on its target (FUN_004648f0), none without one.
- Not ported: dosound a / b (Jab, Strafe, Vine Attack: FUN_004c9b40 /
  FUN_004f4590), prgsound (charge-ups, Hurricane), monsters' own stsound.

## Missiles

Missiles.txt record (+0xb64, 0x1a4 bytes): +0x12 TravelSound, +0x14 HitSound,
+0x34 ProgSound. TravelSound plays on the new missile (FUN_004cd540), so a
looping one lasts its life; HitSound on it as it strikes (FUN_004d2d70).

## States

States.txt record (+0xbc, 0x3c bytes): +0x26 onsound plays on the unit as the
state comes on (FUN_004d9b20, S→C state packets), +0x28 offsound as it goes
(FUN_004d9c30).

## The player's own

Per class (0x72a008 → 11 dwords): hit, death, impossible, needmana, needkey,
cantcarry, cantuseyet, not_in_town, chat_a, the first quest line
(act1_complete_andariel), the first footstep (light / medium / heavy walk
dirt). FUN_004cc410 plays hit on GH / KB and death on DT, after the impact.

Unit sound events (server FUN_00553380(unit, id) → client FUN_004cb9c0):
0x13 impossible, 0x14 cantuseyet, 0x15 needmana, 0x16 needkey, 0x17 cantcarry,
0x18 not_in_town; 0x19..0x20 the chat lines (+0x20 − 0x32 + 2 id); 0x21.. a
quest line (+0x24 − 0x21 + id), some delayed (FUN_004dcd40). Quest
completions seen: 0x21 Andariel, 0x23 Den, 0x24 Tools, 0x25 Tower, 0x30 Cain,
0x30 Cain, 0x32 Radament, 0x33 Summoner, 0x34 Tainted Sun, 0x36 Tombs,
0x40..0x43 Act 3, 0x48 Jade Figurine found, 0x4a Izual, 0x4b Diablo. d2d now
plays the matching class cues for Cain's rescue (0x30), the Seven Tombs
aftermath (0x36), and Act 3 completions (0x40..0x43). Act 1's
`act1_find_tree` is event 0x2d when the Inifuss Tree gives the scroll;
`act1_find_cairn` is event 0x27, throttled to every 64th Cairn Stone use
without the scroll.

The other 23 classic `find` cues have sound-table entries and client event
handling, but no trigger in the 1.14d server executable. The server voice
dispatcher FUN_00553380 has 97 direct call sites; checking their immediate or
computed event arguments finds no calls for these IDs. The two generic quest
voice helpers also have no hidden callers: FUN_005455f0 is only used by the
Den quest callback, and FUN_00595420 only by the Countess callback. Thus the
assets cannot play through normal quest progression in this build. Adding
them would require inventing new trigger conditions rather than porting an
existing game.exe event.

| Act | Event IDs | Sound-table cues without a server trigger |
|---|---|---|
| 1 | 0x26, 0x28–0x2c, 0x2e–0x2f | burial, catacombs, den, jail, monastery, tower, Tristram, wilderness |
| 2 | 0x37–0x3c | Arcane, Claw Viper, Radament, Summoner, decoy tomb, true tomb |
| 3 | 0x44–0x47, 0x49 | beneath, blackened, drain, Guardian, Lam Esen |
| 5 | 0x4c–0x4f | wilderness, Nihlathak, mountaintop, Worldstone |

The Act 3 Jade event 0x48 is already emitted as the Jade Figurine find cue;
it is not part of the unsupported set. The names above are the class-specific
`find` rows resolved from the client mapping
`class voice table + 0x24 − 0x21 + id`.

## Footsteps (FUN_004caf60 → FUN_004caeb0)

A walking / running unit plays a step on the tick whose animation position
(unit +0x44, 256ths of a frame; +0x4c a tick; +0x48 its length) is nearer a
footfall than the ticks either side (FUN_004e4180's distance round the
period); footfalls FsOff + k × length / FsCnt (players: 2 a cycle, from 0),
each at least ⅔ of a step after the last (+0x84). The row: the first
(players' class table +0x28; monsters' MonSounds Footstep) + 4 × (material −
1) + 24 running. Material (FUN_004cadb0): the floor tile under the unit, its
DT1 material flags 0x20 → 1 dirt, 0x08 → 2 indoor stone, 0x10 → 3 outdoor
stone, 0x40 → 4 sand, 0x400 → 5 snow, 0x80 → 6 wood; none, the level's
SoundEnviron Material 1. A monster's FootstepLayer plays with it at FsPrb %.

## Monsters (MonSounds.txt, 0x96bc5c)

Record: +4 Attack1, +8 Att1Del, +0xc Att1Prb, +0x10 Weapon1, +0x14 Wea1Del,
+0x1c.. the same for 2, +0x34 HitSound, +0x38 HitDelay, +0x3c DeathSound,
+0x40 DeaDelay, +0x44..+0x50 Skill1..4, +0x54 Footstep, +0x58 FootstepLayer,
+0x5c FsCnt, +0x60 FsOff, +0x64 FsPrb, +0x68 Neutral, +0x6c NeuTime, +0x70
Init, +0x74 Taunt, +0x78 Flee, +0x7c.. CvtMo / CvtSk / CvtTgt.

- S1..S4 play Skill1..4 (FUN_004cb890); GH and KB the hit sound.
- **Init** as the monster first shows, outside town (FUN_004cc380).
- **Neutral** (FUN_004cb460), standing, walking or running: a 1-in-15 roll
  (the client's seed) when it's 30 + rand(90) ticks after any monster's last
  neutral, NeuTime after its own last mode sound (+0x7c) and neutral (+0x80),
  outside town, and within dx² + (2dy)² < 490000 client pixels of the player
  (about 31 subtiles, FUN_004b9e10).
- Dying stops its own playing sounds (FUN_004cb2c0); attacking fades its
  neutral (FUN_004cb220). Not ported.
- Taunt and Flee arrive as S→C unit sound events (FUN_004cbde0 0x10 / 0x11)
  the AI sends. d2d plays each MonSounds Taunt on S2 and the Fallen's Flee
  when it retreats after a nearby unit dies.

## Objects (0x7295f8)

By objects.txt Id: 8 sounds, one as the object enters each mode
(FUN_004cb380; not as it first shows), and up to two looping rows each tied to
a mode (8 any; FUN_004cb3e0), replayed while it's in sight and in that mode.
Object 0x3d's loops are by mode at 0x728338. The 13 objects sharing record
0x729328 play only going to a later mode. Dumped whole into
`components/rules/object_sounds.hpp`.

## Items in the panels (client, UI\inv.cpp)

The item record (misc / armor / weapons.txt): dropsound +0x124,
usesound +0x126 (words, Sounds.txt rows), dropsfxframe +0x128.
- `FUN_004c1d60` (drop) / `FUN_004c17f0` (use, via `FUN_004c1e20`): the
  base's sound, then UniqueItems (quality 7: dropsound +0x84, usesound
  +0x86) or SetItems (quality 5: +0x82, +0x84) instead. A row counts only
  when its dropsound is > 0, for the usesound too.
- Use sound: as the cursor takes an item from a grid (`FUN_00487740`) or
  the belt (`FUN_00498a90`, no shift).
- Drop sound: as one goes into a body slot (`FUN_00490780` / `00490ba0`
  hands, `FUN_00490fc0` and others), the belt (`FUN_00498870`) or the
  merc's slots (`FUN_0048b7c0`). Taking a worn item off plays nothing there.
- Refused in a body slot: `FUN_0063de60` says no; if it would fit ignoring
  requirements (flag 1) and the item's class is none (7) or the player's,
  voice 0x14 cantuseyet, else 0x13 impossible (`FUN_004cb9c0`). The merc
  refused: `FUN_004cbde0(0x55)`, hireable female 0x1204 / male 0x1208
  "I can't use that" (monstats 0x10f, the rogue, is the female).
- Requirements `FUN_0062eaf0(item, unit, …, &str, &dex, &lvl)`: str/dex
  from ItemsTxt +0x10a / +0x10c, less stat 91's percent, −10 each
  ethereal; level against `FUN_0062b5b0` (the hover's Required Level);
  false for an unidentified item (flag 0x10) and a wrong class.

## NPC greetings (FUN_004e0590)

By MonStats class (FUN_004e0370's records from 0x72ae28, 0x18 bytes):
greeting, inactive, the first of three time-of-day lines, return; +0x10 the
last played, +0x14 when. Kind 2 plays the return line; else the greeting, kind
0 its inactive line in 1 of 2 (rand(2)), and the time-of-day line (Day phase
1 → first, 2–3 → second, else third) in 1 of 3 or when none was picked; the
group member drawn (FUN_00482680), redrawn up to 20 times while it's the last
one. Kind 1 is the town NPC's think greeting a player near it (S→C unit sound
0x12, on the player); kind 0 / 2 as its menu opens (FUN_004b66b0, from the
talk record's flags at 0x726850).

## Distance (FUN_004b9a00, FUN_004ba020, FUN_004dfc20)

A sound started on a unit (`FUN_004b9a00`, unit given) keeps its offset from
the player in client pixels (`FUN_004b97d0`: dx, 2 dy and 640 up; units by
`FUN_00620900`, path +8 / +0xc; a d2d cell is 80 px of dx and of 2 dy). Its
record's Falloff (+0x47, Sounds.txt) picks a near and a far range:

| Falloff | near (`FUN_004825b0`) | far (`FUN_00482610`) |
|---|---|---|
| 0 | 60 | 400 |
| 1, other | 60 | 700 |
| 2 | 200 | 1000 |
| 3 | 400 | 1500 |
| 4 | 2000 | 2000 |

Every sound tick (`FUN_004ba020`) it isn't started, or is stopped
(`FUN_004df7b0`), once dx² + (2 dy)² (each clamped to ±2000) passes far².
Tracking rows (Sounds.txt Tracking, a unit list at +0x28) take their unit's
offset again each tick; others keep the one they started with. The volume
(`FUN_004dfc20`, 0..255): 255 within near, (far − d) × 255 / (far − near)
past it (d = √(dx² + 4dy²)), times Volume / 255, times `FUN_00516830`'s:
r = √(x² + y² + 2²) for (dx, 2 dy) / 320, capped at 100, and
255 / 10^(log2(r / 2) × 6 / 20), 6 dB down a doubling. Its pan is
dx × 127 / 400 + 128, clamped to 0..255. Both are amplitudes:
`FUN_005165f0` (lo, hi, v) is 2000 log10(v / hi) hundredths of a dB
(−10000 at lo, 0 at hi), so SetVolume is volume / 255 and `FUN_00515890`'s
SetPan leaves the near speaker whole and turns the far one down to
(255 − pan) / 127 (right of the middle) or pan / 127 (left).
`tools/emu/sound_falloff.py` runs `FUN_00516830` and `FUN_005165f0`;
`tests/test_sound_mix.cpp` holds the answers (`rules/sound_mix.hpp`). d2d
plays a world sound as two sources, one silent side each, straight to the
speakers (`AL_SOFT_direct_channels`), at those two amplitudes.

The level's ambient events (`FUN_004e42e0`) are placed the same way, kept
where they start: `FUN_004b99a0` (x, y, 0) → (x, y, 0 + 640) with x =
±(450 + rand(301)) (the sign rand(2) first) and y = rand(201) − 100, both as
`FUN_004b98f0` takes them (y is already 2 dy).

An object's loop starts as it enters a mode (`FUN_004cb460` returns at once
while its mode is unchanged). Stopped past its far range (state 2 in
`FUN_004ba020`), a sound whose record loops (+0x3e), has no Duration (+0x43)
and wasn't told to stop (+0x3d) goes back to waiting (state 0) instead of
being freed, and starts again the first tick it's back in range: walking
back to a torch brings its crackle back. (Record offsets: the Sounds.txt
field table in `FUN_00481950`: Volume +0x3c, Loop +0x3e, Duration +0x43,
Compound +0x45, Falloff +0x47.) A local game paused under the game menu
keeps playing sounds: `FUN_0044efa0` still runs the sound tick
`FUN_00482c20`.

## In d2d

`tools/sound_coverage.py` lists the base install's sounds d2d can't play yet
(3,346 / 3,520 currently covered). Energy Shield, Barbarian Leap, restricted
town skill attempts, and successful life / mana leech now cue their sounds.
Missiles.txt ProgSound follows the original animation-frame trigger
(FUN_004d39c0: AnimLen - 2). Several traced quest completion and object
interactions also dispatch their class or object cue at the server-side event.
The remaining 174 rows split into 125 class-quest phrases, five desert night
ambience variants, and 44 other rows. The class phrases have no matching
server-event dispatch in stock 1.14d; the server xref audit identifies the
missing event IDs. `event_desert_night_1..5` are not selected by any
SoundEnviron row (the desert night rows point at the daytime event group).
The remaining quest music, hireling, NPC, monster, and scene rows still need
their original triggers traced before wiring; the coverage report names each
family. The two shrine sounds are covered by their shrine activation cues;
the audit now counts code cues even when a same-named overlay/state field also
appears in the tables.
`rules::sound_ids` (kSwing, impact, hit_class), `Fighter::hit_class` →
`Blow::hit_class` (+ `Swing::hit_class`, a missile's row) → `Monster::hit_class`
in `Fight::land`; `Fight::monster_sounds` plays the impact on GH / KB / DT,
`Fight::set_pmode` the swing, the skill's start sounds and the player's cry
and impact (`Hero::struck_class`, a melee attacker's MonStats2 HitClass),
`Fight::do_sounds` the do. Missiles: a `Missile::sound` id each, its
TravelSound cued with it as owner, stopped by the client when it's gone from
the View (`Audio::stop_owners`). States: `StateClock` in `dress()`. Footsteps:
`World::footstep_frame`, `Fight::monster_sounds`. Objects: the client's
`Town::object_sounds`. Greetings: `World::greet_sound`. Sound rolls use
`Fight::sound_rng`, apart from the game's.
Left: the player's quest lines, monsters' own skill stsound, dosound a / b,
prgsound, ProgSound, block (FUN_004cb860) and kick sounds, state 0x92, a
missile's hit class on a player, the town's floor material (its DS1 tiles
aren't in `room_floors`: dirt). Cursor item sounds (identify, repair, break,
point-drop and convert) are cued at their successful actions; only the
convert-ready cue remains.

Player attack modes follow the Skills.txt `anim` token: S3 (the Barbarian's
Left Hand Swing and the Druid's Rabies / Hunger) and S4 are attack modes too,
so their attack timing and sound dispatcher run as for A1 / KK / S1.
