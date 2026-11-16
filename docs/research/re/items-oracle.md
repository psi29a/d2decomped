# ITEMS small functions: emulator oracle results

`tools/emu/items_traces.py` runs stock game.exe 1.14d's own code for 39
ITEMS functions against a hand-written model on random unit, item-data and
Items-row bytes (3120 cases; `--mutation-check` breaks the model 20 ways).
It compares the whole EAX (FUN_0062a250: AL only; no EAX comparison for a
case whose model returns `None`, which includes the free model), the ordered callee log (register then stack
arguments), the arena, the fatal-error line and the bytes `ret n` pops.

This page records only where the oracle agrees with, sharpens or disagrees
with the static traces in PR 317's `items-common-trace.md`,
`items-mode-early.md` and `items-mode-late.md`; it does not repeat them.

Functions: FUN_0055d930, 0055e9a0, 0062a360, 0055c460, 0055fe60, 0055fde0,
0055fe00, 0055d940, 0055f2a0, 0055bf30, 0055c4d0, 0055ca00, 0062ba60,
0062e410, 00628950, 0062a250, 0062af50, 006281b0, 006282a0, 006283f0,
006282d0, 00629ea0, 00627dc0, 006288a0, 00629860, 006298a0, 00628440,
006284c0, 006298e0, 00629990, 00629620, 006296b0, 00628980, 00629a00,
00629d00, 0062a2b0, 00627d00, 0062a900, 0062a490. FUN_0055f240 and
FUN_00650e30 run for real inside them.

## Confirmed

The 39 traces held on the 3120 generated cases: the stub pops (`ret 4` / `ret 8`),
the pure helpers' arithmetic, the forwarded argument orders, the fatal
lines (0xd6c, 0xd8a, 0x906, 0xf28, 0x1054, 0x12e0), the seed reset to
`1, 0x29a`, the free of D with line 0x10a, the slot pairing 4/5, 6/7, 11/12
and the set-item row `base + id * 0x1b8` with signed id and count compares (the count cases include 0x80000000 and 0xFFFFFFFF).

## Sharpened

- FUN_0055fe00: a null unit takes the same path as a mode-0 unit (the
  `test esi / je` jumps into the equipment check), so the refresh calls follow for a
  null unit too, whenever FUN_0062ff70 returns nonzero. The oracle hooks that
  callee with arbitrary results, so it does not show that the real callee
  returns nonzero for a null unit. The returned EAX is the last
  callee's result: FUN_0062ff70's 0 when it refuses, FUN_006280d0's when the
  mode is not 0.
- FUN_006282a0 and FUN_006283f0 (byte getters): the rejected value is only in
  AL. On the cases run, a wrong-type unit leaves the unit pointer's upper 24 bits in EAX, a null
  D leaves 0, a null unit leaves 0. The success value keeps D's upper bits.
  The trace states this for FUN_006283f0 only.
- FUN_00629860 / FUN_006298a0: the rejected 100 is written to AX only; a
  wrong-type unit keeps the unit pointer's upper 16 bits in EAX, a null unit
  or missing row gives exactly 0x64.
- FUN_00629990: the success value keeps the Items row pointer's upper 24 bits
  (`mov al`); FUN_006298e0 is zero-extended.
- FUN_0055bf30 and FUN_0055fde0 pass ECX/EDX through to their callees
  unchanged. FUN_0055c4d0's skill arrives in EAX and is pushed second (callee
  argument 1); its FUN_00647960 hook records only the two stack arguments, so
  the oracle does not show that ECX/EDX are passed through.

## Ghidra names that are backwards or misleading

