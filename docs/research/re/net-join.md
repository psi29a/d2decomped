# Joining an open TCP/IP game (1.14d game.exe)

This is what d2d's client must do to join a game hosted by an unmodified
1.14d game.exe through "Other Multiplayer → TCP/IP Game → Host Game".
It covers the transport, the framing and compression, the join sequence
packet by packet, the character upload and save-back, and leaving.
network.md already covers the server loop, the C->S table and the S->C
table. This document fills in the parts that come before the world.

Every address below was read in game.exe. Community names (D2GS "SrvJoinGame",
etc.) appear only where game.exe itself logs them.

## Game types

The front end writes a game type into config +0x19. `FUN_0044d5f0` copies
it to `DAT_007a0610`, which the game-start dispatch at 0x44bc30 switches on.

| Type | Set at | What |
|---|---|---|
| 0 | | single player: a local server (`FUN_0052b7a0(1,1)`) and a local client, mode 1 |
| 8 | 0x435d85 (Host Game button, address in the UI table at 0x70be50) | **TCP host**. Config +0x37 gets the host's own IP (`FUN_0040df60`: gethostname → gethostbyname, first address; if that fails, getsockname after a UDP connect to 24.105.29.30:7). `DAT_007795ec` = 2, game flags +0x209 = 4 |
| 9 | 0x434927 (`FUN_00434790`, Join dialog) | **TCP join**. Config +0x37 = the IP typed in (saved as registry `Diablo II\LastTcpIp`), +0x1d = 1 |
| 6 | 0x43ff5f (`FUN_0043fe40`) | another open-game host, reached from the Battle.net front end (`DAT_0077bbd8`). Not needed here |

On types 6 and 8, the dispatch starts a listen server (`FUN_0052b7a0(0,1)`,
`FUN_0052b250(8)`, `FUN_0052b280(0)`). The host's own client then connects to
it **over TCP**, to its own IP on port 4000, exactly like a remote joiner.
Every other remote type, 9 included, only starts the client (`FUN_0052a750(0, 0x70ef20)`).

`FUN_0044db30` (6 bytes, `mov eax, [0x7a0610]`) is the getter of that game type. Callers
read it two ways: as "not 0" (the handler at 0x468960 (the Party key, apps/d2d/town/update.cpp:259) opens
UI 0x16 only for a nonzero type; `FUN_004b4380` at 0x4b44f2) and, in the action throttle
`FUN_00486d10` (0x486d10, 0x486d19), as 0 or 1 (both pass without the time check). Where a
type of 1 is set was not traced: the store at 0x44d630 (in `FUN_0044d5f0`, the copy from
config +0x19) is the only write to the word in the code.

## Transport

### Client side (0x52a000..0x52b900)

- **Init** (`FUN_0052a750(mode, host)`) sets up the following:
  - a critical section at 0x882ce8;
  - a 0x7bc-byte receive buffer at `DAT_00882b34`, with its fill count at +0x7b8.

  The mode goes to `DAT_00882d10`. Modes 1 and 2 are local (`FUN_0052b7e0`). Mode 0 is TCP: it
  starts the connect thread.
- **Connect thread** (`FUN_0052a680`) runs these calls in order:
  - `WSAStartup(0x101)` and `socket(AF_INET, SOCK_STREAM, 0)`;
  - `htons(4000)`;
  - `inet_addr(FUN_0040e0d0(host))`. `FUN_0040e0d0` accepts a dotted quad, or resolves a name through gethostbyname into a static buffer at 0x75d040;
  - a blocking `connect`. On failure the socket (`DAT_00730ae0`) is set to -1.

  **The port is fixed at 4000.** No option changes it.
- **Connect wait** (`FUN_0052b050`) waits 100 ms for the connect thread. On success it sets `DAT_00882d04` = 1 and starts the receive thread
  (`FUN_0052ab00`, priority 1). It returns 2 for success, 0 for failure and 1 while still pending.
  `FUN_0044bad0` gives up after 10000 ms (0x2711) and calls `FUN_0044e380`, the disconnect.
- **Receive thread** (`FUN_0052ab00`) calls `select` with a 100 ms timeout, then `recv` of up to 0x5b4 bytes. It has two phases:
  1. **Raw.** Bytes go straight into the buffer and `FUN_0052a8d0` splits them. The splitter
     returns 1 once it has seen an 0xAF packet with byte[1] ≠ 0. From then on the thread is in phase 2.
  2. **Framed.** Data collects in a 2496-byte stack buffer. For each complete frame:
     - `len = b0 < 0xF0 ? b0 : (b0 & 0x0F) << 8 | b1`, where len counts the header too;
     - `hdr = len < 0xF0 ? 1 : 2`;
     - `FUN_0040b260(buf + fill, 0x7b8 - fill, frame + hdr, len - hdr)` decompresses the frame. The ECX and EDX arguments are read from the disassembly at 0x52ac79;
     - `FUN_0052a8d0` splits the decompressed stream.

     A partial frame is moved to the front of the buffer (memmove at 0x52acc7).
  `recv` returning 0 or -1 clears `DAT_00882d04`. The debug strings are
  "Client thread close #2/#3/#5/#6".
- **Splitter** (`FUN_0052a8d0`) takes each packet's size from `FUN_0052b920` (the table at `DAT_00730ae8`, one u32 per id 0..0xb4). The variable-size rules:

  | Id | Size |
  |---|---|
  | 0x16 | u16 at +1 |
  | 0x26 | depends on its strings |
  | 0x3e | b[1] |
  | 0x5b | u16 at +1 |
  | 0x94 | (b[1]+2)*3 |
  | 0x9c, 0x9d | b[2] |
  | 0xa6 | u16 at +2 |
  | 0xa8, 0xaa | b[6] |
  | 0xac | b[0xc] |
  | 0xae | u16 at +1, plus 3 |
  | 0xaf | b[1]+1, or 2 when b[1] = 0 |
  | 0xb3 | b[1]+7 |

  The largest packet is 0x204 bytes. Each packet is copied into a 0x210-byte node, with its size at +0x204, a tick at +0x208 and the next pointer at +0x20c.
  Ids ≥ 0xAF go to the system queue `DAT_00882cdc`, and the rest go to the game queue
  `DAT_00882ce4`. Two ids get extra handling:
  - `AF 81` carries a new Huffman table: 128 bytes of nibbles, each nibble plus 1 giving one bit length. It is installed with `FUN_0040adb0`.
  - 0x8F (pong) gets `GetTickCount()` stamped at +0xd.
- **Queue readers**. `FUN_0052a8b0` reads the system queue and `FUN_0052a890` the game queue. Both go through `FUN_0052b820`. In mode 2 a node is held back until it is 500 ms old, which is a lag simulator.
- **Send** (`FUN_0052ae50(len, flag, buf)`). The local modes deliver through `FUN_0052b690`. TCP goes to `FUN_0052ae00`,
  a single `send`. **C->S traffic has no length header and is not compressed.** The server
  frames it by id and size.
- **Close** (`FUN_0052a7e0`) sets `DAT_00882d00`, waits 6 s for the thread, frees the queues and closes the socket.

### Server side

- **Setup** (`FUN_0052b7a0`) stores `FUN_006bf760(mode, 3, 4000, b, FUN_0052b100, FUN_0052b720, FUN_0052b750, FUN_0052b0e0)` in `DAT_00882d08`. The callbacks:
  - `FUN_0052b100` parses C->S frames;
  - `FUN_0052b720` runs when a client connects;
  - `FUN_0052b750` runs when a client disconnects;
  - `FUN_0052b0e0` is the local delivery.

  In a TCP mode `FUN_006bf760` calls
  `WSAStartup`, then `FUN_006c3b50` (or `FUN_006c3360` when `FUN_00410960` says so). `FUN_006c3b50` makes the listening socket:
  - `socket(2,1,0)`;
  - `TCP_NODELAY`, `SO_KEEPALIVE` and `SO_REUSEADDR` all on, and `FIONBIO` (non-blocking);
  - `bind(INADDR_ANY:4000)` and `listen(backlog 25)`;
  - the select thread `FUN_006c3470`, which handles 64 sockets per `select` and reads 1460-byte chunks.
