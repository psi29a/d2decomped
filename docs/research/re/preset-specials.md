# Preset specials and Act 1 quest objects (game.exe 1.14d)

Scope: how DS1 preset units in the Act 1 dungeon and maze levels become units,
which superuniques and objects are special-cased, and what the Act 1 quest
objects do when operated. Every address is game.exe 1.14d. Server side only.

Related: monsters.md ("Preset units on the server"), objects.md (OperateFn
2/4/15, traps), quests.md (a1q1, the quest record layout).

## 1. Preset dispatch

```
FUN_00555910(game, room, preset p)                       // per preset unit when a room comes into play
  u = FUN_005557d0(type=p[5], id=p[1], x=p[2]+roomX, y=p[6]+roomY, mode=p[0], 0)
  if u:
    type 1: FUN_0058f000(u); if p[4]: FUN_00666120()     // p[4] = path / preset flag
    type 2: FUN_00545c90(p[4])                           // object post-hook, see 1.3

FUN_005557d0(game, room, type, id, x, y, mode)
  type 1:            u = FUN_0054e600(...)               // monsters, 1.1
  type 2, id > 0x23d: u = FUN_0054f490(id, ...)          // random object group, 1.2
  type 2, id == 0x23d: nothing
  else:              u = FUN_00555230(type, id, ...)     // plain object / tile unit (type 4 = warp, via FUN_00557ab0)
  if u: u+0xc4 |= 0x3000000                              // "came from a preset" flags
```

### 1.1 Type 1: FUN_0054e600

Id spaces: `0..nMonStats-1` (734 rows) are MonStats, then `734 + suIndex`
(66 superuniques), then `800 + MonPlace code`.

```
if id >= nMonStats:
  id -= nMonStats
  if id < nSuperUniques: return FUN_005a49b0(game, room, x, y, id)   // 1.4
  id -= nSuperUniques; MonPlace switch below
MonStats row:
  0x1f2 / 0x205 in level 0x6e while quest 0x1f is done -> swapped (act 5 only)
  difficulty > 0 and row 0x1c5 / 0x211 in level 0x6e -> nothing
  return FUN_0054e490(...)                                           // plain monster spawn
MonPlace code:
  2  unique pack: FUN_005bde80 picks the class; FUN_005a43e0(0, cls, 0,0,0,1) at a random spot
  3  champion:    FUN_005b2f20(cls) then FUN_005a48c0(0x10) + FUN_0054e1e0(0, cls)
  4  navi (MonStats 0x10a)
  5  bloodraven (0x10b)
  8  0x11c with spawn flag 8
  10/11 tentacles (FUN_0054da60 picks; 10 uses mode 4)
  0x18/0x1a  normal difficulty only: FUN_0054e090 group
  0x19/0x1b/0x1c  nothing
  0x11,0x12,0x16,0x17  level-typed monster: FUN_0063ec70(room, FUN_004e6c50()) + FUN_0054e2a0,
                       FUN_005b2f20 (retry with mode 4)
  0x1d..0x20  same, but mode 0xc (dead) and u+0xc4 |= 0x2000000
  if the spawned MonStats is 0x1b6: FUN_005417d0(7, game+0xa8 + 0xfa + difficulty)   (event slot)
```

Andariel (MonStats 156) is a plain MonStats preset in level 37. Nothing
special is done at spawn time; everything is in a1q6 (section 5.5).

### 1.2 Type 2 groups, id >= 0x23e: FUN_0054f490

```
i = id - 0x23e; if i >= DAT_00731d4c: return 0
return PTR_FUN_00731d28[i](id, ...)       // random-object picker per group
```

Level 25 has one (obj 580). The repo steps the seed only.

### 1.3 Object post-hook: FUN_00545c90

The special cases are object ids 0x1cb -> FUN_0058ad80, 0x1cd -> FUN_0058ae10
and 0x21f -> FUN_00587950. All three are act 2+ objects. No Act 1 object is
special-cased here. Act 1 quest objects wire themselves up through their InitFn
(section 3).

### 1.4 Superunique spawn: FUN_005a49b0(game, room, x, y, su)

