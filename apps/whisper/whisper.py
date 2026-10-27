#!/usr/bin/env python3
"""Talk to whisper inside a game.exe: whisper.py HOST[:PORT] TOKEN VERB [ARGS...]

`screenshot PATH` saves the game's window as a BMP; `follow` runs the merc
brain (below) until Ctrl-C; `party invite|cancel|accept|leave ID` sends
C->S 0x5e; `quests` lists the host player's quests (this difficulty), done
or open: check it before taking anyone on a quest; `town` prints the
loaded rooms of the host player's level as tools/emu's tiles_dump does
(`town D2D.txt` checks each loaded room against `drlg-dump DIR SEED
LEVEL tiles` run with the seed and level it prints first, and
D2_PATCH_INSTALLER set as diff_drlg.py sets it, or the data is pre-1.14d); `host
[normal|nightmare|hell]` clicks from the title screen to a hosted TCP/IP
game with the one character in game.exe's save folder (Makefile `run og
host NAME`); anything else prints the reply.
"""
import math
import socket
import sys
import time

# MonStats rows a follower never attacks: npc, Align 1/2, interact,
# killable 0 or inTown (1.14d; regenerate from MonStats.txt if it changes).
NOT_FOES = {
    34, 35, 36, 37, 106, 107, 108, 109, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 157, 158, 159, 175, 176,
    177, 178, 179, 185, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 210, 217, 218, 219, 220, 221, 222,
    223, 224, 225, 226, 227, 244, 245, 246, 251, 252, 253, 254, 255, 257, 264, 265, 266, 268, 269, 270, 271, 272,
    289, 290, 291, 292, 293, 294, 296, 297, 318, 319, 320, 326, 327, 328, 329, 330, 331, 332, 338, 339, 344, 351,
    352, 353, 355, 356, 357, 358, 359, 363, 364, 366, 367, 368, 369, 377, 378, 401, 405, 406, 408, 410, 411, 412,
    413, 414, 415, 416, 417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 450, 451, 452,
    511, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 522, 523, 527, 534, 535, 536, 537, 538, 539, 543, 545,
    556, 559, 560, 561, 567, 568, 569, 574,
}
MONSTER_GONE = {0, 12}           # MonMode DT (dying), DD (dead)
PLAYER_GONE = {0, 17}            # PlrMode DT, DD: a corpse is a dead player unit
FEET_X, FEET_Y = 400, 292        # the player's feet on an 800x600 screen
FOLLOW_FROM = 5                  # subtiles from the leader before closing in
FIGHT_RANGE = 14                 # subtiles from us; and within that of the leader
DRINK_BELOW = 0.45               # of max life
GONE_PAST = 60                   # subtiles: a leader that far is on another level


class Whisper:
    def __init__(self, address: str, token: str) -> None:
        host, _, port = address.partition(":")
        self.conn = socket.create_connection((host, int(port or 4100)), timeout=10)
        self.reader = self.conn.makefile("rb")
        self.conn.sendall(f"{token}\n".encode())
        if self.reader.readline() != b"ok whisper\n":
            sys.exit("refused (token?)")

    def ask(self, line: str) -> list[str]:
        """The reply's lines, up to and including the closing ok / err."""
        self.conn.sendall(line.encode() + b"\n")
        lines = []
        while text := self.reader.readline().decode():
            lines.append(text.rstrip("\n"))
            if text.startswith(("ok", "err")):
                break
        return lines


# Quest words (d2s order; bit 0: done) by name, rules/quests*.hpp's kQuest.
QUESTS = {
    1: "Den of Evil", 2: "Sisters' Burial Grounds", 3: "Tools of the Trade", 4: "The Search for Cain",
    5: "The Forgotten Tower", 6: "Sisters to the Slaughter", 9: "Radament's Lair", 10: "The Horadric Staff",
    11: "Tainted Sun", 12: "Arcane Sanctuary", 13: "The Summoner", 14: "The Seven Tombs",
    0x11: "Lam Esen's Tome", 0x12: "Khalim's Will", 0x13: "Blade of the Old Religion", 0x14: "The Golden Bird",
    0x15: "The Blackened Temple", 0x16: "The Guardian", 0x19: "The Fallen Angel", 0x1a: "Terror's End",
    0x1b: "Hell's Forge", 0x23: "Siege on Harrogath", 0x24: "Rescue on Mount Arreat", 0x25: "Prison of Ice",
    0x26: "Betrayal of Harrogath", 0x27: "Rite of Passage", 0x28: "Eve of Destruction",
}


def peek(whisper: Whisper, address: int, count: int) -> list[int]:
    """`count` u32s; ValueError when the reply is err or a word is unreadable ("?")."""
    lines = whisper.ask(f"peek {address:x} {count}")
    if not lines or lines[-1] != "ok":
        raise ValueError(lines[-1] if lines else "no reply")
    return [int(line.split()[1], 16) for line in lines[:-1]]


