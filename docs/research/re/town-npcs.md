# Town NPCs walking — game.exe 1.14d

The Rogue Encampment's vendors (Akara 148, Charsi 154, Gheed 147, Kashya
150, Warriv 155) have MonStats AI `Npc` (MonAI 32). Its row in the AI
table (0x73ca18 + 32 × 16) is `{targetMode 0, no init, think
FUN_005e7130}`: no target search, so the driver FUN_005b1740 goes straight
to the think. They're the only Act 1 units the camp's DS1 gives a path:
every other camp monster (the guard rogues, cows) is AI Idle and has none.
aidel is 15 for all of them.

## The path

FUN_00555910 makes a preset monster and hands the preset's path (preset
+0x10) to AI control +0x38 (FUN_0058f000 / FUN_00666120): `{count,
entries}`, each entry 12 bytes `{action, x, y}`, x and y absolute subtiles.
The action is the DS1 path point's (v15+).

## The unit seed before the first think

The unit seed (+0x20) is `{game seed step, 666}` (FUN_00552df0,
town-start.md). Monster init then takes two draws: the look (FUN_005739d0,
inline) and the life (FUN_00573cb0 → FUN_0045c3e0). So an NPC's first think
sees its seed two steps on. (A guard rogue takes up to 11; nothing reads
them.)

## The first think

The spawn files it. A preset monster (DS1 mode 1, NU: drlg.md "Preset
units") comes through FUN_0054e600 → FUN_0054e490 → FUN_005b2f20 →
FUN_005b2a00, which calls FUN_00555230 with flags 1 (3 with spawn flag
0x20). Its type 1 case (0x555393) sets the mode (+0x10) and runs the
monster init (FUN_00574250). Setting NU files the think aidel (15)
frames on (FUN_005a73e0, 0x5a7482). Flag 1 then puts the unit in its room
(FUN_00554850, 0x55545d). A room with a client (FUN_00553160: room +0x78,
its client count, non-zero) goes to FUN_00573780; one without goes to
FUN_00553220 and keeps the aidel. FUN_00573780 drops the unit's event 2s
(FUN_00540e60) and, in mode NU, files event 2 at game frame (+0xa8) + 2
(FUN_005417d0, 0x5737c0). Modes 0 and 12 (DT, DD) file nothing. Other
modes file only in levels 0x6e, 0x76, 0x88 and 0xf7 (also + 2). Nothing
later in the spawn (FUN_00547d90, FUN_00552d60, FUN_005543b0,
FUN_005b21b0, FUN_005b1cf0, FUN_005b2830) files another.

A room's first client (FUN_0053a8e0: FUN_0061a660 adds it, +0x78 counted
up at 0x61a6ef) then sends every monster in its unit list (+0x74, next
+0xe8) to FUN_00573780 when +0x78 is under 2 (0x53a93c). So the first
think, the Npc think's home and stand 20 (FUN_005e6800), runs 2 frames
after the NPC's room comes into play, whether the NPC was made before
then (aidel replaced) or in a room already in play. d2d:
`file_first_think` files it 2 frames on (`UnitState::filed`). `World::tick`
files the states its rooms grow in a frame (`rooms_up`, then
`grow_states`) from that frame. `npc_patrol` files any other state the
first time it meets it, which is when its level comes into play. Checked:
`tools/emu/npc_first_think.py` (the camp, seeds 1 and 7: every pathed
NPC made 15, in play 2), `tests/game/test_npc_first_think.cpp`.

## The AI's commands

AI control +0x20 is a circular list of commands, found by type
(FUN_0058eef0) or made (FUN_0058efa0). The Npc think uses three:

| type | fields (+0xc ..) | made by |
|---|---|---|
| 10 | home x, y | the first think (FUN_005e6800) |
| 4 | x, y, thinks left, stand frames | the path actions (FUN_005e6de0 ...) |
| 7 | mode, x, y, walks left | path actions 4 and 5 |

## The think — FUN_005e7130 (ECX game, EDX unit, stack the driver's block)

1. FUN_005e6800: command 10 with no home yet: home = the unit's spot,
   stand 20 (FUN_005de080: the next think 20 frames on).