- **On connect** (`FUN_0052b720`) sends `[AF 01]` with `FUN_0052b330(0, client, buf, 2)`. Mode 0
  with an 0xAF id is sent **raw**: 2 bytes, no header, no compression.
- **On disconnect** (`FUN_0052b750`) injects a 1-byte C->S packet `[70]` for that client
  through the local path. `FUN_0053f100` handles 0x70 by setting the flag at client +0x504 (`FUN_005377a0`).
- **Frame parser** (`FUN_0052b100`) sizes each packet with `FUN_0052bc20` (the table at `DAT_00730dc0`). The special cases:
  - 0xff is 16 bytes;
  - 0x14 and 0x15 are sized by their strings;
  - 0x66 is u16+3;
  - 0x6c is b[1]+7.

  An id over 0x70 other than 0xff, or a size over 0x204, is refused. The packet class sets which queue it goes to:
  - id < 0x67 is class 1 (game);
  - 0x67..0x70 is class 0 (system);
  - anything else is class 2 (the Battle.net game-server link, only with the GS interface).
- **Pump** (`FUN_0052cfe0`) routes each class:
  - class 0 → `FUN_0053f100`;
  - class 1 → `FUN_0053f3d0`, which sets client +0x3d8 = now and then calls `FUN_0054d750`;
  - class 2 → `FUN_0052cc20`.
- **Send** (`FUN_0052b330(mode, client, buf, len)`):
  - len > 0x204 is fatal;
  - mode 2 and local delivery go through `FUN_0052aeb0`;
  - otherwise it calls `FUN_0040b130` (statistics only);
  - unless mode is 2 or the packet is `[AF ..]` in mode 0, it compresses with `FUN_0040b1b0(local+2, 0x408, buf, len)`;
  - the header is 1 byte (`c+1`) if `c+1 < 0xF0`, else 2 bytes `[0xF0 | n>>8, n & 0xFF]` with `n = c+2`.
  It is sent with `FUN_006c03a0`. Mode 0 means a system packet sent now, and mode 1 a game packet.
- **Game packets are bundled.** `FUN_0053b280` appends each packet to the client's
  current node (`FUN_005392d0`) while the total stays ≤ 0x200 bytes. Otherwise it starts a new
  0x208-byte node (`FUN_00539240`). The flush (`FUN_0052e320`) sends **one compressed frame per
  node**, so one frame usually holds several S->C packets. In an open game (game +0x6a
  = 1 or 2) the flush sends at most 3 nodes per client per pass. It runs every 40 ms or more (`FUN_0052fd90`).
  Three send failures in a row drop the client (`FUN_0052caf0`).

## Client record accessors

These small GAME functions define the corresponding parts of a server client
record. The direct field accessors assert on a null client; the two flag-mask
operations at `+0xa` and the room-membership test do not.

| Function | Field and operation | Caller evidence |
|---|---|---|
| `FUN_005382b0` | byte `+0x1ac`: the client's current act | `FUN_005394a0` and `FUN_0053abe0` use it to index the game's per-act pointers at `game + 0xbc + act * 4` |
| `FUN_00538590` | u32 `+0`: the client id | `FUN_0052caf0` reads it for the disconnect path |
| `FUN_005385f0` | byte `+8`: the player class | `FUN_00532590` passes it as the class to `FUN_00555230`; `FUN_00538620` is its setter |
| `FUN_00538650` | u16 `+0xa`: set mask `DX` when its stack argument is nonzero, else clear that mask | `FUN_00580ec0` (`00580f7c`) and `FUN_0057fca0` (`0057fd3f`) set 8, the save's died bit (status `+0x24`), with EDX = 8 |
| `FUN_00538670` | return `u16 +0xa & DX` | `FUN_0052d350` tests bit 0 in its client-timeout check; other callers supply their own masks |
| `FUN_00538700` | u32 `+0x484`: store the restored unit id in `EDX` | `FUN_0056ae50` stores the unit made from the save's `0x666b` subsection, or `-1` for a null unit; `FUN_005394a0` later resolves values other than 0 and `-1` |
| `FUN_00538760` | u32 `+0x488`: store `EDX` | `FUN_0056a090` copies save header `+0x2c` here at `0056a1ca..0056a1cf`; `FUN_00538790` reads it back for the save written by `FUN_00568f20`. What those four save bytes mean is not traced |
| `FUN_005387c0` | u32 `+0x1a8`: the client's **game pointer** (corrected: it is not a unit id) | `FUN_00539a30` stores its ECX there; `FUN_00538ce0` and `FUN_00538f10` read `[+0x1a8] + 0x1c` as the pool id; `0053c54b..0053c554` passes the result as ECX (the game) to `FUN_00552f60`, the unit lookup by type and id |
| `FUN_005387f0` | test whether client `EDX` is in the room of unit `ECX` | `FUN_00620bb0` gets the unit's room1; the function scans its `+0x48` array of client pointers, count `+0x78`. Player and monster packet paths use a room-specific position only when this succeeds |
| `FUN_00538830` | return `client + 0xd`, the character-name buffer | `FUN_0056a090` compares it with save header `+0x14`; packet builders also read the name through it |
| `FUN_00538860` | set or clear client `+0x3d4` bit 2 from `EDX` | The first enable also calls `FUN_0053fc70(game at +0x1a8, client)`. No call, jump or pointer references this function; see stubs.md |
| `FUN_005388c0` | return client `+0x3d4` bit 2 as 0 or 1 | `FUN_0053fc20` tests it before calling `FUN_0053fb90` |
| `FUN_00538e00` | u32 `+0x17c`: the uploaded character-save buffer | `FUN_00534520` passes it with the size below to `FUN_00534330`; `FUN_00531eb0` compares it with the returning save |
| `FUN_00538e30` | u32 `+0x180`: the uploaded save's byte count | The same two callers use it as the buffer size; a zero size makes `FUN_00534520` return `0xe` |

### Client lookup tables

Four helpers wrap the client tables in critical sections. The id table has
256 buckets at `0x8842a8`, selected by `id & 0xff`, with the next pointer at
client `+0x4ac`; it is guarded by `0x8846a8` and enabled by `0x8846d8`.

- `FUN_00538930(id, state)` returns whether the id's record has that state.
  `FUN_0052c550` uses state 1 and `FUN_0052c8e0` state 4. A disabled table
  returns false.
- `FUN_00538b70(id, destination)` returns 0 when found and 1 when absent or
  disabled. A non-null destination receives the 16-byte name at client `+0xd`.

The separate name table has 256 buckets at `0x883ea8`, selected by the low
byte of `FUN_004112c0`'s case-insensitive name hash, with links at `+0x4b0`;
its critical section is `0x8846c0`. Comparisons are `FUN_00413590`, a
case-insensitive 16-byte `strnicmp`.

- `FUN_00538bf0(name)` returns the matching client id, or 0 when absent.
- `FUN_00538c60(name, destination)` returns 0 when found and 1 when absent.
  A non-null destination receives the record's canonical 16-byte name.

### Verified in the emulator (`tools/emu/clients_small.py`)

`clients_small.py` runs 30 GAME/Clients.cpp functions and the getter at `0x538230` from game.exe 1.14d on random client
sets (up to six records, the id and name tables built the way `FUN_00539a30` builds them) and
compares the return, the hook log, the critical-section calls and every byte of the arena
with a model written from the disassembly (56,770 calls; `--mutation-check` plants 62
defects). A fatal call is the report to `0x408a60` with the line from the table; the process
would exit. The oracle also requires the report to be called from the function under test (or a callee the
case names), so a callee's fatal with the same line is not taken for the function's own. Hooked, so not claimed: the fatal report, `FUN_00537b50`, `FUN_0053fc70`,
`FUN_00620bb0`, `FUN_00531e30`, `FUN_006657c0`, the pool entries and `GetTickCount`.