- FUN_0055fe60 `ITEMMODE_IsItemOnCursor`: not a predicate, a wrapper of
  FUN_0055c2c0 with final argument 0 (EAX is the callee's).
- FUN_0055fe00 `ITEMMODE_GetItemCursorMode`: sets the identified flag and refreshes equipment; FUN_0055d940 `ITEMMODE_GetItemGoldValue` removes wield bookkeeping and the item's stats. Neither is a getter, whatever the name says.
- FUN_006281b0 `ITEM_GetItemData_eD2ItemCmd`: the full transient flag word at
  D `+0x14`, not a command enum.
- FUN_006283f0 `bVariant` reads D `+0x49` (the same byte FUN_00628420 writes as
  the ear-name companion field), FUN_006282a0/FUN_006282d0 `eExchangePage` use
  `+0x46`.

## Not exercised

FUN_006288a0 with a class that has no Items row: the oracle's arena cannot map the null row FUN_006288a0 then reads. Callees
(FUN_006335f0, FUN_0055c2c0, FUN_0055dbc0, FUN_00621000, FUN_006280d0,
FUN_0062ff70, FUN_0055c730, FUN_0063d2b0, FUN_00621190, FUN_0063bde0,
FUN_0055bed0, FUN_00647960, FUN_0062b5b0, FUN_0062ae20, FUN_00628900,
FUN_00410eb0, FUN_0063aaf0, FUN_0063b2c0, FUN_0063dfa0, FUN_00629da0) are
hooked and logged, not verified here.

## Second set: items_traces2.py

`tools/emu/items_traces2.py` runs 37 more ITEMS functions on the same rig
(2960 cases; `--mutation-check` breaks the model 25 ways). `--coverage`
reports no instruction of the 37 bodies left unreached, not counting the
exit code after each fatal halt.

Functions: FUN_00627d90, 006282f0, 00629f20, 0062a670, 00627e20, 00629e40,
00627c90, 0062a060, 00629660, 006296f0, 00629a40, 00628530, 00629800,
0062a180, 0062e7e0, 0062e830, 0062bbd0, 0062c0b0, 0062ea80, 0062b3a0,
0062a310, 0062b450, 0055c270, 0055c6e0, 0055c470, 0055c4f0, 0055c560,
0055d9d0, 0055f500, 0055f5d0, 0055f6e0, 0055f9c0, 0055fb10, 0055ed30,
00561e70, 00562d30, 00562da0. FUN_00629620, 006296b0, 00629a00 and 0055c4d0
(the first set) and the CRT memset run for real inside them.

### Confirmed

The traces in `items-common-trace.md`, `items-mode-early.md` and
`items-mode-late.md` held for the 37 on the cases run: the fatal lines (0x187/0x188/0x18b,
0x608/0x609/0x60c, 0xd7b/0xd7c, 0xd93/0xd94, 0xdcf/0xdd0/0xdd2, 0xf3d/0xf3e,
0x10c7/0x10c8/0x10ca, 0x1155, 0x118f/0x1190/0x1192, 0x1504/0x1505/0x1507,
0x24d/0x24e, 0x394/0x395, 0x12e9/0x12ea, 0x15ea, 0x19a0/0x19a1, 0xd5), the
signed index checks of the ItemTypes and SetItems rows, FUN_0062b450 asking
the parent's mode (not the child's) for each child, and FUN_0055d9d0's
snapshot layout: the left skill (FUN_00620190) at +0x10 (id) / +0x18, the
right (FUN_006201d0) at +0xc / +0x14, a missing hand id 0 and -1.

### Sharpened

- FUN_006282f0: the rejected 0xff is only in AL. A wrong-type unit leaves the
  unit pointer's upper 24 bits in EAX; a null unit or null D leaves 0. The
  success value keeps D's upper bits, as FUN_006282a0 does.
