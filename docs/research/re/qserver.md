# QServer: the game server's socket layer — game.exe 1.14d

`.\Src\QServer\QServer.cpp`, `QServerNT.cpp` and `QServer98.cpp`: the
transport under the TCP/IP host (net-join.md "Server side"). It owns the
listening socket, the clients, the receive queues and a "hacklist" of
banned IPs. d2d's host is its own code (`components/net`); nothing here is
ported. Re-check any row with `tools/ghidra/fn.py <address>`. Winsock
calls are `ws2_32` ordinals (1 accept, 5 getpeername, 6 getsockname,
12 inet_ntoa, 18 select, 19 send, 21 setsockopt, 23 socket).

## The server (`FUN_006bf760`, "TQSERVER", 0xc4c bytes)

`FUN_006bf760(mode, queues, port, force98, parse, connect, disconnect,
local)`: `FUN_0052b7a0` passes `(mode, 3, 4000, b, FUN_0052b100,
FUN_0052b720, FUN_0052b750, FUN_0052b0e0)`. Modes 1 and 2 are local
(no sockets). Otherwise it runs `WSAStartup` and the NT path
(`FUN_006c3360`, `+0` = 1) when `FUN_00410960` says so and `force98` is 0,
else the 98 path (`FUN_006c3b50`, `+0` = 0).

| Offset | What |
|---|---|
| `+0` | 1 = NT (I/O completion ports), 0 = 98 (`select`) |
| `+4` | last client id |
| `+0xc`, `+0x14`, `+0x1c` | client hash by id: mask, bucket heads, one critical section per bucket (0x18 each) |
| `+0x20`, `+0x28` | client hash by IP: bucket heads, critical section |
| `+0x10`, `+0x40` | connections per IP: bucket count (modulo), buckets of {ip, count, next} |
| `+0x48`, `+0x848` | exempt list: up to 256 {ip, mask bits}; count |
| `+0x84c`, `+0x850` | recent connects {ip, tick, count, next}: head, tail |
| `+0x854` | the main critical section |
| `+0x86c` | mode |
| `+0x870`, `+0x874`, `+0x87c` | receive queues per class: heads, tails, count (3) |
| `+0x878` | free packet records |
| `+0x880` | client count |
| `+0x884` | connections allowed per IP (the NT path sets 1) |
| `+0x888`..`+0x8b0` | hacklist: last aging tick, free list, size, critical section, buckets, bucket count |
| `+0x8b4`, `+0x8cc` | accept statistics: critical section, 0xb0 bytes per listener |
| `+0xb8c`..`+0xba8` | listeners (up to 4): count, sockets, ports, flags |
| `+0xbb8`, `+0xbbc` | hacklist callback, another callback (setters only) |
| `+0xbc0`..`+0xbcc` | parse, connect, disconnect, local delivery callbacks |
| `+0xbd0` | hacklist on |
| `+0xbd4` | the 98 path's event |
| `+0xbd8`, `+0xbf8` | accept threads, their stop flags |
| `+0xc08` | completion port per listener |
| `+0xc18`, `+0xc1c`, `+0xc3c` | separate completion threads on/off, their handles, stop flags |

**Client** (`FUN_006bfb70`, 0x664 bytes): `+0` id, `+4` next by id, `+8`
references, `+0xc` closing, `+0x10` socket, `+0x14` listener, `+0x18` IP,
`+0x1c` next by IP, `+0x20` the caller's value, `+0x24` / `+0x28` spam
tick / count, `+0x2c` critical section, `+0x44` read OVERLAPPED, `+0x58`
read flags, `+0x5c` write OVERLAPPED, `+0x70` write flags, `+0x74` send
queue, `+0x78` local address, `+0x88` peer address, `+0x98`..`+0xa4`
times (`FUN_00410a80`), `+0xa8` bytes buffered, `+0xb0` receive buffer
(1460).

**Packet record** (0x5c0 bytes): `+0` client id, `+4` data (1460), `+0x5b4`
length, `+0x5b8` tick, `+0x5bc` next.