- Fatal lines (null client): act get 0x7df, act set 0x7ea (act >= 5: 0x7eb), id 0x870, class 0x8b8,
  `+4` 0x973, `+0x484` 0x97e, `+0x488` 0x994 / 0x99f, game 0x9aa, name 0x9ed, bit 2 0x9f7 / 0xa05,
  area 0x79f (set) / 0x7ae (get, see below), save buffer 0xb55, size 0xb5f, free 0xb7d, start 0xb91, ready 0xb9e.
  `FUN_00538620`, `FUN_00538630`, `FUN_00538670`, `FUN_00539240` and `FUN_005392d0` do not check.
  The getter at `0x538230` (`+0x1b4`, fatal line 0x7ae) follows `FUN_005381f0`'s `ret` with no padding in between,
  so Ghidra has no function entry there and `names.tsv`, which lists functions, has no row for it; the oracle
  runs it by address.
  `FUN_005382b0` and `FUN_005385f0` return a byte in AL (`FUN_005385f0` with `movzx`, `FUN_005382b0` without:
  the rest of EAX is whatever the caller had).
- `FUN_005381f0` calls `FUN_00537b50(area)` (ESI the client) only when the area differs from `+0x1b4`.
- `FUN_00538ce0(id, data, length, total, tag, ...)` (`ret 0x14`, the last two words unused): disabled
  tables or an unknown id give 0. With the count `+0x180` at 0 it allocates `total` bytes from the
  client's game pool (`[[+0x1a8] + 0x1c]`, `GAME\Clients.cpp` line 0xb2a) and stores the block at
  `+0x17c`, otherwise it appends at `+0x17c + count`. `count + length > total` (signed) is the fatal
  line 0xb2f. It stores `tag` at `+0x178`; when the count reaches `total` it sets `+0x3d4` bit 8, stores
  `FUN_00531e30(buffer)` at `+0x18c` and returns 1. **A length-0 chunk leaves the count at 0, so each one
  allocates again and leaks the block (bugs.md 95).**
- `FUN_00538f80` (start) sets `+0x3d4` bit 0x10, state `+0x184` = 5, `+0x188` = 0; `FUN_00538fc0` (ready) is bit
  0x10 and a nonnull `+0x17c`; `FUN_00538f10` (free) releases `+0x17c` (pool free, line 0xb80), clears
  bit 0x10, the count and both state words.
- The packet queue: `FUN_00539240` appends a node (next `+0x204` := 0) after the tail `+0x1bc`, or makes it
  head `+0x1b8` and tail when the queue is empty; `FUN_005392d0` returns the tail.
