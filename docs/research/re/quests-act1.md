# Act 1 quests 2, 4, 5 — game.exe 1.14d

Sisters' Burial Grounds (a1q2), Search for Cain (a1q4) and the Forgotten
Tower (a1q5). Conventions, flag helpers, record layout, message blocks and
the quest log are in `quests.md` (the Den of Evil). Quest init table
`0x731510` (24-byte records: init fn, act, 0x64, flag, id, flag):
a1q2 `FUN_00591210` (2), a1q4 `FUN_005971b0` (4), a1q5 `FUN_00595920` (5).

Callback events used below: +0xa0 NPC messages, +0xa8 talk closes, +0xac
level change (param[5] old level, [6] new level), +0xb0 item event,
+0xb8 event 6, +0xc0 attached unit dies, +0xc4 per quest item on leave,
+0xc8 player leaves, +0xcc message heard ([5] NPC hcIdx, [6] string),
+0xd4 join, +0xd8, +0xec alert, +0xf0 open / chain.

## The quest chain (+0x10, +0xf0)

Each record's +0xf0 is its "open" function. It is called when a quest
before it is done. `FUN_005910f0` (a1q2) is typical:
- state 0 and +9 == 1 → state 1 (given out: Kashya gets her "!");
- state 5 (done) or +9 == 0 (closed in this game) → find record +0x10
  (`FUN_00543640(game, EDX=rec+0x10)`) and tail-jump to its +0xf0.

+0x10: a1q2 → 4 (Cain), a1q4 → 3 (Tools). A quest's completion calls its
own +0xf0, which passes on down the chain.

