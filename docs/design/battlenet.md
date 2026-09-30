# Battle.net — layers, servers and a path for d2d

Status: research, 2026-09-30. Nothing built. Addresses are game.exe 1.14d
(Ghidra headless, main repo project) unless marked *unconfirmed*. Packet
names follow BNetDocs; where game.exe gives no string, the name is theirs,
not ours. Depends on the TCP/IP join work (net-join.md, net-packets.md,
net-host.md) and sits above it (multiplayer.md rule 9, order step 5).

**Recommendation in one line:** never target Blizzard's servers. Target a
self-hosted PvPGN (or jaenster's d2-dedicated-server). Do open Battle.net
first (chat + game list + TCP/IP join to a host). Do a closed realm second;
it needs a byte-exact D2GS client codec.

## The three layers

```
 client ──TCP 6112──> BNCS   (bnetd)     login, CD keys, version check, chat,
                                          open-game list, realm list
        ──TCP 6112──> MCP    (d2cs)      realm: characters, create/join game
        ──TCP 4000──> D2GS   (d2gs)      the game itself (same packets as TCP/IP)
```

Open Battle.net uses only BNCS; the game runs on one player's machine
(TCP/IP port 4000, the host is game.exe or d2d). A closed realm uses all
three; the game runs on the D2GS and characters live on the server (d2dbs).

All of it is linked into game.exe: Bnclient (0x518900–0x5224c0),
the MCP client (D2Launch area, 0x449b70–0x44b2d0), Storm/D2Net transport,
SRP (0x528500), SHA (0x52a120), Warden (0x47fa20–0x4803a0).

### BNCS (port 6112)

Framing: `FF id u16len payload`, len includes the 4-byte header.

| What | Address | Notes |
|---|---|---|
| Connect, fastest-server pick | `0x51ca90` sFastestConnect | port `0x17e0` = 6112 at 0x51cc0b; sends protocol byte `01` |
| Server list override | `0x51c190` | "bnserver-D2DV.ini", registry "BNETIP"; gateways: gateway-ini.md |
| Receive thread | `0x51c7e0` "Client->BNet" | keepalive `FF 00` every 180 s |
| Framer | `0x51c760` | |
| Dispatch | `0x521b00`, table `0x72ffb0` | ids < 0x5e only, so no SID_WARDEN (0x5E) over BNCS |
| Send | `0x51c5c0` | fastcall: CL id, EDX data, stack len; max 0x2000 |
| Bnclient API table | `0x72faa0..0x72fb70` | what the launcher calls (login 0x51c240, realm 0x51b460) |
| Wait for reply | `0x520b70` | 45 s timeout per id |
| Logger | `0x518d40` | BnetLog.txt |

Client → server (senders):

| Id | Name | Sender | Id | Name | Sender |
|---|---|---|---|---|---|
| 00 | NULL | 0x51c7e0 | 29 | LOGONRESPONSE | 0x51a580 |
| 02 | STOPADV | 0x51b1b0 | 31 | CHANGEPASSWORD | 0x51a7d0 |
| 06 | STARTVERSIONING | 0x51a9c0 | 33 | GETFILETIME | 0x51e3d0 |
| 07 | REPORTVERSION | 0x51a8f0 | 3A | LOGONRESPONSE2 | 0x51a660 |
| 09 | GETADVLISTEX | 0x51b240 | 3D | CREATEACCOUNT2 | 0x51a740 |
| 0A | ENTERCHAT | 0x51ae10 | 3E | LOGONREALMEX | 0x51b460 |
| 0B | GETCHANNELLIST | 0x51ac50 | 40 | QUERYREALMS2 | 0x51abf0 |
| 0C | JOINCHANNEL | 0x51aca0 | 41 | QUERYADURL | 0x51abb0 |
| 0E | CHATCOMMAND | 0x51af40 | 42 | *unconfirmed name* | 0x51e0b0 |
| 10 | LEAVECHAT | 0x51af20 | 46 | NEWS_INFO | 0x51dbf0 |
| 12 | LOCALEINFO | 0x51b540 | 50 | AUTH_INFO | 0x521bf0 |
| 15 | CHECKAD | 0x51b010 | 51 | AUTH_CHECK | 0x521dc0 |
| 16 | CLICKAD | 0x51b6c0 | 52 | AUTH_ACCOUNTCREATE | 0x522130 |
| 1C | STARTADVEX3 | 0x51b0e0 | 53 | AUTH_ACCOUNTLOGON | 0x521f60 / 0x522050 |
| 1E | CLIENTID2 | 0x51a490 | 54 | AUTH_ACCOUNTLOGONPROOF | 0x5220b0 |
| 1F | LEAVEGAME | 0x51b1d0 | 55/56 | AUTH_ACCOUNTCHANGE(PROOF) | 0x5221f0 / 0x522310 |
| 21 | DISPLAYAD | 0x51b060 | 57/58 | AUTH_ACCOUNTUPGRADE(PROOF) | 0x5223a0 / 0x5223f0 |
| 22 | NOTIFYJOIN | 0x51b320 | 59 | SETEMAIL | 0x51ba00 |
| 26 | READUSERDATA | 0x51b870 | 5A | RESETPASSWORD | 0x51baa0 |
| 27 | WRITEUSERDATA | 0x51b6f0 | 5B | CHANGEEMAIL | 0x51bb90 |
| | | | 5D | *unconfirmed name* | 0x51bcf0 |

Server → client handlers (table 0x72ffb0): 00 0x520c40, 04 0x5210a0,
05 0x520cb0, 06 0x520f60, 07 0x520e50, 09 0x5210d0, 0A 0x5210f0,
0B 0x5210c0, 0F 0x520660 (chat events), 15 0x521150, 1C 0x520d60,
1D 0x520d70, 20 0x5210e0, 25 0x520fc0 (echoes the ping), 26 0x521200,
28 0x520d50, 29 0x520da0, 2A 0x520c50, 31 0x520c80, 33 0x521110,
3A 0x520dc0, 3D 0x520c60, 3E 0x5211f0, 40 0x5211d0, 41 0x5211e0,
42 0x520fe0, 46 0x521210, 4A 0x521ac0, 4C 0x521ae0, 50 0x521220,
51 0x5212d0, 52 0x5215b0, 53 0x5216b0, 54 0x521760, 55 0x521800,
56 0x5218a0, 57 0x521950, 58 0x5219b0, 59 0x521ab0.

### Login flow (as game.exe does it)

1. Connect 6112, send `01`.
2. **0x50 AUTH_INFO** (`0x521bf0`): protocol 0, platform `IX86`, product
   `D2DV` or `D2XP` (expansion check `thunk_FUN_00408f20`), version byte
   `0x0e`, then locale, time zone bias, LCID, UI language, country
   abbreviation and name.
3. **0x50 reply** (`0x521220`): logon type, server token, mpq filetime,
   mpq file name, CheckRevision formula. The mpq is fetched over BNFTP
   (`0x51f910`, BnDownload.cpp). Also 0x33 GETFILETIME.
4. **CheckRevision** (`0x51e6d0`): extract the DLL from the mpq
   (`0x51e490`), **verify its Authenticode signature** (`0x516de1` →
   WinVerifyTrust `0x517ad6`, certificate organization check `0x517c30`),
   LoadLibrary, call `CheckRevision(game.exe path, "", "", formula,
   &version, &checksum, info)`. New in 1.14d: the DLL must be signed
   ([PvPGN #205](https://github.com/pvpgn/pvpgn-server/issues/205)).
5. **0x51 AUTH_CHECK** (`0x521dc0`): client token, exe version, checksum,
   key count (1 D2DV, 2 D2XP), hashed CD key block(s) (`0x51e340`), exe info
   string, key owner (`0x523aa0`). Keys come from obfuscated files loaded by
   `0x5234d0`; details deliberately left out of this doc.
6. **0x51 reply** (`0x5212d0`): 0x000 ok, 0x100 upgrade (download patch
   mpq, run bnupdate.exe), 0x101 unknown version, 0x102 downgrade,
   0x200 invalid key, 0x201 key in use, 0x202 key disabled, 0x203 wrong
   product.
7. **Account logon**, chosen by the logon type from step 3 (`0x51c240`):
   - type 0 → **0x3A LOGONRESPONSE2** (`0x51a660`): client token, server
     token, double broken-SHA-1 ("XSHA1", `0x5209d0`) of the lowercased
     password, username. Reply `0x520dc0`: 0 ok, 1/2/3 fail, 6 = account
     closed (BNetDocs) → `0x5204d0`.
   - type 1 → **0x53/0x54 SRP (NLS)** (`0x521f60`, `0x5220b0`): 32-byte A,
     then 20-byte M1 (`0x528760`); reply 0x53 (`0x5216b0`) 0 proof,
     1 unknown account, 5 upgrade; 0x54 (`0x521760`) checks M2
     (`0x5224c0`, "Server didn't know our password but told us it did!").
   - other types fail.
8. **Open Battle.net**: 0x0A ENTERCHAT, 0x0C JOINCHANNEL, 0x0E chat,
   0x0F events; 0x1C STARTADVEX3 to host, 0x09 GETADVLISTEX to list,
   0x22 NOTIFYJOIN / 0x1F LEAVEGAME around a TCP/IP join to the host.
9. **Closed realm**: 0x40 QUERYREALMS2, then **0x3E LOGONREALMEX**
   (`0x51b460`): client token, server token, XSHA1 of the realm password
   (BNetDocs says the literal "password"; *unconfirmed*), realm title.
   Reply (`0x51b3e0`): length 8 = fail; else MCP IP at +0x10, port at +0x14
   (ntohs), 64 bytes of cookie/status/chunk data, unique name.

Server-pushed native code on this path: the CheckRevision DLL, the
ExtraWork DLL (0x4C/0x4A, `0x51fab0`, "IX86ExtraWork"), bnupdate.exe, and
Warden modules on D2GS. d2d runs none of them.

### MCP (realm, port from 0x3E reply)

Connection "clt->mcp" in `0x44a070` (`0x6bc6a0`), sends `01`, send
function `0x6bc8a0(conn, buf, len)` with the id as first byte (the u16
length prefix is *unconfirmed*, BNetDocs says `u16 len, u8 id`). Two
dispatch tables: startup `0x44b1c0` / `0x70ed68` (only 01 → 0x44ab80),
then main `0x44b190` / `0x70ed00` (ids < 0x1a, polled from `0x449b70`).

| Id | Name (BNetDocs) | C→S sender | S→C handler |
|---|---|---|---|
| 01 | STARTUP (64 bytes from 0x3E + unique name) | 0x44a1d0 | 0x44ab80 |
| 02 | CHARCREATE | 0x44a2e0 | 0x44aba0 |
| 03 | CREATEGAME | 0x44a400 | 0x44abe0 |
| 04 | JOINGAME | 0x44a4e0 | 0x44ac20 |
| 05 | GAMELIST | 0x44a590 | 0x44b2d0 |
| 06 | GAMEINFO | 0x44a610 | 0x44aca0 |
| 07 | CHARLOGON | 0x44a380 | 0x44abc0 |
| 0A | CHARDELETE | 0x44a7a0 | 0x44ae30 |
| 11 | REQUESTLADDERDATA | 0x44aa10 | 0x44afc0 |
| 12 | MOTD | 0x44aa50 | 0x44b120 |
| 13 | CANCELGAMECREATE | 0x44aa80 | — |
| 14 | CREATEQUEUE | — | 0x44b150 |
| 17 | CHARLIST | — | 0x44b170 |
| 18 | CHARUPGRADE | 0x44a810 | 0x44ae60 |
| 19 | CHARLIST2 (asks for 8) | 0x44aab0 | 0x44b180 |

Also present, names *unconfirmed*: C→S 08 0x44a680, 09 0x44a710,
0B 0x44a870, 0C 0x44a900, 0D 0x44a980, 0F 0x44a9b0, 10 0x44a9e0,
16 0x44aae0; S→C 00 0x44ab70, 08 0x44adf0, 09 0x44ae10, 0B 0x44ae80,
0C 0x44aea0, 0D 0x44aef0, 0E 0x44aec0, 0F 0x44af10, 10 0x44af30.

### Handoff to D2GS

1. **MCP 04 reply** (`0x44ac20`): request id +1, **token** u16 +3, D2GS
   **IP** +7, **game hash** u32 +0xb, result +0xf. Stored in
   DAT_00798fb0 / DAT_00798f88; `0x441500` copies them into the join
   struct (+0x1d token, +0x229 hash, +0x37 IP string).
2. Connect to D2GS on **port 4000** (0xfa0 at 0x52b7be, `0x6bf760`; the
   same port game.exe listens on for TCP/IP, 0x52a6bd).
3. Server sends 0xAF (BNetDocs; *unconfirmed* here), then client sends **0x68
   GAMELOGON** (`0x477f70`, 37 bytes, sent by `0x52ae50`):

   | Off | Size | Field |
   |---|---|---|
   | 0 | 1 | 0x68 |
   | 1 | 4 | game hash |
   | 5 | 2 | token |
   | 7 | 1 | class |
   | 8 | 4 | version |
   | 12 | 4 | 0x2185EDD6 (D2DV) / 0xED5DCC50 (D2XP) |
   | 16 | 4 | 0x91A519B6 |
   | 20 | 1 | locale (`0x525150`) |
   | 21 | 16 | character name |

   TCP/IP uses 0x67 (`0x477ca0`, 46 bytes) instead; leave is 0x69
   (0x477ee0). From here on it's the game protocol in network.md.
4. **0x6D ping** (`0x477dd0`): tick count, a value from `0x44ce70`, and an
   integrity dword read with ReadProcessMemory from an obfuscated
   module+offset (DAT_007a04e8/ec/f0 × 0x470e31c1). Who sets those globals
   is *unconfirmed*; a strict D2GS could check it.
5. **Warden** rides D2GS: S→C 0xAE, C→S 0x66. `0x47fc70` RC4-decrypts a
   module, `0x47fa20` checks an RSA "NGIS" signature, then loads native
   x86 code (`0x4e0730`). game.exe's own server handler for C→S 0x66
   (`0x54d740`) is `return 0;`: **TCP/IP games hosted by game.exe run no
   Warden.**

### Local vs server-side

| Thing | Where it's decided |
|---|---|
| Version check (CheckRevision result) | server compares; client runs a server-sent, signed DLL |
| CD key validity / in use | server (keys hashed with server + client token) |
| Password | server; client sends XSHA1 hash or SRP proof, never the plaintext |
| Open-game list, chat, friends, ladder | server (BNCS) |
| Open Battle.net character | client's own save file, uploaded to the host at join |
| Realm character | server (d2dbs); client only sees CHARLIST |
| Game simulation, open | whoever hosts (game.exe or d2d, TCP/IP) |
| Game simulation, realm | D2GS |
| Anti-cheat | Warden (server-driven, client executes); 0x6D integrity dword |

## Open vs closed Battle.net

| | Open | Closed (realm) |
|---|---|---|
| Servers | BNCS only | BNCS + MCP + D2GS (+ d2dbs) |
| Characters | local .d2s, trusted | server-held, not editable |
| Game host | a player (TCP/IP 4000) | D2GS |
| Client must do | BNCS login, chat, 0x1C/0x09, TCP/IP join | all of that + 0x3E, MCP, 0x68 join, exact D2GS codec, Warden if enabled |
| Mixed game.exe + d2d | yes, if the host speaks TCP/IP D2GS (net-join/net-host) | yes, if d2d's codec is exact and Warden is off |
| Cost for d2d | small on top of TCP/IP | large |

In practice open Battle.net for d2d means a PvPGN server.

## Target servers

### Blizzard (useast/uswest/europe/asia.battle.net)

Do not target. Plainly: d2d should not connect to Blizzard's servers.

- **ToS.** The EULA forbids "third-party programs or tools not expressly
  authorized by Blizzard" and connecting through unauthorized means, and
  forbids emulating or redirecting its matchmaking protocols
  ([EULA](https://www.blizzard.com/en-us/legal/fba4d00f-c7e4-4883-b8b9-1b4500a402ea/blizzard-end-user-license-agreement),
  [Legal FAQ](https://www.blizzard.com/en-us/legal/c1ae32ac-7ff9-4ac3-a03b-fc04b8697010/blizzard-legal-faq)).
  Blizzard v. bnetd (8th Cir. 2005) upheld EULA and DMCA claims against a
  Battle.net emulator
  ([Pinsent Masons](https://www.pinsentmasons.com/out-law/news/battlenet-emulator-broke-dmca-and-eula)).
  Risk to the player: a banned CD key, which is a purchase.
- **What would reject d2d:** the signed CheckRevision DLL must run against a
  real 1.14d Game.exe (d2d would have to lie about the checksum); Warden
  modules are native Windows code d2d can't run, and a wrong or missing
  answer flags the key; the 0x6D integrity dword; ExtraWork; exact packet
  timing and ordering on D2GS.
- **Status:** no official shutdown, but realms are neglected and often
  down; player reports run from March 2025 to April 2026, including a
  December 2025 "permanently down" thread
  ([forum](https://us.forums.blizzard.com/en/d2r/t/diablo-ii-lord-of-destruction-legacy-permanently-down/169806),
  [BNETDocs servers](https://bnetdocs.org/servers)).

### PvPGN + d2cs/d2dbs + D2GS

- [pvpgn/pvpgn-server](https://github.com/pvpgn/pvpgn-server) (GPL-2.0,
  1.99.7.x): bnetd (BNCS), d2cs (MCP), d2dbs (character DB). Lists D2
  1.10–1.13c and 1.14a–d. D2GS is "not part of the PvPGN project".
- Version check is config: a D2DV 1.14d entry exists (ver-IX86-1.mpq,
  version 0x0e / 1.14.3.0), plus `allow_unknown_version` /
  `allow_bad_version`
  ([#205](https://github.com/pvpgn/pvpgn-server/issues/205),
  [wiki](https://pvpgn.fandom.com/wiki/Version_Check)). With these a d2d
  that reports any version gets in; no signed DLL needed server-side.
- Accounts use logon type 0 (XSHA1, 0x3A) for D2; SRP not needed.
- **D2GS**: every build wraps Blizzard's own D2Server.dll/D2Game.dll, no
  from-scratch server exists in that line:
  [pvpgn/d2gs109](https://github.com/pvpgn/d2gs109),
  [RElesgoe/d2gs](https://github.com/RElesgoe/d2gs),
  [tesseract2048/d2gs](https://github.com/tesseract2048/d2gs) (1.13c),
  [Zakamurite/D2GS](https://github.com/Zakamurite/D2GS) (1.11b, Warden
  settings), [pvpgn.pl/d2gs](http://www.pvpgn.pl/d2gs/). There is **no
  1.14d D2GS**; people run a 1.13c D2GS with the version check pinned to
  1.14d clients and report it "works flawless"
  ([1953](https://forums.pvpgn.pro/viewtopic.php?id=1953),
  [2045](https://forums.pvpgn.pro/viewtopic.php?id=2045),
  [2155](https://forums.pvpgn.pro/viewtopic.php?id=2155)). So the realm
  game protocol is 1.13c-as-accepted-by-1.14d; d2d would need to match it.
- Mixed game.exe + d2d realm game: possible, everyone connects to the D2GS.
  Needs Warden off on that D2GS (most PvPGN D2GS builds have it off or
  configurable).

### jaenster/d2-dedicated-server

[jaenster/d2-dedicated-server](https://github.com/jaenster/d2-dedicated-server):
Zig, "a modern PvPGN replacement". realmd does BNCS + MCP on 6112, D2GS
engines 1.06b–1.13c on the game's DLLs, **1.14d via wine**, and a
wine-free native build from the Mac 1.14d port. Newer and the only one with
a 1.14d game server; worth a look before committing to PvPGN. Same author:
[d2-clientless](https://github.com/jaenster) (a 1.14d BNCS/MCP/BNFTP/D2GS
client in Zig, a useful cross-check for our client) and libd2 (clean-room
1.14d engine core). Its DLL-based engines still ship Blizzard binaries;
not verified by us.

### Other references

[BNetDocs](https://bnetdocs.org/) (packet names),
MephisTools diablo2-protocol, D2MOO (D2Game decomp, for D2GS behaviour).

## Recommended path

Target a self-hosted PvPGN on localhost/LAN, version check relaxed. Order:

| # | Item | Size | Depends on |
|---|---|---|---|
| 1 | BNCS transport + 0x50/0x51/0x3A (XSHA1), PvPGN `allow_unknown_version` | ~600 LOC, 2–3 days | nothing; can start now |
| 2 | Chat: 0x0A/0x0C/0x0E/0x0F/0x10, a text UI | ~500 LOC, 2 days | 1 |
| 3 | Open games: 0x1C host ad, 0x09 list, 0x22/0x1F, then TCP/IP join | ~400 LOC, 2 days | 1 + net-join (join), net-host (host) |
| 4 | MCP: 01/07/17 or 19/02/05/03/04, char list/create/select | ~1 kLOC, 1 week | 1, 0x3E |
| 5 | Realm join: 0x68 GAMELOGON + D2GS codec exact to 1.13c-as-1.14d | large, weeks | 4 + net-packets done byte-exact |
| — | CheckRevision natively (hash the user's own Game.exe with the formula) | ~200 LOC | only if a server insists on a real checksum |
| — | SRP 0x53/0x54 | ~400 LOC | only for a server using logon type 1 |
| — | Warden | not planned | d2d can't run native modules; require Warden off |
| — | d2d as BNCS/MCP server | not planned | use PvPGN / d2-dedicated-server |

Items 1–3 give Bret and a friend "log in, chat, see and join each other's
game" with d2d or game.exe as host, almost all of it reusing the TCP/IP
work. Item 5 is the real cost and only pays off for a realm.

Notes:
- Keep it a layer above the game (multiplayer.md rule 9): a
  `components/bnet` that ends in "here is a TCP/IP address + a join
  packet", nothing in the sim.
- Credentials: account name and password come from the user at runtime;
  never stored in the repo or logs. CD keys: game.exe needs them for PvPGN
  (it sends the key block); d2d should send whatever the server accepts and
  never read or ship Blizzard key files.
- A game.exe client on PvPGN still needs the Blizzard-signed CheckRevision
  DLL mpq (from the user's own install or the server admin), not from us.

## Risks

1. **Legal/ToS.** Anything that could reach Blizzard's gateways is a
   liability; default the gateway list to localhost, no Blizzard hosts.
2. **Realm codec.** D2GS is Blizzard's 1.13c D2Server.dll under a 1.14d
   version stamp; d2d must be byte-exact against it, quirks included.
3. **Warden / 0x6D.** If a server enables Warden or checks the integrity
   dword, d2d can't pass. Mixed games need servers with both off.
4. **Blizzard binaries on the server side.** PvPGN's D2GS and jaenster's
   DLL engines run Blizzard code; the admin supplies it, not the repo.
5. **Unverified names.** Packet names are BNetDocs'; 0x42, 0x5D and several
   MCP ids have no game.exe string. The MCP length prefix and the 0x6D
   integrity source are unconfirmed.
