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

## Small functions in the same work package (cited)

None of these is in `Chat/Chat.cpp`; they came with the seed's callee list.

- `FUN_00413750` (162 bytes) is the CRT `strlen`: a null pointer calls
  `FUN_0041c340(0x57)` (the invalid-parameter path) and returns 0; otherwise
  it counts with the word-at-a-time zero-byte test. Not game-specific.
- `FUN_0044b7b0` (12 bytes) returns the dword at `DAT_007a0438 + 0x225`
  (`DAT_007a0438` is the client-state block whose +0x1ef flag 0x20 marks an
  expansion character, `tcpip-menu.md`). Callers: `FUN_0044cf20`,
  `FUN_00477ee0`, `FUN_0047bdf0`, `FUN_0047c100`, `FUN_0047c1f0`. What the
  field means is not traced here.
- `FUN_006b1350` (6 bytes) is the import thunk for `DirectDrawCreate`
  (ddraw.dll); the decompile is a self-call because the jump is indirect.
  Callers `FUN_00511d0e`, `FUN_006b42f0`, `FUN_006b59a0`.
- `FUN_0046dbf0` (69 bytes, `ECX` unused, `EDX` = path, `EDI` = out) copies the
  base name of a path: if the byte 4 before the end is `.`, it walks back to
  the last `\`, and copies the characters between (via `_strncpy`) with a 0.
  It does not check that a `\` exists, so a path with an extension and no `\`
  reads before the string. Other paths leave the output untouched.
- `FUN_0046dc40(table)` (121 bytes, `Gfx.cpp` callers `FUN_0046dcc0`,
  `FUN_0046e740`, `FUN_0046f650`) looks up a loaded graphic by file name: it
  zeroes a 0x104-byte local, takes the base name (`FUN_0046dbf0`), lower-cases
  it (`__strlwr`), hashes it with `FUN_0046db90` (`FUN_00413ce0` mixed down and
  masked with 0x1ff: 512 buckets; a name of 8 or more characters is fatal:
  message `FUN_00408090(0x79)`, `FUN_00681e09(-1)`), and walks the chain from
  `table + 0xc + hash * 4`. A node's next pointer is at +0, its name is the 8
  bytes at +0xc and +0x10, compared as two dwords. It returns the node or 0.

d2d: not ported (no name-hash cache for graphics).

Re-check: `python3 tools/ghidra/fn.py 0046dc40`, and the same for each address.