2. Classes 0x109, 0x200, 0xff, 0xc9, 0xfe have their own steps first
   (Cain in Act 5, Larzuk, Ormus, Jerhyn, Alkor): none in Act 1.
   - 0x109 (Cain, A1Q4): `Q04_GetCainPortalCoordinates`; over 2 off, walk
     there; else `Q04_SetTristramActiveFlag`.
   - 0x200 (A5Q4): `Q34_NpcSpawnPortalInAct5`, then falls through to the
     commands below either way.
   - 0xff (Ormus, the A3Q3 serpent cage): `Q20_GetSerpentLocation`; over 3
     off, walk there; else Skill1 (8) at it and `Q20_ActivateSerpentObject`,
     then returns (no commands this think). No location: falls through.
   - 0xc9 (Jerhyn, A2Q4/A2Q7): `QUEST_ShouldBlockJerhyn` (`FUN_0059f570`, a
     15-byte `BOOL` wrapper over `QUEST_CheckJerhynShouldBlock`) gates it:
     clear → stand 0x28 (40), return. Set → `QUEST_JerhynNpcAILogic`
     (`FUN_0059f580`, 668 bytes): bails (`*pbRunDefaultAi = 1,
     *pbDelayNextMove = 0`) unless the Arcane Sanctuary quest has started
     and its A2Q4 data's `bShouldSpawnJerhyn` is set, and both Arcane
     Sanctuary and Seven Tombs are short of state 0xd with Seven Tombs
     actually under way (`Q14_IsSevenTombsNotStarted` false). With all of
     that and `bJerhynRelocated` still 0: once `Q14_IsSevenTombsAtState1`
     fires `bJerhynWalkPending`, it scans every player and monster for
     Duriel proximity (`IteratePlayerOrMonsterUnits` +
     `Q14_CheckDurielProximity`) and walks Jerhyn to the harem-blocker spot
     (`WalkToCoords`) once seen, else just delays (`*pbDelayNextMove = 1`,
     no walk); once the walk's been issued and he's within 2 of the
     blocker spot, it finds a free spot by the Duriel-portal object
     (`GetFreeCoordinates_WithNeighboorRooms`, shapes 1 then 2 then 3) and
     relocates him there (`SUNIT_RelocateUnit`), setting
     `bJerhynInitial`/`bJerhynRelocated`. Back in the think: a nonzero
     `*pbDelayNextMove` reschedules at 0x14 (20) frames regardless; a zero
     `*pbRunDefaultAi` (the walk/relocate still in progress) returns right
     there. Otherwise (nothing pending) it checks `Q12_CheckJerhynGateOpen`:
     open, with the Jerhyn-gateway command (kind 10) already up, sets its
     `nCmdArg[1]`/`[2]` to the unit's own X + 9 / Y and reschedules at 0x32
     (50); closed, or no such command, falls through to the commands below.
   - 0xfe (Alkor, the A3Q5 golden bird): while `Q22_IsGoldenBirdQuestActive`,
     Skill1 (8) at it, then `Q22_ClearGoldenBirdQuestFlag`, and returns;
     else falls through.
3. FUN_005e68f0, the visitor (below): returns 0 with no player to heed
   and nobody talking, so the NPC goes on with its commands.
