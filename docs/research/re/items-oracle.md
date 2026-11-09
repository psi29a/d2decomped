# ITEMS small functions: emulator oracle results

`tools/emu/items_traces.py` runs stock game.exe 1.14d's own code for 39
ITEMS functions against a hand-written model on random unit, item-data and
Items-row bytes (3120 cases; `--mutation-check` breaks the model 20 ways).
It compares the whole EAX, the ordered callee log (register then stack
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
and the set-item row `base + id * 0x1b8` with signed id and count compares.

## Sharpened

- FUN_0055fe00: a null unit takes the same path as a mode-0 unit (the
  `test esi / je` jumps into the equipment check), so the refresh runs for a
  null unit too, not only a stored item. The returned EAX is the last
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
- FUN_0055bf30, FUN_0055c4d0 and FUN_0055fde0 pass ECX/EDX through to their
  callees unchanged; FUN_0055c4d0's skill arrives in EAX and is pushed second
  (callee argument 1).

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