```
if game+0x6d (difficulty) > 2: return
rec = SuperUniques[su]                                    // FUN_006556e0, 0x34-byte records
if rec.Class < 0: return
if !rec.Stacks (+0x26) and bit su of game+0x1d30 is set: return   // once per game
if rec.AutoPos (+0x24): x = y = 0                         // spawner picks a random spot in the room
u = FUN_005a09e0(room, 0, x, y, -1, 0)                   // spawn rec.Class
if !u or !FUN_005a0120(): return
set bit su in game+0x1d30
monsterdata+0x16 |= 2                                     // superunique flag
FUN_005a0200()
mods: rec.Mod1..Mod3 (+0xc..), stop at 0, skip 0x18; 0x1e (aura) -> remember
      then `difficulty` extra random mods: FUN_005a0600(used[])  (normal 0, NM 1, Hell 2)
FUN_005a2120(game, u, 1)          // mod callbacks (table 0x73c008) + minions (FUN_005a0c00)
if aura: FUN_005a1650(1)
switch rec.hcIdx (+8):
  6    Countess:  FUN_00639db0(u, stat 0x76, 1)          // item_halffreezeduration
                  FUN_005436b0(5)                         // quest-bind a1q5
                  FUN_005b0e00(monsterdata+0x28, 0xd)     // special AI 0xd, table 0x73d358 + 0xd*0x10
  10   Radament:  (difficulty+2) x FUN_005b23c0(4,1,4,0x40); then 0x114, 0x17e, 0x181, 0x185
  0x1a,0x1b,0x1d  quest 0x13 + FUN_00545b50
  0x24..0x26      quest 0x17
  0x27 Cow King:  FUN_005436b0(4)                         // quest-bind a1q4 (kill check, 5.4)
  0x2a            FUN_005b24e0(0x1c5,1,0x14,0x14,0); quest 0x1f; FUN_00545b50; stat 0x76
  0x2b..0x2d      quest 0x23 + FUN_00545b50
  0x3c            FUN_0058f030; FUN_005b24e0(FUN_0063ec70(level,0x1c5),1,10,0x14,0x40); quest 0x22
  0x3e            FUN_005b24e0(0x17d,1,0x14,10,0x40)
FUN_005a4850(0x16, 1)             // MonUMod 22 "questcomplete" for every case, including the default
```

No other Act 1 superunique (Bishibosh, Bonebreak, Coldcrow, Rakanishu, Treehead,
Griswold, Pitspawn, Flamespike, Boneash, the Smith, Corpsefire) has a hardcoded
case. The Smith (su 20) is a plain preset in level 28. He is tied to a1q3 only
through the Malus.

Minions: MinGrp..MaxGrp, each + difficulty when both are nonzero, placed at
radius 3 (already ported).

Act 1 SuperUniques.txt rows:

| su | hcIdx | name | class | mods | MinGrp-MaxGrp | AutoPos |
|---|---|---|---|---|---|---|
| 0 | 0 | Bishibosh | fallenshaman1 | 8,9 | 2 | 1 |
| 1 | 1 | Bonebreak | skeleton1 | 5,8 | 5 | 1 |
| 2 | 2 | Coldcrow | cr_archer1 | 18 | 4 | 1 |
| 3 | 3 | Rakanishu | fallen2 | 17,6 | 8 | 0 |
| 4 | 4 | Treehead WoodFist | brute2 | 5,6 | 2 | 1 |
| 5 | 5 | Griswold | griswold | 7 | 0 | 1 |
| 6 | 6 | The Countess | corruptrogue3 | 9 | 6 | 1 |
| 7 | 7 | Pitspawn Fouldog | bighead2 | 7,18 | 4 | 1 |
| 8 | 8 | Flamespike | quillrat4 | 9,7 | 6 | 1 |
| 9 | 9 | Boneash | skmage_pois3 | 8,5,18 | 0 | 1 |
| 20 | 20 | The Smith | smith | 5 | 0 | 1 |
| 39 | 0x27 | The Cow King | hellbovine | 8,17 | 6 | 1 |
| 40 | 0x28 | Corpsefire | zombie1 | 27 | 5 | 1 |

(The Group column is the MinGrp and MaxGrp value; they are equal in every Act 1 row.)

### 1.5 Quest binding: FUN_005436b0(questId)

