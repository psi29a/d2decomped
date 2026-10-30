# Stubs and dead code in game.exe 1.14d

Code that's in the binary but does nothing, or that nothing reaches. Look
here first before porting something that "should" work. Each row links to
the doc with the details. Add a row whenever a trace ends in one.

**Kinds:**
- **stub**: a table entry or call target whose body is a bare return.
- **orphan**: a real function with no caller, raw pointer or rel32 call.
- **unreachable**: real code whose trigger never happens in the shipped data.
- **suspected unreachable**: no trigger found yet, but not every path to one is checked (the row says which).

| Address | What | Kind | Effect in 1.14d | d2d | Details |
|---|---|---|---|---|---|
| `0x5fd200` | UberBaal think (MonAI 145) | stub (`ret 4`) | uber Baal stands after his target search | `mon_think` returns none | monster-ai.md "AncientStatue and the ubers' MonAIs", bugs.md #26 |
| `0x5f81c0` | UberMephisto think (MonAI 146) | stub (`ret 4`) | uber Mephisto stands | same | same |
| `0x5e9df0` | UberDiablo think (MonAI 147) | stub (`ret 4`) | uber Diablo stands | same | same |
| `FUN_00594270` | Cube portal kind 2, Pandemonium Portal (table 0x6e11c8) | stub (`return 0`) | the three keys stay in the Cube; no way to 133..135 | `World::transmute` refuses | cube.md |
| `FUN_00594280` | Cube portal kind 3, Pandemonium Finale Portal | stub (`return 0`) | the organs stay; no way to Uber Tristram | same | cube.md |
| `FUN_005e9dd0` | makes 0x2c7 (demonhole) at a unit, mode 1, flags 0x40 | orphan | — | not ported | monster-ai.md, as above |
| `FUN_005f7f10` | a make next to Mephisto's think (1.13 uber Mephisto's minions) | orphan | — | not ported | same |
| `FUN_005dd140` | counts uber Mephisto / Diablo / Baal, 0x2c7, 0x2c8, 731..733 in a radius | orphan | — | not ported | same |
| `FUN_005eeaa0` | AncientStatue think (MonAI 132): Rite awake → skill 302 | unreachable | ancientstatue1..3 are never placed or made | not ported | monster-ai.md, as above |
| `FUN_00594700`, `FUN_005947e0`, `FUN_00595160` | a1q4 list append / contains, and another | orphan | — | not ported | quests-act1.md |
| `FUN_00570530` | a monster AI restore (calls the AI toggle) | orphan | — | not ported | monster-ai.md "Curse thinks and other thinks" |
| `FUN_005e2d80` +0x1c | a riding imp's two extra draws when its AI control +0x1c is set | unreachable | the init (FUN_005e2cd0) copies 0x6e34e0's flags, both 0 | not ported | monster-ai.md "The imps' ride and skills" |
| seq_impinferno (Sk5mode of imp6..8) | imp6..8's sequence Inferno (do 126 from an event-4 row) | suspected unreachable | only a riding imp casts Skill5 (ImpRide's pick); the free Imp think casts Skill4 at most, and no level with imp6..8 (125..127, 135) has a siege beast or tower to ride. Probed live 2026-10-25: 50 s on Nightmare 126/127, attacks and missiles, no Inferno. Unchecked: a mount from a preset or DS1 | ported anyway (ai.cpp) | monster-ai.md "The imps' ride and skills" |
| `FUN_005a30e0` mod 24 | thief (MonUMod 24) on a hit | unreachable | MonUMod disables it; `FUN_005a49b0` skips it for Lord De Seis | not ported | monsters.md |
| `FUN_00647640` / `FUN_0056f460` stat 125 | Throw with an item_throwable weapon (stat 125, Properties `throw`): the gate takes it and the do-func throws it as its missiletype | unreachable | no UniqueItems, SetItems, Sets, Runes, Magic prefix / suffix, AutoMagic, CubeMain, Gems or QualityItems row has the `throw` property | the gate (`throw_ready`) takes it; `ammo_index` and the launch don't | throw.md, skills.md "Throw (do 3" |
| `FUN_00587830` | InitFn 70 (table 0x731bc0) | stub (`return`) | nothing happens when the object's room comes up | nothing | quests-act5.md "InitFns 66..70" |
| unit events 0x31, 0x35, 0x39..0x3f, 0x44..0x47, 0x49 | fourteen class quest lines: nine `find` lines (Act 2 radament, summoner, tomb_decoy / true; Act 3 beneath, blackened, drain, guardian, lamesen), two completions (act2_complete_tightspot, the Maggot Queen's; act3_complete_beneath) and act2_begin_tainted, act2_near_tightspot, act2_trigger_field | unreachable | the client maps them to Sounds rows, but no server call or client table makes those ids (the other find lines come from the client's level table 0x72a2c4) | not played | unit-sounds.md "Find lines" |
| `FUN_0054f860` | InitFn 10: queues object event 8 (`FUN_00581250`, mode 0 by day, 1 at night) in level 1 (the Rogue Encampment), else mode 2 + event 0 | unreachable | no objects.txt row has InitFn 10; event 8 runs only for the fissures (399), which take its other branch | not ported | objects.md "Time of day (EnvEffect)" |
| `FUN_0046d660` | a critter's think when MonStats `zoo` (flag 0x16) is set: stand 20, or 20 % each walk to / flee 8 from the player, else wander 8 | unreachable | no Levels.txt cmon names a zoo row (zoo is on ordinary monsters, which never take the critter think) | ported anyway (town.cpp, for mods) | critters.md |
| `FUN_0046d450` | critter row 574 (Worldstone Effect): 5 %, a unit (`FUN_004cdb40`) up to 30 subtiles round it | unreachable | no Levels.txt cmon names row 574 | not ported | critters.md |
| `FUN_0046d780` rows 157 / 158 / 227 / 320 | the birds', maggot's and fish's critter thinks | unreachable | no Levels.txt cmon names them (parrot 319 shares the birds' think and is reached) | ported (shared code) | critters.md |
| `0x543de0` | the quest event 6 wrapper (record slot +0xb8) | orphan (no call, jump or pointer) | no +0xb8 callback runs: Tools of the Trade's reset when the last carrier loses the malus (`0x5918d0`), the Search for Cain's `FUN_00592e60` rewind | not ported | act1-end.md "Tools of the Trade" |

Still reachable, though the content looks dead: the uber levels 133..136
build and populate (`FUN_005559a0`'s uber cases, the Hellfire Torch hook
`FUN_005e0070`, UberIzual's real think `FUN_005f8c80`). d2d ports them
all, so they work once something takes a player there.