4. FUN_005e6ae0, the commands:
   - Command 4 with thinks left: one off; over 3 from its spot
     (FUN_005dc5c0: the larger axis gap plus half the smaller) walk there
     (FUN_005def30), else stand its frames (10).
   - Command 5 (none of the Npc AI's actions make it): odd count a wander
     (FUN_005de200), even a stand.
   - Command 7 with a mode: not 8..11 (S1..S4) → cleared, stand 50. Away
     from its spot: walks left → one off, walk there (stand 25 if it can't
     set off); none left and over 1 off → cleared. Then the facing
     (FUN_00648820, 64 directions): Charsi 0x38, Warriv 0x34, Fara 4,
     Jamella 0x34 / 0x30; Larzuk rand(100) > 4 clears it. A mode left:
     the NPC in it already → stand 50, else set it (FUN_005ddf90) and
     clear. Fara with none: rand(100) < 66 sets S1.
5. FUN_005e7080, the wander: with a path, one step of the unit seed;
   `low % 100 < 66` → an entry by rand(count) (FUN_0045c3e0 on the unit
   seed), its action through the table at 0x741d74:

   | action | fn | does |
   |---|---|---|
   | 1, 3 | FUN_005e6de0 / FUN_005e6ef0 | not there: walk to it, command 4 = {x, y, 12, 10} |
   | 2 | FUN_005e6e80 | the same, then command 4's thinks 20 (there or not) |
   | 4, 5 | FUN_005e6f00 / FUN_005e6fc0 | the same, command 7 = {S1 / S2 if the type has it (FUN_0046c140, MonStats2 mode bits) else 1, x, y, 4}, command 4 = {x, y, 12, 10} |
   | 0, 6 | none | |

   It returns what the action did: 0 when already there.
6. Nothing done: stand 8.

`tools/emu/ai_town_path_actions.py` compares these five path actions with
`npc_think` over 170 cases, holding point selection fixed and hooking movement,
mode availability and command allocation. It compares movement and command
fields, not pathfinding or the action return value. From the worktree root,
with `GAME_EXE` pointing to stock 1.14d:

```sh
mkdir -p build/ai-town-path
uv run --no-sync --project tools/emu python tools/emu/ai_town_path_actions.py --cpp > build/ai-town-path/cases.cpp
c++ -std=c++20 -Icomponents -Icomponents/d2s -Icomponents/txt build/ai-town-path/cases.cpp -o build/ai-town-path/cases
build/ai-town-path/cases > build/ai-town-path/cases.txt
uv run --no-sync --project tools/emu python tools/emu/ai_town_path_actions.py --cases build/ai-town-path/cases.txt --exe "$GAME_EXE"
```

`tools/emu/ai_town_path_dispatch.py` uses the same cases to run FUN_005e7080
and its selected actions. All cases have one path point and use the same
seed, which selects that point; paths with no point and the no-selection
branch are not checked. After generating the cases above:

```sh
uv run --no-sync --project tools/emu python tools/emu/ai_town_path_dispatch.py --cases build/ai-town-path/cases.txt --exe "$GAME_EXE"
```

So an NPC picks a random point of its path two thinks in three, walks
there and lingers 12 (or 20) thinks of 10 frames, walking back when pushed
over 3 off; a point with action 4 / 5 ends with S1 / S2 there, facing its
way (Warriv's turn, Charsi at the anvil). A walk ends with a think at once
(WL's mode end), a special mode at its end goes NU and thinks aidel (15)
frames on (monster-ai.md "When it thinks").

## The visitor — FUN_005e68f0 (ECX game, EDX unit, stack the block)

Only for an NPC with an interact holder (monster data +0x30, whose first
word heads the list FUN_00572c10 adds a talking player to).

- **Who.** FUN_005ddf20 → FUN_005dd0b0 with callback FUN_005dde80: a
  player (type 0) under 16 off by FUN_005dc380 (each axis gap less the
  NPC's size, FUN_00620510: MonStats2 +8, 2 for the camp's; then the larger
  plus half the smaller) and, for an NPC with the interact flags, only one
  FUN_00544590 says yes to: the NPC has a quest message for them (the "!"
  balloon, quests.md). None: the NPC itself. Among several players
  (FUN_005dde80, ECX/EDX the NPC, the candidate, the best so far
  {unit, distance}): for an NPC with the interact flags (class record
  +0xd & `DAT_006ce26c`) a candidate replaces the best only when it has
  the "!" and is strictly nearer, so the nearest such player wins, the
  first met on a tie; for one without the flags every player under 16
  replaces it, so the last met wins.
- **Talking** when the list has a player (FUN_00572dc0), the visitor is in
  it (FUN_00572de0), or the visitor is busy (FUN_00535060: in an
  interaction), or AI control +0x14 > 0. Then: with +0x14 over 36 and the
  NPC over 2 off (+0x18, +0x1c), a walk there (FUN_005ded90; bugs.md 16:
  no path, the think comes aidel on); else with +0x14 > 0 its path stopped
  (FUN_00648730) and stand 8. +0x14 then counts down (below 0: reset to 0).
  With +0x14 at 0 nothing is scheduled.
- **Else**, with a visitor that isn't the NPC:
  - under 3 off (or over 23): path stopped; AI control +0x18 at 0 → 60
    and the player gets unit sound 0x12 (FUN_00553380: the client's
    FUN_004cbde0 → FUN_004e0590, a greeting), else it counts down; stand 20.
  - else FUN_005e6860(16): over 16 from home (command 10) → command 4 =
    {home, 12, 10}, stand 10. Otherwise pace type 1 (FUN_005de190) and a
    walk toward the player (FUN_005de6d0 → FUN_005de4e0, mode 2): at most
    3 subtiles (distance − 2 under 5), ending 2 off, the step split between
    the axes by their gaps and both raised by one until they sum to it.
- **A talk's start** (FUN_00548b00 case 1, within 0x33, NPC MonStats +0xd
  with both interact bits): the NPC's path stopped, AI control +0x14 = 40
  (FUN_0058ec00), its pending think dropped and one next frame; then the
  player, within 6 and not busy, is added to the list (FUN_00573020 →
  FUN_00572c10).

So an NPC with a "!" for you walks up to you inside 16 subtiles, greets
you 2 off and every 60 thinks after, and doesn't stray past 16 from
home; talked to, it stands. Nothing on the server turns it: it faces you
because it walked to you. (Read from the code; npcs.py has no player.)

## Checked

`tools/emu/npcs.py <seed>` runs game.exe's FUN_005e7130 on the camp's
NPCs (the game made, the camp populated), its effects hooked; `drlg-dump
... npcs` runs `rules::npc_think` the same way. `THINKS=150 uv run python
diff_drlg.py 1-40 0 npcs`: 40/40.

## In d2d

`rules::npc_think` (components/rules/town_npcs.hpp) is the think;
`npc_patrol` (components/game/ai.cpp) drives it: stands as frames, walks
along its path (`follow_path`) at Velocity, the special modes as S1 / S2 for their
animation's length. The visitor: `NpcVisitor` (`npc_patrol` fills it
from the player and the NPC's `alert`). Not here (ponytails): the greeting
sound, the think once +0x14 runs out mid-talk, game.exe's pathers for the
walk.

## Act 2's town AIs — Towner and Vendor

Lut Gholein's named NPCs are AI Npc too; its townsfolk and guards
(act2male 195, act2female 196, act2guard1 203) are MonAI 41 Towner, its
market traders (act2vendor1 204, act2vendor2 205) MonAI 42 Vendor. The
AI table (0x73ca18 + 16 × row, `{targetMode, init, think, -}`): 1 Idle
FUN_005b0cd0, 32 Npc FUN_005e7130, 41 Towner FUN_005e7540, 42 Vendor
FUN_005e9e00, 81 JarJar FUN_005e7590 (act2guard2).

- **Towner** (FUN_005e7540): FUN_005e6800 (home), FUN_005e6ae0 (the
  commands), FUN_005e7080 (the wander), else stand 12 — the Npc think
  without the classes' steps and the visitor. A towner with no DS1 path
  stands.
- **Vendor** (FUN_005e9e00): one step of the unit seed; `low % 100 < 20`
  → mode 8 (S1, FUN_005ddfc0(8, 0, 0)), else stand 30. No path, no walk.
- JarJar (FUN_005e7590, the palace guard): its home is AI control
  command 10 (FUN_0058eef0(unit, 10): +0xc, +0x10; +0x18 the frame it last
  moved or heeded). Till he stands aside (FUN_0059b8b0: record 0xb with
  data+0x18 / +0x19 clear and data+0x40 2; no record counts as aside): no
  more than once 200 frames, home from over 1 off (FUN_005ded90), else the
  Npc's visitor (FUN_005e68f0); stand 20. FUN_0059b8f0 swaps the spot for
  data+0x30 / +0x34 (4 up for +0x19) and drops the heeding while +0x18 /
  +0x19 are set, but nothing sets them (stubs.md FUN_0059b8f0): dead. Aside: to 3 north of home
  when over 7 off (over 2 once data+0x40 is 2, FUN_0059aec0); else the
  visitor, then stand 120 in any case: that stand sets NU at once
  (FUN_005de080 → FUN_005a7c20), so a heed's walk is cut and only its
  greet and facing stay. The code after a heed that didn't act (a heeded
  player within 12: rand(1000) < 100 a spot 2 about it, FUN_005df530,
  stand 200; else rand(1000) < 50 a wander 3, FUN_005de200) never runs:
  the guard's interact holder makes FUN_005e68f0 act on any player by, and
  with no player FUN_005ddf20 hands back the guard himself, which skips
  it. d2d: `rules::jarjar_think`, aside from `ArcaneQuest::guard_state`.
  Checked: `tools/emu/jarjar.py` runs FUN_005e7590 natively on 3000
  random scenes (helpers hooked; npcs.py can't, object 122's InitFn makes
  him) and `--dump tests/jarjar_cases.inc` is replayed by test_monsters.

d2d: `Npc::town_ai` (MonStats AI), `rules::npc_think(..., towner)` and
`rules::vendor_think`. A built level's NPCs (Lut Gholein) take their DS1
path, the spot they were made on (the free one nearest their preset spot)
and their unit seed after the init's draws (made(): the look, which for a
townsfolk composite rolls its pieces, then the stat).
Checked: `THINKS=30 diff_drlg.py 1-20 40 npcs` (npcs.py with level 40:
act 2 made for it, the Jerhyn markers stubbed, each unit's MonAI think):
20/20.

## The Kurast Docks (level 75)

The docks' DS1 holds Cain, Asheara, Alkor, Ormus and Meshif with paths
(MonAI Npc), act3male / act3female (294 / 296, Towner) and three act3hire
(359, MonAI Hireable: Fight monsters, the merc's think). Natalya (0x129)
loads as object 0x17e (FUN_00665950). Three marker objects (token `ss`)
make NPCs as their rooms come up, each through FUN_005b2f20 (no DS1
path), on the game's quest words (FUN_00544760 → FUN_0065c310: word ·16 +
bit of game +0x10f4 +0xc):

| object | InitFn | makes |
|---|---|---|
| 378 | 49, FUN_005b70b0 | Hratli (0xfd) at it, while word 0x10 lacks bit 13 |
| 379 | 50, FUN_005b7160 | Hratli at it (his shop) once word 0x10 has bit 13; its unit +0xc4 \|= 0x3000000 |
| 382 | 52, FUN_005bce80 | Natalya at it, else within 3, while word 0x16 lacks bit 13 |

Each keeps its unit in a quest record (types 0xe, 0x14: +0 / +1 made, +0xc
the unit id) so a room coming up again doesn't make a second one.

Checked: `diff_drlg.py 1-20 75 npcs` 20/20 and `LEVELS=75 diff_drlg.py
1-20 0 game` 20/20 (the oracle stubs the quest-word read as clear:
objgroups.QUEST_STUBS).

## The other wanderer — FUN_0054ef50

Unrelated to the town: when a room's seed step (FUN_0054f060) has its low
15 bits clear in a room not in town (room1 +0x78) of a level with MonWndr
(Levels.txt +0x2e), FUN_0054eff0 rolls rand(100) < 3 on it and, with fewer
than 3 made in the game's region (+0x2c4), FUN_0054ef50 makes one of the
act's wandering monsters (table 0x731b2c by act, 0x731b30 {first, count})
at a free spot (FUN_0054dc40, FUN_005b2f20) and adds it to list 8
(FUN_005b1990). About 1 room in 1.1 million; d2d steps the draw only
(objgroups.cpp ponytail). The table: 0x731b2c holds one id, 270 (rogue2, RogueScout,
GoodNpcRanged), and only act 0 has a non-zero count ({0, 1} at 0x731b30;
acts 1..4 {0, 0}): the wanderer is always that rogue scout, only in Act 1
(emu dump).