`FUN_00543640(game, q)` finds the quest record. `FUN_005435c0` links the record
into `unit+0x74`. `FUN_00543a30` then sends that unit's death to the linked
records through FUN_005439a0. FUN_005438e0 is the game-wide dispatch.
FUN_00543a30 forces the game-wide dispatch for superunique hcIdx 0x1a..0x3c and
for MonStats 0xf2, 0xf3, 0x187 and 0x220.

Special cases inside FUN_005436b0:

- q4 on object 0x3d (the invisible object): FUN_00592f80 stores its unit id at data+0x28 and sets +0x49 = 1.
- q8 on MonStats 0xe5: FUN_005991b0.
- q0xc on MonStats 0xfa: FUN_0059c3b0.

## 2. What the Act 1 levels actually preset (emu dump, seed 3)

| level | units of interest |
|---|---|
| 18 Crypt | Bonebreak (su 1); chest obj 397 (op 4, InitFn 57) |
| 19/21-24 crypts, 29-31 Jail, 34-36 Catacombs | unique packs (place 2), champions (3), caskets/urns/chests |
| 30 Jail 2 | Pitspawn Fouldog (su 7) |
| 29, 35 | waypoint obj 157; 27: waypoint obj 119 |
| 25 Tower 5 | Countess (id 740 = su 6), place 100/50 packs, chests 240 (x3) and 6, crate 46, 21 x gold placeholder 269 (InitFn 28), group obj 580 |
| 20, 26, 27 Tower 1-4 | levels 26/27 have only ordinary packs and objects; level 20 builds no presets through this path |
| 28 Barracks | The Smith (su 20), Malus obj 108 (op 21, InitFn 15), bookshelves 179/180 (op 26) |
| 37 Catacombs 4 | andariel (MonStats 156), fires, rogue corpses, 7 fallenshaman places, barrels |
| 38 Tristram | Griswold (su 5), Gibbet obj 26 (op 10, InitFn 7), Wirt's body obj 268 (op 33), trap obj 250 (op 30), champion, unique pack, fallenshaman |
| 4 Stony Field | Cairn stones obj 17..21 (op 9, InitFn 6), invisible obj 61 (InitFn 13), Rakanishu |
| 5 Dark Wood | Inifuss obj 30 (op 12, InitFn 9), Treehead |

The Tower 5 gold placeholder is InitFn 28, `FUN_0054f8c0`:

```
mode 2
n = rand(9) + 1                          // room seed
repeat n: dx = rand&3, dy = rand&3
  if the spot is in this room and free (FUN_0064d800 mask 0x3f11): FUN_00559300(spot)   // drops one gold pile
```

## 3. Tables

OperateFn, `0x732d18` (index -> function):

| # | fn | what |
|---|---|---|
| 1 | 0x586410 | casket/hole: FUN_00585b90 drop, undead-trap chance, FUN_00582510 trap |
| 2 | 0x583c70 | shrine (ported) |
| 3 | 0x5866c0 | urn/basket: mode 1; drop if rand(100) < 21; trap |
| 4 | 0x585f60 | chest (ported) |
| 5 | 0x5868a0 | barrel |
| 6 | 0x594e70 | Tower Tome (a1q5) |
| 7 | 0x584330 | exploding barrel (FUN_00584240) |
| 8 | 0x581d40 | door |
| 9 | 0x593710 | Cairn stones (a1q4) |
| 10 | 0x593480 | Gibbet / Cain's cage (a1q4) |
| 11 | 0x5843d0 | |
| 12 | 0x593af0 | Inifuss tree (a1q4) |
| 13 | 0x5843a0 | |
| 14 | 0x5867a0 | corpse/crate/stash: FUN_00585b90 drop, mode 1, trap |
| 15 | 0x584870 | portal (ported) |
| 16 | 0x581eb0 | trap door |
| 17 | 0x582610 | |
| 18 | 0x583ff0 | secret door |
| 19 | 0x584160 | armor stand (FUN_005594c0) |
| 20 | 0x5841d0 | weapon rack (FUN_00559630) |
| 21 | 0x591ac0 | Malus (a1q3) |
| 22 | 0x5858a0 | well |
| 23 | 0x584e30 | waypoint |
| 24 | 0x59a7e0 | (act 2) |
| 25 | 0x59dc70 | (act 2) |
| 26 | 0x584060 | bookshelf |
| 27 | 0x581bf0 | |
| 28 | 0x5b7a60 | |
| 29 | 0x5821a0 | |
| 30 | 0x581cd0 | trap object |
| 31 | 0x5b9b40 | |
| 32 | 0x564cd0 | stash |
| 33 | 0x583e70 | Wirt's body |
| 34 | 0x5846b0 | |
| 35-38 | 0 | |

