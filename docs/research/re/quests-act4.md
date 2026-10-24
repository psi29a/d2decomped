# Act 4 quests — game.exe 1.14d

Conventions, flag helpers and the record layout are in `quests.md` /
`quests-act1.md` (callbacks: +0xa0 messages, +0xa8 talk closes, +0xac level
change, +0xb0 item event, +0xc0 a bound unit dies, +0xc4 quest item on
leave, +0xc8 leaves, +0xcc heard, +0xd4 joins, +0xd8, +0xec "!", +0xf0
open / chain). Traced 2026-10-14 from the decompile; ported 2026-10-16:
`rules/quests_act4.hpp`, `game/world_quests_act4.cpp`, played by
`tests/quests_d2d.py ... 4`. States from FUN_00544350's EDX: a4q1 1 open, 2
told (0xd3), 3 the fortress after (0x23b), 4 Izual dead (0x201), 5 rewarded
(0xde); a4q2 1 open (0x91), 2 told (0xe1), 3 the fortress / the sanctuary
(0x2af / 0x2bf); a4q3 1 open (0x155, 0x87, 0xf1), 2 told (0x1ce, 0x201), 3
the fortress after (0x1b1), 4 struck (0x125), 5 rewarded (0x212). Message
blocks (quest_msgs.py) and the state → block maps 0x73dba0 {-1, 0, 1, 2, 3,
4}, 0x73d56c {-1, 0, 1, -1, -1}.

## The records

Quest init table `0x731510` (24-byte rows), Act 4's (act 3):

| row | record id | quest word | init fn | file |
|---|---|---|---|---|
| 0x19 | 0x15 | 0x18 | `FUN_005b3780` | (a4q0, no asserts: the act's opening, Tyrael) |
| 0x1a | 0x16 | 0x19 | `FUN_005b42b0` | a4q1.cpp, The Fallen Angel (Izual) |
| 0x1b | 0x17 | 0x1a | `FUN_005b54c0` | a4q2.cpp, Terror's End (Diablo, the seals) |
| 0x1c | 0x18 | 0x1b | `FUN_005b6610` | a4q3.cpp, The Hellforge |

Message tables (+0xdc): 0x73d580, 0x73d708, 0x73dbb8, 0x73e140
(`tools/emu/quest_msgs.py`). Chain (+0x10, record ids): a4q1 → 0x18 (the
Hellforge) → 0x17 (Terror's End). Units (MonStats rows): 0x16f Tyrael (tyrael2), 0xf6
Cain (cain4), 0x196 Izual's ghost, 0x100 Izual, 0xf3 Diablo, 0x199
Hephasto.

**Binding** (`+0xc0` fires only for units bound with `FUN_005436b0`):
`FUN_005b1cf0` by MonStats class — Diablo 0xf3 → 0x17 (unless his MonStats
+.. is 0x2c1), Izual 0x100 → 0x16 (unless 0x2c2), Hephasto 0x199 → 0x18;
`FUN_005a49b0` by superunique — 0x24 Infector of Souls, 0x25 Lord De
Seis, 0x26 Grand Vizier of Chaos → 0x17.

## The opening (a4q0)

Tyrael (0x16f) has 664 (0x298) and "!" (`FUN_005b3750`) till word 0x18
bit 0 (`FUN_005b36d0`: block 0). Hearing 664 (`LAB_005b36a0`) sets bit 0.

## The Fallen Angel (a4q1.cpp, word 0x19)

Data (0x1c bytes): +4 / +8 Izual's spot at death, +0xc told-by-ghost, +0xd
the timer set, +0x10 the ghost due, +0x11 Tyrael's word heard, +0x12 the
ghost spoke, +0x14 the killer.

- **"!"** (`FUN_005b37f0`): Tyrael while not done and (bit 1, or state 1);
  the ghost (0x196) till bit 5.
- **Messages** (`FUN_005b3bb0`): the ghost without bit 5 → block 3; bit 1
  (reward due) → 3; `FUN_005452c0` → 4; else by state from 0x73dba0.
- **Heard** (`FUN_005b39a0`): Tyrael 670 (0x29e): +0x11, state (0xd3),
  unit callback `LAB_005b3860` (bit 2 at state 2; at 3 bit 4 when the ghost
  told, else bit 3). Tyrael 676 (0x2a4) with bit 1: if bit 13 and state ≠ 5:
  state 5, the chain (+0xf0), every player's log (`FUN_005b3940`), game
  quest flag 0xd; then bit 0, bit 1 cleared, bits 2..11 cleared, **2 skill
  points** (`FUN_006272b0(player, 5, 2)`). The ghost 675 (0x2a3): +0x12,
  +0xf, bit 5; the log refreshed.
- **Entering** the fortress (+0xac, `LAB_005b4140`, level 0x67) at state 2,
  not done: state (0x23b) and the bit callbacks.
- **Izual dies** (+0xc0, `FUN_005b4020`): state (0x201), +0xc0 off, the
  killer; in this game: the killer's bits 13 + 1 if not done (and cleared
  2..11), +0x10, his spot (`FUN_00620870`), every player in his room or its
  neighbours (`FUN_00619790`) bits 13 + 1 (`FUN_005b3f20`), the "returns"
  callbacks (`LAB_005b3de0`: bit 13 → sound event `LAB_005b3d70`;
  `LAB_005b3cd0`: not done → bit 14, message 0). Once: the 3-tick timer
  `FUN_005b3e60`: log 4 to everyone; the ghost (0x196) at his spot, mode 8,
  within −1, then 3, then 5 (`FUN_005b2f20`).
  d2d plays the participant's `act4_free_izual` class voice at the kill.
- **Izual's first think** (`FUN_005b4390`, from his AI `FUN_005f89b0`'s
  first call): with +9 and the log (+0xb) under 2, data +0x14 cleared and
  every player not done (bit 0 or 15 without 13 / 14, `FUN_005b3940`) sent
  the record's log again (`FUN_00544190` → S→C 0x5d, no flags): the
  client's Quest Log button. No speech, no sound. d2d: `IzualQuest::greeted`,
  `ev::QuestNews`.