## Connections

| Address | What |
|---|---|
| `FUN_006be450` | Client count (`+0x880`) |
| `FUN_006be470` | True in local modes 1 and 2 |
| `FUN_006be4a0` | Sets the per-IP limit (0 becomes 1) |
| `FUN_006be4e0` | 98: waits on `+0xbd4`; NT: 0 |
| `FUN_006be510` | Lock guard: enters the critical section it's given |
| `FUN_006be530` | Lock guard: leaves it |
| `FUN_006be540` | Client by id (ECX server, EDX id) |
| `FUN_006be5b0` | Client by socket (scans every bucket) |
| `FUN_006be640` | Adds a reference |
| `FUN_006be650` | Frees a client (EDI): its send queue, its critical section |
| `FUN_006be9f0` | True when the IP already has `+0x884` connections (negative limit: never) |
| `FUN_006bebc0` | Wait: 98 waits on the event; NT pumps completions (`FUN_006c30d0`) |
| `FUN_006bec00` | Client by id, returning with its bucket still locked |
| `FUN_006bec70` | Local address (`+0x7c`) |
| `FUN_006becb0` | Local port (`ntohs(+0x7a)`) |
| `FUN_006bed00` | Peer address (`+0x8c`) |
| `FUN_006bed40` | Peer port (`ntohs(+0x8a)`) |
| `FUN_006bed90` | IP as text (`inet_ntoa(+0x18)`) |
| `FUN_006bee20` | IP |
| `FUN_006bee90` | `getpeername` into a local that is never read |
| `FUN_006bef40` | Socket (-1 when unknown) |
| `FUN_006bf0a0` | Sets the caller's value (`+0x20`) |
| `FUN_006bf0c0` | Gets it |
| `FUN_006bf180` | Connections per IP +1 (new bucket entry at 1) |
| `FUN_006bf1f0` | Connections per IP -1 (frees at 0) |
| `FUN_006bfad0` | Sends to every client not closing |
| `FUN_006bfb70` | QSrvAddClient: new id, both hashes, per-IP +1, "Added client %d %s on socket %d" |
| `FUN_006bfcd0` | sRemoveClient: out of both hashes, per-IP -1 |
| `FUN_006c0170` | Shutdown: optional broadcast, stop the threads, free everything, `WSACleanup` |
| `FUN_006c0360` | Drops a reference; at 0 the disconnect callback, remove, free |
| `FUN_006c0420` | QSrvCloseRemoteConnection: marks closing, `closesocket`, drops the reference |
| `FUN_006c06b0` | Disconnect by client id |
| `FUN_006c06d0` | QSDisconnectIP: closes that IP's client, logs |

## Receiving

| Address | What |
|---|---|
| `FUN_006be3c0` | Empties every receive queue |
| `FUN_006bea70` | Packets queued in one class |
| `FUN_006bead0` | Pops a packet from a class: copies up to the caller's size (≤ 1460), returns its length, recycles the record |
| `FUN_006bf370` | Injects bytes as a packet through the parse callback (the local path) |
| `FUN_006bf580` | Packets queued in all classes |
| `FUN_006c08a0` | QSrvProcessMsg: runs the parse callback on the client's buffer until it says "need more" (3). 2 queues the packet in the class it names (and runs the spam check); 4 is PACKET_INVALID: bans the client; 5 drops the bytes. Over-long buffers are fatal (line 0x6ad) |

## Hacklist and flood checks

Only while `+0xbd0` is set (`FUN_006bf4e0`), and never for an exempt IP.
An entry is {ip, expiry s, refused s, next}; the log line is
"[HACKLIST]  Hacklist size is %d", at most every 2 s.