def quests(whisper: Whisper) -> list[str]:
    """The host player's 48 quest words: the client's quest state (S->C 0x28 type 6) is a pointer at
    0x7c0d43 to a record whose +0 points at the 96 bytes (net-packets.md 0x28)."""
    try:
        head = peek(whisper, 0x7c0d40, 2)
        record = (head[0] >> 24) | (head[1] & 0xffffff) << 8
        words_at = peek(whisper, record, 1)[0]
        raw = b"".join(word.to_bytes(4, "little") for word in peek(whisper, words_at, 24))
    except ValueError as error:
        return [f"quest state unreadable (not in a game yet?): {error}"]
    words = [int.from_bytes(raw[2 * k:2 * k + 2], "little") for k in range(48)]
    return [f"{'done' if words[k] & 1 else 'OPEN'}  {words[k]:04x}  {name}" for k, name in QUESTS.items()]


class Memory:
    """game.exe's memory over peek, 256-byte blocks cached; unreadable words read as 0."""
    def __init__(self, whisper: Whisper) -> None:
        self.whisper, self.blocks = whisper, {}

    def r32(self, address: int) -> int:
        base = address & ~0xff
        if base not in self.blocks:
            lines = self.whisper.ask(f"peek {base:x} 64")
            self.blocks[base] = [0 if line.split()[1] == "?" else int(line.split()[1], 16) for line in lines[:-1]]
        if address & 3:   # unaligned: two words
            low = address & ~3
            return (self.r32(low) >> 8 * (address & 3) | self.r32(low + 4) << 32 - 8 * (address & 3)) & 0xffffffff
        return self.blocks[base][(address & 0xff) >> 2]

    def s32(self, address: int) -> int:
        value = self.r32(address)
        return value - (1 << 32) if value & 0x80000000 else value

    def cstr(self, address: int) -> str:
        raw = b"".join(self.r32(address + 4 * k).to_bytes(4, "little") for k in range(64))
        return raw.split(b"\0")[0].decode("latin-1")