- **Join** (`LAB_005b4220`): not done or closed: bit 4 → state 3, log 2,
  data +0xc; bit 3 → 3 / 1; bit 2 → 2 / 1.

## Terror's End (a4q2.cpp, word 0x1a)

Data (0x4c bytes): +0 Tyrael's word heard, +1 the Diablo timer armed, +2
Diablo due, +3 Diablo dead, +4 / +5 the end steps, +6 the start point found,
+8 its object id, +0xc..+0x10 seals 392..396 opened, +0x13 the sanctuary
cleared, +0x14 Diablo dead (this game), +0x15 the 75 s step done, +0x18
ticks / the death tick (`GetTickCount`), +0x1c the kill count, +0x20 the
player, +0x24 / +0x2c / +0x34 the three seal bosses' spots, +0x3c the
room he died in, +0x44 / +0x45 Tyrael's portal due / made, +0x48 bound
deaths.

- **"!"** (`FUN_005b4450`): Tyrael while not done and state 1; classic
  (game +0x70 = 0) with bits 0 and 7; LoD with bit 0 and not bit 9. Cain
  (0xf6): classic bits 0 + 6; LoD bit 0 and not bit 8.
- **Heard** (`FUN_005b4670`): Tyrael 681 (0x2a9): +0, state (0xe1), bit
  callbacks `LAB_005b4560`; 684 (0x2ac): bit 7 cleared; 20000 (LoD): bit 9
  (`FUN_00543520(0x1a, 9)`) and, once, Tyrael's portal (`FUN_005b45e0`).
  Cain 685 (0x2ad): bit 6 cleared; 20001: bit 8.
- **Talk closes** (`FUN_005b4f90`): Tyrael with +0 → logs; LoD, +0x44 and
  not +0x45 → the portal.