| Address | What |
|---|---|
| `FUN_006be6c0` | Logs the hacklist size |
| `FUN_006be700` | Ages entries once a second: the refused time counts down, then the expiry is 120 s; at 0 the entry is freed |
| `FUN_006be890` | Removes an IP |
| `FUN_006be980` | Accept check: a listed IP with refused time left is refused, and the time is reset to 120 s |
| `FUN_006befb0` | QSHackUnlistIP (logged) |
| `FUN_006bf000` | QSHackListSize (logs it) |
| `FUN_006bf040` | Sets the hacklist callback (`+0xbb8`); a null server is fatal |
| `FUN_006bf080` | Sets `+0xbbc` |
| `FUN_006bf0f0` | Frees the hacklist |
| `FUN_006bf260` | Drops recent connects older than 15 s |
| `FUN_006bf320` | Frees the recent connects |
| `FUN_006bf4e0` | Hacklist on/off |
| `FUN_006bf610` | Adds an exempt {ip, mask bits} (ECX server; ignores duplicates; 256 max) |
| `FUN_006bf660` | True when the IP matches an exempt entry (mask table `0x74c3f0`) |
| `FUN_006bf6c0` | Same, fastcall |
| `FUN_006bfea0` | Lists an IP (calls `+0xbb8` first): a new entry gets expiry 120 s and no refused time; one already listed gets refused time 120 s |
| `FUN_006bffa0` | Reconnect flood: counts this IP's connects in 15 s; over `2 × limit + 3` refuses. It appends a new record on every connect, found or not |
| `FUN_006c00a0` | Spam check: adds the parser's weight to the client's count; over 1000 bans unless exempt; each second takes 200 off (or clears it below 200) |
| `FUN_006c0530` | QSHackList: closes the client and lists its IP |
| `FUN_006c0600` | QSHackListIP: closes that IP's client and lists it |
| `FUN_006c0770` | Bans a client: close and list |
| `FUN_006c0800` | QSBanByAddr: bans the client already on that IP (unless exempt) |

## Statistics

| Address | What |
|---|---|
| `FUN_006bf600` | Empty |
| `FUN_006bf6d0` | Copies the accept statistics (0xb0 per listener) and each listener's flag |
| `FUN_006bf750` | Empty; the accept thread calls it on listener 0 |

## The NT path (QServerNT.cpp)

`FUN_006c3360` sets the per-IP limit to 1, runs `WSAStartup` and
`FUN_006c3120` (QSNTInit): a completion port, `socket(2,1,0)`,
`TCP_NODELAY`, `SO_KEEPALIVE`, `SO_REUSEADDR`, non-blocking, `bind` to the
port, `listen` with backlog 250, then the accept thread (and a completion
thread when `+0xc18` is set).

| Address | What |
|---|---|
| `FUN_006c2280` | Posts an overlapped `ReadFile` into the rest of the 1460-byte buffer; a full buffer closes the client |
| `FUN_006c23b0` | Appends data to the client's send queue |
| `FUN_006c2420` | sQSNTAccept: `accept`, then refuses "user on hack list", "user logged on twice" (also bans the client already there) or "user spamming with reconnects"; else adds the client, reads both addresses, joins the completion port, posts a read and runs the connect callback. Each step's `QueryPerformanceCounter` goes into the statistics |
| `FUN_006c2760` | Posts an overlapped `WriteFile` of the send queue's head |
| `FUN_006c2860` | Accept thread: polls with `select`, ages the hacklist, accepts; sleeps 10-20 ms between; ends on its stop flag |
| `FUN_006c2c70` | Send: `send` straight away when nothing is queued, queues the rest; else queues behind |
| `FUN_006c2d70` | Completion pump: a read adds the bytes and runs QSrvProcessMsg, then reads again; a write frees the sent node and writes the next; 0 bytes or an error closes the client |
| `FUN_006c3030` | Completion thread: pumps with a 50 ms wait until its stop flag |
| `FUN_006c30d0` | Pumps every listener (when there are no completion threads) |
| `FUN_006c33d0` | Stop: sets the flag, waits 2 s for the accept thread |

## The 98 path (QServer98.cpp)

`FUN_006c3b50` and the select thread `FUN_006c3470` are in net-join.md.

| Address | What |
|---|---|
| `FUN_006c3410` | Send: one `send` |
| `FUN_006c3440` | Adds a client and runs the connect callback |
