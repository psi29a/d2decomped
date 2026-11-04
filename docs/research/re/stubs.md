# Stubs and dead code in game.exe 1.14d

Code that's in the binary but does nothing, or that nothing reaches. Look
here first before porting something that "should" work. Each row links to
the doc with the details. Add a row whenever a trace ends in one.

**Kinds:**
- **stub**: a table entry or call target whose body is a bare return.
- **dead**: code computes data that no consumer reads.
- **orphan**: a real function with no caller, raw pointer or rel32 call.
- **unreachable**: real code whose trigger never happens in the shipped data.
- **suspected unreachable**: no trigger found yet, but not every path to one is checked (the row says which).

| Address | What | Kind | Effect in 1.14d | d2d | Details |
|---|---|---|---|---|---|
| `FUN_0044e770`'s `endgame2` branch | the endgame screen's expansion layout (`ui\menu\endgame2`, twelve 256-wide cels for 800x600), picked by `DAT_007a04f4` (S->C 0x01 byte +6, the expansion flag) | unreachable | the screen only comes with `DAT_007a0604`, set only by `FUN_0044ec80`, which the client calls only in a classic game (0x4a2d75: an expansion game jumps to `FUN_00483340`) | not ported | quests-act4.md "The ending" |
| `FUN_00458f40` | room2 result from `FUN_0061aae0` is saved in EDI and never read | dead | the computed room2 pointer is discarded | not ported | drlg.md "A room's life on the server" |
| `FUN_0046d450` | critter row 574 (Worldstone Effect): 5 %, a unit (`FUN_004cdb40`) up to 30 subtiles round it | unreachable | no Levels.txt cmon names row 574 | not ported | critters.md |
| `FUN_0046d660` | a critter's think when MonStats `zoo` (flag 0x16) is set: stand 20, or 20 % each walk to / flee 8 from the player, else wander 8 | unreachable | no Levels.txt cmon names a zoo row (zoo is on ordinary monsters, which never take the critter think) | ported anyway (town/walk.cpp, for mods) | critters.md |
| `FUN_0046d780` rows 157 / 158 / 227 / 320 | the birds', maggot's and fish's critter thinks | unreachable | no Levels.txt cmon names them (parrot 319 shares the birds' think and is reached) | ported (shared code) | critters.md |
| `0x4fb204–0x4fb22a` | DAT palette loop fills a local four-byte-stride array | dead | the local array is never read; the active palette comes from PL2 | not ported | palette.md "DAT and active-palette layout" |
| `FUN_005347b0` | Player.cpp callback | stub (`ret 0xc`) | the call returns without a Player action | not ported | "Player stub 005347b0" below |
| `FUN_005347c0` | Player.cpp callback | stub (`ret 0x10`) | six calls return without a Player action | not ported | "Player stub 005347c0" below |
| `FUN_00538860` | client `+0x3d4` bit 2 setter, with a first-enable `FUN_0053fc70` callback | orphan (no call, jump or pointer) | this setter never runs; `FUN_0053fc70` also has a static caller, `0052c410` | not ported | net-join.md "Client record accessors" |
| `0x543de0` | the quest event 6 wrapper (record slot +0xb8) | orphan (no call, jump or pointer) | no +0xb8 callback runs: Tools of the Trade's reset when the last carrier loses the malus (`0x5918d0`), the Search for Cain's `FUN_00592e60` rewind | not ported | act1-end.md "Tools of the Trade" |
| `FUN_0054f860` | InitFn 10: queues object event 8 (`FUN_00581250`, mode 0 by day, 1 at night) in level 1 (the Rogue Encampment), else mode 2 + event 0 | unreachable | no objects.txt row has InitFn 10; event 8 runs only for the fissures (399), which take its other branch | not ported | objects.md "Time of day (EnvEffect)" |
| `FUN_0055f4f0` | ItemMode.cpp callback | stub (`ret 4`) | eight calls return without an item-mode action | not ported | "ItemMode stub" below |
| `FUN_00570530` | a monster AI restore (calls the AI toggle) | orphan | — | not ported | monster-ai.md "Curse thinks and other thinks" |
| `LAB_00587830` | InitFn 70 (table 0x731bc0) | stub (`return`) | nothing happens when the object's room comes up | nothing | quests-act5.md "InitFns 66..70" |
| `FUN_005888d0`'s fallback spots | a5q2's cage portal made again at the door's own x (0x588a3b), then at `FUN_00545340(2, 0x8000, .., 100)`'s free spot (0x588a65), when the first make fails | unreachable | the first make, an object's `FUN_00555230` at door x + 2, has no collision test and fails only if `FUN_00620290`'s allocation does | not ported (act5.cpp: the portal at door x + 2) | quests-act5.md "Rescue on Mount Arreat" |
| `FUN_00594270` | Cube portal kind 2, Pandemonium Portal (table 0x6e11c8) | stub (`return 0`) | the three keys stay in the Cube; no way to 133..135 | `World::transmute` refuses | cube.md |
| `FUN_00594280` | Cube portal kind 3, Pandemonium Finale Portal | stub (`return 0`) | the organs stay; no way to Uber Tristram | same | cube.md |
| `FUN_00594700`, `FUN_005947e0`, `FUN_00595160` | a1q4 list append / contains, and another | orphan | — | not ported | quests-act1.md |
| `FUN_005a30e0` mod 24 | thief (MonUMod 24) on a hit | unreachable | MonUMod disables it; `FUN_005a49b0` skips it for Lord De Seis | not ported | monsters.md |
| `FUN_005dd140` | counts uber Mephisto / Diablo / Baal, 0x2c7, 0x2c8, 731..733 in a radius | orphan | — | not ported | same |
| `FUN_005e2d80` +0x1c | a riding imp's two extra draws when its AI control +0x1c is set | unreachable | the init (FUN_005e2cd0) copies 0x6e34e0's flags, both 0 | not ported | monster-ai.md "The imps' ride and skills" |
| `FUN_005e9dd0` | makes 0x2c7 (demonhole) at a unit, mode 1, flags 0x40 | orphan | — | not ported | monster-ai.md, as above |
| `0x5e9df0` | UberDiablo think (MonAI 147) | stub (`ret 4`) | uber Diablo stands | same | same |
| `FUN_005eeaa0` | AncientStatue think (MonAI 132): Rite awake → skill 302 | unreachable | ancientstatue1..3 are never placed or made | not ported | monster-ai.md, as above |
| `FUN_005f1140`'s command branch | QuillRat (MonAI 14) shoots at a pending command's unit | unreachable | only QuillMother (75) of its kin sends one, to its own group of quillbears, which never read it; no group holding quillrats (packs, champions, Flamespike) has a sender | not ported | monster-ai.md "AI commands" |
| `FUN_005f1750`'s cast branch | SandMaggot's other think (MonAI 15) casts Skill1 when its AI command 14's +0x18 is 1 | unreachable | its main think (`FUN_005f1800`) never touches command 14; `FUN_005f1750` makes it (`FUN_0058efa0`, the word 0) and only clears the word; the one 0xe sender, FetishShaman, sends to its own group of fetishes | not ported (`other_think` keeps the branch on a word that stays 0) | monster-ai.md "Curse thinks and other thinks" |
| `FUN_005f7f10` | a make next to Mephisto's think (1.13 uber Mephisto's minions) | orphan | — | not ported | same |
| `0x5f81c0` | UberMephisto think (MonAI 146) | stub (`ret 4`) | uber Mephisto stands | same | same |
| `0x5fd200` | UberBaal think (MonAI 145) | stub (`ret 4`) | uber Baal stands after his target search | `mon_think` returns none | monster-ai.md "AncientStatue and the ubers' MonAIs", bugs.md #26 |
| `FUN_00622e90` | Units.cpp callback | stub (`ret 8`) | the call returns without a Units action | not ported | "Units stub" below |
| `FUN_0063be30` | Inventory.cpp callback | stub (`ret 8`) | 26 calls return without an Inventory action | not ported | "Inventory stub" below |
| `FUN_00647640` / `FUN_0056f460` stat 125 | Throw with an item_throwable weapon (stat 125, Properties `throw`): the gate takes it and the do-func throws it as its missiletype | unreachable | no UniqueItems, SetItems, Sets, Runes, Magic prefix / suffix, AutoMagic, CubeMain, Gems or QualityItems row has the `throw` property | the gate (`throw_ready`) takes it; `ammo_index` and the launch don't | throw.md, skills.md "Throw (do 3" |
| `FUN_0066ae70` | a level's random room (`FUN_0045c3e0` on the level seed +0x1c4, room count +8, walked along +0x10 / +0x24): `FUN_0066b2b0`'s last fallback for a spot of a level without a spawn list (+0x90), after its waypoint room (`FUN_0066ad80`), a warp slot's room (`FUN_0066b1f0`) and the room at its middle less 2 (`FUN_00642630`) | suspected unreachable | an emu sweep of every non-town level, 3 map seeds (0x1234, 0x9e3779b9, 77), never reached it: each has a waypoint or warp room, or a room at its middle (only map seeds checked, not every seed) | not ported (objgroups.cpp: none added) | drlg.md "Spawn tiles and the start spot" |
| seq_impinferno (Sk5mode of imp6..8) | imp6..8's sequence Inferno (do 126 from an event-4 row) | suspected unreachable | only a riding imp casts Skill5 (ImpRide's pick); the free Imp think casts Skill4 at most, and no level with imp6..8 (125..127, 135) has a siege beast or tower to ride. Probed live 2026-10-25: 50 s on Nightmare 126/127, attacks and missiles, no Inferno. Unchecked: a mount from a preset or DS1 | ported anyway (ai.cpp) | monster-ai.md "The imps' ride and skills" |
| unit events 0x31, 0x35, 0x39..0x3f, 0x44..0x47, 0x49 | fourteen class quest lines: nine `find` lines (Act 2 radament, summoner, tomb_decoy / true; Act 3 beneath, blackened, drain, guardian, lamesen), two completions (act2_complete_tightspot, the Maggot Queen's; act3_complete_beneath) and act2_begin_tainted, act2_near_tightspot, act2_trigger_field | unreachable | the client maps them to Sounds rows, but no server call or client table makes those ids (the other find lines come from the client's level table 0x72a2c4) | not played | unit-sounds.md "Find lines" |