- **The seals** (objects 392..396, InitFn 56 nothing; OperateFn 52 for 393
  and 395 `FUN_005b5630`; 54 / 55 / 56 for 392 / 394 / 396): closed (mode
  0) → mode 1 (event 1 at its anim end), its flag +0xc..+0x10 by id
  (0x188..0x18c). 54 / 55 / 56 (`FUN_005b6b70` / `6bc0` / `6c10`) first
  keep a spot (`FUN_00620870` + (−12, −52) / (−39, +33) / (+32, +16) into
  +0x24 / +0x2c / +0x34), then `FUN_005b6ad0` moves it to a free spot
  (`FUN_00545340`: size 3, mask 0x3f11, 100 rings; its size-13/14/15
  argument is unused) and makes object 131 (the flash, `FUN_00555230` type
  2) there; only when the flash is made does the seal open (`FUN_005b5630`).
  The flash's InitFn 59 (`FUN_0054fe10`): mode 1, event 7 at +27 frames,
  event 1 at FrameCnt1 + 1. Its event 7 (`FUN_005449e0`: mode 2; in level
  108 `FUN_005b5750`) makes the seal's superunique at the kept spot
  (`FUN_00545c30`, ids [0x744304]+0xb28.. = 0x24 / 0x25 / 0x26), retried
  10 frames on when it can't.
- **The free spot** (`FUN_00545340(room, &spot, size, mask, &room_out, -,
  rings)`): for ring c = 1 .. rings − 1, rows r = c, c − 2 .. > −c, columns
  k = −c, −c + 2 .. < c: the (2 size + 1)² square centred on spot + (k − 1,
  r − 1) (`FUN_0064d800`: no bit of mask) in a room → spot + (k, r). d2d:
  `Level::free_area`.
- **All five open and three bound deaths** (seal op, the start point's
  InitFn 55 `FUN_005b5590`, or a death `FUN_005b52e0`): `FUN_005b5230`
  once (+0x13): every monster in level 108 but Diablo, not dead, not a
  pet, set to mode 0 (dies: `FUN_005a7e60` / `FUN_005a7c20`); and if not
  armed: +2, +1, the 1-tick timer `FUN_005b4be0`.
- **The timer** (`FUN_005b4be0`): with +2, +0x18 counts ticks; past 9 and
  with the start point's object (+8, object 255 "diablo start point"),
  wherever the players are: Diablo (0xf3) at its
  spot, mode 1, within −1 / 5 / 10 (`FUN_005b4b60` → `FUN_005b2f20`), flags
  0x3000000, +0x11; done. With +3 (dead): 75 s (wall clock) → once
  `FUN_0052e2a0`; 90 s → +5: classic every player `FUN_005b4a80` (saved,
  start town 0x67, sound 0x1b8, the end `FUN_0053d710(0x1175d)`); 95 s →
  reset, classic `FUN_00530590` (the game ends).
- **Diablo dies** (+0xc0 `FUN_005b52e0`, class 0xf3): (classic: `FUN_00545760` first)
  +0x14, his room (+0x3c); with a killer, once: +2 0, +3 / +4 / +5 1, +1,
  the tick; the timer. Every player: logs; unit callbacks `FUN_005b5140`
  (in his room or a neighbour, not done: `FUN_005b4d20` — bits 13 and 0,
  bits 2..11 cleared; classic bits 6 / 7, sound 0x250 and the next
  difficulty opened `FUN_00538680(diff)`; +0x1c count), `LAB_005b4df0`,
  `LAB_005b4970` (not done: bit 14), `FUN_005b49c0` (bit 13: sound 0x1a4).
  Rewarded players' counts → `FUN_00545990(0x40)` each.
- **Tyrael's portal** (LoD, `FUN_005b45e0`): object at Tyrael + (5, 0)
  (`FUN_00545340(2, 0x400, .., 12, 100)`, then `FUN_00555230` type 2),
  flags 0x3000000, +0x44 0, +0x45 1. Its operate `FUN_005b5880`: LoD, bit
  0 or 13: in act 4 sets word 0x1c bits 0 and 13, sound 0x55a and the
  waypoint (`FUN_00554190`), then to level 0x6d (109, Harrogath,
  `FUN_0054b830`). d2d has no Act 5: it stops there.
