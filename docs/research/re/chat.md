# Chat/Chat.cpp: overhead message nodes

Traced from game.exe 1.14d. `FUN_00661110` and `FUN_006611a0` are run in `tools/emu/chat_node.py`, the IME helpers in `tools/emu/chat_ime.py`; the rest of the file is cited, no oracle run.

## Overhead message node (`FUN_00661110`, 138 bytes, `.\Chat\Chat.cpp`)

`FUN_00661110(?, text, now)` makes the node a unit keeps at `unit + 0xa4` for
the text over its head. `game-srcfiles.tsv` has `00661110 Chat/Chat.cpp`, and
the allocation passes the assert string `.\Chat\Chat.cpp` with line `0x32`.

- It measures `text` (inline strlen). An empty string returns 0 and makes no
  node. The length is clamped to 0xfe (254).
- It allocates through `FUN_0040b430(".\\Chat\\Chat.cpp", 0x32, 0)`
  (`FUN_0040a080`, with a log call `FUN_00408900` when that returns 0). The
  size is 0x110 (`mov edx, 0x110` at 0x66114d; the ECX pool is the first
  argument). Nothing outside the fields below is written.
- Node fields:

  | Offset | Value |
  |---|---|
  | +0x00 | `length * 8 + 0x7d`: the time it stays, in frames |
  | +0x04 | `now` + that time: the frame it expires (`now` is the 3rd argument) |
  | +0x08 | u8 from `FUN_00525150`: the language id kept in `DAT_008829ec` (read from strtable.cpp line 1099 on first use, 0 if it is over 13) |
  | +0x0c | 0 (set to 0 here; `FUN_0049f410` passes the node on to `FUN_00661230`) |
  | +0x10 | the text, `length + 1` bytes copied by `_strncpy`, then a 0 at +0x10 + length + 1 (`length` is the clamped one, so a longer text keeps 255 characters, the last NUL at +0x10f) |

- Callers pass `now` from two places: the server's shrine (`FUN_00583c70`,
  after `wsprintfA("%d", 0xe63 + shrine row)`) uses `game + 0xa8`; the client's
  0x26 handler (`FUN_0049f410`) uses `DAT_007bf20e`. Each frees the unit's old
  node with `FUN_006611a0` (the shrine before, the 0x26 handler after).
- Other callers: `FUN_004a1600` (0x27 NPC info, kind 3), `FUN_0054a290`,
  `FUN_005de330` (`ai_speech_trigger`, `names.tsv`).

`_strncpy` (`FUN_00687330`, 292 bytes) is the Visual Studio CRT `strncpy`:
it copies at most `count` bytes and zero-fills the rest if the source ends
first. It returns the destination. Nothing in it is specific to the game.

`FUN_006611a0(pool, node)` (31 bytes, `ret 8`) frees a non-null node through
`FUN_0040b480(".\\Chat\\Chat.cpp", 0x44, 0)` and does nothing for a null one.

**Oracle:** `uv run python chat_node.py` (tools/emu) makes nodes for selected
text lengths from 1 to 300 characters at three `now` values and compares all 0x110 bytes
(poisoned with 0xab) with the table above, then frees one and a null.

**d2d:** the same stay, with the length clamped to 254
(`overhead_frames`, FUN_00661110). The host's overheads
(`apps/d2d/town/draw.cpp`) stay min(length, 254) x 8 + 125 frames, so a
255-character all-digit string id stays 2157 frames, the same as 254.
A shrine's own message is a 4-digit id, 157 frames
(`components/game/world/tick.cpp`). d2d does not keep the language byte,
the expiry frame or the list link, and it does not store the node's
text: only a string id is shown. The network path still accepts an
all-digit string whose numeric value is 1..65535, including leading
zeroes.

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
  Callers `LAB_00511d0e` (inside another function), `FUN_006b42f0`, `FUN_006b59a0`.
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

## Chat input IME helpers (`.\UI\chat.cpp`)

Run in `tools/emu/chat_ime.py` (123 cases; the Imm and language imports and
the allocator hooked). d2d has no IME support: not ported.