### ItemMode stub

`FUN_0055f4f0`'s whole body is `RET 4`, discarding one stack argument.
Its eight call sites are in `FUN_00533c70`, `FUN_005380d0`, `FUN_0054bd90`,
`FUN_0054ced0` (two), `FUN_0054d130`, `FUN_0056ac10` and `FUN_00597890`.

### Units stub

`FUN_00622e90`'s whole body is `RET 8`, discarding two stack arguments.
Its only call site is `0064bee7` in `FUN_0064b860`.

### Inventory stub

`FUN_0063be30`'s whole body is `RET 8`, discarding two stack arguments.
Ghidra's `XrefsTo 0063be30` reports 26 direct call sites.

### Player stub 005347b0

`FUN_005347b0`'s whole body is `RET 0xc`, discarding three stack arguments.
Its only call site is `0057db72` in `FUN_0057d9b0`.

### Player stub 005347c0

`FUN_005347c0`'s whole body is `RET 0x10`, discarding four stack arguments.
It has six call sites: five in `FUN_0057dd60` and one in `FUN_0057dfb0`.

Still reachable, though the content looks dead: the uber levels 133..136
build and populate (`FUN_005559a0`'s uber cases, the Hellfire Torch hook
`FUN_005e0070`, UberIzual's real think `FUN_005f8c80`). d2d ports them
all, so they work once something takes a player there.