- `FUN_00539a30(game, id, type, name, name2, +0x60, +0x50c byte, 2 unused words)` (`ret 0x1c`): disabled tables,
  a null game or `game + 0x8c >= 8` (signed) give 0 with no allocation. Otherwise it takes 0x518 bytes from
  the game pool (line 0x347), zeroes them, stores the id, `GetTickCount() + 0x2bf20` (180,000 ms) at `+0x3d8`
  and the game at `+0x1a8`. It then calls `FUN_0053fe70(game)`, which is fatal (line 0x27, reported from
  `FUN_0053fe70`, not from `FUN_00539a30`) when `[game + 0x1d28]` is null, so by then the 0x518-byte block is
  allocated and the id, tick and game stores are done. If bit 8 of the game's info word (`[game + 0x1d28] + 8`) is set the record
  gets the raw type at `+0xc` and the class `FUN_006657c0(type, info byte +0xc)` at `+8`, else the type at
  `+8`. Both names go through `FUN_004135d0(dest, src, 0x7fffffff)`: the copy is unbounded, so a name longer
  than 15 bytes runs into the next field (the TCP join checks the name first, stage 1 and 2 above). A
  null `src` (the host's own client passes no second name) is `FUN_0041c340(0x57)`: the CRT error word
  `0x7790f4` and `SetLastError` get 0x57 and nothing is copied. It then clears the queue, links the record at the head of the game list (`+0x4a8` / game `+0x88`), the id bucket (`+0x4ac`),
  the name bucket (`+0x4b0`, hash of the argument string) and bumps `game + 0x8c`. The 16 slots from `+0x3dc` (8
  bytes each) are set to word 0xffff, byte 0, dword 0xffffffff.
- The lookups take the id-table section `0x8846a8` (the name lookups `0x8846c0`) and leave it before returning (a fatal exit
  does not). A name lookup compares 16 bytes with the case-insensitive `FUN_00413590`, and stops at
  the first record whose id is nonzero (bugs.md 96). The copies use `FUN_004135d0(dest, name, 0x10)`: at most
  15 bytes and a NUL, no padding.
- Not covered here: `FUN_00537b50` (the area-change fan-out, 442 bytes), `FUN_005389a0` (the ping average) and `FUN_00539da0` (1211 bytes).

## Compression (S->C only)

The compression is canonical Huffman over bytes. Only the 256 code lengths are stored.

- **Default lengths**: 256 bytes of static `.data` at **VA 0x007076c0**. They are all in 1..11, and the Kraft sum is exactly 1. The masks
  are at 0x007077c0, the codes at 0x0075b6a0, and the decode table at 0x0075afa0 (256 pointers,
  with sub-tables at 0x0075b3a0).
- **Init** (`FUN_0040b0c0`, called at startup from `FUN_00405c30`) zeroes the statistics (0x75b7a0,
  0x75bfa0), then runs `ECX = 0x7076c0; JMP FUN_0040adb0`.
- **Build** (`FUN_0040adb0(lengths)`). It refuses a table with a 0 or a length ≥ 16. It copies the lengths into 0x7076c0 and then builds the codes:
  1. Order the symbols by length **descending**. Ties go by symbol value ascending.
  2. The first symbol gets code 0.
  3. Each following symbol gets `code = (prev_code + 1) >> (prev_len - len)`.
- **Compress** (`FUN_0040b1b0(dst=ECX, cap=EDX, src, n)`) writes codes MSB-first and zero-pads the last byte. It returns the byte count, or 0 if `cap` runs out.
- **Decompress** (`FUN_0040b260(dst=ECX, cap=EDX, src, n)`) works through a 32-bit window. It stops when the next code
  would need more bits than were loaded. The padding can never decode as a symbol, because the
  all-zero prefixes belong to the longest (11-bit) codes.
- **Custom table**: the server may send `AF 81 <128 bytes>` at any time (client `FUN_0052a8d0`).
  Unmodified game.exe hosts never send it; no sender was found in the server code.

**Tested:** a Python model of the build, compress and decompress steps, run on the lengths
from the user's game.exe, matches `FUN_0040b1b0` and `FUN_0040b260` byte for byte under
tools/emu. The test covered 300 random buffers of 1..400 bytes.

**Extracting the table.** Don't ship it. Read the 256 bytes at VA 0x7076c0 from the user's own game.exe, for example:

```python
import struct
d = open(path_to_game_exe, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, optsz = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 52)[0]
for i in range(nsec):
    o = pe + 24 + optsz + 40 * i
    vsz, va, rsz, raw = struct.unpack_from('<IIII', d, o + 8)
    if base + va <= 0x7076c0 < base + va + rsz:
        lengths = d[raw + 0x7076c0 - base - va:][:256]
```

d2d already locates game.exe for the MPQs. It can pull these bytes at startup, check that
the Kraft sum is 1, and refuse to start networking otherwise.

## The join, packet by packet

The joiner is game type 9, and the host runs types 8 + 0. "C" is the client, "S" the host's
server. `[..]` is one packet; sizes include the id byte. All fields are little-endian.

### 0. The probe (Join dialog)

`FUN_00434790` connects (`FUN_0052a750(0, ip)`, then `FUN_0052b050`), and
`FUN_00431c90` waits for a system packet:
- 0xAF means success (`DAT_00779990` = 1);
- 0xB0 means failure.

Then it **closes the connection** (`FUN_0052a7e0`). So the host sees a connection that
receives `AF 01` and hangs up. Only after character select does the real connection
start. d2d doesn't need the probe.

### 1. Connect

| Dir | Packet | Where |
|---|---|---|
| C | TCP connect to host:4000 | `FUN_0052a680` |
| S | `[AF 01]`, 2 bytes, **raw** (no header, not compressed) | `FUN_0052b720` |

**Seen on the wire (2026-09-30):** a real 1.14d host, running an open TCP/IP game on the
LAN, accepted a connection on port 4000 and sent exactly the 2 bytes `af 01`, unframed. That
matches `FUN_0052b720`. Nothing past this point has been checked against a live host yet.

Everything after this is compressed and framed, in both phases of the client.
The client's connect loop (`FUN_0044bad0`) pops system packets through `FUN_0045c850`:
0xAF sets `DAT_007a0618` = 1, and 0xB0 clears it.

### 2. Join request (C->S 0x68, 37 bytes)

`FUN_0044f360` sends it when the game type is 3, 7 or 9. The builder is `FUN_00477f70`, and the layout comes from the disassembly at 0x477f76.

| Off | Size | Field |
|---|---|---|
| 0 | 1 | 0x68 |
| 1 | 4 | `DAT_007a05c0` (config +0x229); not checked in TCP |
| 5 | 2 | game token: low half of `DAT_007a0520` (config +0x1d = **1** for TCP join) |
| 7 | 1 | character class (`FUN_0047aa20`, or byte 2 of `DAT_007a0520`) |
| 8 | 4 | version: vtable slot 0x5c of the front-end interface (`0x72faa0`, from `FUN_00518c00`, in config +0x225) = `FUN_0051ca10` = **0x0e** |
| 0xc | 4 | `0x2185edd6`, or `0xed5dcc50` when `FUN_00408f20` ≠ 0; not checked in TCP |
| 0x10 | 4 | `0x91a519b6`; not checked in TCP |
| 0x14 | 1 | language (`FUN_00525150`, strtable index 0..13) |
| 0x15 | 16 | character name, NUL-terminated |

The host (`FUN_0053f100`, case 0x68; log "[JOIN 1] %d clientid - Begin") checks the packet in three stages.

**Stage 1**, with no GS interface (`DAT_00883d50` = 0): `FUN_0053eff0(token, version)`. Each failure has a reason code:
- token ≠ 1, or game 1 doesn't exist → 6
- version ≠ **0x0e** → 0x10
- 8 players already (game +0x8c > 7) → 0xf
- fewer than 2 or more than 15 characters, or no NUL in the 16-byte field → error
- the name is already in use in the game → 6

A failure is sent as `[B4, u32 reason]` (5 bytes, `FUN_0053b260`), and then the client is dropped (`FUN_0052b4f0`).

**Stage 2**: `FUN_0052c690` checks that the name is terminated and the class is < 7. At `0052c726` it sets EDI to 0x10 for `FUN_0052c5b0`, which checks the name in EDX. The jump table at `0052c600` / `0052c610` accepts ASCII letters and apostrophe (`'`), hyphen (`-`) and underscore (`_`); spaces, digits and non-ASCII bytes are rejected. At `0052c5d9`..`0052c5e6`, a mark is rejected if another mark was seen, if it is first, or if the next byte is NUL.

In isolation `FUN_0052c5b0` accepts 1..15 bytes: its NUL case at `0052c5f6` accepts whenever at least one character was seen. The `tools/emu` oracle confirms that it accepts `X`. A TCP join still requires 2..15 because stage 1's `FUN_0053eff0` rejects a NUL in either of the first two bytes before stage 2 runs. d2d's `valid_join_name` follows that combined TCP rule.

**Stage 3**: `FUN_0052fa50` (SrvJoinGame). `FUN_00539a30` adds the client. That fails if the game already has 8 players, and the failure sends 1 byte and drops the client. It makes a 0x518-byte client record, sets its timeout to now + 180000, links it into game +0x88, and hashes it by client id into `DAT_008842a8` and by name into `DAT_00883ea8`.
There is **no game password and no level limit check in the TCP path.** The 0x67 create
packet has fields that look like them (+0x2b, +0x2c), but no check was found.

### 3. Accepted

`FUN_0052c260` then `FUN_0052fa50` queue the following (game queue, compressed, one frame):

| Dir | Packet | Layout |
|---|---|---|
| S | `[01]` 8 bytes, game flags | +1 difficulty (game +0x6d), +2 u32 game flags (`FUN_0053fd40`: 0x800 hardcore, 0x100000 expansion, 0x200000 ladder), +6 expansion (game +0x70 ≠ 0), +7 ladder (game +0x74 ≠ 0) (`FUN_0053b340`, id in DL = 1) |
| S | `[00]` 1 byte, "loading" | `FUN_0053b320`, DL = 0 |
| S | `[02]` 1 byte | 0x52fc08, sent only with no GS interface (TCP) |

The client record's state (+4) is 1 after `FUN_005386d0(client, 1)`.

The client handles these as follows:
- S->C table `DAT_007114d0`, 0x01 → `0x45c8b0`: stores b[1] (difficulty), the u32 at +2, b[6] and b[7] (`FUN_0044dc70`/`dc80`/`dc90`, `FUN_0047a9a0`).
- `FUN_0044dca0` (24 bytes, no arguments, `ret`) reads that ladder byte back, and only when the game type at `0x7a0610` is 2 or 3. It subtracts 2 and, if the unsigned result is at most 1, returns the dword at `0x7a04f8`. Every other type returns 1. `FUN_0044dc90` is `mov dword [0x7a04f8], ecx` then `ret`. `FUN_0045c8b0` calls it at `0045c8cb` with ECX equal to the zero-extended byte at packet +7. This function has no assert. The source-file rows on either side are `GAME/Game.cpp` (`FUN_0044c890`, `FUN_0044e200`; `.\GAME\Game.cpp` is at `0x6d5d1c`). The dispatch at `0x44bc30` does not give types 2 and 3 a branch of their own. They fall through with the other client types. `tools/emu` on this binary: types 0, 1, 4, 6, 8 and 9 return 1 with the flag 0 or 1. Types 2 and 3 return the flag. The decompiler prints stack arguments here. The asm does not read them. At `004ec0e9` (`FUN_004ec090`) and `004a73e1` (`FUN_004a7354`) those pushes stay up, EAX is pushed next, then EDI, and `FUN_006538a0` takes all six (`ret 0x18`). The other calls are `004a7551`, `004a7583` and `004a75b8` in `FUN_004a74a0`, `004ecd5e` in `FUN_004eccf0`, `004ecfe4` in `FUN_004ecf50`, and `004ed1bf` in `FUN_004ed150`. `FUN_004ec090` sits between the `UI/SkillDesc.cpp` rows `FUN_004eb940` and `FUN_004ed570` (`.\UI\SkillDesc.cpp` at `0x6dbf24`).
- 0x02 → `0x45c910`: `JMP 0x477da0`, which **sends C->S `[6B]`** (1 byte).

### 4. Character upload (C->S 0x6c)

The client sends its save **right after 0x68, without waiting for a reply**. The call path is
`FUN_0044f360` → `FUN_0044e200` → `FUN_004781d0` (for game types 6, 7, 8 and 9).

| Off | Size | Field |
|---|---|---|
| 0 | 1 | 0x6c |
| 1 | 1 | chunk length n (0xff for every chunk but the last) |
| 2 | 4 | total save size |
| 6 | n | data |
| 6+n | 1 | one unused byte: the packet is n+7 bytes (0x106 for a full chunk) |

On the host (`FUN_0053f100`, case 0x6c):
- the total must be < 0x2000;
- the data goes to `FUN_0052db10` → `FUN_00538ce0(client id, data, n, total, 0)`.

`FUN_00538ce0` appends to client +0x17c. **If a chunk would run past the total, it's a fatal error in game.exe, and the whole host process dies.**
When the buffer reaches the total, it sets client flag +0x3d4 |= 8 and computes a checksum (`FUN_00531e30`).

### 5. Join act (C->S 0x6b)

On the host, `FUN_00530190` (SrvJoinAct) creates the player from the upload through `FUN_00539760`. That calls
`FUN_005345a0`, then `FUN_00534520`, then `FUN_00534330`. The checks:

- `FUN_00534330`: the save is at least 8 bytes, starts with magic 0xaa55aa55, and has a version of 0x5c or more (`FUN_0056b180`). Older versions go to `FUN_00534020`.
- `FUN_0056a090` checks the header:
  - size ≥ 0x14f;
  - the checksum at +0xc matches `FUN_00411130`;
  - the size at +8 equals the upload;
  - the version is 0x5c..0x60;
  - **the name at +0x14 equals the name in 0x68** (stricmp);
  - byte +0x28 ≤ 7; `FUN_00538620` then stores that class byte at client +0x8
    (`MOV [ECX+8],DL`; the save byte is loaded at `0056a16f` and passed in
    EDX at `0056a17a`).
  - client +0xa starts at 0; `FUN_00538630` copies the low word from save
    +0x24 into client +0xa. If bit 0 is set, game.exe clears it in both the
    save field and client +0xa, then returns 2 (`0056a187`..`0056a1a8`).
  Parse errors are mapped to a client reason through `DAT_006e1208`.
- `FUN_00539760` compares the character with the game:
  - a classic character in an expansion game → 0x17;
  - an expansion character in a classic game, or class > 4 in a classic game → 0x18;
  - a softcore character in a hardcore game → 0x13;
  - a dead hardcore character → 0x15;
  - a hardcore character in a softcore game → 0x14.

Any failure sends `[B4, u32 reason]` and drops the client.

No difficulty-access check turned up. `FUN_0056a090` reads bit 0x80 of save byte
0xa8+difficulty, but doesn't refuse on it. See the open questions.

On success, the host queues:

| Dir | Packet | Layout |
|---|---|---|
| S | `[03]` 12 bytes, load act | +1 act, +2 u32 game +0x7c (map seed), +6 u16 area (`FUN_0061ae80`), +8 u32 game +0x80 (`FUN_0053abe0` → `FUN_0053b390`, DL = 3) |
| S | `[53]` 10 bytes | three values from the act (`FUN_0061c330`), sent by `FUN_0053c900` from 0x53ac4f |

The state goes to 2 (`FUN_0052c210`). Then `FUN_005394a0` places the player in the act, and the state goes to 3
(0x53023c). Log: "[JOIN 5] %d clientid, SrvJoinAct: Added to game."

### 6. Start act

On a later server frame, `FUN_0052d440` handles each client in state 3 whose act is ready (`FUN_0061a460`). Log: "[JOIN 6] ... SCMD_STARTACT":

| Dir | Packet | |
|---|---|---|
| S | `[04]` 1 byte, "load complete" | DL = 4 at 0x52d510. Then the state goes to 4, and `FUN_0055df00`, `FUN_0052c410` and `FUN_0055b620` run |
| S | `[5A 02 04 ...]` event message, player joined, to everyone | built at 0x52d440 (id 0x5a, type 2, color 4, then the names) |

From state 4 on, the flush also runs `FUN_0052d980` and `FUN_0052da00`, which send the world updates. From here the stream is
the normal S->C game traffic: assign player 0x59, units, items and so on (network.md, and the S->C
packet research).

### Summary

```
C: connect :4000
S: AF 01                 (raw)
C: 68 (37)               (raw, unframed from here on for all C->S)
C: 6C (262) x k, 6C (n+7)
S: [01 (8)][00][02]      (one compressed frame)
C: 6B
S: [03 (12)][53 (10)]...
S: [04] [5A ...] ...     (world)
C: 6D (13) every 5 s
```

## Live check (2026-09-30, a real 1.14d host on the LAN)

A stale-version join against an open TCP/IP game hosted by an unmodified
1.14d game.exe (the host in the Rogue Encampment):

| Step | Bytes |
|---|---|
| connect | `af 01` raw |
| C `[68]`, version 0x0d, token 1, class 0, name "Probe" | 37 bytes |
| S frame 1 | `04 05 46 9c` → decompressed `b4 10 00 00 00` |
| S frame 2 | `06 7a 09 a5 f5 c0` → decompressed `01 00 04 00 10 00 01 00` `00` `02` |

Confirmed: the 1-byte frame header (length including itself), the Huffman
table read from the user's game.exe, and B4 reason 0x10 for a bad version.
Not expected: after the B4 the host still flushed `[01] [00] [02]` before
closing (game flags 0x00100004: expansion 0x100000 plus bit 0x4, unexplained;
difficulty 0, expansion 1, ladder 0). So the refusal doesn't stop the game
queue's accept packets; a client must act on B4 and ignore what follows.
No character entered the host's game.

### Full join (same day, same host)

`tools/emu/join_live.py <save.d2s> [host]` joined with a real level-99 save
(`Mule_abcd.d2s`, 2516 bytes), stayed 15 s, left with `[69]`, and saved the
returned B3 chunks.

- In the world (`[04]`) 0.28 s after connect; socket closed by the host at 15.3 s.
- 295 packets, all split by game.exe's own size table (VA 0x730ae8) plus the
  variable rules: no desync, no unknown id.
- Order: `01 00 02`, C answers `6b`, then `59` (assign player) **before**
  `03`/`04`, then `aa 76 94 22 27 23 5e 28 29 0b 5f`, stats `1d/1e/1f`, items `9c/9d`.
- Histogram (id x count): 00x1 01x1 02x1 03x1 04x1 05x1 06x1 07x10 0bx1 0dx1
  15x2 1cx1 1dx19 1ex6 1fx9 20x5 22x2 23x4 27x1 28x1 29x1 48x3 51x24 53x1
  59x2 5ax1 5bx2 5ex1 5fx1 65x2 67x12 6dx19 75x1 76x2 7bx5 7ex1 7fx1 81x1
  8bx1 8cx2 8dx2 8fx3 94x1 95x1 96x1 9cx40 9dx26 9ex18 9fx2 a0x10 a8x4 aax13
  acx11 b0x1 b3x10.
- Leave: 10 B3 chunks (2516/2516 bytes), `B0`, then `05 06`, then close, as
  `FUN_005303d0` predicts.
- Save-back differs from the sent save in 13 bytes, all header:
  | Offset | Sent | Returned | Meaning |
  |---|---|---|---|
  | 0x0c | `8ee003a5` | `4e5607a5` | checksum |
  | 0x30 | `bba6e660` | `b642bd6a` | save timestamp |
  | 0xa8..0xaa | `00 00 84` | `80 00 00` | difficulty bytes: Hell act 5 → Normal act 1 (the host's game) |
  | 0xab | `7543c724` | `f221f26c` | map id → the host game's seed 0x6cf221f2 |

  Items, stats, skills, quests and waypoints came back byte-identical: the
  host rewrites only where the character last was.

## Keep-alive and timeouts

- **Ping** (C->S 0x6d, 13 bytes; `FUN_00477dd0`). The main loop (`FUN_0044efa0`) calls it, and it sends at most once every 5000 ms. Layout:
  - +1 u32 `GetTickCount()`;
  - +5 u32 `FUN_0044ce70() >> 1`;
  - +9 u32, a value read from a module's memory multiplied by 0xb640de41, i.e. an anti-cheat probe (the code multiplies by -0x49bf21bf). A zero there is fine for TCP.
- **Pong** (S->C 0x8f, 33 bytes: the id, then 32 zero bytes; `FUN_0053e020`). The host answers every ping
  (`FUN_0052c400` → `FUN_005389a0`), keeps a latency average, and stamps client +0x3d8.
- **The host has no idle timeout in TCP.** `FUN_0052d350` would drop a client after 45 s without packets, or 10 s in some states. But its condition requires `DAT_00883d50` ≠ 0, i.e. a realm. A TCP host drops a client only when 3 sends in a row fail (`FUN_0052e320`, `FUN_0052e110`), or when the client leaves.
- **Client timeouts** (`FUN_0044eec0`, only for remote types):
  - disconnect if the socket closes (`FUN_0052a640` = 0);
  - disconnect after 300000 ms from start when `DAT_007a0624` is set;
  - disconnect after 30000 ms from start (`DAT_007a05f8`, written only in `FUN_0044e200`) while `DAT_007a061c`, `DAT_007a0620` and `DAT_007a0624` are all 0 (S→C 0x04 sets `DAT_007a061c`, LAB_0045c9a3, so the game coming up ends this);
  - for types 7 and 9, disconnect as soon as `DAT_007a0618` = 0 (a `B0` or a failed connect).
- A game with nobody in it closes after 300000 ms (`FUN_0052fd90`).

## Leaving

- **Client**: sends C->S `[69]` (1 byte, `FUN_00477ee0`), from several callers (the menu, death, etc.).
- **Host** (`FUN_005303d0`):
  1. `FUN_0052ca10`: in an open game, every player is saved.
  2. Queue `[05]` (DL = 5).
  3. Send all remaining B3 save chunks (below).
  4. Queue `[06]` (DL = 6).
  5. Send `[B0]` right away (`FUN_0053b220`, mode 0: compressed, its own frame).
  6. Flush the game queue.
  7. `FUN_00539da0` removes the client and saves it again.
  8. `Sleep(100)` in open games.
  9. `FUN_0052b570` closes the socket.

  On the wire the order is: B3 chunks, then B0, then a frame with [05][06].
- **Socket loss**: the lib's disconnect callback injects `[70]`. The client record stays until
  sends fail 3 times. `FUN_0052caf0` then calls `FUN_00539da0`, which saves and removes it.

### The client's leave flags

When the client leaves a game it runs one fixed sequence: send C->S 0x69
(`FUN_00477ee0`), clear the quit flag (`FUN_0044b880`, ECX 0), set the "we left"
flag (`FUN_0044c860`, ECX 1), set the leave flag (`FUN_0044d520`). Read at
`FUN_004b9210` subtype 0x17 (0x4b924b..0x4b9261), `FUN_004b4380` (0x4b4522..0x4b4539)
and `FUN_004b9180` (0x4b91be..0x4b91d4). `FUN_0044dd60` does the same
four steps inline, in another order (quit flag, we-left flag, 0x69, leave flag).

| Address | What | Evidence |
|---|---|---|
| `DAT_0070ee8c` (`FUN_0044b880`, `mov [0x70ee8c], ecx`) | The quit-program flag. `FUN_0044b8a0` (the game's run function) sets it to 1 at 0x44b946 before its state loop. After state 1 (shutdown: `FUN_004fa550` .. `FUN_0044bb80`) it tests it at 0x44b98a: nonzero returns 0 (and `DAT_007a0440` = 0); zero returns 3 for config +0x19 of 2, 3, 6 or 7, else 4, so the front end comes back up. Every caller read here passes 0 (0x4b91c3, 0x4b4527, 0x4b9250, 0x44dd60, 0x44e3f3): leaving a game goes back to the menus, it doesn't quit. | `python3 tools/ghidra/fn.py 0044b880`, `fn.py 0044b8a0`, `uv run --project tools/emu python tools/emu/disasm.py 4b9180` |
| `DAT_007a0674` (`FUN_0044c860`, `mov [0x7a0674], ecx`) | The "we left" flag: the client's own leave (C->S 0x69) has gone out. Set to 1 by the leave sequence (ECX 1 at 0x4b91cf, 0x4b4533, 0x4b9257), by `FUN_0044dd60` (0x44dd67) and by the message handler at 0x44e0b0 right after its `FUN_00477ee0` (0x44e0eb); set to 0 with `DAT_007a061c` at 0x44e366. Its getter (0x44c870, not a Ghidra function) has one caller, the code at 0x45ca60 (follows the S->C handler at 0x45ca30): in game types 7 and 9 with the flag clear it calls the disconnect `FUN_0044e380` with reason 8, i.e. a host-side end of the game that we didn't ask for is a disconnect with reason 8, and one we asked for isn't. | `python3 tools/ghidra/fn.py 0044c860`, `fn.py 0044e380`, `uv run --project tools/emu python tools/emu/disasm.py 45ca60 25` |

### D2Win shutdown (`FUN_004fa550`)

The tail of both front-end runs and of the game's run function (callers
`FUN_004359d0` at its end, `FUN_00442150`, `FUN_0044b8a0` after state 1).
It takes no arguments and always returns 1. Its calls, in order (0x4fa550..0x4fa581):

| Address | What | Evidence |
|---|---|---|
| `FUN_004f9290` | Frees the leading nodes of the list at `DAT_007d55c0`. Does nothing, returns 1, when `DAT_007d55c4` is above 0 (signed, `jg`) or the head is null; `FUN_004fa550` zeroes `DAT_007d55c4` first. A node has its flags at +8 and its next link at +0x3c (the link `FUN_004f98e0` follows from `DAT_007d55bc`). Pass 1: while the head's flags lack bit 1 (mask dword 0x6ce268, value 1), save the next link, free the head with the debug free `FUN_0040b3c0(node, ".\Src\D2WinMain.cpp", 0x25a, 0)`, make the next link the head; a null head ends it. Pass 2 from the node that stopped pass 1: the same with bit 2 (mask dword 0x6ce26c, value 2) and line 0x263. It stops at the first node holding bit 2. Always returns 1. Also called at the end of `FUN_004f98e0` (the per-frame draw) when `DAT_007d55d8` is 0. | `python3 tools/ghidra/fn.py 004f9290`, `uv run --project tools/emu python tools/emu/disasm.py 4f9290` |
| `FUN_00507140` | Closes the front end's pentspin sprite: `FUN_004faae0` (CloseFile, mpq-archives.md) on the dword `DAT_0087e96c` (menu.md says `FUN_005070d0` loads it), then stores 0 there. No arguments, no return value. | `python3 tools/ghidra/fn.py 00507140`, `uv run --project tools/emu python tools/emu/disasm.py 507140` |
| `FUN_005015f0` | Releases the font table (`Src/D2WinFont.cpp` in `game-srcfiles.tsv`). Entries are 0x14 bytes, 14 of them: dword +0 at 0x841da8 + 0x14 * n, dword +4 at 0x841dac + 0x14 * n, until the pointer reaches 0x841ec4. Per entry: a non-zero +0 is closed with `FUN_004faae0` (CloseFile, mpq-archives.md) and zeroed; a non-zero +4 is freed with the debug free `FUN_0040b3c0(ptr, ".\Src\D2WinFont.cpp", 0x135, 0)` and zeroed. Then one more `FUN_004faae0` on the dword `DAT_00841da4`, set to 0. The other three dwords of an entry are not touched. Also called by `FUN_00456d80`. What loads the entries is not traced. | `python3 tools/ghidra/fn.py 005015f0`, `uv run --project tools/emu python tools/emu/disasm.py 5015f0` |
| `FUN_004f59a0` | A getter: `mov eax, [0x7c8cbc]`, no arguments. `FUN_004fa550` pushes 0x21 before the call and never pops it: the 0x21 is the count argument of `FUN_00506e00` (`ret 4`), and the getter's result goes to that function's ECX. It is the window handle: `FUN_004f90a0` passes the getter's result to `GetWindowRect` as the HWND. | `python3 tools/ghidra/fn.py 004f59a0`, `uv run --project tools/emu python tools/emu/disasm.py 4fa550` |
| `FUN_00506e00` | Removes a table of message handlers. ECX = the owner (a null ECX raises internal-error line 0x5a), EDX = the table, one stack dword = the count (`ret 4`); `FUN_004fa550` passes `DAT_007c8cbc`, 0x72dc48 and 0x21. The table is 33 entries of 12 bytes (kind, code, handler address). Per entry it calls `FUN_00506cf0(owner, kind, code, handler)`, which for a null owner raises line 0x30 and by kind calls `FUN_00420ab0` with a tag: kind 0 tag 0x534d5347, kind 1 0x534d5348, kind 3 0x534d5349, kind 2 0x534d534a (any other kind does nothing). `FUN_00420ab0` ends in `FUN_00426720(tag, object, code, handler)`, which looks the (tag, object) pair up in a hash (`DAT_00779300`) and unlinks the entry with that code, and that handler when non-zero, from its list (a critical section around it). Kind 0 with code 0x111, 0x101 or 0x100 raises lines 0x34, 0x35, 0x36 instead; the table has none. Entries of 0x72dc48 in order: kind 0 codes 0x200, 0xa0, 0x2a3, 0x201, 0x202, 0x204, 0x205, 0x102; kind 3 codes 0x8, 0x25, 0x27, 0x24, 0x23, 0x2e, 0x26, 0x28, 0x21, 0x22, 0xd, 0x52, 0x4e, 0x48, 0x1b, 0x9, 0x20, 0x11, 0x70, 0x4d; kind 0 codes 0x1c, 0x112; kind 2 codes 0x2c, 0x11; kind 0 code 0x20a. The mirror is `FUN_00506da0` (same arguments; null owner line 0x4d) calling `FUN_00506c40` (null owner line 0x13, lines 0x17..0x19 for the three window messages) per entry, which goes to `FUN_00420bf0`/`00420c10`/`00420c30`/`00420c50`; `FUN_004f8fe0` calls it with `FUN_004f59a0`, 0x72dc48 and 0x21 (0x4f8ff9..0x4f9011). That the 420bf0 family adds to the hash `FUN_00426720` removes from is not read. | `python3 tools/ghidra/fn.py 00506e00 00506cf0 00426720`, `uv run --project tools/emu python tools/emu/disasm.py 506e00`, table: `uv run --project tools/emu python -c "import sys; sys.path.insert(0,'tools/emu'); import emu; e=emu.Emu(); print([hex(e.r32(0x72dc48+4*i)) for i in range(99)])"` |

`FUN_004fa550` itself (0x4fa550..0x4fa581): store 0 in `DAT_007d55c4`; `FUN_004f9290`; `FUN_00507140`; `FUN_005015f0`; push 0x21; `FUN_004f59a0` (result to ECX); `mov edx, 0x72dc48`; `FUN_00506e00`; return 1. So it frees the D2Win list's leading nodes, closes the pentspin and the font table, then removes the 33 handlers. It reads no argument, and `FUN_00442150` and `FUN_004359d0` return other globals after it (`DAT_0077bda0`, `DAT_007795e8`). d2d: not ported (`git grep -n 004fa550 components apps tests` finds nothing); d2d has no handler registry to unwind.

### Who calls the shutdown and the window-handle getter

| Address | What | Evidence |
|---|---|---|
| `FUN_00442150` | ECX = a record pointer, kept in `DAT_0077bbd8`. Calls `FUN_004f8fe0` (the D2Win setup: font 1, pentspin load `FUN_005070d0`, the handler table `FUN_00506da0` on 0x72dc48 x 0x21), `FUN_004f90a0(1)` (resizes the window; failure prints "Failed to resize window... this is fatal!" and exits -1), `FUN_00441b70` (the screen itself: it clears `DAT_0077bda0`, reads the screen object at record +0x225 into `DAT_0077b09c`, null raises line 0xce7, and sets `DAT_0077bda0` to 4 when that object's method +0x68 returns 0), then `FUN_004fa550`. Returns `DAT_0077bda0`. Its only reference is a data word at 0x70cd14. The same shape as `FUN_004359d0`, which keeps ECX in `DAT_007795d4`, ends in `FUN_004fa550` and returns `DAT_007795e8`. | `python3 tools/ghidra/fn.py 00442150 004f8fe0 004f90a0`, `python3 tools/ghidra/calls.py 00442150` |
| `FUN_00451880` | ECX = a 0x10-byte window-message record: +0 a frame stamp, +4 the message, +8 wParam, +0xc lParam. Raises internal-error line 0x12a unless +0 equals the frame clock `FUN_0044da90` (`DAT_007a0498`). Then `FUN_00420b00(hwnd, message, wParam, lParam, &0, &0)` with the window handle from `FUN_004f59a0`: that dispatches through the handler hash. It looks up tag 0x534d5347 with (hwnd, message), and then, by message, tag 0x534d5348 with the low word of wParam for 0x111 (WM_COMMAND), 0x534d5349 with wParam for 0x100 (key down), 0x534d534a with wParam for 0x101 (key up); if the window is not `DAT_007792f4` it repeats once with window 0; message 0x82 also calls `FUN_00420a10`. So the kinds of the 0x72dc48 table (above) are: 0 any message, 1 a command id, 3 a key down, 2 a key up. The only caller, `FUN_004519c0`, walks a queue (`PTR_DAT_0070f7a0`: chunks of 500 records of 0x10 bytes, index at chunk +0x1f44, end index at +8000, next chunk at +0x1f48; line 0x189 if the index passes 499) and plays every record whose stamp is due, who fills the queue is not traced. | `python3 tools/ghidra/fn.py 00451880 00420b00 004519c0`, `uv run --project tools/emu python tools/emu/disasm.py 451880` |
| `FUN_00451f10` | No arguments, 12 bytes: `FUN_004f59a0`, then `FUN_00420360(hwnd)`, which calls `FUN_00426370(tag, hwnd)` for tags 0x534d5348, 0x534d5349, 0x534d534a, 0x534d5347 and returns 1. `FUN_00426370` finds the (tag, window) node in the handler hash and, under the critical section, pushes a new level on it: a 0x10-byte record allocated at `.\SOURCE\SEVT.CPP` line 0x11c, whose +0xc holds the node's previous list (`node[3]`), becomes `node[3]`; it sets `DAT_007792f8`. So handlers added from now on sit above the existing ones. Its only caller is `FUN_0044f360` (the game's run function), at line 46 of the C: after `GetVersionEx`, `FUN_00475df0` and `FUN_00483310`, before `FUN_004680b0`. | `python3 tools/ghidra/fn.py 00451f10 00420360 00426370`, `python3 tools/ghidra/calls.py 00451f10` |
| `FUN_00451f20` | The pair of `FUN_00451f10`: `FUN_004f59a0`, then `FUN_00420320(hwnd)`, which calls `FUN_004269c0(tag, hwnd)` for the same four tags. `FUN_004269c0` finds the node and reads the top level's +0xc (the level below): when it is 0 it calls `FUN_00426810(tag, window)`; otherwise it calls `FUN_00426210` and makes that lower level `node[3]` again. It sets `DAT_007792f8`. Its caller is the call at 0x44d569, in the unlisted function at 0x44d550 (`FUN_00475ef0`, `FUN_00468170`, `FUN_00451850`, `FUN_004794b0`, `FUN_00478a50`, `FUN_00451f20`, return 1), which is not in `game-functions.tsv`. What `FUN_00426210` and `FUN_00426810` do is not read. | `python3 tools/ghidra/fn.py 00451f20 00420320 004269c0`, `uv run --project tools/emu python tools/emu/disasm.py 44d550` |
| `FUN_004359d0` | The front-end screen run (d2d: `apps/d2d/frontend.hpp:23` calls it the character-select init; only its tail is read here). ECX = the record, kept in `DAT_007795d4`. Same frame as `FUN_00442150`: `FUN_004f8fe0` first, `FUN_004f90a0(1)` on each path, then the screen. The tail (0x4359d0 C lines after `LAB_00435c4a`): `FUN_004f9180(&LAB_004309b0)`, `FUN_004fa590(&LAB_00430bb0)`, `FUN_00515020`, a `Sleep(0)` loop while `FUN_00514fb0` is non-zero, `FUN_00515070`, `FUN_0042f480`, `FUN_0042ef50`, `FUN_00524e40`; it zeroes the six dwords at record +0xa5, +0xa9, +0xad, +0xb1, +0xb5, +0xb9; `FUN_004fa550`; returns `DAT_007795e8`. Its only reference is a data word at 0x70896c. d2d: it ports the screen's layout, not this shutdown (`frontend.hpp:23`, `scene.hpp:60` cite it for the init and the background draw). | `python3 tools/ghidra/fn.py 004359d0`, `python3 tools/ghidra/calls.py 004359d0` |
| call at 0x460557 (`FUN_004f59a0`; inside a function whose start is not in `game-functions.tsv`, between `FUN_00460480` and `FUN_00460730`) | The getter's result goes to ECX of `FUN_00451db0` with EDX 0x711d40 and a pushed count of 4 (0x460555..0x460563). `FUN_00451db0` is the register side of the same shape as `FUN_00506da0` (null ECX line 0x153; per 12-byte entry `FUN_00451c70(code, handler)`); `FUN_00451e10` (null line 0x166) takes the same arguments to undo it, and the code runs `FUN_00451bb0(0x460490)` between the two (0x460568..0x460580). The table at 0x711d40: kind 0 code 0x201 handler 0x45fe30; kind 0 code 0x202 handler 0x45fed0; kind 3 code 0x1b handler 0x45ffb0; kind 3 code 0xd handler 0x45ff70, i.e. with the tags above a mouse-down, a mouse-up, and key-down 0x1b and 0xd for the span of that call. That `FUN_00451e10` undoes `FUN_00451db0` is by its shape; its body past the first entry is not read. | `uv run --project tools/emu python tools/emu/disasm.py 460540`, `python3 tools/ghidra/fn.py 00451db0 00451e10` |

## Save-back (S->C 0xb3)

In an open game the **host** writes the joiner's save and sends it back. The joiner stores it
on its own disk.

- **Serialize** (`FUN_00532400`). For game +0x6a = 1 or 2 it calls `FUN_00532340`:
  1. `FUN_00569ad0` writes the .d2s into an 8 KB buffer.
  2. `FUN_00538ce0` stores it in client +0x17c.
  3. `FUN_00538f80` sets flag +0x3d4 |= 0x10.

  Single player (type 3) writes the file straight away instead (`FUN_00532240`). The callers are:
  - leaving (`FUN_00539da0` → `FUN_0052c900`, the player and everyone else);
  - `FUN_0052ca10`;
  - `FUN_0052e2a0` (every player; drains the chunks at once);
  - `FUN_0052e9c0`, `FUN_00534630`, `FUN_0057fca0`.

  Which game events call these was not traced.
- **Send** (`FUN_0052e110`, from each flush when `FUN_00538fc0` finds flag 0x10 set and a buffer):
  one chunk every 6th call. The counter is at client +0x184 and reloads to 5. Each chunk is sent with `FUN_0052b330(0, ...)`:

  | Off | Size | Field |
  |---|---|---|
  | 0 | 1 | 0xb3 |
  | 1 | 1 | n (≤ 0xff) |
  | 2 | 1 | 1 on the first chunk (offset 0), else 0 |
  | 3 | 4 | total size |
  | 7 | n | data |

  When the whole buffer has gone out, `FUN_00538f10` clears it. After three send failures the client is dropped.
- **Client** (`FUN_0045c850`, case 0xb3 → `FUN_0045c620`):
  - reassembles the chunks in `DAT_007a5224`, starting over on a first-chunk flag;
  - when the byte count reaches the total, calls `FUN_0045c520`.

  `FUN_0045c520` checks that the magic is 0xaa55aa55 and that there are ≥ 0x82 bytes (≥ 0x14f for version ≥ 0x5c). It copies the status word into the client's character. Then it writes `<save dir><name>.d2s` with fopen "wb".
  There is no checksum check on the client side.

## Other system packets (S->C, client `FUN_0045c850`)

| Id | What |
|---|---|
| AF | connect accepted (`DAT_007a0618` = 1); `AF 81` is the custom Huffman table |
| B0 | refused, or leave done (`DAT_007a0618` = 0, which leads to a disconnect) |
| B2 | 0x35 bytes (`FUN_0053b1b0`: a name and two u16); not traced |
| B3 | save chunk (above) |
| B4 | `[B4, u32 reason]`: every reason 1..0x1a calls `FUN_0044e380` (disconnect, with a message per reason) |

## Create game (C->S 0x67, 46 bytes; the host's own client)

A d2d host needs this later. `FUN_00477ca0` builds it, called from `FUN_0044f360` when the type is not 3, 7 or 9, with ECX = 0x7a05dc
(the game name is the host character's name) and EDX = `FUN_0047a990()`.

| Off | Size | Field |
|---|---|---|
| 0 | 1 | 0x67 |
| 1 | 16 | game name |
| 0x11 | 1 | game type: 3 single, 1 for type 6, 2 for type 8 (→ game +0x6a) |
| 0x12 | 1 | class |
| 0x13 | 1 | config +0x20d |
| 0x14 | 1 | difficulty, config +0x210 (→ game +0x6d) |
| 0x15 | 16 | character name |
| 0x25 | 2 | config +0x207 |
| 0x27 | 4 | game flags, config +0x209 (4, plus 0x800 hardcore, plus 0x100000 expansion; `FUN_00434a00`) |
| 0x2b | 1 | config +0x20e |
| 0x2c | 1 | config +0x20f |
| 0x2d | 1 | language |

The host checks it in `FUN_0052c330` (strings terminated, +0x29 < 0xf, flags & 6) and creates the game in `FUN_00530bf0`.
Then the same 01/00/02 sequence follows. The host's own client also uploads its save (type 8 is in
`FUN_0044e200`'s list).

## What d2d needs

Estimates assume the S->C game packet decoders are done separately (see network.md).

1. **TCP client transport** (about 150 lines, half a day):
   - a socket to host:4000 on a thread or a poll loop;
   - phase 1 raw until `AF xx` (xx ≠ 0), then 1-/2-byte length frames;
   - C->S sent raw.

   Use TCP_NODELAY. The packets are small and the server sets it on its side.
2. **Huffman codec** (about 120 lines plus a test, half a day). The build, encode and decode steps as above.
   Load the lengths from game.exe at runtime (VA 0x7076c0) and check that the Kraft sum is 1. Also handle `AF 81`.
   The test is a golden round trip against tools/emu (`FUN_0040b1b0`/`FUN_0040b260`) and needs no game data in the repo.
3. **Splitter** with the S->C size table (about 80 lines, 2 hours). Read the u32 table
   `DAT_00730ae8` from game.exe too, or hand-code the ~180 sizes plus the 13 variable rules
   above. A wrong size desyncs everything after it, so it needs a check that
   decodes a whole captured session.
4. **Join state machine** (about 200 lines, 1 day):
   - send 0x68, with version 0x0e, token 1, the class and the exact save name;
   - stream the .d2s as 0x6c chunks right away (0xff each, total < 0x2000);
   - on `02`, send 0x6b;
   - handle 01/03/04 (difficulty, flags, act, map seed);
   - B4 reasons → UI text;
   - ping 0x6d every 5 s;
   - 0x69 on leave.
5. **Save-back** (done): `JoinSession` reassembles B3 and hands each whole save over once;
   d2d writes it through `CharacterStore::save_bytes` (it must parse as the same character with
   a good +0xc checksum;
   a .bak the first time; temp file + rename). Live: the host sends it on leave, 2531 bytes,
   the 13 header bytes above changed.

Later, for a d2d host:
- the listen side (port 4000, AF 01 raw on accept);
- the C->S size table;
- 0x67 and 0x68 checks that answer with the same B4 codes;
- the 01/00/02 → 6B → 03/53 → 04 sequence;
- saving through B3.

That is roughly 2–3 days on top of the server work in multiplayer.md.

### Risks

- **A mis-sized 0x6c can kill the host process.** A chunk past the declared total is fatal in
  `FUN_00538ce0`, and the whole host dies, the daughter's game included. d2d must send exactly `total`
  bytes.
- **The S->C stream must decode exactly.** One wrong size in the splitter corrupts every
  packet after it. Nothing resynchronizes inside a frame.
- **The save must pass the host's full .d2s loader.** That covers the checksum, the version 0x5c..0x60 and every item.
  A d2d-written .d2s that game.exe can't read gets a B4 and no join.
- **Save ownership.** The host overwrites the joiner's file through B3 at every save. If d2d's
  client doesn't write it back, progress made in her game is lost.

### Open questions

- Is there a difficulty or act access check (Nightmare or Hell for a character that hasn't unlocked it)?
  `FUN_0056a090` reads bit 0x80 of save[0xa8 + difficulty] and doesn't refuse; `FUN_0056b180`'s
  sub-parsers were not read.
- The 30 s client timeout in `FUN_0044eec0`: `DAT_007a05f8` is written only once, and
  `DAT_007a061c` is what keeps a long game alive: S→C 0x04's handler sets it (collision.md,
  "What keeps a walk to a unit going"). `DAT_007a0620` is the leave flag (quests-act4.md).
  Read from the code, not watched live.
- What +0x13, +0x25, +0x2b and +0x2c in 0x67 mean (config +0x20d, +0x207, +0x20e, +0x20f), and what the u32 at +1 of 0x68 means.
- The game events that trigger an in-game save (callers of `FUN_00532400`).
- Where B2 is sent, and whether TCP ever sends it.
- `FUN_006c3360`, the other listen path picked by `FUN_00410960`, is the I/O completion port path: qserver.md "The NT path".
