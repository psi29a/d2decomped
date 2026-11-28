# Src/McpConnect.cpp: the realm client's state

Traced from game.exe 1.14d: each function's disassembly and decompile,
and a byte scan of game.exe for rel32 calls, raw pointers and the
instructions that touch each global. Not run in `tools/emu`. The file's
assert path `.\Src\McpConnect.cpp` (`0x6d5af0`) appears in
`FUN_00449e70`; the realm client spans `0x449b70`..`0x44b2d0`
(battlenet.md "MCP", which lists the packets, their senders and their
reply handlers). This page covers the small accessors around them. d2d
has no realm client, so none of this has a counterpart in d2d.

Many callers are code Ghidra has no function for: the MCP reply handlers
from battlenet.md's table (`0x44aba0`, `0x44ae30`, …) and the front end's
loop steps (`0x432fa0`, `0x43dbc0`, …). They are named by address.

## The pieces

**The reply flags.** Each of these MCP replies stores its result in
`mcp_result` (`FUN_00449bf0`, src-frontier.md), then sets a flag of its
own to 1. A screen's loop step polls the realm (`FUN_00449b70`) and ends
its loop (`FUN_004f9190`) once the flag is set, the realm connection is
down (`FUN_00449b30`) or the screen is cancelled (`DAT_00779974`, or
`DAT_0077bd60` for `0x43dbf0`); it clears the flag first, through the
setter with 0.

| Flag | Reply (handler) | Setter | Getter | The getter's callers |
|---|---|---|---|---|
| `DAT_007992e8` | 0x02 CHARCREATE (`0x44abab`); the 0x02 sender `FUN_0044a2e0` clears it (`0x44a2f8`) | `FUN_00449c20` | `FUN_00449c10` | `0x432fa0` |
| `DAT_007992e0` | 0x07 CHARLOGON (`0x44abcb`) | `FUN_00449c40` | `FUN_00449c30` | `0x432fe0`, `0x43dbf0` |
| `DAT_00799298` | 0x0A CHARDELETE (`0x44ae48`), only when the reply's request id is `DAT_00798f8c` | `FUN_00449c60` | `FUN_00449c50` | `0x433020` |
| `DAT_007992f0` | 0x18 CHARUPGRADE (`0x44ae6b`) | `FUN_00449c80` | `FUN_00449c70` | `0x433060` |
| `DAT_00799100` | 0x0B (`0x44ae8b`) | `FUN_00449c90` | `0x449ca0` | none |
| `DAT_007992a0` | 0x0C (`0x44aeab`) | `FUN_00449cc0` | `0x449cb0` | none |
| `DAT_007992a8` | 0x0D (`0x44aefb`) | `FUN_00449d90` | `0x449d80` | none |
| `DAT_00798f5c` | 0x0F (`0x44af1b`) | `FUN_00449db0` | `0x449da0` | none |
| `DAT_00798f84` | 0x10 (`FUN_0044af30`, three sites) | `FUN_00449dd0` | `0x449dc0` | none |

Each setter is `(value; ret 4)` and each getter returns the dword. The
getters at `0x449ca0`, `0x449cb0`, `0x449d80`, `0x449da0` and `0x449dc0`
are code Ghidra has no function for, and nothing calls or points to them
(byte scan).

**The 0x0E message.** Its handler (`0x44aec0`) passes the two strings
after the id to `FUN_00449d10`, which copies them to `0x799294` and
`0x798f60` and sets `DAT_00798f58` to its first argument, 1.
`FUN_00449cd0` would copy them back out and return the flag, but nothing
calls it.

**The join.** The 0x04 JOINGAME reply (`0x44ac20`) keeps, when its
request id is `DAT_00798f8c`, the id in `DAT_00798f80` (`FUN_00449bd0`,
a word), the token in `DAT_00798fb0` (`FUN_00449be0`), the game hash in
`DAT_00798f88` (`FUN_00449de0`) and the server's address as text at
`0x7992cc` (`FUN_00449bc0` returns the buffer): `FUN_0040e080` turns
the address into dotted text (`inet_ntoa` through `0x6b013e`, copied to
`0x75d020`, which it returns) and 0x12 bytes of it are copied
(`FUN_004135d0`). The chat loop `FUN_00441500` copies them into the frontend
record (`0x44190c`: `+0x19` := 3, `+0x1d` the token, `+0x229` the hash,
`+0x37` the address), as battlenet.md "Handoff to D2GS" says. The 0x10
reply (`FUN_0044af30`) fills the same three from its `+1`, `+5` and `+9`
(the address only when it isn't 0, the result 0x28 when both it and `+0xd`
are 0).

