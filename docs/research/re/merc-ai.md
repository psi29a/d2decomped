# The merc's AI (MonAI 61 "Hireable")

Every hireling row in MonStats (roguehire 0x10f, act2hire 0x152, act3hire
0x167, act5hire1 / 2 0x230 / 0x231) has AI `Hireable`, MonAI row 61: AI
table `0x73ca18 + 61*16` = `{0, no init, think 0x5e52d0, other think
0x5e5280}`. The other think (Diablo's, monster-ai.md "Curse thinks and
other thinks") clears state 0xc, toggles the AI back and stands 1 frame.

Port: `components/rules/merc.hpp` (`hireable_think`), run by
`Fight::merc_turn`. Checked: `tools/emu/merc.py` runs FUN_005e52d0 natively
on random setups against the port (30,000 cases: every move tried, stand,
teleport, attack hand-off and the merc's seed after, all equal);
`tests/monsters/test_monster_ai.cpp` replays 16 of them.

## The think (FUN_005e52d0)

Calls: `FUN_005e52d0(ECX game, EDX merc, [8] AI params)`. The owner is
`FUN_0058f0d0` (monster data +0x28 → AI control, +0x2c the owner's id); none,
or not a player: it stands 10.

1. In a walk or run (mode 2 / 0xf) it does nothing.
2. Its distance to the owner, `FUN_005dc380`: per axis the gap less the
   merc's size (`FUN_00620510`, not under 0), then (short + 2 × long) / 2.
   The thresholds are 16 and 24 (and a wander of 5), unless the AI control's
   +0x14 is 0x11..0x13, when they're v and 2v (and v / 2). +0x14 is also the
   attack think's skill chance (0, +10 per miss), so only a command could put
   it there.
3. Over 100: `FUN_005e3930` mode 3, a teleport. Over 24: mode 1, run, pace
   60. Over 16 with the owner walking (2) or walking in town (6): mode 0;
   running (3): mode 0 at pace 60.
4. Not standing (mode 1): it drops its events and stands 5.
5. On collision bit 0x40 (`FUN_0064d910` at its own spot, mask 0x40): 6 in
   128 (`FUN_00472210`, 12 for all but 0x152 / 0x230 / 0x231) it wanders 5
   (`FUN_005de200`).
6. Outside town (`FUN_0061ab00`), a target from `FUN_005ddc30` under 25 off:
   the attack think `FUN_005e5050`.
7. On bit 0x40: wander 5. In another spawn area than the owner (`FUN_0061b130`, area ids
   at both spots, which carry across rooms and levels): mode 0, pace 60.
8. Within 1 of the owner: path type 7 for the next move (`FUN_005de190`
   with EDX 7), to a spot 4 about the owner (`FUN_005df530`); else 4 away on
   each axis (`FUN_005defe0`); else steps 0x28 to the owner's path end
   (`FUN_005ded90`).
9. Else 5 in 100 (`FUN_0045c390`) a spot 16 about the owner; else it stands 5.

All rolls are on the merc's seed (unit +0x20). A spot "n about" a unit
(`FUN_005df530` about the owner; `FUN_005de200`, `FUN_005df400` about
itself): one step of the seed picks the axis n off (low bit 0: y), the other
is rand(n) (`FUN_0045c3e0`); two more steps flip x, then y.

## Following (FUN_005e3930)

`FUN_005e3930(ECX game, EDX owner, [8] merc, [c] mode, [10] run, [14]
pace, [18] reach)`. A move's ctx is set by `FUN_005de190(unit, EDX type,
pace, steps)` → `FUN_005a6260`: path type, pace % (velocitypercent for the
next mode) and steps (cap 0x4d), each only when not 0; the next
`FUN_005a7c20` uses and clears them. (`FUN_005de190` inlines the same
lookup `FUN_005a6190`/`GetAiParamFromUnit(unit)` does — not a call to
it: the unit's `pUnitDataMonster->pPathParamOrName.pPathParam`, null
unless the unit is `UNIT_MONSTER` with monster data. Mode 1's "once"
extra call below calls `GetAiParamFromUnit` itself and feeds
`FUN_005a6260`/`SetCombatAndMonStats` directly, skipping `FUN_005de190`.
`GetAiParamFromUnit` has exactly two callers in game.exe: here (mode 1's
extra call) and the pet's `FUN_005e3ea0` follow, `pet-ai.md`
"FUN_005e3ea0 (after the owner)".)

- **Mode 0.** The owner's heading: `direction64` from him to his path end
  (path +0x18 / +0x1a), into 8 (`DAT_00745600`). Round the compass from
  there, a spot 8 off his path end (`DAT_006e34f0` → `DAT_006ea998` /
  `DAT_006ea978`: S, SW, W, NW, N, NE, E, SE in level subtiles) on the level
  he stands on, steps 0x28 at the pace: the first that finds a path. None:
  wander 4.
- **Mode 1.** With the owner walking (mode 2): steps 100 to his path end.
  Else his footsteps, newest first (player data +0xa0 the cursor, +0xa8 20
  spots of 8 bytes): the first over 5 off (`FUN_005dc5c0`), walking at the
  pace, steps 100; then as the mode asks with path type 0xf (`FUN_005dede0`,
  a walk in state 0x3c, Decrepify: traced against stock 1.14d,
  `tools/emu/ai_merc_decrepify_probe.py`; `rules::MercView::slowed` — the
  same decrepify-gated Run/Walk pattern as the pet's `FUN_005deeb0`,
  `pet-ai.md` "FUN_005e3ea0 (after the owner)"); then, once, with type 1.
  Twenty tried: a wander of a quarter of the gap (4 at least), pace 15.
- **Mode 3.** A spot in the owner's room (`FUN_0054dc40`: up to 20 tries
  inside the room's rect less a subtile each side, `FUN_0054dac0`, on the
  room's seed +0x6c, where MonStats class 0x16b fits, `FUN_005b2f20`), the
  merc put there (`FUN_00554ea0`), standing 5.
- Modes 2 (wander), 4 and 5 aren't reached from the think.

## Its walk %

A hireling is a monster unit: FUN_00555230's type-1 case (0x555393..0x5553a0)
runs FUN_00574250, whose stat init FUN_00573cb0 sets velocitypercent (0x43) to
75 (0x573f14..0x573f1a); the hireling's level stats (FUN_00572840) leave it.
Its walk and run go at MonStats Velocity x max(0x43 + its think's pace + its
gear's faster run/walk (stat 96, 150 v / (150 + v)), 25) % (FUN_00623f50).
d2d: `Fight::merc_turn`; V: `tools/emu/walk_velocity.py` (merc cases, with
the gear's stat 96).

## The footsteps

A player's data +0xa0 (cursor) / +0xa8 (20 spots). `FUN_00580c20`, the walk
and run update: more than 25 ms (GetTickCount) after the last, when the
player is over 45 (squared subtiles) from the newest spot, it's written and
the cursor moves on. `FUN_00554ea0`, a unit put somewhere (a warp), writes
the arrival.

## The attack think (FUN_005e5050)

`FUN_005e5050(ECX game, EDX merc, class, owner, target, seed, params)`.
Port: `rules::merc_attack` (with the skill pick), checked by
`tools/emu/merc_attack.py` (FUN_005e5050 and FUN_005e4d30 natively on
game.exe's own tables: 15,000 cases equal, every outcome, +0x14 and the
seed); `tests/monsters/test_monster_ai.cpp` replays 16.

- The skill chance: 98 for the melee mercs (0x152, 0x230, 0x231); else the
  AI control's +0x14 + 40 + 2 × the merc's level, at most 95 (0x5f). A
  rand(100) on its seed at or over the chance: +0x14 += 10, no skill; under:
  +0x14 = 0, a skill.
- aip1 (MonStats +0x56 by difficulty) 0 — the shooters: under 4 off, half
  the time (rand(100) < 50) it backs off 4 from the foe (`FUN_005df530`,
  else `FUN_005defe0`); else, with the skill, `FUN_005e4d30`.
- aip1 not 0: over 2 off or not in melee (`FUN_00622c40`) it walks at the
  foe, stopping MonStats2 +0xe off (`FUN_005ded00`); in melee with the
  skill, `FUN_005e4d30`.
- Else it stands 10.

## Its skills (FUN_005e4d30)

The merc's hireling.txt row at its level (`FUN_006562f0`, row +0x1c Level).
A table: DefaultChance, then per Skill1..6 it has (`FUN_006442a0` > 0) that
isn't an aura already on (+0x230 1 and its state set) the running sum of
Chance + ChancePerLvl × (level − Level) / 4 (skill 0x29 only within its
level / 2 + 4 of the foe). A rand(sum + 1) on its seed: at DefaultChance or
over, the first skill whose sum covers it — an aura (Skills.txt +4 & flag)
starts (`FUN_005701b0`), else `FUN_005dead0(Mode, skill, foe)`. Under:
roguehire fires MonStats Skill1 in Sk1mode; act2hire (and act3hire, the
Act 5 mercs) swing A1 in melee (`FUN_005ddf90(4)`); else it stands 10.
`FUN_005dead0` accepts any mode byte below 16 and passes it to the mode start
without an animation lookup (0x5dead3..0x5deb33). d2d likewise uses the row's
mode; its A1 substitution only supplies timing when that COF is absent.

hireling.txt's columns: DefaultChance, Skill1..6, Mode1..6, Chance1..6,
ChancePerLvl1..6, Level1..6, LvlPerLvl1..6 (Rogue Scout, Fire: 75; Inner
Sight 10; Fire Arrow 25 + 8 a level).

## Its skill levels (FUN_00572840, the level-up)

The hireling row at its level: per Skill1..6 (a 0 or a Mode over 15 ends
them), from the skill's reqlevel on, Level + (LvlPerLvl × (level − Level)
>> 5), 1..32 (`rules::merc_skill_levels`). The same function sets its
stats: Str / Dex + (per level × d >> 3) (10 at least), life (HP + HP/Lvl ×
d, 40 at least), defence, AR, damage (+ Dmg/Lvl × d >> 3) and all four
resistances Resist + (Resist/Lvl × d >> 2). It sets the base max life
(stat 7, 0x5729d1) and then the life (stat 6, 0x5729dc) to the same value:
a merc whose level a kill raises (`FUN_0057e860`) has its new level-up
life, its items' max life left out, and one above that loses the rest
(d2d `Fight::merc_share`; `tools/emu/merc_levelup.py`).

## Its life regeneration

The level-up (`FUN_00572840`; its callers: the save's merc `FUN_00533c70`,
`FUN_0056aa50`, `FUN_00573270` and the experience `FUN_0057e860`) also sets
stat 0x4a to the base stat 7 it has just set, in 256ths, / 2000, 0 at least
(0x572ac2..0x572af2): its level-up life, not its items'. A Rogue at level 1
(40 life) gets 5 256ths a frame. A merc is unit type 1, so its unit event 3
is a monster's (`FUN_005417d0` files it in list 1, `FUN_005a7f80` runs
table 0x6e2490[3]): `FUN_005a6920` (combat.md "Regeneration"). Once a
frame it takes the total stat 0x4a (`FUN_00625480`: with its items'
Replenish Life, stat 74), files itself for the next frame; a total of 0
removes it; a negative total under one point of life does nothing in a
town room or with no room (`FUN_00620bb0`, `FUN_0061ab00`); else life
(stat 6) += it: over the max life (`FUN_00625d10`) the max, and the event
removed; under 1 it's 0 and, out of mode 0 / 12, the kill (`FUN_0057ccb0`).
Nothing files the event when the merc is made. The damage path does when a
hurt leaves a monster-type unit alive with a total stat 0x4a (`FUN_0057c6c0`,
0x57c8fd..0x57c92f), a poison does (`FUN_0057ac50`), and so does a healing
potion's state on a non-player (`FUN_005be3f0`, 0x5be62b..0x5be659): so the
merc regenerates from a hurt, poison or potion until it's full again. A
poison's −rate is stat 0x4a in state 2's own list, on top of this, and a
healing potion's rate is stat 0x4a in its own state's list (0x5be559),
filing the event only when that list is new (0x5be5e2).
`tools/emu/merc_regen.py` runs `FUN_00572840` and `FUN_005a6920` against
`test_merc_regen --merc-regen`, from a filed event.

d2d: `rules::merc_life_regen`, the merc's `stats.regen` (set while the event
is filed) by `Fight::hurt_side` on a hurt or a poison and by
`Fight::merc_drinks` on a healing potion (again at a level-up,
`merc_level_stats`), added with its items' Replenish Life once a frame by
`Fight::apply_regen`, which ends it as `FUN_005a6920` does.
ponytail: the event's frame isn't kept: a hurt re-files it for the next
frame, so each frame with a hurt may cost game.exe one add (by the order of
the event and the damage in a frame, not traced), and the first comes the
frame after the first hurt; d2d adds in every frame from the hurt's own.
The merc's poison and healing potion stay their own rates
(merc_poison_rate, merc_heal_rate), not parts of stat 0x4a; d2d files on
every timed drink. A state of the States group at datatables +0x14c (`FUN_0063a750`,
which states not traced) takes the level-up's part off, and state 0x34
holds back a gain and ends the event: neither is modelled. Burning
(`FUN_0057add0`, state 0x73, 0x57ae28) files it too, and d2d's merc takes no
burning; the barrel's and a shrine's damage (World) don't start it;
`FUN_005beac0` (rejuvenation) files it only for a timed potion, and
Misc.txt's rvs / rvl have no length; the filers at 0x54d11f, 0x534b02 and
0x54fba7 aren't read (the 0x55xxxx / 0x56xxxx ones look like item timers).

## Its gear

The save's "jf" list (after the corpse list): a hired merc's "JM" item list,
what it wears (head, torso, the hands); `d2s::parse_merc_items`. Its items'
stats join its base as a unit's do, read as game.exe reads any unit's:

- damage (FUN_0057b420): a two-handed weapon reads stats 0x17 / 0x18 (its
  base damage there, the bow's or polearm's on top); a one-handed weapon or
  none 0x15 / 0x16, which hold none of its base (1 / 2 at least: an Act 3
  merc's sword hits for its own damage only);
- attack rating (a monster's, combat.md): stat 19 + dex × 5, × (100 + 119) %;
- defence (FUN_006223f0): stat 31 + dex / 4, + skill_armor_percent;
- resistances: base + items', the penalty, −100 .. 75 + max-resist (95);
- life: the level-up's + its items' maxhp (7) and hp per level (216, an
  eighth a level), with their maxhp percent (76) on the level-up's;
- block (FUN_00622720, a monster's): stat 20 (MonStats ToBlock, blank for
  every hireling row, so its shield's block and its items' to-block), 75 at
  most, only with a shield (FUN_006225f0: classes 0xf3 / 0x136 / 0x14d
  always, act3hire 0x167 never, the rest a shield worn). No merc but the
  Iron Wolf can hold a shield, so in 1.14d a merc never blocks.

A hireling's base stats come from MonStats' Normal columns whatever the
difficulty (FUN_00573cb0 forces the column when FUN_0063ee90 names it):
ToBlock, ResDm, ResMa are blank for all five.

## In d2d

`Fight::merc_turn` runs the think, then on an attack the attack think: a
move (WL, or RN at the foe), a skill in its hireling Mode (or MonStats
Sk1mode for roguehire's Skill1), a swing, a stand. On the mode's action
frame the skill goes through `Fight::fire` / `spot` with a merc
`Caster`: its missiles strike with the merc's weapon damage, attack rating
and level and no synergies (Fire Arrow, Cold Arrow, the Act 3 bolts and
balls); Inner Sight puts its state about the merc; a melee skill (Jab, Bash,
Stun) builds its hit as the player's do (`Fight::skill_swing`) at its level.
Its four resistances are on its fighter less the difficulty's penalty
(FUN_0057be00 takes it off a player and a hireling alike: its ctx +0x14 is
set only for a monster outside FUN_0063ee90's five hireling classes;
DifficultyLevels ResistPenalty, −100..75). An SQ skill plays its MonStats
slot's MonSeq sequence (Jab: seq_act2guardjab, 14 frames of A1, hits on
frames 5 and 9). A self cast (Frozen Armor, do 18) puts its state on the
merc for auralen: skill_armor_percent on its defence, its melee strikers
frozen calc1 frames, the pick passing it over while it runs.

## Its target (FUN_005ddc30)

The skill-set target (FUN_005dd610) first, else FUN_005dd0b0 mode 6:
FUN_005dce60 walks every near room's units (mode 5's FUN_005dcf70 skips
town and unsearched rooms), FUN_005dcbd0 each: an enemy (FUN_005dc970)
under 0x31 off (`near_distance`, the candidate's size), in sight
(FUN_00622aa0 mask 4, always), threat 2 or more the primary, else the
secondary, the nearest each (the first at a tie). FUN_005dc970 passes over
a monster in state 0x92 (invis) within melee (a player in it 80 % of the
time unless in melee). FUN_005dd510 (a monster searching): no secondary,
the primary; no primary, the secondary; the secondary under 6 off and no
type 2 (toward) path to the primary: mode 7 (FUN_005dcc60, the nearest
other threat-2 enemy in sight under 0x31) when within 0x13, else the
secondary; else the primary. Port: `rules::search_sight`, `sight_choice`;
the candidates the merc's room's near list (`near_rooms`), each room's
monsters newest first. Checked by `tools/emu/search.py`'s mode 6 run
(FUN_005ddc30 natively, the searcher's path count random: 30,000 cases
equal, 360 through the path test; the invis rule is read, not run).

## Its auras

A friendly aura it starts (do 65, `FUN_005cf010`: Act 2's Prayer, Defiance,
Blessed Aim, Thorns, Might) puts its aurastats on itself and on the player
within its aurarangecalc subtiles of it, at its level for the skill; the
player's friendly aura and a totem's go on the merc the same way. Prayer's
hitpoints (edns, 256ths) heal the player and the merc in range every
perdelay frames. Its Holy Freeze (do 81) is an enemy aura as the
player's: its aurastats (velocitypercent, attackrate / other_animrate at
-dm34) on the monsters within its range, its cold on them each perdelay
(struck by the merc). d2d: `Fight::friendly_auras`, `enemy_auras`,
`prayer_pulse`, `enemy_pulse`.

## Not built yet

- Vitality on its items gives no life (a hireling has no CharStats row;
  not traced further); d2d keeps no merc mana.