- FUN_0062a670 writes AX only (D's upper 16 bits stay); FUN_00629800 and
  FUN_0062a180 write AL only (the Items row pointer's upper 24 bits stay).
  The ItemTypes getters' rejections are the whole EAX, not just AL:
  FUN_0062e7e0 and FUN_0062e830 give 0 (`xor eax, eax`), FUN_0062bbd0 and
  FUN_0062c0b0 give 7 (`mov eax, 7`); their row bytes are zero-extended.
- FUN_00627c90: the 0x74-byte block is zeroed with the CRT memset before
  `+0xc = -1` and the store into unit `+0x14`; a failed allocation is fatal
  0xd5 before either.
- FUN_00562da0's table at 0x6e117c holds code only at index 3
  (FUN_00562d30) and 4 (0x55f120, a bare `ret`, which game-functions.tsv
  doesn't list as a function); indexes 0..2 and 5..7 are null. Any index
  the IsBadCodePtr import rejects is fatal 0x15ea.
- FUN_00562d30 stops after the durability step when FUN_00562c40 returns
  any nonzero value, not only 1 (the trace says 1). It returns the last
  callee's result: FUN_00562c40's, or FUN_006289f0's 0 when the item isn't
  stackable.
- FUN_0055f6e0's call to FUN_0053d130 takes four stack arguments (1, 0x46,
  quantity, 0): the 0 is left on the stack from the FUN_00625480 call, which
  pops three of the four it was pushed.
- FUN_0055f9c0: the packet's kind byte (DL for FUN_0053c520) is the owner's
  unit type, 6 without an owner, next to the owner's id or -1.
- FUN_0055f5d0 compares the six FUN_00627ec0 / FUN_00627f80 answers and the
  FUN_00627f20 one by their low 16 bits only.
- FUN_0055ed30: unit types 2, 4 and 5 take the cell from `[[unit+0x2c]+0xc]`;
  the other types (signed: 0, 1, 3, 6 and up, negatives) ask FUN_006488c0
  of the path at `+0x2c`, or uses 0 without one.

### Not exercised

Callees hooked and logged, not verified here: FUN_006335f0, 0062b400,
00629da0, 00625480, 00625500, 00627260, 006295b0, 0063b2c0, 0063dfa0,
0063dfd0, 00451f70, 0055c110, 0063bef0, 0063bde0, 00629bb0, 006439b0,
00647960, 005701b0, 00644140, 00629b50, 00620190, 006201d0, 00643ad0,
00643ce0, 00625820, 006277f0, 00627910, 00627ec0, 00627f80, 00627f20,
006272b0, 005531c0, 0053d130, 0055c070, 0055bff0, 006450f0, 00645120,
00624690, 00628170, 0063cc70, 00621000, 0053eed0, 0063ad90, 006488c0,
0063c550, 00557fd0, 006280a0, 00629930, 00625e00, 00562c40, 006289f0,
00623060, 006230c0, 006287b0, 0055da70, 0053c520 and the allocator.

## Third set: items_traces3.py

`tools/emu/items_traces3.py` runs 24 more Items.cpp functions on the same
rig (1920 cases; `--mutation-check` breaks the model 21 ways). `--coverage`
reports no instruction of the 24 bodies left unreached, not counting the
exit code after each fatal halt. Every callee is hooked.

Functions: FUN_00627df0, 0062a8d0, 0062a0f0, 0062a2f0, 006288d0, 00629900,
006299b0, 0062a1e0, 0062a270, 00629c80, 00629cc0, 00629d40, 00629f70,
00628480, 0062ba80, 0062e6f0, 0062e740, 0062e790, 0062e880, 0062a5d0,
0062a620, 00558610, 00558450, 005584c0.

### Confirmed

The readings cited in `drops.md`, `combat.md`, `item-names.md`, `items.md`,
`store.md`, `weapon-switch.md` and `rules.hpp` held on the cases run:

- the item-data fields D `+0x10` (sockets) and D `+0x18` bit 0x400000
  (ethereal);
- quality 4..9 (FUN_0062a0f0) and the plain quality, nonzero and up to 3 or
  above 9 (FUN_0062a2f0);
- the Items row's `+0x104`, `+0x137`, `+0xdc` bits 0 and 1, `+0x12a`, the
  word `+0x11e` (0x27 not sellable) and `+0x13d`;
- the ItemTypes fields `+0xc` and `+0xe` (signed words), `+0x10`, `+0x13`,
  `+0x21` (below 7) and `+0x22`;
- the potion bonus by class: life ×1.5 for classes 0, 3, 6, ×2 for 4 and
  for a non-player; mana ×1.5 for 0, 3, 6 and ×2 for 1, 2, 5;
- the fatal lines 0xea6/0xea7, 0x119b/0x119d/0x11a5, 0x1040, 0x104a,
  0x105e, 0x68d, 0xd5e and 0xd7d.

### Sharpened

- FUN_00558610 takes x in ECX, k in EDX and a second k on the stack (`ret
  4`). For signed x at most 110 (`jg 0x6e` not taken) it returns x
  unchanged; otherwise `(x - 100) * stack / (x - 100 + EDX) + 100`, with
  a 32-bit `imul` and an `idiv` rounding toward zero. Its one caller,
  FUN_00558640, passes the same k in both (0x558746, 0x5587bf, 0x558847),
  so drops.md's formula holds.
- FUN_0062a5d0 / FUN_0062a620: ×1.5 is `v + (v sar 1)` (arithmetic shift,
  so it rounds down for negatives too), ×2 is `lea`. Both wrap at 32 bits.
  A player of class above 6 (unsigned) gets v unchanged.
- FUN_00558450 / FUN_005584c0: the index is a byte. It is FUN_006427f0's AL,
  plus 2 when FUN_00554010's byte `+0x6d` is nonzero, so it wraps at 256.
  An index at or past the table's count (an unsigned `jb`; 4 in stock
  game.exe) gives the table's last code, `hp4` / `mp4` (0x731f58 /
  0x731f6c).
- FUN_0062a2f0 and FUN_0062a270 take their argument in EAX and end with a
  plain `ret`. FUN_0062a270 returns 1 at the first inventory node whose
  FUN_0063dfd0 answer is nonzero.
- FUN_006288d0 writes AL only: the Items row pointer's upper 24 bits stay;
  a wrong-type unit leaves the unit pointer's; a null unit gives 0.
- FUN_0062e880 rejects with `or al, 0xff` over whatever EAX held: the unit
  pointer (wrong type), the type index (out of range) or 0 (a null row).
  A row byte of 0xff takes the same path. The success value keeps the row
  pointer's upper 24 bits.
- FUN_0062a0f0 compares quality signed (`jl` / `jg`). Unsigned would give
  the same answers for 4..9, so the oracle can't tell the two apart.

### Not exercised

- FUN_006288d0 with a class that has no Items row: it reads the row
  unchecked, at address 0x104, which the arena does not map.
- The ItemTypes lookups test only `rows + index * 0xe4` for null, so a null
  rows pointer with an index above 0 reads near address 0. The cases pair
  a null table with index 0 only.
- The callees FUN_006335f0, 0062b400, 00625480, 0063b2c0, 0063dfd0,
  0063dfa0, 00620bb0, 0061a1b0, 006427f0 and 00554010 are hooked and logged,
  not verified here.
