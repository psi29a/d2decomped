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
| `FUN_0046e150` | sets `+8` of a unit's overlays of one row | orphan (no call, jump or pointer) | — | not ported | gfx.md "Overlays" |
| `FUN_0046ea50` | loads a unit's COF with no type check | orphan (no call, jump or pointer) | — | not ported | gfx.md "The unit's COF list" |
| `FUN_0046f3f0` | frees the monster COF cache alone (`FUN_00470290` frees both) | orphan (no call, jump or pointer) | — | not ported | gfx.md "The COF caches" |
| `FUN_0047fc40`, `FUN_0047ffb0` | the Warden module's deleting destructor; a module cache write (API `+0xd0`) | orphan (no call, jump or pointer) | modules are freed inline and cached by callback 2 (`FUN_00480040`) instead (both run in warden_client.py) | not ported | bnclient.md "WardenClient.cpp" |
| `FUN_00480e30` | `UNITMODE_SetModeAndLoadCof`: clears unit flag 0x40, sets the mode and loads its cof on success, the same two calls as its sibling `FUN_00480e70` without the hit overlay or mode sound | orphan (no call, jump or pointer) | — | not ported | collision.md, `FUN_00480e70`'s entry |
| `FUN_004da170`, `FUN_004da190`, `FUN_004da660` | motion record raw x, y; the started flag | orphan (no call, jump or pointer) | — | not ported | gfx.md "The motion record" |
| `FUN_004da920`, `FUN_004da990`, `FUN_004da9c0` | GfxUtil.cpp 16-byte node list: add, find, free | orphan (no call, jump or pointer) | — | not ported | gfx.md "Unused list helpers" |
| `FUN_004f62c0`, `FUN_004f62e0`, `FUN_004f65a0` | the renderer thunks for slots +0xb0 (a filled rectangle), +0xb4 (a square dot), +0xd0 (one pixel) | orphan (no call, jump or pointer; byte-scanned) | the drivers' entries for those slots are never reached | not ported | video-drivers.md "The slot map" |
| `0x4fb204–0x4fb22a` | DAT palette loop fills a local four-byte-stride array | dead | the local array is never read; the active palette comes from PL2 | not ported | palette.md "DAT and active-palette layout" |
| `0x510130`, `0x5102f0`, `0x510320`..`0x510380`, `0x5123a0`, `0x5126f0` | the DirectDraw driver's empty slots +0xd4, +0x54, +0x78, +0x60..+0x6c, +0x4c, +0x2c, +0x94, +0xcc: what Glide and Direct3D put there is perspective, the hit silhouette, the environment's colour, the cache flush | stub (`ret`, `ret n` or `xor eax, eax`) | the default mode has no perspective, hit silhouette or coloured light | draws as these do | video-drivers.md "The slot map" |
| `FUN_0051ad70` | byte-stuffs a buffer (`00` → `FF 01`, `FF` → `FF FF`) | orphan (no call, jump or pointer) | nothing escapes BNCS data | not ported | bnclient.md "BnSend.cpp leftovers" |
| `FUN_0051d590`, `FUN_0051d6a0`, `FUN_0051d6d0`, `FUN_0051d7a0`, `FUN_0051d7f0` | the news array's destructor, clear, reserve, append, reserve exact | orphan (no call, jump or pointer) | template members the news code inlines or never uses | not ported | bnclient.md "BNNews.cpp" |
| `FUN_0051ea20` | download percent = done × 100 / total | orphan (no call, jump or pointer) | the download loops compute it inline | not ported | bnclient.md "BnDownload.cpp" |
| `FUN_005347b0` | Player.cpp callback | stub (`ret 0xc`) | the call returns without a Player action | not ported | "Player stub 005347b0" below |
| `FUN_005347c0` | Player.cpp callback | stub (`ret 0x10`) | six calls return without a Player action | not ported | "Player stub 005347c0" below |
| `FUN_00538860` | client `+0x3d4` bit 2 setter, with a first-enable `FUN_0053fc70` callback | orphan (no call, jump or pointer) | this setter never runs; `FUN_0053fc70` also has a static caller, `0052c410` | not ported | net-join.md "Client record accessors" |
| `0x543de0` | the quest event 6 wrapper (record slot +0xb8) | orphan (no call, jump or pointer) | no +0xb8 callback runs: Tools of the Trade's reset when the last carrier loses the malus (`0x5918d0`), the Search for Cain's `FUN_00592e60` rewind | not ported | act1-end.md "Tools of the Trade" |
| `FUN_0054f860` | InitFn 10: queues object event 8 (`FUN_00581250`, mode 0 by day, 1 at night) in level 1 (the Rogue Encampment), else mode 2 + event 0 | unreachable | no objects.txt row has InitFn 10; event 8 runs only for the fissures (399), which take its other branch | not ported | objects.md "Time of day (EnvEffect)" |
| `FUN_00559830` | drops an item at a spot (a twin of `FUN_00559130`) | orphan (no call, jump or pointer) | — | not ported | itemmode.md |
| `FUN_0055d930` | bare `ret`, called by `FUN_0057f410` | stub | nothing | not ported | itemmode.md |
| `FUN_0055d940`, `FUN_0055f2a0`, `FUN_0055f740` | wield refresh; the other hand's item; a scroll into a tome | orphan (no call, jump or pointer) | — | not ported | itemmode.md |
| `FUN_0055e9a0` | `return 0`, the body of C→S 0x22 (`FUN_0054b380`) | stub | the packet changes nothing there | not ported | itemmode.md |
| `FUN_0055f4f0` | ItemMode.cpp callback | stub (`ret 4`) | eight calls return without an item-mode action | not ported | "ItemMode stub" below |
| `FUN_0055fb80` | a gamble pick with GambleUber / GambleUltra upgrades | orphan (no call, jump or pointer) | — | not ported | itemmode.md |
| `FUN_00561b00`'s page-2 branch | `FUN_00568770` for a swap on page 2 | dead | page 2 is refused earlier in the same function | not ported | itemmode.md "C→S handlers' bodies" 0x1f |
| `FUN_00570530` | a monster AI restore (calls the AI toggle) | orphan | — | not ported | monster-ai.md "Curse thinks and other thinks" |
| `LAB_00587830` | InitFn 70 (table 0x731bc0) | stub (`return`) | nothing happens when the object's room comes up | nothing | quests-act5.md "InitFns 66..70" |
| `FUN_005888d0`'s fallback spots | a5q2's cage portal made again at the door's own x (0x588a3b), then at `FUN_00545340(2, 0x8000, .., 100)`'s free spot (0x588a65), when the first make fails | unreachable | the first make, an object's `FUN_00555230` at door x + 2, has no collision test and fails only if `FUN_00620290`'s allocation does | not ported (act5.cpp: the portal at door x + 2) | quests-act5.md "Rescue on Mount Arreat" |
| `FUN_0058a500`'s rerun | a5q3's marker event 7 queued again 25 frames on (`FUN_005417d0`) when fana (object 558) isn't made | unreachable | fana's make (`FUN_00555230` ECX 2) has no collision test and fails only with `FUN_00620290`'s allocation (see `FUN_005888d0`'s row) | one try (act5.cpp prison_update) | quests-act5.md "Prison of Ice" |
| `FUN_0058eea0` | AiGeneral.cpp: moves a monster's AI command ring head (+0x20) n nodes on (back for n < 0), or to a given node | orphan (no call, jump or pointer) | the ring is only pushed (`FUN_0058ec90`), popped (`FUN_0058ed10`) and emptied (`FUN_0058ede0`) | not ported | monster-ai.md "AI commands" |
| `FUN_00594270` | Cube portal kind 2, Pandemonium Portal (table 0x6e11c8) | stub (`return 0`) | the three keys stay in the Cube; no way to 133..135 | `World::transmute` refuses | cube.md |
| `FUN_00594280` | Cube portal kind 3, Pandemonium Finale Portal | stub (`return 0`) | the organs stay; no way to Uber Tristram | same | cube.md |
| `FUN_00594700`, `FUN_005947e0`, `FUN_00595160` | a1q4 list append / contains, and another | orphan | — | not ported | quests-act1.md |
| `FUN_0059b8f0` | the palace guard's (JarJar, FUN_005e7590) other spot: a2q4's data (quest record 0xb, +0x18 the 0x48-byte block FUN_0059f2a0 zeroes) +0x30 / +0x34, 4 up for +0x19, while its byte +0x18 or +0x19 is set; FUN_0059b8b0 counts him not aside then | dead | nothing writes data +0x18 / +0x19 / +0x30 / +0x34: every FUN_00543640(.., 0xb) caller (0x59ade7..0x59b9b9) and the a2q4 code (0x59a000..0x59ba40) only reads them, so the block's memset leaves them 0 and both tests reduce to data +0x40 | not ported | town-npcs.md JarJar |
| `FUN_005a30e0` mod 24 | thief (MonUMod 24) on a hit | unreachable | MonUMod disables it; `FUN_005a49b0` skips it for Lord De Seis | not ported | monsters.md |
| `0x5a4200` mod 40 | worms_on_death (MonUMod 40) as it dies: `FUN_005b2490(0x227, 1, 1, 0)` and state 104 (corpse_nodraw) | unreachable | no `FUN_005a4850` caller passes 40: the fixed mods, `FUN_005b21b0`'s families, `FUN_005b1cf0`'s bosses, Skills.txt sumumod (6 rows: 32, 33, 42) and SuperUniques Mod1..3 (none 40), and MonUMod row 40 has no cpick / upick | not ported | monsters.md |
| `FUN_005dd140` | counts uber Mephisto / Diablo / Baal, 0x2c7, 0x2c8, 731..733 in a radius | orphan | — | not ported | same |
| `FUN_005df2a0` | a step of n subtiles toward a unit on each axis (sign of the difference), started as move mode 2 (`FUN_005a7e60` / `FUN_005a7c20`) | orphan (no call, jump or pointer) | — | not ported | monster-ai.md "Orphan AI helpers" |
| `FUN_005e0060` | MonUMod 22's death hook for the Ancients (class 0x21c..0x21e, 0x5a32ed) | stub (`ret`) | nothing happens about them as each dies | nothing | superuniques.md "MonUMod 22" |
| `FUN_005e2d80` +0x1c | a riding imp's two extra draws when its AI control +0x1c is set | unreachable | the init (FUN_005e2cd0) copies 0x6e34e0's flags, both 0 | not ported | monster-ai.md "The imps' ride and skills" |
| `FUN_005e9750` | AiThink4.cpp: an act chooser in Diablo's shape (`FUN_005e8810`'s weights and cast test; 1.13 uber Diablo's, beside `FUN_005e9dd0`) | orphan (no call, jump or pointer) | — | not ported | monster-ai.md "Orphan AI helpers" |
| `FUN_005e9dd0` | makes 0x2c7 (demonhole) at a unit, mode 1, flags 0x40 | orphan | — | not ported | monster-ai.md, as above |
| `0x5e9df0` | UberDiablo think (MonAI 147) | stub (`ret 4`) | uber Diablo stands | same | same |
| `FUN_005eb5c0` | MonAI 143 ShadowMasterNoInit's init (table 0x73ca18 + 143 x 16 + 4): AI control +0x1c the owner's skill level, Attack on both hands | unreachable | no MonStats row has AI 143 (MonStats +0x1e, every row read in game.exe's tables) | not ported | pet-ai.md "The Shadows" |
| `FUN_005edc00` | AiThink4.cpp: stores x, y in AI control +0x18 / +0x1c (asserts on a null control) | orphan (no call, jump or pointer) | — | not ported | monster-ai.md "Additional think helpers" |
| `FUN_005eeaa0` | AncientStatue think (MonAI 132): Rite awake → skill 302 | unreachable | ancientstatue1..3 are never placed or made | not ported | monster-ai.md, as above |
| `FUN_005f1140`'s command branch | QuillRat (MonAI 14) shoots at a pending command's unit | unreachable | only QuillMother (75) of its kin sends one, to its own group of quillbears, which never read it; no group holding quillrats (packs, champions, Flamespike) has a sender | not ported | monster-ai.md "AI commands" |
| `FUN_005f1750`'s cast branch | SandMaggot's other think (MonAI 15) casts Skill1 when its AI command 14's +0x18 is 1 | unreachable | its main think (`FUN_005f1800`) never touches command 14; `FUN_005f1750` makes it (`FUN_0058efa0`, the word 0) and only clears the word; the one 0xe sender, FetishShaman, sends to its own group of fetishes | not ported (`other_think` keeps the branch on a word that stays 0) | monster-ai.md "Curse thinks and other thinks" |
| `FUN_005f2460`'s Skill1 | the Swarm's Swarm Move (Skills.txt 166, srvstfunc 48 / srvdofunc 90; MonStats swarm1..5 Skill1, its sequence `seq_swarmmove` starting in WL) | unreachable | the Swarm think (MonAI 19) never calls the skill helper `FUN_005dead0`: out of melee it walks, in melee A1, else stands | not ported: a sequence that walks first and isn't Charge stands (ai.cpp) | monster-ai.md, ai.cpp's Charge note |
| `FUN_005f7f10` | a make next to Mephisto's think (1.13 uber Mephisto's minions) | orphan | — | not ported | same |
| `FUN_005f8010` | a player pick in the monster's level within 0x3fc, weighted by class (35, 40, 30, 20, 25, ...) from the unit's seed (1.13 uber Mephisto's) | orphan (no call, jump or pointer) | — | not ported | monster-ai.md "Orphan AI helpers" |
| `0x5f81c0` | UberMephisto think (MonAI 146) | stub (`ret 4`) | uber Mephisto stands | same | same |
| `FUN_005fc130` | AiBaal.cpp: a weighted pick over n weights from the seed at EAX +0x20 (1 when none hits) | orphan (no call, jump or pointer) | — | not ported | monster-ai.md "Orphan AI helpers" |
| `0x5fd200` | UberBaal think (MonAI 145) | stub (`ret 4`) | uber Baal stands after his target search | `mon_think` returns none | monster-ai.md "AncientStatue and the ubers' MonAIs", bugs.md #26 |
| `FUN_00611660` | ProfCore.cpp: adds the QueryPerformanceCounter ticks since a start to profile row n (the 64-bit time at +0x68, of which the low dword is logged; count +0x70) | orphan (no call, jump or pointer) | the 23 rows at `0x743800` stay 0 | not ported | "Profile dump" below |
| `FUN_006116b0` | ProfCore.cpp: logs the profile rows (D2ProfileDump) | dead (its rows are never written) | runs on leaving the client mode (0x44b980), but logs only zeros, and only with a log file open | not ported | same |
| `0x611730` | ProfCore.cpp: `FUN_006116b0`, then zeroes each row's time and count | orphan (no call, jump or pointer) | — | not ported | same |
| `FUN_00612050`, `FUN_00612120` | a unit's debug name (`NULL Unit`, `monster %d`, ...) | orphan (no call, jump or pointer) | — | not ported | datatbls.md "DataTbls.cpp" |
| `FUN_00620360`, `FUN_00620710`, `FUN_00620780`, `FUN_006207f0`, `FUN_00620830`, `FUN_00620980`, `FUN_00620c70`, `FUN_00620cd0`, `FUN_00620d30`, `FUN_00620d80`, `FUN_006211c0`, `FUN_00621420`, `FUN_006219f0` | Units.cpp accessors: tile path setters, \|dx\| / \|dy\|, screen x / y, target type / id / set, a monster's speed word, a mode setter, a 20-byte row, a skill node | orphan (no call, jump or pointer) | — | not ported | units.md |
| `FUN_006218d0` | copies a frame's event to unit `+0x4e` | orphan (no call or jump; one pointer-shaped dword at `0x6b41e3`) | — | not ported | units.md "Animation" |
| `FUN_00622e90` | Units.cpp callback | stub (`ret 8`) | the call returns without a Units action | not ported | "Units stub" below |
| `FUN_006281b0`, `FUN_006282a0`, `FUN_00628440`, `FUN_006284c0`, `FUN_00629740`, `FUN_006297a0`, `FUN_006298e0`, `FUN_00629990`, `FUN_00629d00`, `FUN_0062b560` | Items.cpp getters: item data `+0x14` / `+0x46`, ItemTypes `+0xc` / `+0x13`, Items bitfield1 bits 3 and 2, durwarning, hasinv, gemsockets | orphan (no call, jump or pointer) | — | not ported | itemmode.md "Items.cpp" |
| `FUN_0062a360` | `return 0`, six callers | stub | whatever asks gets 0 | not ported | itemmode.md "Items.cpp" |
| `FUN_0063b2e0`, `FUN_0063be40`, `FUN_0063cab0`, `FUN_0063ce60`, `FUN_0063cf30`, `FUN_0063d010`, `FUN_0063d0f0`, `FUN_0063d590`, `FUN_0063db90` | Inventory.cpp: last item, a hand-item test, a torso type test, worn item by type / code (twice), worn set count, corpse list free, a flag pair | orphan (no call, jump or pointer) | — | not ported | inventory.md |
| `FUN_0063bd00`, `FUN_0063c550` | bare `ret`; called from 2 and 8 places (item moves) | stub | nothing | not ported | inventory.md |
| `FUN_0063be30` | Inventory.cpp callback | stub (`ret 8`) | 26 calls return without an Inventory action | not ported | "Inventory stub" below |
| `FUN_00647640` / `FUN_0056f460` stat 125 | Throw with an item_throwable weapon (stat 125, Properties `throw`): the gate takes it and the do-func throws it as its missiletype | unreachable | no UniqueItems, SetItems, Sets, Runes, Magic prefix / suffix, AutoMagic, CubeMain, Gems or QualityItems row has the `throw` property | the gate (`throw_ready`) takes it; `ammo_index` and the launch don't | throw.md, skills.md "Throw (do 3" |
| `FUN_00653d60`, `FUN_00653db0`, `FUN_00654dd0`, `FUN_00654fe0` | MonStats `+0x32` setter; a one-table unique-name loader; a treasure class by name; a monitempercent row | orphan (no call, jump or pointer) | — | not ported | datatbls.md "MonsterTbls.cpp" |
| `FUN_00663d40`, `FUN_00663e00`, `FUN_00663ed0` | MisStream.cpp: the stream record's allocator, a touch walk and the stream fill | orphan (no call, jump or pointer) | — | not ported | units.md "Misc" |
| `FUN_0066a470`, `FUN_0066a580` | expfield.d2's half size and a byte lookup | orphan (no call, jump or pointer) | — | not ported | datatbls.md "FieldTbls.cpp and AnimTbls.cpp" |
| `FUN_0066ae70` | a level's random room (`FUN_0045c3e0` on the level seed +0x1c4, room count +8, walked along +0x10 / +0x24): `FUN_0066b2b0`'s last fallback for a spot of a level without a spawn list (+0x90), after its waypoint room (`FUN_0066ad80`), a warp slot's room (`FUN_0066b1f0`) and the room at its middle less 2 (`FUN_00642630`) | suspected unreachable | an emu sweep of every non-town level, 3 map seeds (0x1234, 0x9e3779b9, 77), never reached it: each has a waypoint or warp room, or a room at its middle (only map seeds checked, not every seed) | not ported (objgroups.cpp: none added) | drlg.md "Spawn tiles and the start spot" |
| `FUN_00670e70` | allocates a maze room beside a room and picks both rooms' presets (`FUN_00670d70`, `FUN_006709b0`) | orphan (no call, jump or pointer) | the maze generator calls its sibling `FUN_00670de0` | not ported | drlg.md "Ten more DRLG functions" |
| `FUN_00675f80` | corner border coordinates for OutPlace (8 corners, one variant shift) | orphan (no call, jump or pointer) | — | not ported | drlg.md "Ten more DRLG functions" |
| `0x6c7af0`, `0x6c7cc0`..`0x6c7ce0`, `0x6c8030`..`0x6c80a0`, `0x6c80f0`, `0x6c8100`, `0x6c8190`, `0x6c88a0`, `0x6c8a40` | the GDI driver's empty slots: DirectDraw's, plus +0x30 (no options, so no Contrast), +0x34..+0x3c (no cutscene mode) and +0x58 / +0x5c (no gamma) | stub | under `-w`: no startup videos, Gamma greyed out, Contrast ignored | plays the videos (deviations.md); applies neither slider yet | video-drivers.md "The slot map" |
| seq_impinferno (Sk5mode of imp6..8) | imp6..8's sequence Inferno (do 126 from an event-4 row) | suspected unreachable | only a riding imp casts Skill5 (ImpRide's pick); the free Imp think casts Skill4 at most, and no level with imp6..8 (125..127, 135) has a siege beast or tower to ride. Probed live 2026-10-25: 50 s on Nightmare 126/127, attacks and missiles, no Inferno. Unchecked: a mount from a preset or DS1 | ported anyway (ai.cpp) | monster-ai.md "The imps' ride and skills" |
| unit events 0x31, 0x35, 0x39..0x3f, 0x44..0x47, 0x49 | fourteen class quest lines: nine `find` lines (Act 2 radament, summoner, tomb_decoy / true; Act 3 beneath, blackened, drain, guardian, lamesen), two completions (act2_complete_tightspot, the Maggot Queen's; act3_complete_beneath) and act2_begin_tainted, act2_near_tightspot, act2_trigger_field | unreachable | the client maps them to Sounds rows, but no server call or client table makes those ids (the other find lines come from the client's level table 0x72a2c4) | not played | unit-sounds.md "Find lines" |

