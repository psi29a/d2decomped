# Act 1's end — game.exe 1.14d

This file covers the Tools of the Trade (a1q3), Andariel and Sisters to the Slaughter (a1q6), the move to Act 2, and waypoint travel. Conventions follow quests.md: `FUN_0065c310` / `360` / `3a0` test / set / clear a player quest bit, and `FUN_00544350` sets the state.

**The quest event struct** passed to the callbacks is {[0] game, [1] event, [2] npc / monster, [3] player, …, [5], [6]}. For event 3 (enter level, `0x543b90`, called from `FUN_005380d0` when the level changes), **[5] is the level being left and [6] is the level entered**. Tools of the Trade tests [5] == 1, and a1q6 tests [5] == 1 as well as [6] ∈ 34..37.

| event | slot | wrapper |
|---|---|---|
| 0 NPC messages | +0xa0 | `FUN_00545780` |
| 2 talk closed | +0xa8 | `0x543d50` |
| 3 enter level | +0xac | `0x543b90` |
| 4 item into inventory | +0xb0 | `0x543d80` |
| 5 item dropped | +0xb4 | `0x543db0` |
| 6 | +0xb8 | `0x543de0` |
| 8 monster dies | +0xc0 | `0x543a30` (walks the monster's +0x74 list) |
| 9 / 10 player leaves | +0xc4 / +0xc8 | `0x543bd0` |
| 11 message heard | +0xcc | `0x5443b0` |
| 13 join | +0xd4 | |
| 14 | +0xd8 | `FUN_00546270` |

The other record slots are +0xe8 log state, +0xec NPC has a quest, and +0xf0 chain / start.

## Tools of the Trade (quest 3, a1q3.cpp)

a1q3.cpp is at `0x591340..0x5920d0`. The record is set up by `FUN_00591f70`:
- +0xe0 = quest 3; +0xdc = message table `0x737198`; +0xd = 5, +0x10 = 6.
- The quest's own data (rec+0x18 →) is 0xa4 bytes:

| off | meaning |
|---|---|
| +1 | the malus exists in this game (dropped from the stand) |
| +4 | the malus item's unit id |
| +8 | cleared by the reset (`0x5918d0`) |
| +0x98 | 2 once the malus has dropped, 0 after a reset |
| +0x9c | how many players in the game carry the malus |
| +0xa0 / +0xa1 | `data[2]` / `data[3]`, flags for the talk-closed callback |

### Flags (quest 3)

| bit | meaning |
|---|---|
| 0 | done (imbue used) |
| 1 | reward due (imbue available) |
| 2 | given (Charsi spoke, state 2) |
| 3 | state 3 / 4 seen |
| 6 | the malus has been picked up once (the voice line played) |
| 13 | returned in this game |
| 14 | returned by someone else |
| 15 | closed |

Game-wide flag: `FUN_00544720(3, 0xd)` is set when the malus is returned while the state is 4.

### States

| state | set at | by |
|---|---|---|
| 2 | line 0x10c | Charsi's message 0x92 heard (`FUN_00591490`) |
| 3 | line 0x189 | leaving level 1 (the town) while in state 2, `LAB_00591810` |
| 3 | line 0x1c8 | reset `0x5918d0`: the last malus carrier left or the malus was lost |
| 4 | line 0x2c2 | malus stand operated (`FUN_00591ac0`) |
| 5 | line 0xf7 | malus returned while in state 4 |

### Callbacks

The slots are listed with their event numbers from quests.md.

| slot | event | function | behaviour |
|---|---|---|---|
| +0xa0 | 0 NPC messages | `FUN_005916a0` | See "Messages" below |
| +0xa8 | 2 talk closed | `0x5913c0` | `data[2]` → mark the flags (`0x591340`), log 1; `data[3]` → log 1 |
| +0xac | 3 enter level | `LAB_00591810` | See "Enter level" below |
| +0xb0 | 4 item into inventory | `FUN_00591960` | See "Malus picked up" below |
| +0xb8 | 6 | `0x591a90` | count−−; at 0 with +0x98 == 2 → reset `0x5918d0` |
| +0xc4 | 9 quest item carried out | `0x591a20` | count−−; at 0 with `data[1]` and +0x98 == 2 → log 0 refresh |
| +0xc8 | 10 player leaves | `0x591a60` | `FUN_00545240` (participant removal) |
| +0xcc | 11 message heard | `FUN_00591490` | See "Charsi" below |
| +0xd4 | 13 join | `FUN_00591ed0` | bit 2 → state 2, log 1; bit 3 → state 3, log 1 |
| +0xd8 | 14 carrier count | `0x5919d0` | count++; if count == 1, `data[1]` is set and +0x98 == 2 → log 2 |
| +0xe8 | log state | `FUN_00591d30` | See "Log state" below |
| +0xec | NPC has a quest | `FUN_00591c30` | NPC 0x9a (Charsi) |
| +0xf0 | chain | `FUN_00591e40` | |

- **Messages:** when the player has !bit0 or bit13:
  - Holding 'hdm ' (`FUN_00558110`) with clvl ≥ 8 and !bit0 → block 3.
  - Holding it otherwise → nothing.
  - Not holding it, with !bit0 and state ∉ {0, 4}: block `0x737630[state]`, used only if it is valid (≠ −1, < 6).
- **Enter level:** the level being left (ev[5]) must be 1 (the town) and the state 2.
  - If rec+9 == 0 (quest inactive), clear the +0xac slot and stop.
  - Players with bit0 or bit1 are skipped.
  - Otherwise: if the log ≠ 1, log 1 is sent per player (`FUN_00544300(1, 0x5912e0, 1)`, rec+0x14 = 0). Then state 3, mark the flags, and clear the +0xac slot.
- **Malus picked up:** if !bit6, set bit 6 and play voice 0x24 (`FUN_00553380`). Then log 2 refresh (rec+0x14 = 0).
- **Charsi:** see the pseudo-code below.
- **Log state:**
  - bit 1 → 10;
  - bit 13 → 0xd, or 0xb if bit 1 is set;
  - bit 14 → 0xc;
  - state < 5 → rec+0xb;
  - otherwise, with game flag 13 → 12 if clvl ≥ 8, else 4.

  Holding the malus with !bit0 → 2.

Other helpers:
- **`0x591340`, mark flags (per player):** state 2 → set bit 2; state 3 or 4 → set bit 3.
- **`FUN_005912e0`, per-player log send:** calls `FUN_00544190(game, player, 3)` unless (bit 0 or bit 15) and not (bit 13 or bit 14).

### Messages (`0x737198`, block format as quests.md)

State → block, from `0x737630`: 0 → −1, 1 → 0, 2 → 1, 3 → 2, 4 → 3 (the code skips state 4), 5 → 4.

NPC hcIdx: Gheed 0x93, Akara 0x94, Kashya 0x96, Charsi 0x9a, Warriv 0x9b, Cain 0x109. Message ids are decimal.

| block | messages |
|---|---|
| 0 | Charsi 146 (kind 1) |
| 1 | Akara 148, Kashya 149, Cain 147, Charsi 150, Gheed 151, Warriv 153 |
| 2 | Kashya 156, Warriv 159, Charsi 157, Cain 154, Akara 155, Gheed 158 |
| 3 | Kashya 162, Warriv 165, Charsi 163 (kind 0), Cain 160, Akara 161, Gheed 164 |
| 4 | block 3 without Charsi |
| 5 | empty (npc −1) |

Unmarked entries are kind 2. Charsi's 146 (0x92) and 163 (0xa3) are the lines whose "heard" event drives the quest.

### Charsi: message heard (`FUN_00591490`)

```
if npc != 0x9a: return
if msg == 0xa3 && !bit0 && FUN_00558110(player,'hdm '):
    FUN_00545200(participants, player)
    set bit13, bit1                                     // reward due
    FUN_005455b0 (quest message 0xca7)
    FUN_00544160(player,'hdm ')                         // remove the malus
    data+0x9c--
    party (FUN_00540510, fn 0x591430): members with clvl >= 8 && !bit0 && !bit1 -> set bit13, bit1
    if state == 4:
        state 5 (line 0xf7); data[3] = 1
        FUN_00544720(3, 0xd); chain via rec+0xf0
elif msg == 0x92:
    state 2 (line 0x10c); data[2] = 1
```

### Malus stand: object 108, OperateFn 21 = `FUN_00591ac0` (InitFn 15)

```
rec = quest 3 record
if !rec: SetMode(obj,2); voice 0x13; return
if obj already used (mode != 0) or bit0 or bit1: return
if clvl < 8: voice 0x13; return                        // "I can't use this yet"
obj+0xb8 = 'hdm ' (0x206d6468)
FUN_00559a30(2, &ctx, 0, -1, 0)                        // drop the item from the object
SetMode(obj,2)
data[1]=1; data+0x98=2; data+0x9c++; data+4 = item id (or -1)
if state != 4: state 4 (line 0x2c2); mark the flags for every player (FUN_005537d0(0x591340))
if log != 1: log 0 via FUN_00544300(0, 0x5912e0, 0)
```

### Reset (`0x5918d0`)

This runs when the last carrier drops or loses the malus (the +0xb8 path):
```
if !rec+9: return 0
data+0x98 = 0
if !data[1]: return 0
state 3 (line 0x1c8); log 1 per player (rec+0x14 = 1); data+8 = 0
if data[1] == 1: obj = FUN_00552f60(game, 2, data+4); if obj && obj.class == 0x6c: SetMode(obj,0) -> return 1
data[1] = 0
```
The stand re-arms (mode 0), so the malus can be dropped again.

### The Smith (superunique 20)

- SuperUniques row 20: class 402 (`smith`), hcIdx 20, Mod1 5 (extra strong), no minions, TC 890 / 891 / 892.
- It has no class case in `FUN_005a49b0`'s switch (cases 6, 10, 0x1a.., …), so there is no quest record and no MonUMod special. The monster-death dispatcher `0x543a30` walks the dying unit's +0x74 quest list (attached by `FUN_005436b0`), and the Smith gets none, so his death has no quest hook either.
- The Smith is a plain superunique; the malus comes only from the stand.
- **AI 98, row `0x73ca20 + 98*16`** = `{0x5e3890, 0, 1, 0}`. Its think `FUN_005e3890` uses the ctx layout from "Andariel" below:
  ```
  if ctx.inMelee: FUN_005ddf90(game, mon, 4 /*A1*/, tgt); return
  p = clamp(life% (FUN_00621f20: stat6>>8*100 / maxhp>>8), 0, 100)
  FUN_005de190(mon, 0, (100 - p) >> 1, 0)    // aimode data +0x1c = speed bonus, grows as he's hurt
  FUN_005dec80(game, mon, tgt, 7)             // walk to the target
  ```
  It uses no aip parameters and makes no RNG draws in the think.

### Imbue

- **Client menu:** `FUN_004b19a0` (called on NPC open, `FUN_004b66b0`) walks the 9 records at `0x7253e0`, each 5 u32: {npc, quest, bit, needSet, fn}. The count is at `0x7253dc`.
  - When the flag matches, fn patches the NPC's menu record (`0x726c48`, 39 bytes each).

  | npc | quest.bit | fn | patch |
  |---|---|---|---|
  | 0x9b Warriv | 6.0 | `0x4b65f0` | 3 entries; [1] = 0xd36 "go east" → `0x4b5140` |
  | 0xd2 | 14.0 | `0x4b6620` | |
  | 0x96 Kashya | 2.0 | `0x4b6410` | 3 entries; [1] = 0xd45 "hire" → `0x4b5c60` (also when clvl > 7) |
  | 0x9a Charsi | 3.1 | `0x4b3700` | 4 entries; [2] = 0xfb1 "imbue" → `0x4b35b0` |
  | 0x94 Akara | 1.0 | `0x4b6da0` | |
  | 0x1ff | 0x23.1 | `0x4b3730` | |
  | 0x200 | 0x26.1 | `0x4b3760` | |
  | 0x203 | 0x24.0 | `0x4b6650` | |
  | 0x16f | 0x1c.0 | `0x4b6680` | |

  npc-menu.md's "Kashya hire not quest-gated" is only half the story: the entry also opens with quest 2 bit 0.
- **`0x4b35b0` imbue:** closes the menu, sets UI mode `0x7c0c6b` = 7 and opens the item panel at `0x4c0620`. Closing it calls `0x4c02f0` from `FUN_004b3f10`. The panel's send path isn't traced; the server expects C→S 0x38 kind 0 with the cursor item.
- **Server, C→S 0x38: `FUN_00579d60(game, player, kind, npcGuid, itemId)`**, kind 0, npc class 0x9a:
  ```
  result = 7 (fail)
  item = cursor item and == itemId (FUN_00578610)
  require quest3 bit1
  require FUN_0062c590(item)                       // eligibility below
  FUN_00558270(item, &s)                           // 0x84-byte create struct from the old item
  s+0x2a = game+0x78 (word)
  s+0x80 |= 0x20 | (item flags & 0x400000 /*ethereal*/ ? 4 : 2)   // 4 forces ethereal, 2 forbids
  personalized (0x1000000): copy the name
  FUN_0055eea0: remove the old item from the cursor
  s+0x30 = 6 (rare)
  s+0xc  = ilvl = max(clvl,1) + (clvl > 5 ? 4 : 0)  // FUN_00558200
  new = FUN_00558d90(game, &s, 0)                  // seed +0x2c not copied: a fresh item seed
  place: FUN_00560200 (cursor/inventory) else drop at the player (FUN_00555da0 / FUN_00558aa0, mode 3)
  FUN_00591790(game, player): set bit0, clear bit1; unless bit15: rec+0xa = 0, FUN_00544070 (line 0x164)
  result = 6; reply S->C {0x58, npcGuid, result} via FUN_0053d8d0
  ```
  - Quality inside creation (`FUN_00557ab0` → `FUN_00557450`):
    - `FUN_00556f60` returns +0x30 = 6 at once for expansion items, with no RNG; classic items still roll.
    - Quality is forced to 4 before the switch when `FUN_0062e990 == 0`.
    - Case 6 calls `FUN_005c21a0` (rare affixes). If that fails it falls back to `FUN_00557380` / `FUN_00557320` / `FUN_005572a0`.
    - Version > 99: `FUN_00556ca0` ethereal (flag 4 forces it after its rand), then `FUN_005c1940` auto-prefix (Items +0xf8).
- **Eligibility, `FUN_0062c590`.** The item is rejected if any of these hold:
  - type 4 (gold, Items +0x11e);
  - flag 0x1000 or 0x800;
  - Items +0xdc bit 0 clear;
  - throwable (ItemTypes +0x10, `FUN_0062ba80`) and unit+0xc8 bit 0x2000000 clear;
  - a quest item other than 'leg ';
  - socketed children (`FUN_0062a270`);
  - quality 4..9.

  Accepted: quality 1..3 (low, normal, superior).

## Andariel server AI (MonAI 34 "Andariel")

Andariel = MonStats row 156 (`andariel`), AI 34. Skill1 164 AndrialSpray (Sk1mode SQ 14),
Skill2 201 AndyPoisonBolt (Sk2mode A1 4), Sk1lvl/Sk2lvl 1. aidel 15/11/9.
aip1..aip4 are s16[3] at MonStats +0x56/+0x5c/+0x62/+0x68, indexed by difficulty `game+0x6d`.

### AI table

- `FUN_005b15d0(mon, commanded)` returns the AI row:
  - commanded != 0: row = `0x73d358 + mode*16`. Modes 10/11/12 apply only when `FUN_00623470` holds.
  - otherwise: row = `0x73ca18 + MonStats.AI(+0x1e)*16`, for AI < 0x94.
- Row layout: `{+0 targetMode, +4 initFn, +8 thinkFn, +0xc alt}`.
- Row 34 @ `0x73cc38` = `{1, 0, FUN_005f5830, 0}`. It has no init fn.
- `FUN_005b0e00` (setup) sets `ctrl[1] = row+8`, falling back to `LAB_005b0cd0` when that is null. It calls `row+4` when present.

### Per-think driver `FUN_005b1740`

ctx is a local `int[8]`:
- `[0]` AI ctrl (monster data +0x28);
- `[2]` target; `[5]` distance; `[6]` inMelee; `[7]` MonStats rec.

Steps, in order:
1. `FUN_005b10e0`:
   - state 0x15 → idle 3;
   - `FUN_005b0f50` handles door opening (MonStats flag `DAT_006ce274`);
   - `FUN_005b0ff0` handles the pending unit at ctrl+0xc.
2. `FUN_005b1650` picks the target. Row mode 1 → `FUN_005de890`. With no target the think is skipped.
3. `FUN_005b13e0`:
   - `FUN_005b1140`, first-sight speech. It fires when all of these hold:
     - the monster is id 0xfa, or `FUN_005a0180` or `FUN_0063e9f0` is true;
     - dist < 20;
     - the target is a player;
     - `!FUN_005dd220`.

     It then calls `FUN_00553380(0)`, `FUN_005dd230(1)` and idles 20.
   - Then the teleport mod `FUN_005b11f0` (see monsters.md), then the minion leash.
4. `(ctrl+4)(ctx)` → `FUN_005f5830`.

### Target search `FUN_005de890` → `FUN_005dd7f0`

- Picks the nearest enemy within `aidist = MonStats+0x52[diff]`. The field is a byte; 0 means 35.
  - Distance comes from `FUN_005dc530`. A candidate must be < 0x37.
  - `ctx.dist` = that distance. `ctx.inMelee` = `FUN_00622c40(mon, target, 0)`.
- No target found:
  - if `FUN_005dd2b0` (`FUN_005734e0` ∈ {3, 0x13}) && `FUN_0046c140` → wander `FUN_005de200(5)`;
  - else if the `FUN_0064d910` collision test && `FUN_0046c140` → wander 5;
  - else idle n. With d = distance to the nearest player:
    - d < 25 → n = 10;
    - d < 35 → n = d − 10;
    - otherwise n = 25.

### Think `FUN_005f5830` (fastcall ECX=game, EDX=mon, stack ctx)

`rand100` is one inline LCG step on `seed = mon+0x20`:
- `seed = lo*0x6ac690c5 + hi`;
- the result is the new lo, taken as an unsigned `% 100` (MUL 0x51eb851f, SHR 5).

`FUN_0045c390(seed,100)` does the same step through a call.

```
d = game+0x6d; ms = ctx[7]; tgt = ctx[2]
if ctx.inMelee:
    if ms.Skill1 >= 0 && rand100() < aip1[d]:
        FUN_005dead0(game, mon, mode=Sk1mode(+0x180)=14 SQ, skill=Skill1=164, tgt, 0, 0); return
    FUN_005ddf90(game, mon, 4 /*A1*/, tgt); return          // plain melee
if rand100() < aip2[d]:  FUN_005de080(game, mon, 5); return  // idle 5 frames
if rand100() < aip3[d]:
    if ms.Skill1 >= 0 && FUN_0045c390(seed,100) < aip4[d]:
        FUN_005dead0(game, mon, 14, 164, tgt, 0, 0); return  // spray at range
    if ms.Skill2 >= 0:
        FUN_005dead0(game, mon, Sk2mode(+0x181)=4, Skill2=201, tgt, 0, 0); return  // poison bolt
FUN_005de190(mon, 1→7, 0, 0)       // aimode data (+0x2c)+0x18 = 7
FUN_005dec80(game, mon, tgt, 7)    // walk to target
```

RNG draws per think:
- melee: 1 draw;
- range: 1 draw, then a 2nd if not idling, then a 3rd only when the aip3 roll passes and Skill1 ≥ 0.

The walk path itself may draw more; see `FUN_005deb60` flag 2 below.

### Helpers

- **`FUN_005de080(game, mon, n)`**: idle.
  - If mode != 1: set mode NU via `FUN_005a7e60` and `FUN_005a7c20`.
  - Then `FUN_00540e60(2,0)` clears AI event 2.
  - Then it requeues event 2 at `game+0xa8 + max(n,1)`.
- **`FUN_005ddf90(game, mon, mode, tgt)`**: set the mode against the target.
- **`FUN_005dead0(ECX=game, EDX=mon; mode byte, skill, tgt, x, y)`**: use a skill. mode must be < 16. Steps:
  1. `FUN_005a7e60`;
  2. `FUN_006439b0(mon, skill, -1)`, then `FUN_00620210` sets it as the current skill;
  3. `unit+0xc4 |= 0x40`;
  4. `FUN_00649070(path, 1)`;
  5. `FUN_005a7c20(game, &mode, 0)`.

  On failure it idles 10 and returns 0; otherwise it returns 1.
- **`FUN_005dec80(game, mon, tgt, n)`** = `FUN_005deb60(game, mon, tgt, mode 2 WL, 0, 0, pathArg 1, flags n)`.
- **`FUN_005deb60`**:
  - mode 0xf with state 0x3c becomes mode 2 (`FUN_005a61f0`);
  - `FUN_00649070(path, pathArg)`, then set the mode.
  - If `FUN_005a7c20` returns 0:
    - flag 4 → `FUN_00540e60(2,0)`;
    - flag 1 → `FUN_005dd230(1)` (when the AI ctrl exists);
    - flag 2 → `rand100() < 70 ? FUN_005de200(4) : idle 10`.
- **`FUN_005de200(mon, r)`**: random wander.
  - One LCG step: low bit 0 → y = r, x = `FUN_0045c3e0(seed, r)`; low bit 1 → x = r, y = `FUN_0045c3e0(seed, r)`.
    `FUN_0045c3e0` is the plain rand(n): one step, `lo % n` (`lo & (n-1)` for a power of two), 0 when n < 1.
  - Two more LCG steps follow; the low bit of each negates x, then y.
  - Then `FUN_005deb60(0, WL, x+ux, y+uy, 1, 0)`.
- **`FUN_005de190(ECX=mon, EDX=k, a, b)`** → `FUN_005a6260(monData+0x2c, k, a, b)`:
  - +0x18 = k;
  - +0x1c = a if nonzero;
  - +0x20 = b if nonzero, capped at 0x4d.

### Skill dispatch

- `srvstfunc` table @ `0x732140` (< 0x5b); called from the skill-start path.
- `FUN_0056f7f0` is the do dispatcher. It works from the skill row (0x23c bytes):
  - calls `srvdofunc` (skill +0x2e, < 0xbf) through the table @ `0x7322b0`;
  - then, if `srvmissile` (+0x46) ≥ 0, sets `unit+0xc4 |= 0x40` and calls `FUN_0056ecb0`.

  `FUN_0056ecb0` builds one missile:
  - flags 0x21; source = owner pos; target = skill target from `FUN_0056d2c0`;
  - adds flag 0x1000 when the monster has state 0x13;
  - then calls `FUN_0059fa30`. It makes no RNG draws.

#### AndyPoisonBolt (201)

- It has no srvst/srvdo funcs, so it takes the `FUN_0056ecb0` path: one `andypoisonbolt` (203) at the target, fired on the A1 event frame.
- Missile 203: Vel 20, Range 50, 32 dirs, dmg 1280..1792 (<<8 → 5..7).
  - Poison: EMin 32 / EMax 64; MinELev 38/44/40/46/52; ELen 800 +10/lvl.

#### AndrialSpray (164)

- srvstfunc 46 = **`FUN_005cb4d0`**.
  - Gets the target (`FUN_00553540(game, mon)`) and the current skill (`FUN_00620250`). It returns 0 if either is missing.
  - Stores the target position in the skill: `FUN_00644560(skill, x)` and `FUN_006445a0(skill, y)`.
    - For unit types 2, 4 and 5 the position is path +0xc/+0x10.
    - Otherwise it is `FUN_006488c0` / `FUN_00648900`.
  - Returns 1. srvmissile is empty, so srvdofunc does all the firing.
- srvdofunc 88 = **`FUN_005cb580(ECX=game, EDX=mon; skillId, skillLvl)`**. It creates one missile per SQ event frame:
  ```
  missile = Skills[skillId].srvmissilea(+0x48)            // 32 andarielspray
  tx,ty = skill target (FUN_006444a0/4d0); if 0 → FUN_0056d2c0; fail → return 0
  dir64 = FUN_00621dc0(mon, tx, ty)                         // FUN_0064fdc0 from unit pos
  d8    = DAT_00745600[dir64] & 7                           // 64→8: (dir64+4)>>3 &7
  x,y   = unit pos (FUN_0045adf0/45ae20)
  (dx,dy) = DIR32[DAT_006e3188[d8]];  x+=dx; y+=dy          // radius-3 ring point in facing dir
  f = (unit[0x11] >> 8) - 4   (FUN_00621810: current anim frame); f = clamp(f, 0, 8)
  k = DAT_006e3140[d8*9 + f]; if k != 99: (dx,dy)=DIR32[k]; x+=dx; y+=dy   // sweep
  FUN_0059fa30({flags 0x20, owner=mon, src=mon, missile, tx=x, ty=y, level(+0x30)=skillLvl})
  return 1
  ```
  - The missile starts at Andariel's position and is aimed at (x, y).
  - `FUN_0059fa30` gives it velocity `Vel + VelLev*lvl/8`, scaled ×75/100, and range `Range + RangeLev*lvl`.
  - The function makes no RNG draws.
  - `DIR32` (`FUN_0063e7e0`) is dx `DAT_006ea998` and dy `DAT_006ea978`, both signed bytes:
    ```
    dx: 0,-1,-1,-1,0,1,1,1, 0,-1,-2,-2,-2,-2,-2,-1, 0,1,2,2,2,2,2,1, 0,-3,-3,-3,0,3,3,3
    dy:-1,-1,0,1,1,1,0,-1, -2,-2,-2,-1,0,1,2,2, 2,2,2,1,0,-1,-2,-2, -3,-3,0,3,3,3,0,-3
    ```
  - `DAT_006e3188[d8]` = `29,28,27,26,25,24,31,30`.
  - `DAT_006e3140[d8][f]` (99 means no offset):
    ```
    0: 27,14,15, 3,99, 7,21,22,31      4: 31,22,23, 7,99, 3,13,14,27
    1: 26,12,13, 2,99, 6,19,20,30      5: 30,20, 7, 6,99, 2, 1,12,26
    2: 25,10,11, 1,99, 5,17,18,29      6: 29,18,19, 5,99, 1, 9,10,25
    3: 24, 8, 9, 0,99, 4,15,16,28      7: 28,16,17, 4,99, 0,23, 8,24
    ```
  - The sweep crosses the facing direction on anim frames 4..12. Frame 8 (f = 4) aims at the ring point with no extra offset.
- Missile 32 `andarielspray`:
  - Vel 15, Range 40; Pierce, CollideKill, ResultFlags 4;
  - dmg 1792..2560 +1280/lvl (in 1/256 units, so 7..10 +5 per level);
  - poison: EMin 32 / EMax 64 +32/lvl; ELen 400 +10/lvl. Param1 193 and Param2 103 are client-side.
- The SQ event frames come from MonSeq.txt `seq_andarielspray` (MonStats Sk1mode): 18 frames of SC, event 2 on frames 4..12. AnimData ANSCHTH has the same events. So one cast fires 9 sprays, f = 0..8, matching the f-clamp.

### Death

- No Andariel-specific branch was found in the AI (row 34 has no init or alt fn).
- The death visuals come from missile `andycontrol0` (307):
  - client func 29, firewall freq 25, shake 7 s;
  - it spawns `andycolumnfirebase`, `andyfirewallmaker` and `andyfallingdebris1`;
  - 308–319 are client-only.
- Who spawns 307 (the a1q6 kill hook or the MonUMod boss mods in the 31..42 range @ `0x73c0b8`) is left to the a1q6 section.

### Open

- Resolved: the spray's event frames (MonSeq, above) and `FUN_0045c3e0` (rand(n)).
- The direction `FUN_0064fdc0` → `FUN_0064fc60`: the smaller delta ×127 / the larger (16.16, 32-bit) indexes a 128-entry table @ `0x6eb7e0` of `{vx, vy, eighth}`. The eighth steps up at 13, 26, 39, 53, 68, 85 and 105, then folds into its octant; 0 faces +x+y.
- In melee `FUN_00622c40(mon, tgt, 0)`: `FUN_00641530` distance ≤ MonStats2 MeleeRng (+0xe; Andariel 0) + 1, then a path test (`FUN_00622aa0`, mask 0x804). Close up (both sizes < 4, deltas < 8), `FUN_00641530` reads the 8×8 table @ `0x6eb180`, one less when either size is 3.

## a1q6 Sisters to the Slaughter (quest 6) and the move to Act 2

Source file a1q6.cpp; FUN_00544350 line numbers are given as L0x...
`state` is the record's +0xc and is set by FUN_00544350(rec, EDX=state, file, line).
`log` is the record's +0xb and is set by FUN_00544300(rec, EDX=log, player, fn, flag).
`bit n` means a bit of the player's quest-6 flags (pcdata+0x10+diff*4, tested with FUN_0065c310 and set with FUN_0065c360).

### Init FUN_00596990

The record fields are set as follows.

| field | value |
|---|---|
| +0xe0 flag quest | 6 |
| +0xdc message table | 0x7382e0 |
| +0xa | 1 |
| +0xb log | 0 |
| +0xc state | 0 |
| +0xd | 4 |
| +0x10 | 0x25 |
| +0xe8 | 0 |

The callbacks are:

| slot | event | function |
|---|---|---|
| +0xa0 | 0 messages | FUN_00595e20 |
| +0xa8 | 2 talk closed | 0x595b80 |
| +0xac | 3 enter level | FUN_00596010 |
| +0xc0 | 8 monster dies | FUN_005965a0 |
| +0xcc | 11 message heard | FUN_00595c60 |
| +0xd4 | 13 join | 0x596900 |
| +0xec | NPC has quest | FUN_005967f0 |
| +0xf0 | start | 0x5968e0 |

The data block (+0x18) is allocated at L0x345. Its fields:

| offset | contents |
|---|---|
| +0x000 | Cain player-id list |
| +0x084 | Akara player-id list |
| +0x108 | Kashya player-id list |
| +0x18c | Andariel's unit id |
| +0x192 | u16 tick counter |
| +0x194 | Andariel dead |
| +0x195 | Cain talk pending |

Each player-id list is 32 u32 ids plus a u16 count at +0x80. The lists are handled by FUN_00545200 (add), FUN_00545240 (remove), FUN_00545290 (contains) and FUN_00545300 (clear). Init clears all three lists and zeroes +0x18c..+0x195.

### Flag bits (quest 6)

| bit | meaning |
|---|---|
| 0 | done (rewarded by Warriv) |
| 1 | reward due (Andariel killed) |
| 2 | given (Cain spoke, state 2) |
| 3 | in the Catacombs with log 1 |
| 4 | in the Catacombs with log ≠ 1 |
| 13 | took part (set together with bit 1) |
| 14 | cleared by someone else |
| 15 | closed |

Game flag: FUN_00544720(game, EDX=6, 0xd) marks quest 6 as completed for the game.

### State machine

| state | meaning | set by |
|---|---|---|
| 0 | init | |
| 1 | available | timer 0x596580, L0x25d |
| 2 | Cain gave it | message heard, Cain 166, L0xc0 |
| 3 | Catacombs reached | enter level 34..37 at state ≤ 2 (L0x166); or leaving town at state 2 (L0x195) |
| 4 | Andariel dead | FUN_005965a0, L0x2b0 |
| 5 | done | Warriv 183 with bits 1 and 13 (L0xd6); or entering level 0x28 at state 4 (L0x187) |

The state → message-block table is at 0x7382c4: [-1, 0, 1, 2, 3, 4].

Log states are sent through FUN_00595b20. It calls FUN_00544190(6) unless (bit 0 or bit 15) and not bit 13 and not bit 14.

| log | when |
|---|---|
| 1 | Cain talk closed (flag 1); entering level 37 while log < 2 |
| 2 | join with bit 4 |
| 3 | Andariel-dead timer, tick 12, unless log is already 3 or 0xd (flag 1); start timer 0x596580 |
| 0xd | Warriv reward (flag 0) |

Start (+0xf0, 0x5968e0): if state == 0 and +9 == 1, FUN_00543f10(rec, fn 0x596580, 0x14 ticks). 0x596580 sets state 1 if the state is still 0.

### Message blocks (0x7382e0, 0xc4 bytes each, entries {npc, strId, kind})

| block | entries |
|---|---|
| 0 | Cain 166 A1Q6InitCain (kind 1) |
| 1 | Akara 168, Kashya 172, Charsi 169, Cain 167, Gheed 170, Warriv 171 (kind 2) |
| 2 | Kashya 178, Warriv 177 ("my caravan can only go east, if th…"), Gheed 175, Cain 173, Charsi 176, Akara 174 (kind 2) |
| 3 | Kashya 181, Cain 184, Charsi 180, Gheed 182, Warriv 183 A1Q6SuccessfulWarriv ("The caravan is prepared…"), Akara 179. Kinds: 0 for Kashya, Cain, Warriv and Akara; 2 for Charsi and Gheed. |
| 4 | the same entries as block 3, all kind 2 |

hcIdx values: Cain 0x109 (cain5), Akara 0x94, Kashya 0x96, Charsi 0x9a, Gheed 0x93, Warriv 0x9b, Andariel 0x9c.

### FUN_00595e20 (messages; picks the block for one NPC and player)

```
if npc in {Cain, Akara, Kashya} and the player is in that NPC's list      -> block 3
elif bit1                         -> block 4 for Cain/Akara/Kashya, else block 3
elif player in rec+0x1c           -> block 4
elif state==1 and npc==Cain and !bit0                                     -> block 0
elif state!=0 and !(bit0 && !bit13) and !(state>=4 && !bit13) and !(bit0 && bit13)
                                  -> block 0x7382c4[state]
```

### FUN_00595c60 (message heard; ev[5] = npc, ev[6] = strId)

```
Cain 166:  state=2 (L0xc0); data+0x195=1; every player runs 0x595bd0; FUN_00545780
Cain 184:  remove the player from the Cain list
Akara 179: remove the player from the Akara list
Kashya 181: remove the player from the Kashya list
Warriv 183: send the NPC messages;
   if bit1:
     if bit13: log=0xd (flag 0); state=5 (L0xd6); FUN_00544720(6, 0xd)
     clear bit1; set bit0; add the player id to rec+0x1c; FUN_005455b0 (0xca7 msg, FUN_0053d670(6,...))
```

- **0x595b80 (talk closed):** if npc == Cain and data+0x195 == 1: rec+0x14 = 0; log=1 (flag 1); data+0x195 = 0; slot +0xa8 = 0.
- **0x595bd0 (per player):** if !bit0 && !bit1: state 2 → set bit 2; state 3 → set bit 3 if log == 1, else bit 4.
- **0x596900 (join):** does nothing if bit 0 or bit 15 is set. Otherwise the first match wins:
  - bit 4 → log 2, state 3;
  - bit 3 → state 3, log 1;
  - bit 2 → state 2, log 1.
- **FUN_005967f0 (NPC has quest):**
  - Cain: in the Cain list, or (!bit0 && state == 1 && !bit1).
  - Warriv: !bit0 && bit1.
  - Akara and Kashya: in their list.

### FUN_00596010 (enter level; ev[5] = level left, ev[6] = level entered)

```
if level not in 34..37 (0x22..0x25) or the record is inactive:
   if state==4 && level==0x28: state=5 (L0x187)
   elif ev[5]==1 (leaving the Rogue Encampment): FUN_00545310;
        if state==2 and the player has !bit0 && !bit1: state=3 (L0x195); every player runs 0x595bd0
else (Catacombs 1..4):
   if state<=2: state=3 (L0x166)
   if level==37 and log<2: log=1 (flag 1)
   (other catacomb levels: 0x596062 path, log 0 flag 0; not verified)
   every player runs 0x595bd0; FUN_00544070 (L0x180)
```

Catacombs 4 has no other quest hook. Andariel comes from the level preset. Her unit is bound to the quest when it spawns (next section).

### Andariel's death hook

Registration happens in the monster spawn chain: FUN_005b2a00 at 0x5b2edf calls FUN_005b1cf0(unit), which switches on MonStats BaseId (the +2 field of a 0x1a8-byte row).

```
case 0x9c (andariel):
  if hcIdx == 0x2c3 (uber variant): FUN_005a4850(0x17,1); (6,1); (0x1d,1); no quest
  else: FUN_005a4850(0x16,1); FUN_005436b0(unit, quest=6)
```

- FUN_005436b0(unit, EDX=?, id) finds the record with FUN_00543640 and calls FUN_005435c0, which links the record into the unit's +0x74 list.
- FUN_005a4850(unit, slot) writes the byte into the first free slot of monster-data+0x1c (9 slots) and calls the table entry 0x73c008[slot].
- When a monster dies, event 8 (0x543a30) walks unit+0x74. For each record it calls +0xc0, here FUN_005965a0.

FUN_005965a0(rec, ev); ev+8 = monster, ev+0xc = killer:

```
slot+0xa8 = 0
if active && killer has !bit0 && !bit1:
   FUN_00596210(killer): set bit13, bit1;
        client = FUN_005531c0(player) (pcdata+0x9c); FUN_00538680(client, EDX=1, diff)
        -> client progression (client+10 bits 8..12) = max(cur, diff*(4|5)+1)   ; Act 2 unlocked
   rng = FUN_005438b0(game) (= game+0x10f4 + 0x18); 64-bit LCG: lo = lo*0x6ac690c5 + hi; hi = carry
   2 times: monster+0xb8 = 0x7361dc[lo%7]; FUN_00559a30(game, 2, monster, ...)   ; drop
   1 time:  monster+0xb8 = 0x736444[lo%7]; FUN_00559a30(...)
   monster+0xb8 = 0
for every player: FUN_00596260 ; 0x596440 ; 0x5963e0
data+0x18c = monster id; data+0x192 = 1
if active: FUN_00543f10(rec, 0x596500, 1); every player runs 0x596170
data+0x194 = 1; state = 4 (L0x2b0); slot+0xc8 = 0x5961c0; slot+0xc0 = 0
```

The drop tables:

| table | codes | items |
|---|---|---|
| 0x7361dc | gcv gcr gcb gcy gcg gcw skc | chipped gems |
| 0x736444 | gsv gsr gsb gsy gsg gsw sku | standard gems |

The per-player helpers:

- **FUN_00596260:** a player in level 0x25 with !bit0 && !bit15 is added to the three lists, then FUN_00596210.
- **0x596440:** a player with bit 13 runs FUN_00540510(party, FUN_00596320). That does the same for members in act 0 who have !bit0 && !bit1.
- **0x5963e0:** a player with !bit0 && !bit1 gets bit 14 set, then FUN_00545920(player, 6, 0) sends the 0xd82 "cleared by someone else" message.
- **0x596170:** a player with bit1 && bit13 gets FUN_00553380(player, EDX=0x21), a voice event.
- **0x5961c0 (leave):** FUN_00545530, then removes the player from the three lists.

Timer 0x596500 runs every tick until it returns 1:

```
data+0x192++
==10: FUN_005537d0(game, FUN_00596490): each player in level 0x25 ->
      FUN_0056d130(level, x, y, 1, 0, obj 0x3b, 0)     ; town portal at the player
==12: if log!=3 && log!=0xd: rec+0x14=0; log=3 (flag 1). Returns 1 (stop).
```

### Move to Act 2

**Client side** (traced by the sibling agent):

1. FUN_004b19a0 walks the table 0x7253e0 (9 rows {npc, quest, bit, needSet, fn}).
2. Row Warriv / quest 6 bit 0 → 0x4b65f0 changes Warriv's menu to 3 entries. Entry [1] is string 0xd36 (3382 WarrivMenu1b) "go east", handler 0x4b5140.
3. The handler closes the menu and sends C→S 0x38 through FUN_004786d0, 13 bytes: {u8 0x38, u32 0 (kind), u32 npcGuid, u32 0x28}.

**Server** FUN_00579d60(game, player, kind, npcId, arg), kind 0, npc hcIdx 0x9b:

```
if bit0 of FUN_00543520(game, player, 6)   ; the player's quest-6 flags for this difficulty
   FUN_0054b830(game, player, 0x28, 0)     ; move
   FUN_005467e0(game, player, npc)         ; flags
   if FUN_00660e00(0x28, &wp) (Levels.txt+0xe4 != 0xff):
      FUN_00660ec0(pcdata+0x1c+diff*4, wp) ; activate the Lut Gholein waypoint bit
```

Other rows in the same handler: Meshif 0xd2 (quest 0xe → 0x4b) and Tyrael 0x16f (quest 0x1a → 0x6d).

**FUN_0054b830(game, player, level, tile):** acts differ → FUN_0053aec0(game, player, level, tile).

FUN_0053aec0:

```
if act(level) != current act:
   FUN_00537340(game, fromLevel, 0x28):
       FUN_00537230(game, act 1, ...)          ; per-act bookkeeping
       FUN_00545100(player, act 1)             ; NPC-intro flags pcdata+0x60+diff*4 vs list 0x7318c8 (11 NPCs);
                                               ; if changed, S->C 0x1a-byte packet via FUN_0053e060
       FUN_00597310(game, player)              ; a1q4 Cain fallback, below
   FUN_0053acc0(game, player, 0x28, tile)
```

FUN_0053acc0 is the act change:

```
if game+0xbc+act*4 == 0: FUN_0053ac70 -> FUN_006194a0 builds the act (seed game+0x7c)
FUN_005386d0; client = FUN_00537860
remove the unit from the old room (FUN_00646020)
room = FUN_0061b060(act, 0x28, tile, &x, &y, ...)       ; town start, spawn index 0 (see town-start.md)
FUN_0064e7e0(room, &pos, ..., mask 0x1c89, 5)           ; nearest free spot
FUN_00650be0 x2 (move the unit)
FUN_005381f0; FUN_0053b320; FUN_005382e0
unit+0x18 = act; unit+0x1c = act ptr
FUN_0053abe0: S->C 12-byte packet via FUN_0053b390 (act, game+0x7c seed, game+0x80, town level) = 0x03 LoadAct
              FUN_0061c330 -> FUN_0053c900 (10-byte packet)
unit+0xc8 |= 0x10000
player: FUN_005754b0
```

**FUN_005467e0 (Warriv branch):**

```
if !q7.bit0: set q7 bit0, bit13; FUN_005455b0; if pcdata+0x4c != 1: pcdata+0x4c=1; S->C {0x61, 2}
             FUN_00544fa0(act 0)                ; mark the Act 1 NPC list 0x7318b0 introduced
if bit13 && bit0: a1q6 rec->+0xac (FUN_00596010) -> state 4, level 0x28 -> state 5
FUN_00597310                                   ; Cain fallback
```

**FUN_00597310, the a1q4 fallback:** runs when quest 4 is neither done nor reward-due, data+0x50 == 0 and state < 6.

```
FUN_00544070(L0x257); FUN_00596ca0(0,1)
state 7 (a1q4.cpp L0x9fd); log 5 (fn 0x5920d0, flag 1)
FUN_00544720(4, 0xd)
every player runs 0x592b10
```

So Cain always reaches Act 2.

## Waypoint travel (server + client packets), game.exe 1.14d

Waypoint object: objects.bin SubClass (+0x167) & 0x40, OperateFn (+0x1b3) 23 → 0x584e30, InitFn 17 → 0x547210.
Levels.bin +0xe4 = waypoint index (0xff = none). Player waypoint data = pcdata + 0x1c + difficulty(game+0x6d)*4.

### Bitfield (per player, per difficulty)
- 16 bytes: u16 header 0x0102, then 7 u16 words (14 bytes).
- Bit table 0x746428 = {u16 word, u16 mask}[0x70] (count at 0x746424). Index i → word 1 + i/16, mask 1 << (i%16).
- Functions: FUN_00660e50 test; FUN_00660ec0 set; FUN_00660e00 level→index; FUN_00660d90 index→level; FUN_006610b0 copy out.
- New: FUN_00660f30 (header 0x0102, index 0 set). Load from save: FUN_00661030, which **always forces index 0**. So the Act 1 town waypoint (Rogue Encampment) is always active, and no operate is needed.

### InitFn 17: FUN_00547210(ctx {+0 game, +4 obj, +8 room})
```
pending = game+0x10f0 -> +0x1110       // list of {room, x, y, next}, filled by travel (below)
for e in pending:
  if e.room == room || (x,y) inside room rect (+0x4c/+0x50, size +0x54/+0x58):
     if obj == 0 || obj+0x10 (mode) == 0:
        SetMode(obj,1)                                // FUN_00624690
        QueueEvent(1, game+0xa8 + (FrameCnt1(+0xdc)>>8))  // FUN_005417d0; event 1 -> mode 2
     free list (objrgn.cpp:0x19b); game+0x10f0->+0x1110 = 0; return
if IsTown(LevelOf(room)) : SetMode(obj,2)             // FUN_0061ab00 -> FUN_006426a0
```
- Initial mode: 2 ("on") in towns (levels 1, 0x28, 0x4b, 0x67, 0x6d). Everywhere else it is 0, unless the room is the arrival room of a pending travel, in which case it plays mode 1 → 2.
- Object mode is shared by everyone in the game. Activation is per player (bitfield).

### Operate: FUN_00584e30(ECX = {+0 game, +4 obj, +8 player, +0x10 objclass})
```
wp = pcdata+0x1c + diff*4
if FUN_00660e00(ObjLevel, &idx): FUN_00660ec0(wp, idx)       // activate, always
switch obj.mode:
 case 0: SetMode(obj,1)
         QueueEvent(game, obj, 1, game+0xa8 + (FrameCnt1>>8) + 1, 0, 0)   // FrameCnt1=15
         // no packet; the panel does not open on first touch
 case 1,2: if !PlayerBusy(player)          // FUN_00535060: FUN_00554100, cursor item FUN_0063c1e0, pcdata+0x4c
         send S->C 0x63 (below)            // FUN_0053d960 -> FUN_0053b280(buf, 0x15)
         FUN_00554120(player, 2, objId)    // interact: +0x64=id, +0x68=2, +0x6c=1
return 1
```
Object events (table 0x6e19b0):
- ev1 = FUN_00581490: if mode == 1 and Mode2 (+0x141) exists, set mode 2. If HasCollision2 (+0x122) == 0, FUN_00623830.
- ev0 = FUN_00581700: mode 1 → 2, then re-queue ev0 at +15 + rand%35 (seed FUN_00546fa0, LCG 0x6ac690c5).

### S→C 0x63 (21 bytes)
| off | size | field |
|---|---|---|
| 0 | u8 | 0x63 |
| 1 | u32 | waypoint object id |
| 5 | 16 | bitfield (u16 0x0102 + 14 bytes) for the current difficulty |

- Sent only on operate when mode ≠ 0 (a second click, or a waypoint that is already lit, e.g. in town).
- Client FUN_0049cf90: opens the panel (FUN_00455f20(1)). If that fails, return. Then:
  - FUN_0049c7f0;
  - DAT_007bf07d = objId;
  - FUN_00661030(DAT_007bf081, pkt+5), which also forces index 0 client-side;
  - FUN_0049c7f0 builds the rows.

### Client rows and click
- Rows: 9 entries of 5 bytes at 0x7bf03c: {u32 levelId, u8 enabled}.
- Per act, the index range comes from the 11-byte table at 0x7224b1 (+4 first, +5 last index).
- A row is enabled iff its bit is set and levelId ≠ the current level. The current level's row is shown but disabled.
- Hit test FUN_0049c510 (EBX = mx, EDI = my): row i hits if enabled and x ∈ [rx, rx+0x118), y ∈ [ry, ry+0x1e), with rect {rx, ry} = 0x7224f8 + i*24. Returns i or -1.
- Mouse-down stores the row in DAT_007bf06d. Mouse-up FUN_0049d010: if it is the same row:
  - FUN_004786a0(CL = 0x49, EDX = DAT_007bf07d, push row.levelId);
  - close the panel (FUN_0049c6c0, FUN_00455f20(0)).
- FUN_004786a0 is the generic 9-byte send: {u8 cmd, u32, u32} → FUN_00478350(buf) with EDI = 9.

### C→S 0x49 (9 bytes): {u8 0x49, u32 objId, u16 levelId, u16 pad}
Handler FUN_0054c5d0:
```
if len != 9: return 3
if GetTickCount() < pcdata+0x160 + 10000:     // 10 s since last level change
    FUN_00553380(player,?); FUN_00554190(player)   // clear interact: +0x64=-1,+0x68=6,+0x6c=0
    return 1
err = FUN_00549570(game, level; EAX=objId, EDI=player):
    no obj ->1; obj in other act ->2; FUN_00548ef0(obj.x,obj.y)!=0 -> that;
    level==0 ->0; level<levels(+0xc5c) && wp idx valid: bit set ? 0 : 2; else 3
if err: if interact target == objId: FUN_00554190(player); return err-ish
FUN_00584f60(game, player, objId, level); return 0
```
(FUN_00548ef0 and the EDX of FUN_00553380 were not traced.)

### Travel: FUN_00584f60(ECX = game, EDX = player, objId, dest)
```
require obj.OperateFn < 0x65 && operateTbl[OperateFn] == 0x584e30
cur = LevelOf(obj); FUN_00554190(player)
require dest != cur && dest != 0 && bit(FUN_00547f50(player), idx(dest))
spawnIdx = (IsTown(dest) || dest in {0x2e,0x4a,0x85,0x86,0x87,0x88}) ? 0xd : 0
FUN_0053aec0(game, player, dest, spawnIdx):
   other act -> FUN_00537340(cur,dest) + FUN_0053acc0(dest, spawnIdx)   // act change
   same act  -> FUN_0061b060(drlg game+0xbc+act*4, dest, spawnIdx, &x,&y, UnitSize)
                FUN_00554ea0(room, x, y, 0, 0)                          // teleport
if player room == arrival room:
   FUN_005809d0(game, player, 0, 2, px, py, 0)   // player mode dispatch, table 0x6e1740[2]
   FUN_0053b4b0(client, ..., x+3, y+3, ...)       // 13-byte S->C unit/pos packet
FUN_00547170(game, obj, ...)   // push {room, dest wp x, y} onto game+0x10f0->+0x1110 -> InitFn 17 lights it
```

### Arrival placement: FUN_0061b060 → FUN_0066b2b0
- If the level def +0x90 (spawn list) is set: idx 13 → FUN_0066ad80, else FUN_0066ac40.
- Otherwise the chain is FUN_0066ad80 → FUN_0066b1f0 → FUN_00642630 (centre) → FUN_0066ae70.
- FUN_0066ad80 finds the waypoint preset:
  - walk the room2 list (+0x10, next +0x24) where flags +0x28 & 0x30000;
  - walk the presets (+0x5c, next +0xc) with type(+0x14) == 2, id(+4) ≤ 0x23c, objects.bin SubClass & 0x40;
  - tile x = preset+8 / 5 + room+0x34, y = preset+0x18 / 5 + room+0x38.
- Fallback: the room centre (+0x34 + +0x3c/2, +0x38 + +0x40/2).
- Subtile = tile*5 (FUN_00643560), then +3 on x and y. FUN_0064e7b0(room, &xy, size, mask 0x1c09, 0) finds the nearest free spot.
- Teleport FUN_00554ea0:
  - FUN_0064e7b0 (0x1c09), then FUN_00650be0 moves the unit;
  - unit+0xc8 |= 0x10000 (0x800 if param7);
  - FUN_00554670.
  - For a player also: S→C reassign FUN_0053bc50; pcdata+0x148/+0x14c = x, y; +0xa4 = tick; a 20-entry position ring at +0xa8 (index +0xa0); event 0xe at frame + 0x32; FUN_005754b0(x, y).

### Not traced
- FUN_00548ef0: probably a range or position check.
- The EDX of FUN_00553380 (probably a "can't do that yet" sound).
- The exact field mapping of FUN_0053b4b0 (13 bytes: {u8, u8 type ≤ 5, u32 id, u8, u16 x, u16 y, u8, u8}).
- FUN_0053aec0's act-change path.


## Not yet traced

- **Imbue:** the client item panel's send path (`0x4c0620` / `0x4c01e0`) that emits C→S 0x38 kind 0.
- **a1q6:** the log state on Catacombs levels 1–3 (the `0x596062` path).
- **Act load:** the contents of the 10-byte act-load packet (`FUN_0053c900`).
- **Waypoints:** `FUN_00548ef0`; the EDX of `FUN_00553380` in the 10-second refusal; the field layout of `FUN_0053b4b0`.
