# Text/Text.cpp: the NPC gossip text list

Traced from game.exe 1.14d and run in the emulator: `tools/emu/text_list.py`
(929 cases) for the list, `tools/emu/quest_name.py` (every u16 message) for the
quest label. d2d has no port of the list: it keeps each NPC's topics in its own
tables (`npc-talk.md`), so nothing here changes d2d except the quest name
(`bugs.md` 46).

## The list (`.\Text\Text.cpp`)

The 0x27 NPC-info packet (`net-packets.md`) carries the topics an NPC offers.
The server builds the block in `QUEST_SendNpcGossipPacket` (call at 0x545804)
and `NPC_BeginInteraction` (call at 0x572d4b), both through `FUN_00661480`; the client fills a fresh list from it in `FUN_004a1600` (mode 1,
the message at packet +6): `FUN_00661240` new list, `FUN_00661510` store,
`FUN_006616e0` sort, kept in `DAT_007bf250`. `FUN_0049f900` (6 bytes) returns
that pointer. The next 0x27 frees the old list (`FUN_006612a0`) first.

| Structure | Layout |
|---|---|
| List (12 bytes, alloc line 0x2b) | +0 pool, +4 count (word), +8 head |
| Node (12 bytes) | +0 message id (word), +4 type (dword), +8 next |

| Function | What it does (allocator calls `FUN_0040b430` / `FUN_0040b480`, file `.\Text\Text.cpp`) |
|---|---|
| `FUN_00661240` (stdcall pool) | New list: count 0, no head. Alloc line 0x2b. |
| `FUN_00661270` (EDI = list) | Free every node, line 0x3c; **count 0 but the head pointer is left as it was** (dangling). |
| `FUN_006612a0` (list) | The same, then frees the header (line 0x4c). |
| `FUN_006612f0` (list, id, type) | Push at the head, id kept as a word, count + 1. Alloc line 0x5a. |
| `FUN_00661330` (list, id) | Remove the first node with the id (head or later), count - 1; an absent id changes nothing. Free line 0x7b. |
| `FUN_00661390` | Number of type 2 nodes. |
| `FUN_006613c0` (list, n) | The nth type 2 id from the head, 0xe8c when there is none. |
| `FUN_00661400` / `FUN_00661440` (list, n) | The nth type 0 / type 1 id, 0xffff when there is none. |
| `FUN_00661480` (list, block) | Zero 34 bytes; byte 0 = the count; entry i from the head: type byte at 2 + 4i, id word at 4 + 4i. Halts (line 0xf8) on a ninth entry, 0xed / 0xee on a null list / block. |
| `FUN_00661510` (list, block) | Free the old nodes, then push the block's entries in packet order (so the list reverses). Halts: 0x10b / 0x10c null, **0x112 when the count byte is 8 or more**. |
| `FUN_006615f0` (fastcall CX count; entries, compare) | Insertion sort of 12-byte entries (word id, dword data) by the fastcall compare; the compare returning > 0 moves the left one later, so ties keep their order. `0x6615d0` compares ids as words. |
| `FUN_006616e0` (list) | Fewer than 2 nodes: nothing. Else copies the (id, type) pairs to a count x 12 buffer (alloc line 0x16c), sorts it by id ascending, stable, writes them back in list order and frees it (line 0x17e). Halts 0x165 on a null list. |

Findings from the oracle and static analysis of the caller:

- The builder writes up to 8 entries but the reader takes a count byte under
  8: an 8-entry list builds, then halts the receiver at line 0x112 (after it
  has already freed its old nodes). Whether an eight-entry list can occur in
  play has not been established here.
- `FUN_00661270` never clears the head, so the client's `FUN_00661510` would
  push onto a freed node if the list weren't new each time. `FUN_004a1600`
  always makes a fresh list (`FUN_00661240`) first, so the stale head is
  never read.
- The sort is by id only (type is data), ascending, stable.

## The Talk submenu's quest label (`FUN_0049f910`)

(`FUN_0049f900`, the 6-byte list getter, was cited for this earlier; the label
function is `FUN_0049f910`, 72 bytes.) CX is the NPC message. The table at
`0x722678` is {u16 message, u16 string id} x 0x20f (count at `0x722eb4`).
`cmp word [0x722808], cx; jbe` sends a message at or above row 100's (164) to
the search of rows 100..count, any other to rows 0..99. Row 100 is 164, so it
is found: 3716. No match: 0xe8c. The string lookup (`0x524a30`) is a tail
jump.

`bugs.md` 46 said 164 fell between the two searches and got 0xe8c; the
disassembly and the run say otherwise, and d2d's `rules::quest_name` had the
same mistake (fixed with a test). `quest_name.py` runs the function on all
65536 messages against the runs in `quests.hpp` (527 have a name).

Re-check: `cd tools/emu && uv run python text_list.py`, `uv run python quest_name.py`.