- **Entering** (`LAB_005b4ea0`): the fortress at state 2 → state; the
  Chaos Sanctuary (0x6c) → state, logs.
- **Join** (`FUN_005b5080`): LoD bit 9 without word 0x1c bit 0 → bit 9
  cleared (again on `LAB_005b5030`); bits 4 / 3 / 2 → states as a4q1.

## The Hellforge (a4q3.cpp, word 0x1b)

Data (0x20 bytes): +0 the forge's mode (0 / 2 / 3), +2 Cain's word heard,
+3 / +4 the gem drop on / its mode, +8 rounds left, +0xc hammer hits, +0x10
hammers about, +0x14 gems per round, +0x1c the soulstone given.

- **"!"** (`FUN_005b5ee0`): Cain at state 1, not done or due, +0x1c 0.
- **Messages** (`FUN_005b62d0`): due → 2; `FUN_005452c0` → 3; state 1: 0
  when the player has `mss ` (`FUN_00558110`), else 1 unless +0x1c or log
  > 2; later states 1 likewise.
- **Heard** (`FUN_005b6100`): Cain 678 (0x2a6): +2, state; 679 (0x2a7):
  gives the soulstone (`FUN_005466b0("mss ", 0, 2, 1)`), +2, +0x1c, state
  (< 2); 680 (0x2a8) with bit 1: state 5 and the chain (if bit 13), bit 0,
  bit 1 cleared.
- **Hephasto dies** (+0xc0 `FUN_005b65d0`): drops `hfh ` (the Hellforge
  Hammer, `FUN_00559a30(7)`), +0x10.