The root: a game's first join (`FUN_00546270`) closes every quest the
player has done (bit 0 or 15: `FUN_00544410` clears +9/+0xa/+0xb, and the
game's copy gets bit 15), runs the +0xd4 joins, then calls the +0xf0 of
quests 1, 8, 0x12, 0x16, 0x1f (each act's first). The Den's +0xf0
(`FUN_00590620`) passes on to a1q2 only when the Den is done (state 5)
or closed (+9 == 0), and Akara's reward calls it; so Kashya's quest
opens once the Den is done.

## Sisters' Burial Grounds (quest 2, a1q2.cpp)

`0x590830..0x5912e0`. Kashya gives it; Blood Raven (MonStats 267/0x10b)
in the Burial Grounds (level 17) is the kill.

### The record

`FUN_00591210` sets it up. +0xdc = `0x736ce8`, +0xe0 = 2, +0xa = 1
(global), +0xd = 4, +0x10 = 4, state 0.

| field | function | when |
|---|---|---|
| +0xa0 | `FUN_00590b10` | NPC messages |
| +0xa8 | `LAB_00590920` | talk closes |
| +0xac | `FUN_00590fa0` | level change |
| +0xc0 | `FUN_00590ec0` | Blood Raven dies |
| +0xc8 | `LAB_00590c10` | player leaves: remove guid from +0x1c list (`FUN_00545240`) |
| +0xcc | `FUN_00590980` | message heard |
| +0xd4 | `LAB_00591180` | player joins |
| +0xec | `FUN_00591080` | Kashya's "!" |
| +0xf0 | `FUN_005910f0` | open / chain |

Data (+0x18, 0xc bytes, `FUN_0040b430` line 0x27f):

| off | meaning |
|---|---|
| +0 | u8 killed (set 1 on death) |
| +1 | u8 killed (set 1 on death) |
| +2 | u8 killer known (0x590c40 reads it) |
| +3 | u8 "talk-close pending": set by A1Q2InitKashya heard |
| +4 | u32 set 1 on death |
| +8 | u32 killer guid (-1 if none) |

States: 0 closed, 1 given out, 2 Kashya spoke, 3 in Burial Grounds,
4 Blood Raven dead, 5 done (a participant took the reward).
Log states: 1 given, 2 in the Burial Grounds, 3 "return to Kashya"
(after the kill), 0xd done.

### Flags (quest 2)

| bit | set when |
|---|---|
| 0 | reward taken (Kashya's A1Q2SuccessfulKashya heard) |
| 1 | reward due (participant at the kill); cleared with bit 0 |
| 2 | marked in state 2 (Kashya spoke) |
| 3 / 4 | marked in state 3: bit 3 if log == 1, else bit 4 |
| 13 | took part in the kill |
| 14 | killed by someone else |
| 15 | closed (join test only) |

Mark (`LAB_00590890`, per player): if !bit0 && !bit1: state 2 → bit 2;
state 3 → bit 3 if log == 1, else bit 4.

### Blood Raven

`FUN_005b1cf0` (monster created, by MonStats base id) case 0x10b:
MonUMod 0xc and 0x16 (`FUN_005a4850(mod, 1)`), attach quest 2
(`FUN_005436b0`), stat 0x76 = 1 (`FUN_00639db0`).

### Events

- **A1Q2InitKashya (81) heard from Kashya (0x96)** (`FUN_00590980`):
  data+3 = 1, state 2, mark all players, refresh (`FUN_00545780`).
- **Talk closes** (`LAB_00590920`), NPC 0x96 and data+3 == 1: +0x14 = 0,
  log 1 (`FUN_00544300` EDX=1, per player `FUN_00590830`), data+3 = 0,
  **+0xa8 = 0** (one shot), mark all players.
- **Level change** (`FUN_00590fa0`; [5] old, [6] new level):
  1. New level 17 and +9 == 1:
     - state < 3 → state 3, +0x14 = 0;
     - log 0 or 1 → log 2 (per player 0x590830), then mark all; done;
     - else if state was < 3 → mark all; done.
  2. Old level 1 (left town): `FUN_00545310` (drop player from +0x1c).
     If state == 2 and the player has !bit0 && !bit1 → state 3, mark all.
- **Blood Raven dies** (`FUN_00590ec0`), +9 != 0:
  1. state 4; data+1 = 1, data+2 = 1, data+8 = killer guid.
  2. Per player (`FUN_005537d0`, the killer as arg):
     - `FUN_00590c40`: participant = the player's room is the killer's
       room or one of its neighbours (`FUN_00619790` room list). If
       participant && !bit0 && !bit1 → set bits 13 and 1.
     - `LAB_00590e70`: if bit 13 and in a party (`FUN_00554630` != 0xffff)
       → over the party (`FUN_00540510`) `FUN_00590d60`: members whose
       level is in Act 1 (`FUN_006427f0` == 0) with !bit0 && !bit1 get
       bits 13 and 1.
     - `LAB_00590dd0`: !bit0 && !bit1 → bit 14,
       `FUN_00545920(player, EDX=2, 0)` (the "done by others" notice).
     - `LAB_00590e30`: bit 13 → voice event 0x22 (`FUN_00553380`).
  3. Timer 0xf ticks (`FUN_00543f10`) → `LAB_00590bf0`: log 3, per player
     0x590830.
  4. +0xa8 = 0; data+0 = 1, data+4 = 1; game flag 0xd for quest 2
     (`FUN_00544720`).
- **A1Q2SuccessfulKashya (92) heard while bit 1 is set:**
  1. If bit 13 and state != 5: +0x14 = 0, log 0xd (no per-player fn),
     state 5, call +0xf0 (opens Cain).
  2. Set bit 0, clear bit 1, `FUN_005455b0` (quest update), add player
     to +0x1c (`FUN_00545200`), **free hireling** `FUN_00579180(player,
     0x96)`, refresh.
- **Join** (`LAB_00591180`), skipped if bit 0 or bit 15:
  - bit 4 → log 2, state 3
  - bit 3 → state 3, log 1
  - bit 2 → state 2, log 1
- **Quest-log send filter** (`FUN_00590830`): send (`FUN_00544190(game,
  player, 2)`) unless (bit0 || bit15) && !bit13 && !bit14.

### Messages

`FUN_00590b10`:
```
if bit1:                 block 3
elif player in +0x1c:    block 4          // took the reward this game
elif state in 1..3 && !bit0: block 0x737180[state]   // {-1,0,1,2,3,4}
```
State 4 (dead, reward not due) gives no messages.

| block | messages (kind 2 unless marked) |
|---|---|
| 0 | Kashya 81 A1Q2InitKashya (kind 1) |
| 1 | Kashya 82, Warriv 86, Charsi 83, Akara 85, Gheed 84 (A1Q2AfterInit*) |
| 2 | Kashya 87, Warriv 91, Charsi 89, Akara 88, Gheed 90 (A1Q2EarlyReturn*) |
| 3 | Kashya 92 (kind 0), Warriv 96, Charsi 94, Akara 93, Gheed 95 (A1Q2Successful*) |
| 4 | Warriv 96, Kashya 92, Akara 93, Gheed 95 (all kind 2) |
| 5 | empty |

### Alert (`FUN_00591080`)

Kashya (0x96) only, !bit0, and (state 1 && !bit1) or bit1.

d2d: `rules::BurialQuest` (test_quests), wired in `World` like the Den;
`World::kashya_merc` hands out the first offer of her list free.

## Search for Cain (quest 4, a1q4.cpp)

Server side, game.exe 1.14d. Calling convention is fastcall (ECX, EDX); Ghidra drops EDX, so EDX values below come from the disassembly. "rec" is the quest record, "data" is rec+0x18 (0x1bc bytes). Flag helpers: `FUN_0065c310` tests, `FUN_0065c360` sets, `FUN_0065c3a0` clears. The flags pointer is `*(FUN_006221a0(player)+0x10+diff*4)`.

### Init (FUN_005971b0)

Sets rec+0xa = 1, rec+0xd = 6 and rec+0x10 = 3. Allocates data (0x1bc bytes, FUN_0040b430). Calls FUN_00545300 twice to init the two extra GUID lists: data+0xb4 (list A, "missed the rescue") and data+0x138 (list B, "heard Cain's thanks").

| slot | fn | role |
|---|---|---|
| +0xa0 | FUN_00592580 | NPC message selection |
| +0xa8 | FUN_005921b0 | talk closed |
| +0xac | FUN_00596de0 | level change |
| +0xb0 | FUN_00592e20 | event 4; if +9, runs LAB_00592130 over players |
| +0xb8 | FUN_00592e60 | event 6; if !data+0x4f and state < 6: state 3, then 592bd0 |
| +0xc0 | FUN_00593e70 | monster death (Cow King) |
| +0xc4 | FUN_00592c80 | quest item removed |
| +0xc8 | LAB_00592cf0 | player leaves game |
| +0xcc | FUN_00592250 | dialog heard |
| +0xd4 | FUN_00597030 | player join |
| +0xd8 | FUN_00592b90 | counts bkd/bks into data+0x7c |
| +0xdc / +0xe0 | 0x737668 / 4 | message block table, quest id |
| +0xec | FUN_00592fb0 | NPC alert (the "!") |
| +0xf0 | FUN_00593d70 | state 0 -> 1; chains to the next record's +0xf0 when state == 6 or +9 == 0 |

- **FUN_00592c80:** only if +9 and state != 6. If the item is bkd or bks: data+0x7c--. Then, if data+0x7c == 0, state < 6, !+0x4f and +0x47: state 3, then 592bd0 (log 1). Losing the scroll rewinds the quest.
- **LAB_00592cf0:** FUN_00545530, then removes the guid from list A and list B.

### Data (rec+0x18)

| off | meaning |
|---|---|
| +0x00 u16[5] | stone object class required at order step i (0x11..0x15) |
| +0x0c u16 | stones touched so far (index) |
| +0x10 u32[5] | guids of the touched stones |
| +0x28 / +0x49 | Stone Sigil (obj 0x3d) guid / known (FUN_00592f80, special case in FUN_005436b0) |
| +0x2c u16 | "need scroll" voice throttle (voice when `(v & 0x3f) == 0`) |
| +0x30 / +0x47 | Inifuss tree guid / known (tree InitFn 0x593fc0) |
| +0x34 / +0x48 | Gibbet guid / known (InitFn 7, FUN_00544990: sets mode = +0x54) |
| +0x38 | scroll item guid |
| +0x3c | guid of the player who opened the Gibbet |
| +0x40 / +0x44 / +0x45 | StoneAlpha guid for the portal / portal timer pending / portal created |
| +0x46 | Tristram-Cain removal timer pending |
| +0x4a | stone order generated |
| +0x4b | scroll obtained |
| +0x4c | stones re-init on join done |
| +0x4d | stones open |
| +0x4e | scroll deciphered |
| +0x4f | all 5 stones done |
| +0x50 | quest resolved (Cain freed or auto) |
| +0x51 | camp Cain spawned |
| +0x52 | camp Cain should spawn |
| +0x53 | Akara intro log pending |
| +0x54 | Gibbet mode (3 = open) |
| +0x58 | tree mode |
| +0x5c..+0x60 | per-stone reset flag (index +0x4b + objId) |
| +0x61 | Tristram Cain removed |
| +0x62 | Tristram Cain spawn failed |
| +0x63 | joiner had bit 15 |
| +0x64 | decipher log pending |
| +0x66 | fallback town portal made |
| +0x67 | Cain AI destination stored |
| +0x68 | camp Cain guid |
| +0x6c / +0x70 | camp start-position object guid / known |
| +0x74 | a player is in Tristram |
| +0x78 | scroll seen |
| +0x7c | count of bks/bkd in the game |
| +0x80 | camp portal anim counter (> 5 sets +0x92) |
| +0x84 / +0x88 | camp start x / y |
| +0x8c / +0x90 | Wirt gold drops left / initialized |
| +0x91 | Cain entered the portal |
| +0x92 | Cain done walking |
| +0x93 | Tristram Cain talked to |
| +0x95 / +0x98 | Cain AI portal made / guid |
| +0x96 / +0xa4 | camp-side Cain portal exists / guid |
| +0x9c / +0xa0 | Cain AI target x / y |
| +0xa8 / +0xac | portal x / y |
| +0xb0 | set by FUN_005946d0 |

### States (rec+0xc)

| st | meaning | set by |
|---|---|---|
| 0 | not started | |
| 1 | available | FUN_00593d70; stone operate |
| 2 | Akara told | heard 0x61 |
| 3 | left town / rewound | leaving town in state 2; +0xb8; +0xc4 |
| 4 | has scroll | Inifuss operate |
| 5 | deciphered / stones | heard 0x70; stone operate; Tristram entry |
| 6 | done | heard 0x76 |
| 7 | auto-completed at Act 2 | FUN_00596de0 (level 0x28), FUN_00597310 |

### Log states (FUN_00544300, EDX = log)

| log | when |
|---|---|
| 1 | talk closed after 0x61; 592bd0 (rewind) |
| 2 | Inifuss operate gives the scroll |
| 3 | decipher (0x70 heard, then talk closed) |
| 4 | fifth stone touched; entering Tristram before the rescue |
| 5 | Act 2 auto-complete |
| 6 | Gibbet animation ends (FUN_00593290) |
| 0xd | reward given (0x76) |

Per-player update, FUN_005920d0: sends 544190(4) unless (bit 0 or bit 15 is set) and bits 13 and 14 are both clear.

### Flag bits

| bit | meaning | set |
|---|---|---|
| 0 | done | 0x76 reward |
| 1 | reward due (Cain freed) | Gibbet operate / anim end; cleared at reward |
| 2 | Akara told | LAB_00592130 when state == 2 |
| 3 | in progress | LAB_00592130 when state is 3..5 |
| 4 | stones done | stone operate (operator + party via LAB_005936b0) |
| 10 | Cow King killed | FUN_00593e70 |
| 13 | took part in the rescue | Gibbet (FUN_005930b0, FUN_00593130) |
| 14 | missed the rescue | LAB_005931c0; FUN_00592b10 (Act 2) |
| 15 | closed | (tested only) |

LAB_00592130 skips players with bit 0 or bit 1 set.

Game flags (FUN_00544720 sets, FUN_00544760 tests): (4, 13) = Cain rescued game-wide; (4, 11) = cow portal opened.

### Inifuss tree: OperateFn 12, FUN_00593af0

1. If +9 == 0: obj mode 1, return.
2. Requires state < 6, obj mode 0, and player bits 0 and 1 clear.
3. If the player has neither bkd nor bks:
   - voice (FUN_00553380, EDX 0);
   - obj+0xb8 = 'bks ' (forced drop);
   - state 4;
   - `FUN_00559a30(game, obj, 2, &out, 0, -1, 0)` drops the scroll.
   - On success: rec+0xc4 = FUN_00592c80; log 2 (bool 1); +0x4b = 1; +0x38 = guid; +0x7c++; +0x58 = 1; +0x78 = 1; obj mode 1.
   - Then +0x47 = 1 and +0x30 = tree guid.
4. Otherwise: voice (EDX 0x2d + n, "already have it").

Tree InitFn 9 (0x593fc0) sets +0x47 = 1. If +9 == 0 or +0x50, it sets +0x58 = 1. Tree mode = +0x58. With no record, mode 2.

### Akara deciphers (heard, FUN_00592250, npc 0x94, dialog 0x70)

1. Requires bks. Remove it (FUN_00544160).
2. `FUN_005466b0(game, player, 'bkd ', ilvl 0, quality 2, dropIfFull 1)`. FUN_005466b0 is (ECX game, EDX player, code, ilvl (0 = default via FUN_00558200), quality, dropIfFull).
3. If that fails: +0x7c--.
4. On success: +0x64 = 1, +0x4b = 1, +0x4e = 1, +0x38 = guid; state 5; log 3 through 5920d0 (bool 0).

The other Akara dialog: **0x61** (A1Q4InitAkara) sets +0x53 = 1, state 2, then 545780.

### Stone order: FUN_00592e90

```
zero data[0..0xf]
seed = FUN_005438b0()                     // 64-bit game seed {lo, hi}
n = 0
while n < 5:
    seed = lo * 0x6ac690c5 + hi           // 64-bit result -> {lo, hi}
    slot = lo % 5
    if data[slot] == 0: data[slot] = {0x11,0x12,0x13,0x14,0x15}[n++]
```

- data[i] is the stone object id (17..21, StoneAlpha..StoneLambda) that must be touched at step i. The function logs "stone %d is class %d".
- It runs lazily when +0x4a == 0: from the stone operate and from FUN_00593cb0.
- FUN_00593cb0 runs when a player reads bkd (dispatched from FUN_00544840; item 'trs ' goes to 59d6a0 instead). It sends packet 0x879 with data[i] - 0x11 (0..4) via FUN_0053d7e0. This is the order shown on the deciphered scroll.

### Stone operate: OperateFn 9, FUN_00593710

1. Generate the order if needed.
2. If player bit 0 or bit 1 is set: voice, return.
3. If the player has no bkd: when `(+0x2c & 0x3f) == 0` and bits 3 and 4 are clear, voice (EDX 0x27 + n). Then +0x2c++ and return.
4. Return if +9 == 0 or state > 5.
5. The sigil comes from +0x28 if +0x49. If !+0x4e, the sigil is obj 0x3d and state == 0: state 1.
6. Return if +0x4f.
7. If state != 5: set state 1 when it is 0; require bkd; set state 5.
8. **If the touched class != data[idx]: return.** A wrong-order touch is silently ignored. There is no reset and no feedback; progress is kept.
9. data+0x10+idx*4 = stone guid; idx++. Return if the stone's mode != 0.
10. If idx < 5: stone mode 1; sigil mode = idx + 1.
11. If idx == 5:
    - stone mode 1; sigil mode 6; +0x4f = 1;
    - remove bkd; +0x7c--;
    - find StoneLambda (0x15) coords, from the touched guids or else from the party units' +0x74 lists;
    - `FUN_0056ede0(game, room, 0, 1, 0x120, x+6, y-3)` spawns missile 288, CairnStones;
    - FUN_0061aed0(room, 0);
    - if log < 4: log 4 (bool 1); in a party, FUN_00540510(LAB_005936b0) gives bit 4 to act-0 members with bits 0 and 1 clear;
    - the operator gets bit 4;
    - FUN_00545760(game, 1): questlist+0x20 = 1, then FUN_005456f0 resyncs flags (0xca7 / 0xd0e).

### Tristram portal

- CairnStones server functions FUN_005abe50 / FUN_005af240 call **FUN_005a9930**(level = missile record +0x44).
- FUN_005a9930 calls `FUN_0056d130(room, x, y, level, 0, 0x3c, 1)`: object 60, permanent red portal. Then FUN_0061aed0(room, 1).
- The level should be 38 (0x26) from the missiles.bin param. This is inferred: the extracted Missiles.txt lacks the func columns.

Re-created on join by stone InitFn 6, **FUN_005935e0** (not a Ghidra function; read from the disassembly):

```
if rec && rec+9 && !+0x4c:
    if +0x4d || +0x50: mode 2
    else if data[0x4b+id] == 1: clear it, mode 0
else:
    +0x4c = 0
    if !+0x45 && id == 0x11:
        +0x40 = guid
        if !+0x44: +0x44 = 1; timer FUN_00592d50, 1 tick     // FUN_00543f10(rec, fn, ticks)
    mode 2
no record: mode 2
```

FUN_00592d50: `FUN_0056d130(room, x+4, y+4, 0x26, 0, 0x3c, 1)`, then +0x45 = 1 and +0x44 = 0.

### Gibbet: OperateFn 10, FUN_00593480

1. Proceeds when there is no record, or when +9 is set, !+0x50 and state < 6.
2. If player bits 0 and 1 are clear and obj mode is 0:
   - obj mode 1;
   - FUN_005417d0(1, frame, ...) and FUN_005417d0(7, game+0xa8 + 0x11, ...) schedule the anim end;
   - +0x54 = 3; +0x3c = player guid;
   - FUN_0061aed0(room, 0);
   - set bits 13 and 1; 5455b0;
   - in a party: 540510(FUN_005930b0), which gives bits 13 and 1 to act-0 members with bits 0 and 1 clear.
3. Otherwise: voice (EDX 0x13).

### Cain rescued: FUN_00593290 (Gibbet mode end, dispatched by FUN_005449e0 for obj 0x1a)

1. If +9 and !+0x50: +0x54 = 3; gibbet mode 3.
2. `FUN_005b2f20(x+3, y+3, 0x92, 1, -1, 0)` spawns Tristram Cain, retrying with FUN_00545340.
3. **Success:** unit flags |= 0x3000000; voice(0) on the opener (+0x3c).
4. **Failure:**
   - log "Cain not created, look in town" (a1q4.cpp line 0x6b6); +0x74 = 0;
   - LAB_00593220 looks for a player in level 0x26. If one is found and !+0x66: town portal object 0x3b to level 1 at (x+6, y+6), and +0x66 = 1;
   - if !+0x51: +0x52 = 1 (Cain goes straight to camp);
   - +0x62 = 1.
5. Over players:
   - FUN_00593130: players in level 0x26 with bits 0 and 1 clear get bits 13 and 1, 5455b0, and party 5930b0;
   - LAB_005931c0: players with bits 0 and 1 clear get bit 14, and FUN_00545920(player, 4, 0) sends packet 0xd82.
6. Log 6 (bool 1).

**Tristram Cain AI** (FUN_005e7130 / 5e77a0 / 5e7880) walks Cain to a portal:

| fn | role |
|---|---|
| FUN_005944b0 | stores the destination (+0x9c, +0x67) |
| FUN_005943b0 | creates a portal via FUN_00555230 (+0x95, +0x98) |
| FUN_00594450 | gets the portal coords |
| FUN_00594360 | returns the coords while +0x96 and !+0x92 |
| FUN_005944f0 | Cain enters: +0x91 = 1, +0x52 = 1; spawns camp Cain if the start pos is known (FUN_005940e0 path); camp-side portal (+0x96, +0xa4) |
| FUN_005945f0 | sets +0x92 |
| FUN_00594610 | reads +0x93, used by FUN_00572c10 (npc 0x92 interact) |

- Talk closed with npc 0x92 sets +0x93 = 1.
- Cain portal: InitFn 61 is FUN_00594290 (mode 1, anim timer). Obj 0xbd uses mode-end FUN_005942c0: 1 -> 2; 2 -> 3 once +0x92 (camp counter +0x80 > 5) or +0x91; 3 -> 4.

**FUN_00596ca0(resolve, removeTristCain)**, EDI = rec:

1. If the gibbet is known (0x1a): set mode 3 if it isn't already. +0x54 = 3.
2. If the tree is known: tree mode 1.
3. If arg1 and !+0x46: +0x46 = 1 and timer LAB_00593260 (1 tick), which runs 5928c0 over monsters and clears +0x46.
4. Else if arg2: run FUN_005928c0 over monsters now.
   - FUN_005928c0 handles monster 0x92. If FUN_00572dc0 == 0: remove the unit (FUN_005a7e60 / 5a7c20) and set +0x61 = 1. Otherwise queue FUN_00592880, which sends packet 0x265 / 0x1045d.
5. If arg2:
   - if !+0x61: FUN_00543140(game, 1, 0x92, 0);
   - +0x50 = 1;
   - if !+0x51: +0x52 = 1.
6. Else: +0x50 = 1.

### Cain in camp

- **Start position:** InitFn 54, FUN_005940e0. Sets +0x6c = guid, +0x70 = 1, +0x84 / +0x88 = x / y. If +0x52 and !+0x51, calls FUN_00592960.
- **FUN_00592960:** `FUN_005b2f20(x, y, 0x109, 1, 5, 0)`. It retries with radius 10 up to 20 times, then 15. Then flags |= 0x3000000, +0x51 = 1, +0x52 = 0, +0x68 = guid.
- **Level change into town** (FUN_00596de0, new level 1): if +0x70, the start object exists, +0x52 == 1 and !+0x51, calls FUN_00592960.
- **Heard, npc 0x109:**
  - 0x7d: add to rec+0x1c; remove from list A; 545780;
  - 0x7e / 0x7b: add to list B; 545780.

### Reward (heard, npc 0x94, dialog 0x76)

1. Requires bit 1. Set bit 0, clear bit 1; add to rec+0x1c; 5455b0.
2. `FUN_005466b0(game, player, 'rin ', ilvl, quality, 1)`:

   | diff | ilvl | quality |
   |---|---|---|
   | Normal | 7 | 4 (magic) |
   | Nightmare | 0x1e (30) | 6 (rare) |
   | Hell | 0x3c (60) | 6 (rare) |

3. FUN_005458e0(player, 4): packet 0xd69. Then 545780.
4. If bit 13: log 0xd; if state != 6, set state 6. If !FUN_00544760(game, 4, 13): FUN_00544720(game, 4, 13), then call +0xf0.
5. If +0x63 == 1: FUN_00544720(game, 4, 13).

Akara (and the gossip NPCs) also deliver A1Q4QuestSuccessful to players with bit 1.

### Talk closed: FUN_005921b0

- **npc 0x94:**
  - if +0x53: log 1 (bool 1), clear it, LAB_00592130 over players;
  - if +0x64: log 3, LAB_00592130, clear it.
- **npc 0x92:** +0x93 = 1.

### Join: FUN_00597030

| joiner has | effect |
|---|---|
| bit 0 | +0x52 = 1; game flag (4, 13); +0x54 = 3; +0x4c = +0x4d = 1 |
| bit 15 | same as bit 0, but +0x63 = 1 instead of the game flag |
| bit 4 | log 4, state 5; +0x4c, +0x4d, +0x4e, +0x78, +0x58 = 1 |
| bit 3 | state 3, log 1 |
| bit 2 | state 2, log 1 |

Then it counts the joiner's bks/bkd into +0x7c:
- bkd: state 5, log 3, +0x4e, +0x78, +0x58 = 1;
- bks: state 4, log 2, +0x78, +0x58 = 1.

### Level change: FUN_00596de0

| transition | effect |
|---|---|
| enter 0x26 (Tristram) | if !+0x51, !+0x50 and state > 5: state 5, log 4 (bool 0), LAB_00592130 |
| leave 1 (town) | 545310; remove from list A; if bits 0 and 1 clear and state == 2: state 3 |
| enter 1 (town) | camp Cain spawn (see above) |
| enter 0x28 (Lut Gholein) | Act 2 auto-complete (below) |

### Not rescued before Act 2

On entering level 0x28, if the player has bits 0 and 1 clear, !+0x50 and state < 6:

1. FUN_00592860 (log);
2. FUN_00596ca0(0, 1): Tristram Cain is removed, +0x50 = 1, +0x52 = 1 (the Rogues rescue him and he appears in camp);
3. state 7; log 5 (bool 1);
4. game flag (4, 13);
5. FUN_00592b10 over players: those with bits 0 and 1 clear get bit 14 and join list A;
6. +0x58 = 1.

- **Result:** no ring. Camp Cain says A1Q4RescuedByRoguesCain (block 6, list A). He still offers identify (0x7d moves the player from list A to rec+0x1c; bit 14 then selects block 8).
- FUN_00597310(player guid) does the same without +0x58. It has no static callers; it is probably the Warriv / act-transition path.

### Messages: FUN_00592580

Block table 0x737668 (0xc4 bytes per block); state table 0x737648 = {-1, 0, 1, 2, 3, 4}.

```
if npc == 0x92: block 9
if +0x4f and player has bkd: remove bkd, +0x7c--
if npc == 0x109 and !listB and bit13: block 5
if bit1: npc == 0x109 ? (listB ? block 7 : block 5) : block 5
if listA: block 6
else if rec+0x1c has guid:
    npc == 0x109 and !listB -> block 5
    bit14 -> block 8
    bit0  -> block 7
else:
    return if bit14 || state == 0 || bit0 || bit15
    has bks    -> block 3
    state == 4 -> block 2
    state <= 5 -> block stateTable[state] (if != -1)
```

| blk | entries (npc: string key) |
|---|---|
| 0 | Akara: A1Q4InitAkara |
| 1 | Akara / Kashya / Charsi / Gheed / Warriv: A1Q4AfterInitScroll* |
| 2 | Kashya / Warriv / Akara / Gheed: A1Q4EarlyReturnS*; Charsi: A1Q4InstructionsCharsi |
| 3 | Kashya / Warriv / Charsi / Gheed: A1Q4SuccessfulScroll*; Akara: A1Q4InstructionsAkara (0x70, decipher) |
| 4 | Kashya / Warriv / Charsi / Akara / Gheed: A1Q4EarlyReturn* |
| 5 | Kashya / Warriv / Charsi / Gheed / Akara (0x76, reward): A1Q4QuestSuccessful*; Cain (camp): A1Q4QuestSuccessfulCain |
| 6 | Cain (camp): A1Q4RescuedByRoguesCain |
| 7 | Cain / Akara / Gheed: A1Q4QuestSuccessful* (repeat) |
| 8 | Cain (camp): A1Q4RescuedByRoguesCain (repeat) |
| 9 | Cain (Tristram): A1Q4RescuedByHeroCain |

Mapping by state: 1 -> blk 0, 2 -> blk 1, 3 -> blk 2, 4 -> blk 3 (with scroll) or blk 2 (without), 5 -> blk 4.

### Alert: FUN_00592fb0

- **Akara (0x94):** true if any of:
  - state 4, bits 0 and 1 clear, and the player has bks (FUN_00554010);
  - state 1, bits 0 and 1 clear;
  - state 6, bit 13 set, bit 0 clear;
  - bit 1 set.
- **Cain (0x109):** true if the guid is in list B, or if it isn't and bit 13 is set.

### Cow King (bit 10)

- Superunique 0x27 (the Cow King) attaches quest 4 in FUN_005a4440 / FUN_005a49b0.
- **Death, FUN_00593e70:**
  1. Return if the killer has bit 10.
  2. Require Diablo done (quest 0x1a bit 0, classic, game+0x70 == 0) or Baal done (quest 0x28 bit 0, expansion).
  3. Set the killer's bit 10. LAB_00593e30 sets bit 10 for every player in level 0x27. FUN_00545990 is a no-op.
  4. unit+0xb8 = 'vps ', then FUN_00559a30(game, unit, 0, ...) x8.
  5. With no killer guid, it skips straight to the level-0x27 iteration.
- **Cow portal, FUN_00594140** (no static xrefs; cube recipe):
  - fails if game flag (4, 11) or bit 10 is set;
  - requires Diablo/Baal done and the player in level 1;
  - `FUN_0056d130(room, x, y, 0x27, 0, 0x3c, 0)`, then game flag (4, 11);
  - on failure: voice.
- **Wirt's body** (obj 0x10c), FUN_00594630: the first time, the drop count is rand(seed, 0x14) (FUN_004bc500, +0x8c / +0x90), and obj+0xb8 = 'gld '. Each operate drops gold (FUN_00559a30 mode 2) until the count runs out.

### Helpers

- GUID lists (u32[32], u16 count at +0x80):

  | fn | role |
  |---|---|
  | FUN_00545200 | add |
  | FUN_00545240 | remove |
  | FUN_00545290 | test |
  | FUN_005452c0 | test rec+0x1c |
  | FUN_00545310 | remove from rec+0x1c |
  | FUN_00545300 | init |

- Other helpers:

  | fn | role |
  |---|---|
  | FUN_00553380 | object voice / sound (EDX = id) |
  | FUN_005b2f20 | spawn monster |
  | FUN_0056d130 | spawn object |
  | FUN_0056ede0 | spawn missile |
  | FUN_00540510 | iterate party |
  | FUN_005537d0 | iterate units |
  | FUN_00543f10 | quest timer |

## The Forgotten Tower (quest 5, a1q5.cpp)

Code: 0x594700..0x595b20. Init `FUN_00595920`. Everything below is server side.
Level ids: 20 (0x14) = Forgotten Tower, 25 (0x19) = Tower Cellar Level 5.
Notation: `rec` is the quest record. `rec+9` = active, `rec+0xa` = open, `rec+0xb` = log state, `rec+0xc` = state.
`d = rec+0x18` is the data block. "bit n" means the player's quest-5 flag bit n (`FUN_0065c310` tests it, `FUN_0065c360` sets it, `FUN_0065c3a0` clears it).
"Mark all" = `FUN_005537d0(game, 0, 0, LAB_00594890)`.
"Set log L" = `rec+0x14 = 0; FUN_00544300(rec, EDX=L, 0, FUN_00594830, 1)`.

### Init `FUN_00595920`

| slot | fn | role |
|---|---|---|
| +0xac | `FUN_00595010` | level change |
| +0xcc | `FUN_00594960` | message heard |
| +0xa0 | `FUN_00594c50` | NPC messages |
| +0xc8 | `LAB_00594bb0` | player leaves |
| +0xc0 | `FUN_00595710` | Countess died (cleared to 0 once she dies) |
| +0xd4 | `LAB_00595860` | player joins |
| +0xec | `FUN_005952c0` | NPC alert |
| +0xf0 | `FUN_00595240` | chain |

Other fields: +0xe0 = 5 (flag), +0xdc = 0x737ed8 (msg blocks), +0xb = 0, +0xa = 1, +0xc = 0, +0xe8 = 0, +0xd = 4, +0x10 = 3 (chain → quest 3).

Data: `FUN_0040b430(a1q5.cpp, 0x40f)`, 0x120 bytes, zeroed. `FUN_00545300` inits the list at d+0x8c.

### Data (0x120)

| off | meaning |
|---|---|
| +0x00..+0x2c | list A: u32 guid[12]; count u16 at +0x30. Players who were told, or had their success talk |
| +0x34..+0x60 | list B: guid[12]; count u16 at +0x64. Killed while in cellar 5; success talk still due |
| +0x68..+0x84 | chest unit ids[8]; count u16 at +0x88 |
| +0x8c | framework guid list (32 entries; count u16 at list+0x80 = d+0x10c). Cellar-5 participants (`FUN_00545200` add, `FUN_00545290` find, `FUN_00545240` remove) |
| +0x110, +0x114 | Countess death x, y |
| +0x118 | Countess dead |
| +0x119 | treasure spawned |
| +0x11a | first success talk pending |
| +0x11b | tome read while log == 0 |
| +0x11c | cleared by the timer |

List helpers (inline in most callers):
- `FUN_00594740(EAX=d, EDI=guid, arg0)`: remove from A (arg0 = 0) or B. Swap-with-last.
- `FUN_00594700` (append) and `FUN_005947e0` (contains) have no xrefs, raw pointers or rel32 calls. They are **dead code**, as is `FUN_00595160`.

### States and log

- rec+0xc (state):
  - 0 = initial;
  - 2 = started (tome read, or Tower entered);
  - 3 = cellar reached, or left town after starting;
  - 5 = Countess dead.
- **States 1 and 4 are never set** in a1q5.
- Quest-log table row 5 (0x723f30 + 0x50): shown 1, icon 4, slot 3, act 0, record 0x7238a4, flag 5.
- Record 0x7238a4: name 3718 `qstsa1q5`; the message for every state is 127.

| log (rec+0xb) | string |
|---|---|
| 1 | 3751 "Look for the Tower in the Black Marsh beyond the Dark Wood." |
| 2 | 3754 "Dispose of the evil Countess." |
| 3 | 3752 "Explore the cellar dungeons beneath the Tower ruins." |
| 4 | 3753 `qstsa1q51b` (same text as 3) |
| 5..10 | 3725 `qstsnull` |
| 11 | 3728 `qstsprevious` |
| 12 | 3727 `qstsother` |
| 13 | 3726 `qstsComplete` |

- Per-player log send `FUN_00594830`: calls `FUN_00544190(5)` unless (bit 0 && !bit 13 && !bit 14).

### Flag bits

| bit | set by | meaning |
|---|---|---|
| 0 | kill: `FUN_00594f10`, `FUN_00594dd0`, `FUN_00595420`, `LAB_00595370` | done. There is no reward talk; bit 0 is set directly at the kill |
| 1 | only cleared (`FUN_00594f10`) | reward pending (unused) |
| 2 | mark, state 2 | progress |
| 3..6 | mark, state 3, log 1..4 | progress |
| 13 | kill (same functions as bit 0) | took part in the kill |
| 14 | kill: `FUN_00594f10` (not in cellar 5), `LAB_00595320` | done by another player / not present |
| 15 | not in a1q5 | join skips the player if set |

Mark `LAB_00594890(player)`:
- Skip if bit 0 or bit 1.
- state 2 → bit 2.
- state 3 → jump table 0x59494c by log: log 1 → bit 3, log 2 → bit 4, log 3 → bit 5, log 4 → bit 6.

### Tome (objects.txt 8)

- **InitFn 4 `FUN_00595a00`:** if !rec+9 → `FUN_00624690(unit, 3)`, so mode 3 = already read / opened.
- **Operate `FUN_00594e70(ctx)`** (ctx: [0] game, [1] unit, [2] player, [4] objtxt):
  ```
  if (!unit || unit->mode == 0) {
      FUN_00624690(unit, 1);                                  // mode 1 = operating
      FUN_005417d0(game, unit, 1, game+0xa8 + (objtxt+0xdc >> 8), 0, 0);  // end-of-anim event
  }
  if (rec+9) {
      FUN_005456a0(player, unit, 0x7f);   // msg 127 A1Q5InitQuestTome -> heard callback
      if (state < 2) {
          state = 2;                      // line 0x202
          if (log == 0) d+0x11b = 1;
      }
  }
  ```

### Heard `FUN_00594960(rec, ev)`

ev[5] = NPC hcIdx, ev[6] = msg.

```
if (msg == 0x7f && rec+9) {              // no NPC check
    bv = false;
    if (d+0x11b == 1) {
        if (log == 0) { set log 1; bv = true; }
        if (log == 3) {
            set log 2; bv = true;
            if (state < 3) state = 3;    // line 0x129
        }
    }
    if (state < 2) { state = 2; mark all; }  // line 0x130
    else if (bv) mark all;
}
if (npc in {0x9a Charsi, 0x96 Kashya, 0x109 Cain(camp), 0x9b Warriv, 0x94 Akara, 0x93 Gheed}
    && msg in 0x8c..0x91) {              // A1Q5Successful*
    if (bit13 && d+0x11a) {
        d+0x11a = 0;
        state = 5;                       // line 0x14c
        rec->0xf0();                     // chain
    }
    if (guid in B) { remove from B; if (A.count < 12) append to A; }
}
```

### Level `FUN_00595010(rec, ev)`

ev[5] = old level, ev[6] = new level.

```
if (rec+9 && new == 20) {
    if (state == 0) { state = 2; set log 3; }                // line 0x267
    else if (state <= 3 && log == 1) set log 4;
    else return;
    mark all; return;
}
if (rec+9 && new == 25) {
    if (state > 3 || log == 2) return;
    if (state != 3) state = 3;                               // line 0x27a
    set log 2; mark all; return;
}
if (old == 1) {                                              // left town
    if (state == 2) { if (!bit0) state = 3; }                // line 0x28d
    else if (state == 5 && rec+9) {
        if (FUN_00594740(d, guid, 0) && A.count == 0 && B.count == 0)
            rec+0xa = 0;
    }
}
```

### Messages `FUN_00594c50(rec, ev)`

Block sent with `FUN_00543790(npc hcIdx, block)`.

```
if (!rec+9) return;
if (bit0 && !bit13) return;
if (state > 3 && guid not in B && guid not in A) return;
if (guid in B)                 block = 2;
else if (bit0 && bit13)        { if (guid not in A) return; block = 3; }
else { block = tbl_7382ac[state]; if (block == -1 || block > 4) return; }
                               // tbl_7382ac = {-1,-1,0,1,2,3,-1}
```

Blocks at 0x737ed8 (keys are `A1Q5<column><NPC>`; string ids per NPC):

| blk | kind | column | Kashya | Warriv | Charsi | Akara | Cain | Gheed |
|---|---|---|---|---|---|---|---|---|
| 0 | 2 | AfterInit | 133 | 132 | 129 | 130 | 131 | 128 |
| 1 | 2 | EarlyReturn | 134 | 136 | 137 | 138 | 135 | 139 |
| 2 | 0 | Successful | 140 | 141 | 144 | 143 | 145 | 142 |
| 3 | 2 | Successful | 140 | 141 | 144 | 143 | 145 | 142 |
| 4 | – | empty | | | | | | |

- Block 2 is kind 0 (a new message, shown with an alert).
- Block 3 is kind 2 (a replay).

### Alert, chain, leave, join

- **Alert `FUN_005952c0(rec, npc, player)`:** returns 1 iff the guid is in B and npc ∉ {0x9b Warriv, 0x93 Gheed}.
- **Chain `FUN_00595240(rec)`:**
  - Returns 1 if (state < 2 && rec+9 == 1) || (state != 5 && rec+9).
  - Otherwise tail-calls quest 3's +0xf0 (`FUN_00543640(rec+0x10)`).
- **Leave `LAB_00594bb0`:** removes the guid from d+0x8c (`FUN_00545240`), then from B, then from A.
- **Join `LAB_00595860`:** skip if bit 0 or bit 15. Otherwise the first match wins:

  | bit | state | log |
  |---|---|---|
  | 4 | 3 | 1 |
  | 6 | 3 | 4 |
  | 5 | 2 | 3 |
  | 3 | 3 | 1 |
  | 2 | 2 | 1 |

### Countess death `FUN_00595710(rec, ev)`

ev[2] = the dying Countess, not the killer (`FUN_005439a0` walks the quest list on the unit).

```
FUN_00620870(countess, d+0x110);                   // always: record x, y
if (rec+9) {
    FUN_00544070(line 0x3b1); rec+0xa8 = 0;
    state = 5;                                     // line 0x3b3
    d+0x11a = 1;
    for each player: FUN_00594f10;
    FUN_00544720(game, 5, 0xd);                    // game quest flag
    if (B.count == 0) rec+0xa = 0; else rec+0xc8 = LAB_00594bb0;
    d+0x118 = 1; rec+0xc0 = 0;
    FUN_00544720(game, 5, 0xd);
    for each player: FUN_00594dd0;
    FUN_00595420(game, d+0x8c, 5, voice 0x25);
    for each player: LAB_005953d0;
    for each player: LAB_00595320;
    FUN_00543f10(rec, 0x5954c0, 7);                // timer
}
FUN_005954f0(game, d);                             // treasure
if (!d+0x119) FUN_005417d0(game, countess, 7, frame+10, 0, 0);   // retry
```

Per-player passes at the kill, in order:

1. **`FUN_00594f10`**, if !bit 0:
   - in level 25 (`FUN_0061a1b0(room) == 0x19`): set 13 and 0, clear 1, voice `FUN_00553380(player)` (0x25), append the guid to B (cap 12);
   - not in level 25 → set bit 14.
   - So **participants = anyone in cellar 5 at the moment of death**. There is no party or room check.
2. **`FUN_00594dd0`:** !bit 0 && !bit 1 && level 25 → add to d+0x8c, set bits 0 and 13. It never fires in practice because pass 1 already set bit 0.
3. **`FUN_00595420(game, list, q, voice)`:** each member of d+0x8c lacking bit 0 → set 13 and 0, plus voice if voice != 0.
4. **`LAB_005953d0`:** bit 13 && party (`FUN_00554630`) != 0xffff → `FUN_00540510(game, LAB_00595370, 0)` over the party.
   - `LAB_00595370`: a party member lacking bit 0 whose level is in act 0 (`FUN_006427f0 == 0`) → set 13 and 0. So partied players anywhere in Act 1 also get credit.
5. **`LAB_00595320`:** still lacking bit 0 → set bit 14 and `FUN_00545920(player, 5, 0)`.

- **Timer 0x5954c0:** if state == 5 → set log 13 (per-player `FUN_00594830`); d+0x11c = 0; return 1.
- **Reward:** none beyond the drop and the tower treasure. Completion = bit 0 at the kill. The success talk (block 2) only moves the player from B to A and fires the chain.

### Tower treasure

- **Chest** = objects.txt 371 LargeChestR, InitFn 47 **`FUN_00595a50`**:
  - adds the chest unit id to d+0x68 (dedupe, max 8);
  - calls `FUN_005954f0`;
  - if d+0x118 && !d+0x119 → `FUN_005417d0(game, chest, 7, frame+10, 0, 0)`.
- Object event 7 for class 0x173 → `FUN_005449e0` (call at 0x544d91) → **`FUN_005956c0`**, which does the same retry.

**`FUN_005954f0(game, d)`** runs only if d+0x118 && !d+0x119 && chest count > 0. For each chest:

```
if (first chest) {
    room = FUN_00463740(death x, y);
    m = room ? FUN_005b2f20(game, room, dx, dy, 0x146, mode 0xc, -1, flags 8) : 0;  // mon 326
    if (!m) { pos = chest pos + (5, 5); m = FUN_005b2f20(... same ...); if (!m) continue; }
}
d+0x119 = 1;
mis = FUN_0056ede0(game, m, 0, 1, 0x14c, chest x, chest y);   // missile 332 towerchestspawner (SrvDo 18)
if (mis) { FUN_0064a710(mis, chest id); FUN_0064a760(mis, 0); FUN_0061aed0(room(mis), 0); }
```

- Mon 326 is the invisible owner (trap-firebolt class). Every chest shares the same owner.

### Countess spawn and FUN_005b0e00

Superunique 6 is placed by `FUN_005a49b0` case 6 (the same case in `FUN_005a4440`):
- `FUN_00639db0(u, 0x76, 1)`: stat 118 item_halffreezeduration. Freeze resistance only; nothing to do with drops.
- `FUN_005436b0(5)`: binds her to quest 5, so her death calls rec+0xc0.
- **`FUN_005b0e00(monData+0x28, 0xd)`**: sets special AI state 0xd. The handler table is at 0x73d358 + 0xd×0x10; see preset-specials.md.
- `FUN_005a4850(0x16, 1)`: MonUMod 22 questcomplete.

### Countess drop

Path: `FUN_005a6600` → `FUN_0055afa0` → `FUN_0055a6d0`.

- **TC pick:** superunique (`FUN_005a03a0 != -1`) → SuperUniques row +0x2c + diff×2. Otherwise:

  | monster | MonStats offset |
  |---|---|
  | champion | +0x88 + diff×8 |
  | unique | +0x8a + diff×8 |
  | normal | +0x86 + diff×8 |

- **Quest-TC override** (MonStats +0x9e quest id, +0x9f bit, TC4 at +0x8c + diff×8):
  - applies if +0x9e != 0 and the killer (or a pet's owner) lacks bit 15, bit 1 and the CP bit;
  - it runs after the superunique pick and depends only on MonStats.
  - The Countess (class 0x2d corruptrogue3) presumably has +0x9e == 0, so there is **no Countess special case in code**.
  - **The rune TC applies on every kill, in every difficulty, independent of quest state.**
- **SuperUniques.bin row 6:** class 0x2d, mods 9, TC u16s = 1004 / 1005 / 1006 (runtime index = bin row + 161 → bin rows 843..845).

| TC | picks | q (M/R/S/U?) | NoDrop | items (prob) |
|---|---|---|---|---|
| Countess | −2 | 1024, 883, 883, 883 | 0 | Countess Item 1, Countess Rune 1 |
| Countess (N) | −2 | 1024, 983, 883, 883 | 0 | Countess Item (N) 1, Countess Rune (N) 1 |
| Countess (H) | −2 | 1024, 983, 883, 883 | 0 | Countess Item (H) 1, Countess Rune (H) 1 |
| Countess Rune | 3 | – | 5 | Runes 4 (15) |
| Countess Rune (N) | 3 | – | 5 | Runes 8 (15) |
| Countess Rune (H) | 3 | – | 5 | Runes 12 (15) |
| Countess Item | 5 | – | 19 | gld,mul=1280 (11), Act 1 Equip C (19), Act 2 Junk (15), Act 2 Good (3) |
| Countess Item (N) | 5 | – | 19 | gld,mul=1536; the Act 1 (N) / Act 2 (N) equivalents, same probs |
| Countess Item (H) | 5 | – | 19 | gld,mul=2048; the (H) equivalents, plus pk1 (1) |

- Negative picks: each entry is rolled exactly |n| times, so exactly one Item-TC and one Rune-TC resolution.
- Rune TC: 3 picks, each 15/20 rune and 5/20 nothing.
- Item TC: 5 picks, each 48/67 item and 19/67 nothing.
- Row layout of the .bin: 736-byte rows; name[32]; picks +0x20; group, level, q1..q4 u16 from +0x24; NoDrop +0x34; 10 × item[0x40] from +0x38; probs from +0x2b8.
- The q-column order (Magic / Rare / Set / Unique) is **inferred**, not verified.
- Source: tools/emu/.cache/data_global_excel_treasureclassex.bin.

## Quest log records (Act 1 quests 2, 4, 5)

**Table 0x723f30** (16 bytes a log entry, index = log position):
`u8 shown, u8 icon, u8 slot, u8 act | u32 record | u32 +8 (= flag here) | u32 flag`.
`FUN_004a1950` finds the entry whose +0xc equals the quest flag; +1 (icon) and +2 (slot) go to the panel.

| log idx | flag | bytes | record | quest |
|---|---|---|---|---|
| 1 | 1 | 01 00 00 00 | 0x7237a4 | Den (reference) |
| 2 | 2 | 01 01 01 00 | 0x7237e4 | Sisters' Burial Grounds |
| 3 | 3 | 01 02 04 00 | 0x723824 | Tools of the Trade |
| 4 | 4 | 01 03 02 00 | 0x723864 | The Search for Cain |
| 5 | 5 | 01 04 03 00 | 0x7238a4 | The Forgotten Tower |

The log order is Den, Burial, Cain, Tower, Tools, Andariel (slots 0..5): Tools is log idx 3, but it sits in slot 4.

**The record** holds 32 u16s:
- [0] is the name.
- [1] is the message the questlast button replays once the quest is done (states 11/13).
- [2] is `rewardState − 1`: with bit 13 set, bit 1 set and bit 0 clear, the client shows state [2]+1. A value of −1 means none.
- Log state s uses [2s+1] as the line string and [2s+2] as the message to replay.
- 3725 "Invalid State" marks an unused state.

The string ids are string.tbl indices (below 10000), so no patch or expansion offset applies.

| rec | name | [1] | [2] | s1 | s2 | s3 | s4 | s5 | s6 | s7 | s10 | 11/12/13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Burial 0x7237e4 | 3715 | 92 | 2 | 3741/81 | 3742/81 | 3743/81 | – | – | – | – | 3743/81 | 3728/3727/3726, msg 81 at 13 |
| Cain 0x723864 | 3717 | 123 | −1 | 3744/97 | 3745/97 | 3746/97 | 3747/97 | 3748/97 | 3749/97 | 3734/97 | 3750/97 | 3728/3727/3726, msg 97 at 13 |
| Tower 0x7238a4 | 3718 | 127 | −1 | 3751/127 | 3754/127 | 3752/127 | 3753/127 | – | – | – | – (3725) | 3728/3727/3726, msg 127 at 13 |

**Strings:**

| id | key | text |
|---|---|---|
| 3715 | qstsa1q2 | Sisters' Burial Grounds |
| 3741 | qstsa1q21 | Look for Blood Raven in the Burial Grounds next to the Cold Plains. |
| 3742 | qstsa1q22 | Kill Blood Raven. |
| 3743 | qstsa1q23 | Return to Kashya for a reward. |
| 3717 | qstsa1q4 | The Search for Cain |
| 3744 | qstsa1q41 | Go through the Underground Passage to the Dark Wood, search for the Tree of Inifuss, and recover t… |
| 3745 | qstsa1q42 | Take the Scroll of Inifuss to Akara. |
| 3746 | qstsa1q43 | Go to the Cairn Stones in the Stony Field. Touch the Stones in the order found on the Scroll of In… |
| 3747 | qstsa1q44 | Find and rescue Deckard Cain. |
| 3748 | qstsa1q45 | Cain has been rescued and is now at the Rogue Encampment. |
| 3749 | qstsa1q46 | Visit Cain and Akara in the Rogue Encampment. |
| 3734 | qstsa1q4x | The person with the horadric scroll quit the game. (state 7) |
| 3750 | qstsa1q46b | Talk to Akara for a reward. (state 10) |
| 3718 | qstsa1q5 | The Forgotten Tower |
| 3751 | qstsa1q51 | Look for the Tower in the Black Marsh beyond the Dark Wood. (s1) |
| 3754 | qstsa1q52 | Dispose of the evil Countess. (s2) |
| 3752 | qstsa1q51a | Explore the cellar dungeons beneath the Tower ruins. (s3) |
| 3753 | qstsa1q51b | same text (s4) |
| 3726 / 3727 / 3728 | qstsComplete / qstsother / qstsprevious | Quest completed. / Another player completed it first. / You completed this quest in a previous game. |
| 3729 / 3730 | qstsThankYouComeAgain / …Multi | "You cannot complete this quest in this game. You can complete it by creating a new game [or joining a…]" |

String 3731 (qstsThankYouComeAgainSingle, "Cain was rescued without your help.") has no code reference; its only occurrence is in a data table at 0x72b710. Nothing in the log uses it.

**`FUN_004a1950`: the text picker.** It runs the same generic path for all three quests; the Den, and records 0x723a64 / 0x723c64 / 0x723d24, are special-cased and those cases don't touch these quests.

The inputs:
- F: the player's flags (`FUN_004b32d0`).
- G: the second flag set (`FUN_004b32e0`).
- s: the log state the server sent (`0x7bf356[idx]`).
- `0x7bf380[idx]`: the last shown state. A change sets +0x263, the new-text mark.

The steps, with b0 = F bit 0, b1 = F bit 1, b13 = F bit 13:
```
if b13:
  if b0: s = 13                               -> 3726, replay [1]; +0x266 = F bit 12 (done-anim flag)
  elif [2] != -1 && b1: s = [2]+1             -> Burial: s3 "Return to Kashya"
  else goto L
elif b0: s = 11                               -> 3728
L: if b1 && F bit 15: s = 10; show if [21] != 3725  (Burial 3743, Cain 3750; Tower: hidden)
if s == 0: hidden unless (G bit 13 or G bit 15) and !b13 and !b1 (entry shown, replay 3725)
line = [2s+1]; replay = [2s+2]
if (!b0 && !b13 && !b1 && G bit 13 && flag != 0x13) || F bit 14:
    line = 3729 + (multiplayer DAT_007a0610 != 0)
if line == 3727 && single player: line = 3729
```
The panel's field +0x266 decides whether the entry is drawn: 2 hides it, 3 shows it.

For Cain, G bit 13 is the game flag that `FUN_00544720(4, 13)` sets, both on the rescue and on the skip to Act 2. A player who hasn't finished the quest then reads 3729/3730; F bit 14 (not rescued by you) gives the same line.

**Name table 0x722678** (`{u16 message, u16 name}`), used for the Talk submenu labels:
- 81..96 → 3715 Sisters' Burial Grounds;
- 97..126 → 3717 The Search for Cain (the table lists 123, 126, 124, 125 in that order);
- 127..145 → 3718 The Forgotten Tower;
- 146..165 → 3716 Tools of the Trade;
- 166..184 → 3719 Sisters to the Slaughter;
- 64..80 → 3714 Den.

**Server packet 0x50, "quest special" (15 bytes; client `FUN_0045e370` → `FUN_004b9210`):**

| subtype (u16 at +1) | meaning |
|---|---|
| 1 | the u16 counts at +3/+5/+7 → `DAT_007bf2a4/a8/ac`; the Den's "Monsters remaining" reads `DAT_007bf2a4` |
| 2 | remove the hire offer whose id is the u16 at +3 from the client's list at 0x7c0c85 (16-byte entries), then `FUN_004939b0` (see Kashya below) |

## Deckard Cain in town

**The a1q4 data fields involved** (record+0x18):

| field | meaning |
|---|---|
| +0x50 | Cain has left Tristram (set by `FUN_00596ca0`) |
| +0x51 | camp Cain spawned |
| +0x52 | camp Cain wanted |
| +0x61 | the Tristram Cain was warped |
| +0x62 | the cage opened but Cain couldn't be placed |
| +0x63 | set with bit 15 on join |
| +0x66 | a fallback portal was made |
| +0x68 | camp Cain's GUID |
| +0x6c | the "cain start position" object's GUID |
| +0x70 | the start position is known |
| +0x84 / +0x88 | its x / y |
| +0x91 | Cain walked through the portal |
| +0xb4 | a GUID list (32 entries + u16 count at +0x80, helpers 0x545200 add, 0x545240 remove, 0x545290 contains, 0x545300 clear) of players who get the "rescued by the Rogues" line |

**The spawn: `FUN_00592960(game, x, y)`, with the room in ESI.**
1. Step (x+i, y+i), i < 21, until the point is inside the room.
2. `FUN_005b2f20(x, y, 0x109, 1, 5, 0)`: radius 5.
3. On failure, 20 more diagonal steps at radius 10, then (x, y) at radius 15.
4. On success: unit+0xc4 |= 0x3000000, d51 = 1, d52 = 0, d68 = GUID.

**Where the spawn is called:**
- `FUN_005940e0`: the InitFn 54 of object 385 "cain start position" (token `ss`), a camp preset. It records d6c / d70 = 1 / d84 / d88, and spawns if d52 == 1 && !d51.
- The level callback `FUN_00596de0`, on arriving in level 1: if d70, the d6c object exists, d52 == 1 and !d51, it spawns at the object's position.

**What sets d52 = 1** (camp Cain wanted):

| trigger | function | notes |
|---|---|---|
| join with bit 0 | `FUN_00597030` (0x597061: `EDX = 4`, own record) | done in an earlier game |
| join with bit 15 | same | also sets d63 = 1 |
| Tristram Cain reaches the portal | `FUN_005944f0`, via his AI `FUN_005e77a0` / `FUN_005e7880` (the 0x20f variant uses 0x58a940 / 0x58a980) | sets d91 = 1. If d70, it spawns at once through 0x5940e0; if d51, it then makes object 0xbd (189 "cain portal") at camp Cain with `FUN_00555230(2, 0xbd, …)`, his arrival portal |
| the cage can't place Tristram Cain | `FUN_00593290` (the Gibbet / object 0x1a mode-end hook, from `FUN_005449e0`) | normally spawns MonStats 0x92 at cage + 3 with 0x3000000. If that fails twice: log "Cain not created, look in town, dammit" (line 0x6b6), a town portal (object 59 to level 1) at cage + 6 if a player qualifies and !d66, d52 = 1 if !d51, d62 = 1 |
| Cain moved to town | `FUN_00596ca0(a, b)` (callers below) | details below |

`FUN_00596ca0` is called from `FUN_00596de0` on arriving in level 40 (Lut Gholein) and from `FUN_00597310`, both with (0, 1):
1. Gibbet d34 (class 0x1a) → mode 3; d54 = 3.
2. If !d47, the object at d30 → mode 1.
3. If a: timer 0x593260 (1 tick). If b: `FUN_005928c0` for each player (Tristram Cain warps).
4. If !d61: `FUN_00543140` drops the stored inactive monster 0x92.
5. d50 = 1; if !d51, d52 = 1.

**His menu** (record hcIdx 0x109 in 0x726c48, built by `FUN_004b4830`):
- The entry 0xfb4 is built only when `FUN_0062a530(player)`, the unidentified count, is non-zero. Otherwise the record's entry count becomes 2 and identify is dropped.
- When quest 4 bit 0 and bit 1 are both clear, the entry reads 0xfb5 (4021) "Identify Items: " followed by `"%d"` of count × 100 (0x4b4ba2..0x4b4bf7).
- Otherwise it reads 0xfb4 (4020) "Identify Items".
- The Act 2..5 Cains use the same record logic, keyed by their own hcIdx.

**Unidentified count, `FUN_0062a530`** (used on both client and server): items without flag 0x10 (identified) that are either:
- stored (node 1) on page 0 (inventory) or page 3 (the cube, by the usual numbering);
- or equipped (node 3).

The stash and the belt aren't counted.

**The request:** clicking identify runs `FUN_004b2020`. It opens a wait box (`FUN_004b7eb0`, 60 s timeout) and sends packet **0x34** `{u8 0x34, u32 npc guid}` via `FUN_00478680`.

**The server:** table 0x6e0d18 [0x34] → `FUN_0054bba0` (the length must be 5) → `FUN_00578460(game, player, npcGuid)`:
```
npc = unit(npcGuid); if !npc || npc != player's talk NPC (FUN_00554d00): reply(9, gold, -1, 0) [line 0xa6d]
class must be 0x109 (camp), 0xf4..0xf6 (Acts 2-4), or 0x208 (Act 5); else return
n = FUN_0062a530(player); if n == 0: reply(9, …) [0xa81]
if !(q4 bit 0) && !(q4 bit 1):
    if !FUN_00576d90(player, n*100): reply(0xc, gold, -1, 0) [0xa90]   // take gold; not enough
for each item: node 1 on page 0 or 3, or node 3, and not identified (FUN_006280a0 flag 0x10) → FUN_00562590 (identify)
reply(3, gold, -1, 0) [0xaaa]  (FUN_0053d740: NPC transaction, packet 0x2a)
```
So identifying is free once the quest is done (bit 0) or its reward is due (bit 1), from any act's Cain. Otherwise it costs 100 gold an item, charged all at once, and nothing is identified if the gold is short. That is the whole penalty for not rescuing him.

**Camp Cain's lines** (a1q4 blocks at 0x737668):

| block | entries | when |
|---|---|---|
| 5 | Cain 123 A1Q4QuestSuccessfulCain (kind 1, "…identify items for you at no charge") | reward due / bit 13 |
| 6 | Cain 125 A1Q4RescuedByRoguesCain (kind 1) | the player is in the d+0xb4 list |
| 7 | Cain 123 (kind 2) | done, in the +0x1c list |
| 8 | Cain 125 (kind 2) | bit 14, in the +0x1c list |
| 9 | Tristram Cain 124 A1Q4RescuedByHeroCain (kind 1) | – |

- String 126 A1Q4TragedyOfTristramCain is a Talk topic under the Cain name.
- Camp Cain's heard callback (`FUN_00592250`): 125 → add the player to +0x1c and remove them from +0xb4; 126 / 123 → add to +0x1c.

**Act 2, not rescued:**
- a2q0 (record id 7, flag 8, init `FUN_00598810`, blocks 0x738fc8) has the message callback `FUN_005986b0`.
- Talking to Cain2 (0xf4): if quest 4 bit 14 is set and the player isn't in a2q0's own list (data+0, `FUN_00545290`), it sends block 1. That is Cain2 125 A1Q4RescuedByRoguesCain, kind 0, played at once ("Oh... Blessings on the Rogues!…").
- It then calls `FUN_005940a0`, which removes the player from quest 4's d+0xb4 list.
- Block 0 is Jerhyn's (0xc9) 253 JerhynActIntroGossip1 while flag 8 bit 0 is clear.
- Cain2 always stands in Lut Gholein; no spawn gate was found. The only effects of a missed rescue are the identify fee, that one line, and quest 4 bit 14 / log line 3729/3730 (set by `FUN_00596de0` → `FUN_00592b10` on arriving in level 40).

## Kashya's reward: a free hireling

**The trigger:** `FUN_00590980` hears 92 A1Q2SuccessfulKashya with quest 2 bit 1 set. It sets bit 0, clears bit 1, and calls `FUN_00579180(game, player, 0x96)`:
```
rec = FUN_00535ea0(game, npc, &i)       // NPC record in game+0x1d24 (64 x 0x44)
offers = rec+0x10; if !rec || !offers: return   // no hire list -> no merc
if game+0x70 (expansion):
    if FUN_00574ec0(game, player, 7, 1): FUN_00577010(rec); return   // has a merc (alive or dead): nothing
else:
    if FUN_00574ec0(game, player, 7, 0): return                        // has a live merc: nothing
row = FUN_006564d0(game+0x70, 0x96, difficulty game+0x6d, 0)  // Kashya's first hireling.txt row
n = row.NameLast(+0x116) - row.NameFirst(+0x114) + 1
for k < n over offers (16 bytes: +0 u16 id, +8 hired, +0xc valid):
    if offer.hired != 1 && offer.valid == 1:
        offer.hired = 1
        send packet 0x50 {0x50, u16 2, u16 offer.id} (FUN_005531c0 client, FUN_0053d7e0)  // client drops the offer
        m = FUN_005b23c0(row.Class(+8), 1, 4, 0) || radius 6 || radius 12 ; none -> return
        FUN_00573270(game, player, m, 0xffff, offer, 0)   // bind as the merc: offer seed -> stats (FUN_006637f0), no gold
        break
FUN_00577010(rec)   // if no valid unhired offers remain: free the list, rec+0x21 = 0, FUN_00576070 (regenerate)
```
- The free merc is the first valid, unhired offer in Kashya's current list, with the same seed, level and stats as the hire screen shows. No gold is taken.
- If the player already has a merc there is no reward: in LoD any merc, even a dead one; in classic only a live one. There is no deferral, because bit 0 is already set.
- The offer list has to exist. The client asks for it whenever Kashya's menu is built (`FUN_004b4830`: hcIdx 0xfc / 0xc6 / 0x203 / 0x96 → packet 0x38 `{u32 3, u32 npc, u32 player}` via `FUN_004786d0`; server `FUN_0054bca0`). Talking to her therefore creates it first.
- The client shows nothing special: packet 0x50 subtype 2 removes the offer from the list at 0x7c0c85, and the merc appears.
- The hire menu itself is unchanged. It is level-gated (clvl > 7, `FUN_004b6410`), not quest-gated.