InitFn, `0x731bc0`:

| # | fn | what |
|---|---|---|
| 1 | 0x54f9d0 | shrine (ported) |
| 2 | 0x54fbb0 | urn: frame = rand(9) if rand(100) < (lvl/8 + 5), else 0 |
| 3 | 0x54fcb0 | chest (ported) |
| 4 | 0x595a00 | Tower Tome: mode 3 if a1q5 is not active |
| 6 | 0x5935e0 | Cairn stones |
| 7 | 0x544990 | Gibbet |
| 8 | 0x5500c0 | |
| 9 | 0x593fc0 | Inifuss: mode from data+0x58; mode 2 if a1q4 is absent |
| 13 | 0x594020 | invisible obj 61: binds to a1q4 (-> FUN_00592f80) |
| 15 | 0x544950 | Malus |
| 16 | 0x552b30 | |
| 17 | 0x547210 | |
| 22 | 0x54fb40 | |
| 28 | 0x54f8c0 | gold placeholder (section 2) |
| 47 | 0x595a50 | a1q5 tower object registration (below) |
| 54 | 0x5940e0 | a1q4: records the town-portal spot |
| 57 | 0x54fd90 | FUN_0054fcb0 (chest init) + FUN_005540a0 |

Act 1 quest objects in objects.txt:

