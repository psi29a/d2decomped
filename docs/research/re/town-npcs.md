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
3. FUN_005e68f0: only with someone in the NPC's interact list (monster
   data +0x30, FUN_00572c10 adds to it): turn to them, a 60-think count,
   walk back home past 16 (FUN_005e6860). Returns 0 with the list empty.
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

So an NPC picks a random point of its path two thinks in three, walks
there and lingers 12 (or 20) thinks of 10 frames, walking back when pushed
over 3 off; a point with action 4 / 5 ends with S1 / S2 there, facing its
way (Warriv's turn, Charsi at the anvil). A walk ends with a think at once
(WL's mode end), a special mode at its end goes NU and thinks aidel (15)
frames on (monster-ai.md "When it thinks").

## Checked

`tools/emu/npcs.py <seed>` runs game.exe's FUN_005e7130 on the camp's
NPCs (the game made, the camp populated), its effects hooked; `drlg-dump
... npcs` runs `rules::npc_think` the same way. `THINKS=150 uv run python
diff_drlg.py 1-40 0 npcs`: 40/40.

## In d2d

`rules::npc_think` (components/rules/town_npcs.hpp) is the think;
`npc_patrol` (components/game/ai.cpp) drives it: stands as frames, walks
along `walk_path` at Velocity, the special modes as S1 / S2 for their
animation's length. Not here (ponytails): FUN_005e68f0 (a busy NPC stands
still, thinking 8 frames after), game.exe's pathers for the walk.

## The other wanderer — FUN_0054ef50

Unrelated to the town: when a room's seed step (FUN_0054f060) has its low
15 bits clear in a room not in town (room1 +0x78) of a level with MonWndr
(Levels.txt +0x2e), FUN_0054eff0 rolls rand(100) < 3 on it and, with fewer
than 3 made in the game's region (+0x2c4), FUN_0054ef50 makes one of the
act's wandering monsters (table 0x731b2c by act, 0x731b30 {first, count})
at a free spot (FUN_0054dc40, FUN_005b2f20) and adds it to list 8
(FUN_005b1990). About 1 room in 1.1 million; d2d steps the draw only
(objgroups.cpp ponytail).