- **The forge** (objects 376: InitFn 48 `FUN_005b5a20` mode = +0 (3 with no
  record); OperateFn 49 `FUN_005b5c10`): not done / due: mode 0 with `mss `
  carried → mode 1 (event at its anim end), +0 = 2, the logs, `mss `
  taken; without it the player's unit event; mode 2 with `hfh ` in hand
  (`FUN_0063bef0` on the player's +0x60, item code +0x80): +0xc += 1; the
  third → mode 3, +0 = 3, `hfh ` taken, bits 13 + 1, +4 / +3 = 1, +8 = 4,
  +0x14 = 1, every player in act 4 (`FUN_005b5b00`: not done: `hfh ` /
  `mss ` taken, bits 13 + 1, +0x14 count), state (0x125), event 7 at its
  anim end, the player's `LAB_005b5bb0` (bit 14). 
- **The gems** (event 7 at the strike + FrameCnt3 (+0xe4 >> 8: 22 frames),
  `FUN_005449e0` object 0x178 → `FUN_005b6710`):
  mode 4 once; each round +0x14 drops at the forge (`FUN_00559a30(2)`; 1
  alone: `FUN_005b5b00` adds the striker's party in act 4 without bit 1),
  each `rand(7)` on the quest seed (`FUN_005438b0`) from round +8: 4
  0x73e56c (perfect: gpv gpr gpb gpy gpg gpw skz), 3 / 2 0x73e588
  (flawless), 1 0x73e5a4 (plain); a dropped round → +8 − 1 and the next 20
  frames on; the last, LoD: a rune `rand(11)` from 0x73e114 (r01–r11) /
  0x73e514 (nightmare r12–r22) / 0x73e540 (hell r15–r25).
- **Item event** (+0xb0 `LAB_005b5a90`): `mss ` picked up → state 1;
  +0xc4 / +0xd8 count hammers about.
- **Join** (`FUN_005b64e0`): `mss ` carried → state 1; bit 5 → state 2,
  log 4; bits 3 / 2 → states as a4q1.

## Act 4's other objects

| object | InitFn / OperateFn | does |
|---|---|---|
| 342 hellgate (Durance 3) | 44 / 46 `FUN_00584750` | word 0x16 bit 13 or 0: the warp (`FUN_00546ac0`); from level 0x66 the fortress waypoint (0x67) given and act 4 opened (`FUN_0053aec0`) |
| 345..347 hellfire | 22 `FUN_0054fb40` | mode 2 unless the objects.txt flags +0x141 / +0x13f say; event 0 at +25 frames |
| 259, 373 skeleton spawns | 26 `FUN_005500e0` | mode 1 (client fn 7 plays it) |
| 358, 359 hell brazier | 34 `FUN_005500f0` | an **objrgn** step: odd → mode 1 |
| 380 burning soul | 51 `FUN_005501f0` / 48 `FUN_005869f0` | init: mode 1 / 2 → event 9 at +35; operate: rand(100) < 90 → a chest drop (`FUN_00585b90`) and mode 5; else mode 1, events 1 and 9 (+20) |
| 383, 384 souls | 2 / 3 | the usual urn / chest |
| 255 diablo start point | 55 `FUN_005b5590` | a4q2's +6 / +8 (and the all-seals check) |
| 131 the flash | 59 | in level 108 its event 7 makes the seal's boss |
| 369 trapped soul placeholder | 46 `FUN_005506d0` | the trapped souls (monsters.md "Act 4's population") |
| 399 fissure | 58 `FUN_0054fdb0` | an **objrgn** step: its first eruption (event 8) at that % 250 + 25 frames |

## The ending (traced 2026-10-16)

Classic and LoD part at Diablo's death; the server's game +0x70 is the
expansion flag, the client's `DAT_007a04f4` (S→C 0x01 byte +6).

Diablo dies (`FUN_005b52e0`, class 0xf3): classic only, `FUN_00545760`
sends every player its quest bits. a4q2's data: +1, +3, +4, +5 set, +0x15
clear, +0x18 = GetTickCount. Each player with word 0x1a bit 13 gets
`FUN_005b4d20` (bits 13, 0; classic: bits 6, 7 and the progression,
`FUN_00538680`) and, off Diablo's unit (`FUN_005b49c0`), S→C 0x5d {0x17,
flags 2} plus sound event 0x4b. The client (`FUN_004a2cb0`): classic sets
`DAT_007a0604` (play the end on leaving) and bumps its own progression
(`FUN_0044ec80`), and `DAT_007bc9d4`; LoD sets `DAT_007bc9d8` instead.

The timer (`FUN_005b4be0`, real milliseconds, not frames):
- 75 s: `FUN_0052e2a0` over the clients, once (+0x15): in a realm game
  (game +0x6a 1 / 2) each client's character saved (`FUN_00532400`); a
  single player or TCP/IP game (+0x6a 3) does nothing.
- 90 s, classic: each player (`FUN_005b4a80`). With bit 13: warped to the
  Pandemonium Fortress (`FUN_0053aec0` level 103) and S→C 0x5d {0x17,
  flags 1}, which sets the client's leave flag (`DAT_007a0620`). Without:
  S→C 0x50 subtype 0x17; the client sends C→S 0x69 (leave) itself
  (`FUN_00477ee0`).
- 95 s, classic: the game closes (`FUN_00530590`).

LoD never runs the 90 / 95 s steps. The game goes on; Tyrael's portal
(OperateFn 73, `FUN_005b5880`, LoD only, act 4 only) sets word 0x1c bits 0
and 13 the first time and sends S→C 0x5d {0x17, flags 2} and S→C 0x61 {5}
(`FUN_0053d940`: play cinematic 5), then warps to Harrogath (109). The
hellgate sends 0x61 {4} the same way.

The client's cinematics (`FUN_00482ef0`, table 0x721d9c, 12 bytes each):
0 d2_promo, 1 d2intro, 2..4 ACT0{2,3,4}START, 5 ACT04END, 6 D2X_INTRO, 7
D2X_OUT. Classic: on leaving the game (`FUN_0044f360`) with `DAT_007a0604`
set, ACT04END plays, then the endgame screen (`FUN_0044e770`:
ui\menu\endgame, or endgame2 in LoD, with its endgameok button), then the
main menu. No credits roll; they stay a menu item. LoD: cinematic 5
with `DAT_007bc9d8` set plays ACT04END and then D2X_INTRO.

The endgame screen (`FUN_0044e770`, classic `endgame`): its palette
`palette\endgame\pal.dat`; the 8 frames are two 320 × 480 halves
(256 + 64 wide, 256 + 224 tall) at FUN_004f6480 bottoms (0, 256),
(256, 256), (0, H−1), (256, H−1) and (W/2, 256), (W/2+256, 256), (W/2, H),
(W/2+256, H), so at 800 × 600 they leave black bands. Font42 (font 3),
colour 4, centred: 0x13ea at baseline 62, 0x13eb 107 (classic only), the
name 187 (`FUN_0044e5a0`), 0x13ec 232, then the title for the game's
difficulty (`FUN_005068a0`) and the name at 277 (LoD 100 / 272 / 357).
OK: `endgameok` at (W/2−48, H−80), pressed frame 1; 0x13ee in font16
colour 0 at baseline H−88 (pressed: 2 left, 2 down); its box W/2±48,
H−112..H−80; down then up on it (`FUN_0044e460` / `FUN_0044e4f0`) ends it.

d2d (2026-10-16): `World::act4_update` keeps the 90 / 95 s timer
(`diablo_dead_at`; the 75 s step is realm-only), `end_movie` is the client's
DAT_007a0604; any leave with it set plays ACT04END (skippable) and then
`Screen::Endgame` (`render_endgame`), OK to the title. Classic only: LoD's
end (Tyrael's portal, cinematic 5) waits for Act 5.

## The quest log (+0xb)

`FUN_00544350` sets only the state (+0xc). The log moves through
`FUN_00544300(record ECX, log DL, callback, arg, flag)`: +0xb = DL, and
with flag 1 the callback over the players (`FUN_005b3940` / `FUN_005b4e40`
/ `FUN_005b59c0`: S→C 0x5d to each unless done (bit 0 or 15) without 13 /
14). Besides the joins (2 / 1 / 1, written directly):

| quest | where | log | sent |
|---|---|---|---|
| a4q1 | the fortress at state 2 (`LAB_005b4140`) | 1 if 0 | no |
| a4q1 | Tyrael's talk closes with 670 heard (`LAB_005b41d0`, data +0x11) | 1 | yes |
| a4q1 | Izual's first think (`FUN_005b4390`) | 2 if under 2 | yes |
| a4q1 | the 3-tick timer (`FUN_005b3e60`), state 4 | 3 unless 4 / 13 | yes |
| a4q1 | the ghost's 675 heard | 4 unless 4 | yes |
| a4q1 | Tyrael's 676 (state 5) | 13 | no |
| a4q2 | the fortress at state 2 / the sanctuary (`LAB_005b4ea0`) | 1 if 0 | no / yes |
| a4q2 | Tyrael's talk closes with 681 heard (`FUN_005b4f90`, data +0) | 1 | yes |
| a4q2 | Diablo dies (`FUN_005b52e0`), open | 13 | yes |
| a4q3 | the forge's InitFn (`FUN_005b5a20`), open | 2 unless 2 / 3 / 13 | yes |
| a4q3 | the soulstone set (`FUN_005b5c10`) | 3 | yes |
| a4q3 | the third blow | 13 | yes |
| a4q3 | Cain's talk closes with 678 / 679 heard (`FUN_005b6440`, data +2) | 4 with the stone given (+0x1c), else 1 | yes |
| a4q3 | Cain's 680 (state 5) | 13 | no |

The soulstone's pickup (`LAB_005b5a90`) opens a4q3 (state 0 → 1) only
without bits 0 / 13 / 1 and with the log under 13; the chain from a4q1
(`LAB_005b5f40`) only needs state 0 and +9. d2d: `IzualQuest` /
`HellforgeQuest` / `TerrorQuest` `news`, drained in `World::act4_update`.