| Id | name | op | init |
|---|---|---|---|
| 8 | TowerTome | 6 | 4 |
| 17-21 | StoneAlpha..StoneLambda | 9 | 6 |
| 26 | Gibbet (Cain's cage) | 10 | 7 |
| 30 | Inifuss | 12 | 9 |
| 59 / 60 | town portal / permanent town portal | 15 | - |
| 61 | invisible object | 0 | 13 |
| 108 | Malus | 21 | 15 |
| 268 | wirt's body | 33 | - |
| 269 | gold placeholder | 14 | 28 |

## 4. Quest-record conventions

- `FUN_00543640(game, q)` returns the quest record.
- Record fields:
  - `+9`: active in this game
  - `+0xb`: log state
  - `+0xc`: state, changed by `FUN_00544350(file, line)`
  - `+0x18`: private data
  - `+0xe0`: quest number
- Callbacks sit at `+0xa0 + event*4`:
  - 0xa0 NPC message
  - 0xa8 talk close
  - 0xac level enter
  - 0xb0 item picked up
  - 0xc0 bound monster died
  - 0xc8 player leaves
  - 0xcc message heard
  - 0xd4 join
  - 0xec NPC "!"
- Player flags live in the player's quest block for the difficulty:
  - `FUN_0065c310(flags, q, bit)` tests, `FUN_0065c360` sets, `FUN_0065c3a0` clears.
  - Bit 0 = done, bit 1 = reward due, bit 13 = took part.
- `FUN_005438b0()` returns the quest rng (LCG 0x6ac690c5).
- `FUN_00559a30(2, ...)` drops the item whose code is in `obj+0xb8` (used by every quest object).
- `FUN_00553380(player)` plays the "can't use / not yet" voice.
- `FUN_005537d0(unit, fn)` runs fn for each player (unit 0 = all).

## 5. Act 1 quest objects and hooks

### 5.1 a1q2 Den of Evil: nothing preset. a1q2 Blood Raven (init FUN_00591210)

Blood Raven is MonPlace 5 in the Burial Grounds. Callbacks:

- +0xc0 FUN_00590ec0 (kill): per player, FUN_00590c40 sets q2 bits 13 and 1.
- +0xcc FUN_00590980: Kashya (0x96), messages 0x51 and 0x5c (reward).
- +0xac FUN_00590fa0, +0xa0 FUN_00590b10, +0xec FUN_00591080.

### 5.2 a1q3 Tools of the Trade: Malus (op 21, init 15)

InitFn 15 `FUN_00544950`:

```
q = quest record 3
if q: data+1 = 1; data+4 = obj unit id
      if !q.active: data+0x98 = 2
      mode = data+0x98
else if mode != 2: mode 2
```

OperateFn 21 `FUN_00591ac0`:

```
if !q3.active: mode 2; FUN_00553380(player); return
if mode == 0 and player lacks q3 bits 0 and 1:
  if stat 0xc (clvl) < 8: FUN_00553380(player); return      // too low: nothing drops
  obj+0xb8 = 'hdm '; FUN_00559a30(2, ...)                  // Horadric Malus
  mode 2; data+1 = 1; data+0x98 = 2; store the item id; FUN_00544350(a1q3, 0x2c2)
```

Turn-in: +0xcc FUN_00591490, Charsi (0x9a) message 0xa3. If the player has
'hdm ', remove it, set bits 13 and 1, and change state (message 0x92). The
reward itself (imbue) belongs to Charsi's NPC code. Other callbacks:
+0xa0 FUN_005916a0, +0xb0 FUN_00591960, +0xd4 FUN_00591ed0, +0xe8 FUN_00591d30,
+0xec FUN_00591c30.

### 5.3 a1q4 Search for Cain (init FUN_005971b0)

Inifuss tree, op 12 `FUN_00593af0`:

```
if q4.active and state < 6 and mode == 0 and player lacks bits 0,1
   and player has neither 'bkd ' nor 'bks ':
  obj+0xb8 = 'bks '; FUN_00559a30(2, ...)                  // Scroll of Inifuss
  obj+0xc4 = FUN_00592c80; mode 1; data+0x47 = 1; data+0x30 = tree id
else FUN_00553380(player)
```

Akara (0x94), handled by FUN_00592250:

- Message 0x61 gives the quest.
- Message 0x70 swaps 'bks ' for 'bkd ' (via FUN_005466b0).
- Message 0x76 with bit 1 gives the reward: `FUN_005466b0('rin ', ilvl, quality, 1)`.
  - ilvl is 7 / 30 / 60 by difficulty.
  - Quality is 4 (magic) in normal and 6 (rare) in NM and Hell.

Cairn stones, InitFn 6 `FUN_005935e0`:

```
q = quest record 4
if !q: mode 2 unless already 2; return
d = q.data
if !q.active or d+0x4c:
  d+0x4c = 0
  if !d+0x45 and obj id == 0x11 (StoneAlpha): d+0x40 = unit id; once: d+0x44 = 1, FUN_00543f10(1)
  mode 2; return
if !d+0x4d and !d+0x50: if d[0x4b + id] == 1 (stone already lit): d[0x4b+id] = 0, mode 0
else mode 2
```

Cairn stones, op 9 `FUN_00593710`:

```
first use: FUN_00592e90 shuffles the order: classes 0x11..0x15 into 5 slots, quest rng % 5
if player has bit 0 or 1: voice; return
if player lacks 'bkd ': every 64th try (counter +0x2c) voice, unless bits 3/4 are set; return
if this stone is next in order: record it; stone mode 1; invisible obj 0x3d mode = idx + 1
on the 5th stone:
  invisible obj mode 6; d+0x4f = 1; remove 'bkd '
  p = position of StoneLambda (0x15)
  FUN_0056ede0(0, 1, 0x120, p.x + 6, p.y - 3)
  set player bit 4; for each player LAB_005936b0
```

`FUN_0056ede0(src, count, 0x120, x, y)` wraps FUN_0059fa30. It fires only if
the target is within 100 of the unit (FUN_006417f0 < 0x65). It is a missile or
event spawn at the portal spot, the portal flash; the id is not confirmed.

The red portal to Tristram, `FUN_00592d50`:

```
FUN_0056d130(room, x + 4, y + 4, level 0x26, 0, object 0x3c, 1)   // at the unit id in d+0x40 (StoneAlpha)
d+0x45 = 1
```

Gibbet, InitFn 7 `FUN_00544990`: with q4, mode = d+0x54, d+0x48 = 1, and
d+0x34 = the gibbet's unit id. Without q4, mode 2.

Gibbet, op 10 `FUN_00593480`:

```
if player lacks bits 0 and 1:
  mode 1; sound; d+0x54 = 3; d+0x3c = player id
  FUN_005417d0(7, game+0xa8 + 0x11)                        // game event slot
  FUN_0061aed0(room, 0)
  set bits 13, 1; broadcast FUN_005930b0 (bits 13 + 1 for every player not in town)
else voice
```

Cain spawns:

- `FUN_00593290` spawns cain1 (0x92) at the gibbet + 3 via FUN_005b2f20. If
  that fails, it opens town portal object 0x3b to level 1 at + 6.
- `FUN_00592960` puts cain5 (0x109) in town near the portal spot, trying
  radius 5, 10 and 15. InitFn 54 `FUN_005940e0` records that spot.

Cow King kill, +0xc0 `FUN_00593e70`. The Cow King is bound to q4 by FUN_005a49b0:

```
if quest 0x1a (classic) / 0x28 (expansion) done for the killer:
  set q4 bit 10 ("killed the Cow King": no more cow portal)
  FUN_00545990(0x40); drop 'vps ' x 8                      // 8 stamina potions
```

Other q4 functions:

- `FUN_00594140`: the cow-level portal in Rogue Encampment,
  `FUN_0056d130(..., level 0x27, 0, 0x3c, 0)`. It is blocked by bit 10.
- `FUN_00594630`: the Tristram gold. It rolls a count once with the quest rng
  (`FUN_004bc500(0x14)`), then drops one 'gld ' pile per call.
- Callbacks: +0xac FUN_00596de0, +0xb0 FUN_00592e20 ("cairn stone item
  acquired"), +0xb8 FUN_00592e60, +0xd8 FUN_00592b90, +0xc4 FUN_00592c80,
  +0xec FUN_00592fb0, +0xa0 FUN_00592580, +0xa8 FUN_005921b0, +0xd4 FUN_00597030.

Wirt's body, op 33 `FUN_00583e70`: in mode 0, obj+0xb8 = 'leg ',
FUN_00559a30(2, ...), mode 1, sounds. There is no quest check; the leg drops
once per object.

### 5.4 a1q5 The Forgotten Tower (init FUN_00595920)

Tower Tome:

- InitFn 4 `FUN_00595a00`: mode 3 if q5 is not active in this game.
- op 6 `FUN_00594e70`: mode 1 and a sound. If q5 is active,
  `FUN_005456a0(0x7f)`; if state < 2, `FUN_00544350(a1q5, 0x202)` and d+0x11b = 1.

InitFn 47 `FUN_00595a50` (the tower level objects):

```
append the unit id to d+0x68[ d+0x88 ] (max 8, no duplicates)
FUN_005954f0(q, d)
if d+0x118 and !d+0x119: FUN_005417d0(7, game+0xa8 + 10)
```

Countess:

- Bound to q5 by FUN_005a49b0, with stat 0x76 = 1 and AI 0xd.
- Completion, `FUN_00594dd0(game, player)`: if the player lacks q5 bits 0 and 1,
  is in level 0x19 (25) and q5 exists, it sets bit 0 and bit 13 (FUN_00545290 /
  FUN_00545200 first).
- `FUN_00594c50` gives party credit (FUN_00543790 with a state from 0x7382ac).
- Her runes come from her TC, not from the quest script.

### 5.5 a1q6 Sisters to the Slaughter (init FUN_00596990)

Andariel death, +0xc0 `FUN_005965a0`:

```
if q6.active and killer is a player without q6 bit 0 or bit 1:
  FUN_00596210(game, player)
  2 x: obj(corpse)+0xb8 = table 0x7361dc[rng % 7]; FUN_00559a30(2, ...)
       // gcv gcr gcb gcy gcg gcw skc   (chipped gems / chipped skull)
  1 x: +0xb8 = table 0x736444[rng % 7]; FUN_00559a30(2, ...)
       // gsv gsr gsb gsy gsg gsw sku   (standard gems / skull)
  clear +0xb8
if q6.active:
  per player FUN_00596260, LAB_00596440, LAB_005963e0
d+0x18c = Andariel's unit id; d+0x192 = 1
if active: FUN_00543f10(1); for all players LAB_00596170
d+0x194 = 1; FUN_00544350(a1q6, 0x2b0)
record+200 (0xc8, player leaves) = LAB_005961c0; record+0xc0 = 0      // one-shot
```

The rng is quest rng `FUN_005438b0`. The other callbacks are
+0xac FUN_00596010 (entering level 37), +0xa0 FUN_00595e20, +0xcc FUN_00595c60
and +0xec FUN_005967f0. The act-2 transition (Warriv) is outside the scope of
this note.

### 5.6 Other operate functions worth porting (Act 1 dungeons)

Bookshelf, op 26 `FUN_00584060`, in mode 0:

```
mode 2; clear obj+0xc4 bit 1
if rand(20) < 13: +0xb8 = rand&1 ? 'isc ' : 'tsc '        // identify / town portal scroll
else:             +0xb8 = rand&1 ? 'ibk ' : 'tbk '        // tomes
FUN_00559a30(2, ...)
```

Trap object, op 30 `FUN_00581cd0`: in mode 0, FUN_005dfa00(room, 0) and
FUN_005dfa00(room, 1), mode 1, event.

Casket, urn and crate/corpse (1, 3, 14) all use `FUN_00585b90` for the drop and
`FUN_00582510` for the trap.

## 6. Repo cross-check (components/game, components/rules)

Implemented:

- `components/game/gamedata.cpp` (~322-420) ports FUN_0054e600:
  - MonStats rows and MonPlace 2/3/5/0x11/0x12
  - superunique spot then radius 5
  - Mod1..3 (`rules/uniques.hpp` superunique_mods)
  - minions MinGrp..MaxGrp + difficulty at radius 3
- `add_object` (~477) reads OperateFn, InitFn 1 and 3, and PreOperate.
- `World::operate` (components/game/world.cpp) handles op 2 and op 4. Interact
  also accepts 23 (waypoint), 32 (stash) and 15 (portal).
- `rules/quests.hpp` has DenQuest only.

Gaps, most important first:

1. FUN_005a49b0 details missing:
   - Once per game (game+0x1d30 bitset) unless Stacks: a superunique in two
     presets spawns once.
   - AutoPos: x = y = 0, a random spot, instead of the preset spot.
   - `difficulty` extra random mods (FUN_005a0600); mod 0x18 skipped; aura 0x1e
     via FUN_005a1650.
   - MonUMod 22 (questcomplete) on every superunique.
   - hcIdx 6, the Countess: stat 0x76 = 1, AI 0xd, bind to q5.
   - hcIdx 0x27, the Cow King: bind to q4.
   - The gamedata.cpp comment "the Countess, the Smith ... aren't built" is half
     wrong: the Smith has no special case.
2. There is no quest-unit binding (FUN_005436b0 / unit+0x74) or death dispatch
   (FUN_00543a30). Without it there are no Andariel, Countess or Cow King quest
   hooks.
3. Quest objects are not built:
   - op 6 (tome), 9 (stones), 10 (gibbet), 12 (Inifuss), 21 (Malus), 33 (Wirt)
   - InitFn 4/6/7/9/13/15/47/54
   - Also missing: the Tristram portal (FUN_00592d50), Cain spawns
     (FUN_00593290 / FUN_00592960), the Akara/Charsi/Kashya turn-ins and the
     Andariel gem drop.
4. Ordinary dungeon objects are built (World::operate): ops 1/3/5/7/14,
   8/16/18, 19/20, 22 and 26, plus InitFn 2 (the trap roll, not an urn frame)
   and InitFn 28 (its seed steps only; the gold piles are not dropped). Still
   missing:
   - op 30 (trap)
   - monster door opening (FUN_005b0f50)
   - the locked door (mode 6) and the 0x8000-blocked door (mode 4)
5. Type-2 object groups >= 0x23e (FUN_0054f490, table 0x731d28) only step the
   seed. The MonPlace codes 4, 8, 10/11, 0x18/0x1a and 0x1d..0x20 (dead
   monsters, mode 0xc) have no cases, and the MonStats 0x1b6 event is missing.

Open questions:

- The FUN_0056ede0 id 0x120 (the portal flash missile, most likely).
- The exact per-player effects of the a1q6 callbacks FUN_00596260, LAB_00596440
  and LAB_005963e0.
- The group table contents at 0x731d28.