### ItemMode stub

`FUN_0055f4f0`'s whole body is `RET 4`, discarding one stack argument.
Its eight call sites are in `FUN_00533c70`, `FUN_005380d0`, `FUN_0054bd90`,
`FUN_0054ced0` (two), `FUN_0054d130`, `FUN_0056ac10` and `FUN_00597890`.

### Profile dump

`FUN_006116b0` (Logging\ProfCore.cpp) logs "PROFILING DUMP FREQUENCY %d:"
with `QueryPerformanceFrequency` (line 0x4e), then "FUNCTION: %30s, TIME
%12d, COUNT %5d" (line 0x54) for each of the 23 0x78-byte rows at
`0x743800`: the name at +0, the 64-bit time at +0x68 (the low
dword is logged), the count at +0x70, from
"Sample" and "Server Loop" to "Com Find Path" and "Com Path Step". Both
lines go through `FUN_006248e0`, which writes only while a log file is
open. Only `FUN_00611660` adds to a row and only `0x611730` resets them (no
other instruction operand or data dword points into the rows); neither has
a call, jump or pointer in the image, so every row is 0. The
client calls the dump at 0x44b980, on its way out of the client mode.
Checked by `tools/emu/profile_dump.py`. d2d has no profiler of game.exe's
rows; nothing to port.

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