**The game list record** at `0x799108`, 0x18c bytes. `FUN_00449d50`
returns it and `FUN_00449d60` zeroes it (the chat loop, after reading
it). The 0x05 GAMELIST reply (`FUN_0044b2d0`) and the 0x06 GAMEINFO reply
(`FUN_0044aca0`) fill it when their request id is `DAT_00798f8c`; a
status of −1 or −2 puts 0x32 or 0x33 in `mcp_result` instead of the rest.

| Offset | 0x05 | 0x06 |
|---|---|---|
| `+0` | 1 when the reply runs past its two strings | |
| `+4` | the request id | the request id |
| `+8` | the game's id (`+3`) | |
| `+0xc` | | `+7`: the seconds since the game started |
| `+0x10`, `+0x11`, `+0x12` | | `+0xb`, `+0xc`, `+0xd`: level, level difference, max players |
| `+0x14` | the player count (`+7`) | the player count (`+0xe`) |
| `+0x18` | the status (`+8`) | the status (`+3`) |
| `+0x1c` | the name (0x10, from `+0xc`) | |
| `+0x2c` | the description (0x10) | the description (from `+0x2f`) |
| `+0x4c` | | a word per player: the class (`+0xf` bytes) |
| `+0x6c` | | a word per player: the level (`+0x1f` bytes) |
| `+0x8c` | | 0x10 bytes per player: the names after the description |

chatdlg.md "The game list" and "A game's details" read it.

**The create queue.** The 0x14 CREATEQUEUE reply (`0x44b150`) keeps the
position from `+1` in `DAT_00799104` (`FUN_00449e30`; read by
`FUN_00449e20`, which `FUN_004453b0` shows) and then sets
`DAT_00798f7c` to 1 (`set_798f7c`, `FUN_00449e50`). `FUN_00449e40`
returns that flag: the chat screen (`FUN_00441b70` at `0x441fab`,
`FUN_004456d0`, `FUN_00445ff0`) and the queue's loop step `0x443880` test
it.

**The character list.** The 0x17 CHARLIST and 0x19 CHARLIST2 replies
(`0x44b170`, `0x44b180`) pass the packet and its length to
`FUN_00449e70`, which keeps a copy in `DAT_007992f4` (`0x449e60` returns
it to `0x43ac27`).

**The connect thread.** `FUN_0044a130` starts the `clt->mcp` connection
on a thread (`CreateThread`, `0x44a070`) and keeps its handle in
`DAT_007992a4`; `FUN_00449ac0` returns it. `FUN_00432400` (`0x43240f`,
`0x432420`) and the loop step `0x43dbc0` wait on it with
`WaitForSingleObject(handle, 0)`.

**The CD key decode.** `FUN_00449920` (chatdlg.md) decodes a 16-character
key; `FUN_00449890` is its first step and `FUN_004497f0` one turn of its
last loop, which `FUN_00449920` has inline.

## The functions

Rows run in address order.

