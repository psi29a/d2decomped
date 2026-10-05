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
- name empty, shorter than 2 or longer than 16 → error
- the name is already in use in the game → 6

A failure is sent as `[B4, u32 reason]` (5 bytes, `FUN_0053b260`), and then the client is dropped (`FUN_0052b4f0`).

**Stage 2**: `FUN_0052c690` checks that the name is terminated and the class is < 7. For the name characters it calls `FUN_0052c5b0`: letters, plus at most one of `' - _`, which can't be first or last.

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
  - byte +0x28 ≤ 7.
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
  - disconnect after 30000 ms from start (`DAT_007a05f8`, written only in `FUN_0044e200`) while `DAT_007a061c`, `DAT_007a0620` and `DAT_007a0624` are all 0;
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
- The 30 s client timeout in `FUN_0044eec0`: `DAT_007a05f8` is written only once. It is
  unclear which of `DAT_007a0620` (from `DAT_007a0460`) or `DAT_007a061c` keeps a long game
  alive. Watch it under the emulator or in a live session before copying the behavior.
- What +0x13, +0x25, +0x2b and +0x2c in 0x67 mean (config +0x20d, +0x207, +0x20e, +0x20f), and what the u32 at +1 of 0x68 means.
- The game events that trigger an in-game save (callers of `FUN_00532400`).
- Where B2 is sent, and whether TCP ever sends it.
- `FUN_006c3360`, the other listen path picked by `FUN_00410960`, was not read.