- `FUN_0047ad60` (EDI = the IME context; 148 bytes) refreshes the candidate
  list at `0x7bb5f4`. `ImmGetCandidateListCountA` gives the byte size and
  `ImmGetCandidateListA(context, 0, NULL, 0)` the list's size; both must be
  non-zero or nothing changes. Then it frees the old list (line 0xeb),
  allocates the first size (line 0xed), fills it with
  `ImmGetCandidateListA(context, 0, list, size)`, and when the list's style
  (+4) is 2 and its count (+8) is 1 frees it again (line 0xf2) and clears the
  pointer. (The redivivus decompile names the two values the other way round;
  the disassembly allocates the `CountA` result.)
- `FUN_0047b010` (86 bytes) is "is the IME language in use": the mode at
  `0x7bb5dc` 9 wants the system default language 0x404 or 0xc04 (Traditional
  Chinese), 8 wants 0x804 or 0x1004 (Simplified); any other mode asks
  `ImmIsIME(GetKeyboardLayout(0))`.
- `FUN_0047b070` (24 bytes, `ret 4`) sets the message's +0x18 to 1 and +0x1c
  to 0 (handled, result 0).

### IME notification, initialization and shutdown

`tools/emu/chat_ime_lifecycle.py --exe /path/to/Game.exe` runs the three
remaining mapped `UI/chat.cpp` functions on the 1.14d executable: 432
notification cases, 768 initialization cases and 48 shutdown cases. The
IME calls, language gate, candidate refresh, allocator and event-table
registration are scripted and logged; no IME is opened on the host. The
script checks return and stack cleanup. This verifies scoped stock control
flow, not parity with d2d or an end-to-end Windows IME.

- `FUN_0047ae00` (one message pointer, `ret 4`) sets message +0x18 = 1
  and +0x1c = 0, calls `FUN_00420290`, then obtains the window's IME
  context. Null context returns immediately. Message +8 selects the
  notification: code 2 in mode 9 asks whether the current keyboard layout
  is an IME and sets `0x7bb5f0` to 0 if so, 4 otherwise; code 3 in mode 9
  refreshes candidates; code 4 frees the candidate list for mode 9 or 8
  (source lines 0x160 or 0x16b); code 5 refreshes it for mode 9 or 8.
  Code 6 queries conversion in every mode, then modes 7 and 12 set the
  byte at `0x7bb5e0` and status at `0x7bb5f0` to conversion bit 0. Code 8
  in modes 6, 8 and 9 reads open status and sets that byte and status to
  1 and 3/5/4 respectively when open, else both zero. Every non-null
  context is released. The candidate-list helper is hooked; its own
  behavior is checked by `chat_ime.py` above.
- `FUN_0047b090` is IME **initialization**, rather than a key handler. It
  registers four window handlers from table `0x712f4c`, obtains and stores
  the mode at `0x7bb5dc`, and only modes 6, 7, 8, 9 and 12 proceed to
  the `FUN_0047b010` language gate. For an accepted language and non-null
  context it clears the low words at `0x7bb5f8` and `0x7bc850`. Mode 6
  with system language 0x411 registers seven handlers from `0x712fa8`,
  marks `0x7bb5e1`, requests IME open, and sets enabled/status 1/3 on
  success or 0/0 on failure. Modes 7 and 12 with language 0x412 or 0x812
  open the IME, request conversion 1 while retaining the sentence mode,
  read conversion back, set status to bit 0 and mark registered. If that
  bit is clear, the old enabled byte is retained. Modes 8 and 9 clear
  enabled, mark registered, simulate hotkey 0x10 or 0x70 if the layout is
  not an IME, request open and set status 5 or 4. These three branches
  register the seven handlers and every acquired context is released.
  Other language/mode combinations leave the second table unregistered.
- `FUN_0047b2e0` unregisters the four-handler table. If the byte at
  `0x7bb5e1` is nonzero, it also unregisters the seven-handler table,
  obtains the context, closes it if present, and for modes 7 and 12
  resets conversion to zero while preserving the returned sentence mode.
  It releases a non-null context, clears the low words at `0x7bb5f8`
  and `0x7bc850`, and frees the candidate list at line 0x29d. The registered
  byte itself is not cleared. With that byte zero, those additional
  operations are skipped.

The oracle scripts the language gate independently to exercise both outcomes;
`chat_ime.py` checks that gate's own language rules. Event registration,
formatting, callbacks and actual platform IME behavior are outside these
checks. Re-check the instruction-level calling conventions with
`tools/emu/disasm.py` and the three addresses; Ghidra omits register arguments
and attributes the pushed handler count to the window getter.