| Function | Operation | Callers |
|---|---|---|
| `FUN_004497f0` | ECX character, EDX index, stack pointer to the key value (`ret 4`): the character made upper case (`_toupper`); under `8` it is XORed with the value's low 3 bits and the value shifted right 3; from `8` to `@` with EDX's bit 0. The same steps as one turn of `FUN_00449920`'s loop at `0x449a20`..`0x449a5e` (value 0x13ac9741, index 15 down to 0) | orphan: no call, jump or pointer (byte scan) |
| `FUN_00449890` | ECX key (16 characters), EDX out: for each of the 8 pairs, the value `0x6d59f0[first]` × 24 + `0x6d59f0[second]`; one of 0x100 or more loses 0x100 and sets the pair's bit (1, 2, 4, …) in `*EDX`. The pair is rewritten as the value's two hex digits (`0`..`9`, `A`..`F`) | `FUN_00449920` (`0x44997b`) |
| `FUN_00449ac0` | returns `DAT_007992a4`, the connect thread's handle | `FUN_00432400` (`0x43240f`, `0x432420`), `0x43dbc2` |
| `FUN_00449bc0` | returns `0x7992cc`, the game server's address as text | `FUN_00441500` (`0x441938`) |
| `FUN_00449bd0` | returns the word `DAT_00798f80`, the last 0x04 reply's request id | `FUN_00441500` (`0x44166b`, `0x44177c`) |
| `FUN_00449be0` | returns `DAT_00798fb0`, the join token | `FUN_00441500` (`0x441918`), `0x44388e` |
| `FUN_00449c10`, `FUN_00449c30`, `FUN_00449c50`, `FUN_00449c70` | return the 0x02, 0x07, 0x0A and 0x18 reply flags | see "The reply flags" |
| `FUN_00449c20`, `FUN_00449c40`, `FUN_00449c60`, `FUN_00449c80`, `FUN_00449c90`, `FUN_00449cc0` | (value; `ret 4`): set the 0x02, 0x07, 0x0A, 0x18, 0x0B and 0x0C reply flags | see "The reply flags" |
| `FUN_00449cd0` | (two buffers; `ret 8`): copies `0x799294` and `0x798f60` into them; returns `DAT_00798f58` | orphan: no call, jump or pointer (byte scan) |
| `FUN_00449d10` | (flag, two strings; `ret 0xc`): the strings into `0x799294` and `0x798f60`, `DAT_00798f58` := the flag | `0x44aee3` (the 0x0E reply, flag 1) |
| `FUN_00449d50` | returns `0x799108`, the game list record | `FUN_00441500` (`0x4415cf`) |
| `FUN_00449d60` | zeroes the 0x18c bytes at `0x799108` | `FUN_00441500` (five sites) |
| `FUN_00449d90`, `FUN_00449db0`, `FUN_00449dd0` | (value; `ret 4`): set the 0x0D, 0x0F and 0x10 reply flags | see "The reply flags" |
| `FUN_00449de0` | returns `DAT_00798f88`, the game hash | `FUN_00441500` (`0x441927`) |
| `FUN_00449df0` | (a, b, c; `ret 0xc`): `FUN_0044a2e0` (ECX a, EDX b, c), the 0x02 CHARCREATE sender | `FUN_00435e10` (`0x435edd`) |
| `FUN_00449e10` | (a; `ret 4`): `FUN_0044a380` (ECX a), the 0x07 CHARLOGON sender | `FUN_00434a00` (`0x434bb9`), `FUN_00441a90` (`0x441b46`) |
| `FUN_00449e20` | returns `DAT_00799104`, the create queue position | `FUN_004453b0` (`0x445443`) |
| `FUN_00449e30` | (value; `ret 4`): `DAT_00799104` := the value | `0x44b154` (the 0x14 reply) |
| `FUN_00449e40` | returns `DAT_00798f7c`, the create queue flag | `FUN_00441b70` (`0x441fab`), `0x443885`, `FUN_004456d0` (`0x445a57`), `FUN_00445ff0` (`0x446015`) |
| `FUN_00449e70` | (packet, length; `ret 8`): frees the old copy `DAT_007992f4` (line 0x21e); a length of 0 stops there. Else a new copy (line 0x227). Id 0x17: the words at `+1`, `+3`, `+5`, `+7` must be under 0x100 (fatal lines 0x233..0x236). Id 0x19: the same (0x242..0x245), then it steps over `+7` entries from `+9` (a dword, two strings each) without using them. Any other id is fatal (line 0x254) | `0x44b172`, `0x44b182` (the 0x17 and 0x19 replies), `0x43ac84` |
| `FUN_0044a170` | ECX string, EDX pointer to a length, EDI destination, stack limit (`ret 4`): the length grows by the string's size with its NUL; reaching the limit is fatal (line 0x26). Copies the string to EDI and returns the byte after it | the MCP senders: `FUN_0044a1d0`, `FUN_0044a2e0`, `FUN_0044a380`, `FUN_0044a400` (three), `FUN_0044a4e0` (two), `FUN_0044a590`, `FUN_0044a610`, `FUN_0044a680`, `FUN_0044a710`, `FUN_0044a870` (two), `FUN_0044a900`, `FUN_0044aae0` |
