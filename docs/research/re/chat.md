# Chat/Chat.cpp: overhead message nodes

Traced from game.exe 1.14d (cited, no oracle run).

## Overhead message node (`FUN_00661110`, 138 bytes, `.\Chat\Chat.cpp`)

`FUN_00661110(?, text, now)` makes the node a unit keeps at `unit + 0xa4` for
the text over its head. `game-srcfiles.tsv` has `00661110 Chat/Chat.cpp`, and
the allocation passes the assert string `.\Chat\Chat.cpp` with line `0x32`.

- It measures `text` (inline strlen). An empty string returns 0 and makes no
  node. The length is clamped to 0xfe (254).
- It allocates through `FUN_0040b430(".\\Chat\\Chat.cpp", 0x32, 0)`
  (`FUN_0040a080`, with a log call `FUN_00408900` when that returns 0). The
  size is in a register the decompiler drops, so it is not read here. The
  writes below reach offset 0x10 + 0xff, so the node is at least 0x110 bytes.
- Node fields:

  | Offset | Value |
  |---|---|
  | +0x00 | `length * 8 + 0x7d`: the time it stays, in frames |
  | +0x04 | `now` + that time: the frame it expires (`now` is the 3rd argument) |
  | +0x08 | u8 from `FUN_00525150`: the language id kept in `DAT_008829ec` (read from strtable.cpp line 1099 on first use, 0 if it is over 13) |
  | +0x0c | 0 (set to 0 here; `FUN_0049f410` passes the node on to `FUN_00661230`) |
  | +0x10 | the text, `length + 1` bytes copied by `_strncpy`, then a 0 at +0x10 + length + 1 |

- Callers pass `now` from two places: the server's shrine (`FUN_00583c70`,
  after `wsprintfA("%d", 0xe63 + shrine row)`) uses `game + 0xa8`; the client's
  0x26 handler (`FUN_0049f410`) uses `DAT_007bf20e`. Each frees the unit's old
  node with `FUN_006611a0` (the shrine before, the 0x26 handler after).
- Other callers: `FUN_004a1600` (0x27 NPC info, kind 3), `FUN_0054a290`,
  `FUN_005de330` (`ai_speech_trigger`, `names.tsv`).

`_strncpy` (`FUN_00687330`, 292 bytes) is the Visual Studio CRT `strncpy`:
it copies at most `count` bytes and zero-fills the rest if the source ends
first. It returns the destination. Nothing in it is specific to the game.

**d2d:** reads the same, cited from the decompile, no oracle run.
`apps/d2d/town/draw.cpp:148` and
`components/game/world/tick.cpp:242` use "length x 8 + 125 frames" (a shrine's
4-digit string id: 157 frames), which is this formula. d2d does not keep the
language byte, the expiry frame or the list link.

Re-check: `python3 tools/ghidra/fn.py 00661110`, and `grep -n 00661110
docs/research/re/game-srcfiles.tsv`.