def town(whisper: Whisper) -> list[str]:
    """tools/emu/drlg.py tiles_dump over a live game: player +0x2c path, +0x1c room1, +0x10 room2, +0x58 level
    (+0x1b4 act, its +0x458 the map seed; offsets found in the emulator). Only rooms near the player have tiles (room +0x54); the rest say so."""
    mem = Memory(whisper)
    player = mem.r32(0x7a6a70)
    room1 = mem.r32(mem.r32(player + 0x2c) + 0x1c) if player else 0
    if not room1:
        return ["no player room (not in a game yet?)"]
    level = mem.r32(mem.r32(room1 + 0x10) + 0x58)
    ranges, libs = [], {}   # drlg.py dt1_index: every DT1 game.exe has loaded
    node = mem.r32(0x8adbb4)
    while node:
        name = mem.cstr(node).split("\\")[-1].lower()
        buf = mem.r32(node + 0x104)
        ranges.append((mem.r32(buf + 0x110), mem.r32(buf + 0x10c), name))
        libs[mem.r32(node + 0x108)] = name
        node = mem.r32(node + 0x10c)

    def find(tile: int) -> tuple[str, int]:
        if tile in libs: return libs[tile], -1
        return next(((name, (tile - base) // 0x60) for base, count, name in ranges if base <= tile < base + 0x60 * count), ("?", tile))

    x0, y0 = mem.s32(level + 0x1c), mem.s32(level + 0x20)
    rooms, room = [], mem.r32(level + 0x10)
    while room:
        rooms.append(room)
        room = mem.r32(room + 0x24)
    out = [f"level {mem.r32(level + 0x1d0)} seed {mem.r32(mem.r32(level + 0x1b4) + 0x458)}"]
    for room in sorted(rooms, key=lambda r: (mem.s32(r + 0x38), mem.s32(r + 0x34))):
        where = f"room {mem.s32(room + 0x34) - x0},{mem.s32(room + 0x38) - y0}"
        tiles = mem.r32(room + 0x54)
        if not tiles:
            out.append(f"{where} not loaded")
            continue
        dt1s = [find(mem.r32(room + 0x68 + 4 * k))[0] for k in range(32) if mem.r32(room + 0x68 + 4 * k)]
        out.append(f"{where} mask {mem.r32(room + 0x50):x} dt1s {' '.join(dt1s)}")
        for kind, count, array in (("wall", 0x08, 0x14), ("floor", 0x0c, 0x1c), ("shadow", 0x10, 0x24)):
            entries = mem.r32(tiles + array)
            for k in range(mem.r32(tiles + count)):
                entry = entries + 0x30 * k
                dt1, index = find(mem.r32(entry + 0x18))
                out.append(f" {kind} {mem.s32(entry + 8)},{mem.s32(entry + 12)} o{mem.r32(entry + 0x1c)} {dt1}:{index}")
    return out


def rooms_of(lines: list[str]) -> dict[str, list[str]]:
    """A tiles dump by room: "room X,Y" -> its mask line and tile lines."""
    rooms, room = {}, None
    for line in lines:
        if line.startswith("room "):   # "room X,Y not loaded" ends the room before it too
            room = " ".join(line.split()[:2]) if " mask " in line else None
            if room:
                rooms[room] = []
        if room:
            rooms[room].append(line)
    return rooms


def town_check(live: list[str], d2d: list[str]) -> list[str]:
    """Each room the live game has up, against d2d's: match, or the first line that differs."""
    ours, out = rooms_of(d2d), [live[0]]
    for room, lines in rooms_of(live).items():
        mine = ours.get(room)
        if mine == lines:
            out.append(f"{room}: match ({len(lines) - 1} tiles)")
        elif mine is None:
            out.append(f"{room}: d2d has no such room")
        else:
            k = next((k for k, (a, b) in enumerate(zip(lines, mine)) if a != b), min(len(lines), len(mine)))
            out.append(f"{room}: game.exe {lines[k] if k < len(lines) else '(end)'!r} / d2d {mine[k] if k < len(mine) else '(end)'!r}")
    return out


# 800x600 menu clicks, title screen to a hosted TCP/IP game (1.14d).
HOST_CLICKS = [(400, 100),   # the title screen away (on the main menu: the logo, nothing)
               (400, 416),   # Other Multiplayer
               (400, 333),   # TCP/IP Game
               (400, 189),   # Host Game
               (690, 555)]   # OK: the first (only) character
DIFFICULTY = {"normal": (400, 280), "nightmare": (400, 323), "hell": (400, 366)}


def host(whisper: Whisper, difficulty: str = "normal") -> str:
    """Clicks through to a hosted game; ponytail: fixed waits, no screen reading. A character with only Normal open
    gets no difficulty box, and that click lands on the empty list."""
    difficulty = difficulty.lower()
    if difficulty not in DIFFICULTY:
        return f"err unknown difficulty {difficulty!r}: normal, nightmare or hell"
    for x_pos, y_pos in [*HOST_CLICKS, DIFFICULTY[difficulty]]:
        whisper.ask(f"click {x_pos} {y_pos}")
        time.sleep(2)
    whisper.conn.settimeout(120)   # loading the act keeps game.exe's thread (and whisper's reply) busy past 10 s
    for _ in range(30):
        if whisper.ask("state")[-1] == "ok":
            return "in game: " + whisper.ask("state")[0]
        time.sleep(1)
    return "err no player after 30 s (screenshot to see where it stopped)"


PARTY = {"invite": 6, "cancel": 7, "accept": 8, "leave": 9}   # C->S 0x5e (FUN_00478780)


def party(whisper: Whisper, action: str, player: int) -> list[str]:
    """C->S 0x5e: action, u32 player."""
    return whisper.ask(f"send 5e {PARTY[action]:02x} " + " ".join(f"{b:02x}" for b in player.to_bytes(4, "little")))


def to_screen(me: tuple[int, int], spot: tuple[float, float], lift: int = 0) -> tuple[int, int]:
    """A subtile on screen, relative to our feet (isometric: 16 px x, 8 px y a subtile)."""
    dx, dy = spot[0] - me[0], spot[1] - me[1]
    return int(FEET_X + (dx - dy) * 16), int(FEET_Y + (dx + dy) * 8 - lift)


def on_player(click: tuple[int, int], feet: tuple[int, int]) -> bool:
    """Within a player's sprite (feet up ~90 px): a click there asks them to trade."""
    return abs(click[0] - feet[0]) < 24 and -95 < click[1] - feet[1] < 12


def on_screen(point: tuple[int, int]) -> bool:
    return 10 <= point[0] <= 790 and 10 <= point[1] <= 540   # above the control panel


def follow(whisper: Whisper) -> None:
    """A merc: stay by the other player, fight what's near us both, drink when low.

    ponytail: one leader (the nearest other player), the left skill only,
    belt columns 1-4 in turn; warps and waypoints are left to the driver.
    """
    last_drink, belt, partied, last_seen = 0.0, 0, set(), None
    while True:
        state = dict(zip(*[iter(whisper.ask("state")[0].split())] * 2))
        if "x" not in state:
            time.sleep(1)
            continue
        me = (int(state["x"]), int(state["y"]))
        life, max_life = (int(v) for v in state["life"].split("/"))
        if max_life and life < max_life * DRINK_BELOW and time.time() - last_drink > 1.5:
            whisper.ask(f"key {0x31 + belt}")
            belt, last_drink = (belt + 1) % 4, time.time()

        units = [list(map(int, row.split())) for row in whisper.ask("units")[:-1]]
        players = [u for u in units if u[0] == 0 and u[1] != int(state["id"]) and u[3] not in PLAYER_GONE]
        leader = min(players, key=lambda u: math.dist(me, u[4:6]), default=None)
        if leader and math.dist(me, leader[4:6]) > GONE_PAST:   # still listed, but on another level
            leader = None
        if leader and leader[1] not in partied:   # take their invite if they made one, else make ours
            party(whisper, "accept", leader[1])
            party(whisper, "invite", leader[1])
            partied.add(leader[1])
            print(f"party with {leader[1]}", flush=True)
        foes = [u for u in units if u[0] == 1 and u[2] not in NOT_FOES and u[3] not in MONSTER_GONE
                and math.dist(me, u[4:6]) <= FIGHT_RANGE
                and (leader is None or math.dist(leader[4:6], u[4:6]) <= FIGHT_RANGE)]
        if leader:
            last_seen = tuple(leader[4:6])
        foe = min(foes, key=lambda u: math.dist(me, u[4:6]), default=None)
        if foe and on_screen(target := to_screen(me, foe[4:6], lift=24)):
            whisper.ask(f"move {target[0]} {target[1]}")   # let the client light it up first
            time.sleep(0.05)
            whisper.ask(f"click {target[0]} {target[1]}")
            print(f"fight {foe[1]} class {foe[2]} at {foe[4]},{foe[5]}", flush=True)
        elif leader is None and last_seen:   # they took a warp: to where they were, then the nearest warp
            # a warp (type 5) or a town portal (object class 59)
            warp = min((u for u in units if u[0] == 5 or (u[0] == 2 and u[2] == 59)), key=lambda u: math.dist(last_seen, u[4:6]), default=None)
            reach = 60 if warp and warp[0] == 2 else 30   # a town portal lands away from where they left
            if warp and math.dist(last_seen, warp[4:6]) < reach:
                target = to_screen(me, warp[4:6], lift=20)
                if on_screen(target):
                    whisper.ask(f"move {target[0]} {target[1]}")
                    time.sleep(0.1)
                    whisper.ask(f"click {target[0]} {target[1]}")   # walks to the mouth, but a posted click won't go in
                    time.sleep(2.5)
                    ids = " ".join(f"{b:02x}" for b in warp[1].to_bytes(4, "little"))
                    whisper.ask(f"send 13 {warp[0]:02x} 00 00 00 {ids}")   # interact: the host takes it within 5
                    print(f"warp {warp[1]} at {warp[4]},{warp[5]}", flush=True)
                    time.sleep(1.5)
        elif leader and math.dist(me, leader[4:6]) > FOLLOW_FROM:
            dx, dy = leader[4] - me[0], leader[5] - me[1]
            step = min(1.0, 12 / math.hypot(dx, dy))     # at most 12 subtiles a click
            short = max(0.0, 1 - 3 / math.hypot(dx, dy))  # stop beside them, not on them
            spot = (me[0] + dx * step * short, me[1] + dy * step * short)
            target = to_screen(me, spot)
            if on_screen(target) and not any(on_player(target, to_screen(me, u[4:6])) for u in players):
                whisper.ask(f"click {target[0]} {target[1]}")
                print(f"follow {leader[1]} at {leader[4]},{leader[5]} from {me[0]},{me[1]}", flush=True)
        time.sleep(0.3)


def main() -> None:
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    whisper = Whisper(sys.argv[1], sys.argv[2])
    verb, args = sys.argv[3], sys.argv[4:]
    if verb == "follow":
        try:
            follow(whisper)
        except KeyboardInterrupt:
            return
    if verb == "quests":
        print("\n".join(quests(whisper)))
        return
    if verb == "host":
        print(host(whisper, *args))
        return
    if verb == "town":   # town [D2D.txt]
        live = town(whisper)
        print("\n".join(live if not args else town_check(live, open(args[0]).read().splitlines())))
        return
    if verb == "party":   # party invite|cancel|accept|leave PLAYER_ID
        print("\n".join(party(whisper, args[0], int(args[1]))))
        return
    if verb == "screenshot":
        whisper.conn.sendall(b"screenshot\n")
        head = whisper.reader.readline().decode()
        if not head.startswith("ok "):
            sys.exit(head.strip())
        with open(args[0] if args else "whisper.bmp", "wb") as out:
            out.write(whisper.reader.read(int(head.split()[1])))
        print(head.strip())
        return
    print("\n".join(whisper.ask(" ".join([verb, *args]))))


if __name__ == "__main__":
    main()
