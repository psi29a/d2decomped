#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Acts 1-5's quests end to end: drive a headless d2d over devctl.

Usage: quests_d2d.py <path-to-d2d> [acts]

`acts` picks the acts to play, "1234" by default; each one starts on its
own from a new character (ctest runs quests_act1..4 side by side).

A new Amazon (made sturdy: 400 Vitality) plays Act 1's six quests
through the World: the NPC talks, the quest objects (Tree of Inifuss,
Cairn Stones, Cain's Gibbet, the Moldy Tome, the Malus), the kills, the
rewards (Charsi's imbue on the held item), Warriv's way east to Lut Gholein, checking
each quest's flag bits
(docs/research/re/quests.md, act1-end.md). Travel is `debug goto`; each
level's monsters go by `debug kill`. Skips without the MPQs or the 1.14d
patch (the quest objects' tables).
"""
import os
import platform
import re
import socket
import subprocess
import sys
import tempfile
import time

d2d = sys.argv[1]
acts = sys.argv[2] if len(sys.argv) > 2 else "12345"
if not acts or set(acts) - set("12345"):
    sys.exit(f"acts must be digits from 1-5, got {acts!r}")
data = os.environ.get("D2_MPQ_DIR") or os.path.expanduser("~/Workspace/private/diablo2")
patch = os.environ.get("D2_PATCH_INSTALLER")
if not os.path.exists(os.path.join(data, "d2data.mpq")) or not patch or not os.path.exists(patch):
    print("SKIP: the MPQs or D2_PATCH_INSTALLER not found")
    sys.exit(0)

home = tempfile.mkdtemp(prefix="d2d_quests_")
user = os.path.join(home, "Library/Preferences/d2d" if platform.system() == "Darwin" else ".config/d2d")
os.makedirs(os.path.join(user, "save"))
with open(os.path.join(user, "d2d.cfg"), "w") as f:
    f.write(f"patch = {patch}\n")
sock_path = os.path.join(tempfile.gettempdir(), f"d2d_quests_{os.getpid()}.sock")
log_path = os.path.join(home, "d2d.log")
env = dict(os.environ, HOME=home)
env.pop("D2_MPQ_DIR", None)
log = open(log_path, "w")  # noqa: SIM115 - d2d writes to it for the whole run


def log_text():
    with open(log_path) as text:
        return text.read()


proc = subprocess.Popen([d2d, "--headless", "--seed", "3", "--data", data, "--devctl", sock_path, "--start-screen", "ingame",
                         "--start-class", "0", "--start-name", "Quester", "--no-save", "--no-video"], env=env, stdout=log, stderr=log)


def cmd(line):
    with socket.socket(socket.AF_UNIX) as s:
        s.settimeout(60)
        s.connect(sock_path)
        s.sendall((line + "\n").encode())
        out = ""
        while not out.endswith("\n") or not out.rsplit("\n", 2)[-2].startswith(("ok", "err")):
            chunk = s.recv(65536)
            assert chunk, f"{line!r}: connection closed after {out!r}"
            out += chunk.decode()
    assert not out.rsplit("\n", 2)[-2].startswith("err"), f"{line!r} -> {out!r}"
    return out


def state():
    return dict(re.findall(r"(\w+)=(.*?)(?= \w+=|$)", cmd("state").split("\n")[0]))


def bits(quest):
    return int(cmd(f"debug quest {quest} show").split("bits=")[1].split()[0], 16)


def until(what, test, seconds=20):
    deadline = time.time() + seconds
    while not test():
        assert time.time() < deadline, f"timed out: {what}"
        time.sleep(0.2)


def level():
    return int(cmd("debug level").split()[1])


def alive():
    rows = [line.split("\t") for line in cmd("monsters").splitlines() if line and not line.startswith("ok")]
    return [row for row in rows if len(row) > 5 and not row[5].startswith("0/")]


def go(level_id):
    cmd(f"debug goto {level_id}")
    until(f"arrive on level {level_id}", lambda: level() == level_id)
    cmd("debug kill")


def presets():
    objects, rooms = [], []
    for line in cmd("debug presets").splitlines():
        fields = line.split("\t")
        if fields[0] == "room": rooms.append((int(fields[1]), float(fields[2]), float(fields[3])))
        elif len(fields) == 6: objects.append((int(fields[0]), int(fields[1]), float(fields[2]), float(fields[3]), fields[5]))
    return objects, rooms


def warp_to_room(lvlprest):
    room = [r for r in presets()[1] if r[0] == lvlprest]
    assert room, f"no LvlPrest {lvlprest} room on level {level()}"
    cmd(f"debug warp {room[0][1]} {room[0][2]}")
    cmd("debug kill")
    time.sleep(1)                                   # its rooms come up
    cmd("debug kill")


def object_index(object_id):
    found = [o for o in presets()[0] if o[1] == object_id]
    assert found, f"no object {object_id} on level {level()}"
    return found[0][0]


def find_npc(name):
    """`npcs` lists the ones by the player: walk the level (Lut Gholein's wide) till `name` is."""
    width, height = map(float, cmd("debug level").split()[4:6])
    for spot in [None] + [(x, y) for y in range(4, int(height), 10) for x in range(4, int(width), 10)]:
        if spot:
            cmd(f"debug warp {spot[0]} {spot[1]}")
            time.sleep(0.2)
        npc = [l.split("\t") for l in cmd("npcs").splitlines() if l.split("\t")[0].endswith(name)]
        if npc: return npc
    raise AssertionError(f"{name} not on level {level()}")


def talk(name):
    """Walk up to a camp NPC, talk, close the talk (a quest given counts as it closes)."""
    npc = find_npc(name)
    cmd(f"debug warp {float(npc[0][5]) + 0.9} {float(npc[0][6]) + 0.7}")
    cmd(f"cmd interact {npc[0][4]}")
    until(f"{name} speaks", lambda: state()["speech"] != "0")
    cmd("key Escape")
    until(f"{name}'s talk closes", lambda: state()["speech"] == "0")


def talk_until(name, test, what):
    """Talk to `name` until `test` holds: one greeting plays a talk (FUN_004a10e0, the first
    kind-0 message), so an NPC with several quests' lines takes a talk each."""
    for _ in range(4):
        talk(name)
        if test(): return
    raise AssertionError(f"{name}: {what}: {cmd('debug quest 3 show')}")


def sweep(width, height):
    """Walk the level a room at a time (its rooms come up as the player nears), killing as they do."""
    for y in range(4, height, 8):
        for x in range(4, width, 8):
            cmd(f"debug warp {x} {y}")
            time.sleep(0.25)
            cmd("debug kill")


def menu_pick(text):
    line = [l.split("\t") for l in cmd("menu").splitlines() if l.startswith(text + "\t")]
    assert line, f"no {text!r} in the menu: {cmd('menu')!r}"
    cmd(f"click {line[0][1]} {line[0][2]}")


def service(name, slot, done, what):
    """The NPC menu's entry before Cancel (imbue, sockets, personalize), then a
    click on the item worn in body slot `slot`, as a player would; worn again after."""
    item = re.search(rf"^\[(\w+) loc=1 slot={slot} .* id=(\d+) ", cmd("items"), re.MULTILINE)
    assert item, f"nothing worn in slot {slot}"
    open_menu(name)
    line = [l.split("\t") for l in cmd("menu").splitlines() if "\t" in l][-2]
    cmd(f"click {line[1]} {line[2]}")
    x, y = cmd(f"debug itemat {item.group(2)}").split()[1:3]
    cmd(f"click {x} {y}")
    until(what, done)
    cmd(f"click {x} {y}")                             # worn again
    time.sleep(0.3)
    for spot in range(40):                            # else into the pack
        if state()["held"] == "-":
            break
        cmd(f"cmd grid {spot % 10} {spot // 10}")
        time.sleep(0.3)
    assert state()["held"] == "-", f"the {item.group(1)} found no room"
    return item.group(1)


def open_menu(name):
    npc = find_npc(name)
    cmd(f"debug warp {float(npc[0][5]) + 0.9} {float(npc[0][6]) + 0.7}")
    cmd(f"cmd interact {npc[0][4]}")
    until(f"{name}'s menu", lambda: state()["menu"] != "0" or state()["speech"] != "0")
    if state()["speech"] != "0":
        cmd("key Escape")
        until(f"{name}'s talk closes", lambda: state()["speech"] == "0")
        cmd(f"cmd interact {npc[0][4]}")
        until(f"{name}'s menu", lambda: state()["menu"] != "0")


def topic(name):
    """A quest topic (kind 2) under the NPC's Talk: heard as it plays."""
    open_menu(name)
    first = cmd("menu")
    menu_pick("talk")
    try:
        until(f"{name}'s talk submenu", lambda: any(l.startswith(("introduction\t", "gossip\t")) for l in cmd("menu").splitlines()))
    except AssertionError:
        raise AssertionError(f"{name}: no talk submenu; menu was {first!r}, now {cmd('menu')!r}, {cmd('debug quest 6 show')}")
    topics = [l.split("\t") for l in cmd("menu").splitlines()[1:] if l.split("\t")[0] not in ("talk", "introduction", "gossip", "cancel", "ok")]
    assert topics, f"{name} has no quest topic: {cmd('menu')!r}"
    cmd(f"click {topics[0][1]} {topics[0][2]}")
    until(f"{name} speaks", lambda: state()["speech"] != "0")
    cmd("key Escape")
    until(f"{name}'s talk closes", lambda: state()["speech"] == "0")


def pick_up(code, seconds=20):
    """Walk onto a ground item and take it into the inventory."""
    ground = cmd("ground")
    found = re.search(rf"^{code}\t.*?\t#(\d+)\t(\S+)\t(\S+)$", ground, re.M | re.S)   # a label can run over lines
    assert found, f"no {code} on the ground: {ground!r}"
    cmd(f"debug warp {found[2]} {found[3]}")
    cmd(f"cmd pickup {found[1]}")
    until(f"{code} carried", lambda: f"[{code} " in cmd("items"), seconds)


def operate(index, seconds=4):
    cmd(f"cmd interact {index}")
    time.sleep(seconds)


def act1():
    # The Den of Evil (quest 1).
    talk("Akara")
    assert bits(1) & 0x4, f"Akara didn't give the Den: {bits(1):#x}"
    go(8)
    sweep(48, 48)
    until("the Den cleared", lambda: bits(1) & 0x2002 == 0x2002)
    go(1)
    points = int(state()["skillpts"])
    talk("Akara")
    assert bits(1) & 1 and int(state()["skillpts"]) == points + 1, f"Den not rewarded: {bits(1):#x}"
    print("OK: The Den of Evil")

    # Sisters' Burial Grounds (quest 2): the chain opens it once the Den's done.
    talk("Kashya")
    assert bits(2) & 0x4, f"Kashya didn't give quest 2: {bits(2):#x}"
    go(17)
    until("Blood Raven's death counts", lambda: bits(2) & 0x2002 == 0x2002)
    go(1)
    talk("Kashya")
    assert bits(2) & 1 and not bits(2) & 2, f"quest 2 not done: {bits(2):#x}"
    assert state()["merc"] != "-", "Kashya's rogue didn't join"
    # Shift + a belt key: the merc drinks it (FUN_00562390).
    def belt():
        return sum(1 for line in cmd("items").splitlines() if " loc=2 " in line)
    before = belt()
    cmd("cmd belt 0 merc")
    until("the merc drinks a belt potion", lambda: belt() == before - 1)
    print("OK: Sisters' Burial Grounds")

    # The Search for Cain (quest 4).
    talk("Akara")
    assert bits(4) & 0x4, f"Akara didn't give quest 4: {bits(4):#x}"
    go(5)
    warp_to_room(161)                                # Act 1 - Inifus
    operate(object_index(30))                        # the Tree of Inifuss
    scroll = [l.split("\t") for l in cmd("ground").splitlines() if l.startswith("bks\t")]
    assert scroll, "no Scroll of Inifuss"
    cmd(f"cmd pickup {scroll[0][4].lstrip('#')}")
    until("the scroll carried", lambda: "bks" in cmd("items"))
    go(1)
    talk("Akara")
    assert "bkd" in cmd("items"), "Akara didn't decipher the scroll"
    go(4)
    warp_to_room(160)                                # Act 1 - Cairn Stones
    stones = {o[1]: o[0] for o in presets()[0] if 17 <= o[1] <= 21}
    operate(stones[17])                              # draws the order
    order = re.findall(r"the stones' order (\d+) (\d+) (\d+) (\d+) (\d+)", log_text())
    assert order, "no stone order"
    for stone in order[-1]:
        operate(stones[int(stone)], 3)
    until("the way to Tristram", lambda: any(l.startswith("2\t") for l in cmd("debug portals").splitlines()))
    cmd("cmd interact -2002")
    until("Tristram", lambda: level() == 38, 30)
    cmd("debug kill")
    gibbet = object_index(26)
    gibbet_at = next(o for o in presets()[0] if o[0] == gibbet)
    cmd(f"debug warp {gibbet_at[2] + 1.2} {gibbet_at[3] + 1.1}")
    cmd("debug kill")
    operate(gibbet)
    until("Cain's rescue counts", lambda: bits(4) & 0x2002 == 0x2002, 30)
    until("Cain walks into his portal", lambda: "Cain has gone to the camp" in log_text(), 60)   # leaving first, he stays (the level stops)
    go(1)
    talk("Akara")
    assert bits(4) & 1 and not bits(4) & 2, f"quest 4 not done: {bits(4):#x}"
    print("OK: The Search for Cain")

    # The Forgotten Tower (quest 5): the Moldy Tome in the Stony Field.
    go(4)
    warp_to_room(162)                                # Act 1 - Tower Tome
    operate(object_index(8))
    until("the tome's text", lambda: state()["speech"] != "0")
    cmd("key Escape")
    until("quest 5 given", lambda: bits(5) & 0x4)
    go(25)
    until("the Countess's death counts", lambda: bits(5) & 1, 30)
    assert re.search(r"^r\d\d\t", cmd("ground"), re.MULTILINE), "the Countess dropped no rune"
    print("OK: The Forgotten Tower")

    # Tools of the Trade (quest 3): Charsi gives it from character level 8.
    go(1)
    cmd("debug stat 12 8")
    talk_until("Charsi", lambda: bits(3) & 0x4, "didn't give quest 3")
    go(28)
    forge = [r for r in presets()[1] if 202 <= r[0] <= 205]   # Act 1 - Barracks Forge W / E / S / N
    assert forge, "no Barracks Forge"
    warp_to_room(forge[0][0])
    operate(object_index(108))                        # the Malus's stand
    malus = [l.split("\t") for l in cmd("ground").splitlines() if l.startswith("hdm\t")]
    assert malus, "no Horadric Malus"
    cmd(f"cmd pickup {malus[0][4].lstrip('#')}")
    until("the malus carried", lambda: "hdm" in cmd("items"))
    go(1)
    talk_until("Charsi", lambda: bits(3) & 0x2, "the imbue isn't due")
    weapon = re.search(r"^\[(\w+) loc=1 slot=4 .* id=(\d+) ", cmd("items"), re.MULTILINE)
    assert weapon, "no weapon worn"
    open_menu("Charsi")
    menu_pick("imbue")                                # the inventory opens; the next item click imbues it
    x, y = cmd(f"debug itemat {weapon.group(2)}").split()[1:3]
    cmd(f"click {x} {y}")
    until("Charsi imbued", lambda: bits(3) & 1)
    assert "Charsi imbued the " + weapon.group(1) in log_text(), "not the axe"
    for col in range(10):                             # back into the pack, as a player would place it
        cmd(f"cmd grid {col} 0")
        time.sleep(0.3)
        if state()["held"] == "-":
            break
    assert state()["held"] == "-", "the axe found no room"
    print("OK: Tools of the Trade")

    # Sisters to the Slaughter (quest 6): Cain sends the player; Andariel; Warriv east.
    # (Camp Cain stays through camp arrivals: the last one came from the Barracks.)
    if os.environ.get("QUESTS_STOP") == "6":                # debugging: leave d2d up at this point
        print("STOP", sock_path, log_path, flush=True)
        time.sleep(1800)
    topic("Cain")
    assert bits(6) & 0x4, f"Cain didn't give quest 6: {cmd('debug quest 6 show')}"
    go(37)
    until("Andariel's death counts", lambda: bits(6) & 0x2, 30)
    go(2)                                               # the Blood Moor cleared: within the act it stays so
    time.sleep(1)
    assert not alive(), f"the Blood Moor wasn't cleared: {len(alive())} left"
    go(1)
    open_menu("Warriv")
    assert any(l.split("\t")[0].lower().startswith(("go east", "east")) for l in cmd("menu").splitlines()), f"no way east: {cmd('menu')!r}"
    print("OK: Sisters to the Slaughter (Warriv offers the way east)")
    east = [l.split("\t") for l in cmd("menu").splitlines() if l.split("\t")[0].lower().startswith(("go east", "east"))]
    cmd(f"click {east[0][1]} {east[0][2]}")
    until("the caravan lands in Lut Gholein", lambda: level() == 40, 30)
    assert "through Warriv's caravan" in log_text(), "no caravan arrival logged"
    print("OK: Warriv's caravan lands in Lut Gholein")
    open_menu("Warriv")                                 # Lut Gholein's Warriv: back west
    menu_pick("go west")
    until("the caravan lands in the camp", lambda: level() == 1, 30)
    print("OK: Warriv's caravan goes back west")
    # Act 1 was left and stays as it was: game.exe's server keeps its acts
    # until the game ends (FUN_0052c7f0), so the Blood Moor is still clear.
    cmd("debug goto 2")
    until("back in the Blood Moor", lambda: level() == 2)
    time.sleep(2)
    assert not alive(), f"the Blood Moor came back after the trip east: {len(alive())} alive"
    print("OK: an act left stays as it was (the Blood Moor still clear)")


def act2():
    # Act 2's quests (docs/research/re/quests-act2.md), from a new character's
    # first steps in Lut Gholein. Quest rooms are found by their LvlPrest
    # (the W / E / S / N four of each), not by a sweep.
    def show(quest, key):
        return int(cmd(f"debug quest {quest} show").split(key + "=")[1].split()[0])

    def quest_room(first):
        room = [r for r in presets()[1] if first <= r[0] <= first + 3]
        assert room, f"no LvlPrest {first}..{first + 3} room on level {level()}"
        warp_to_room(room[0][0])

    def chest(object_id, code):
        """The quest chest in the room the player's in: open it, take its part."""
        found = [o for o in presets()[0] if o[1] == object_id]
        assert found, f"no object {object_id} on level {level()}"
        cmd(f"debug warp {found[0][2]} {found[0][3] + 1}")
        cmd("debug kill")
        operate(found[0][0])
        pick_up(code)

    def item_id(code):
        return re.search(rf"^\[{code} .*?id=(\d+)", cmd("items"), re.M)[1]

    def take_portal(slot, to):
        portal = [l.split("\t") for l in cmd("debug portals").splitlines() if l.startswith(f"{slot}\t")]
        assert portal, f"no portal {slot}: {cmd('debug portals')!r}"
        cmd(f"debug warp {portal[0][2]} {portal[0][3]}")
        cmd(f"cmd interact {-2000 - slot}")
        until(f"through portal {slot} to level {to}", lambda: level() == to, 30)

    go(40)
    # Act 2's opening: Jerhyn's greeting (quest 8 bit 0) moves him into the palace.
    talk("Jerhyn")
    assert bits(8) & 1, f"Jerhyn's greeting not heard: {bits(8):#x}"
    print("OK: Jerhyn's greeting (act 2's opening)")

    # a2q1: Radament's Lair. Atma tells (bit 2); Radament in Sewers 3 (49)
    # drops the Book of Skill (+1 skill point); Atma's thanks: done. The
    # chest in his room (object 355) gives the Horadric Scroll.
    talk("Atma")
    assert bits(9) & 4, f"Atma didn't tell of Radament: {bits(9):#x}"
    go(47)
    go(49)
    quest_room(341)
    until("Radament's death counts", lambda: bits(9) & 0x2002 == 0x2002)
    pick_up("ass")
    points = int(state()["skillpts"])
    cmd(f"cmd use {item_id('ass')}")
    until("the book's skill point", lambda: int(state()["skillpts"]) == points + 1)
    chest(355, "tr1")                                   # in Radament's room
    go(40)
    talk_until("Atma", lambda: bits(9) & 1, "Atma didn't close Radament's Lair")
    print("OK: Radament's Lair, the Book of Skill (a2q1)")

    # a2q2 starts: Cain reads the scroll (bit 3, the scroll gone).
    talk_until("Cain", lambda: bits(10) & 8, "Cain didn't read the scroll")
    assert "[tr1 " not in cmd("items"), "the scroll stayed"

    # a2q3: The Tainted Sun. The Lost City's dark; the altar in the Claw
    # Viper Temple (61) brings the sun back and gives the Viper Amulet.
    go(44)
    until("the sun goes dark", lambda: "dark=true" in cmd("debug quest 11 show"), 10)
    go(61)
    altar = [o for o in presets()[0] if o[1] == 149]
    assert altar, "no Tainted Sun altar"
    cmd(f"debug warp {altar[0][2] + 1} {altar[0][3] + 1}")
    cmd("debug kill")
    operate(altar[0][0])
    assert bits(11) & 0x2002 == 0x2002 and "dark=false" in cmd("debug quest 11 show"), f"the altar: {bits(11):#x}"
    pick_up("vip")
    print("OK: the Tainted Sun's dark and its altar (a2q3)")

    # a2q2: The Horadric Staff. The shaft in the Maggot Lair (64), the Cube
    # in the Halls of the Dead (60); Cain sees them (bits 4..6); the Cube
    # makes the staff, which opens the Arcane Sanctuary (quest 12, state 1).
    go(64)
    quest_room(505)
    chest(356, "msf")
    go(60)
    quest_room(456)
    chest(354, "box")
    go(40)
    talk_until("Cain", lambda: bits(10) & 0x70 == 0x70, "Cain didn't see the parts")
    for code, col in (("vip", 0), ("msf", 1)):
        cmd(f"cmd hand {item_id(code)}")
        until(f"{code} in hand", lambda: state()["held"] == code)
        cmd(f"cmd grid {col} 0 cube")
        until(f"{code} in the Cube", lambda: state()["held"] == "-")
    cmd("cmd transmute")
    until("the Horadric Staff", lambda: "[hst " in cmd("items"))
    assert show(12, "arcane") == 1, "the staff didn't open the Arcane Sanctuary"
    print("OK: the Horadric Staff made (a2q2)")

    # a2q3 done by Drognan's thanks; a2q4: Drognan (state 2), Jerhyn (3),
    # the Sanctuary (4); a2q5: the Summoner dies; Horazon's Journal (object
    # 357): done, and the portal to the Canyon of the Magi.
    talk_until("Drognan", lambda: show(12, "arcane") == 2 and bits(11) & 1, "Drognan")
    talk_until("Jerhyn", lambda: show(12, "arcane") == 3, "Jerhyn didn't open the palace")
    go(74)
    assert show(12, "arcane") == 4, "the Sanctuary didn't mark quest 12"
    quest_room(525)
    until("the Summoner's death counts", lambda: show(13, "summoner") == 2, 10)
    journal = [o for o in presets()[0] if o[1] == 357]
    assert journal, f"no Horazon's Journal on level {level()}"
    cmd(f"debug warp {journal[0][2] + 1} {journal[0][3] + 1}")
    operate(journal[0][0])
    assert bits(12) & 1 and show(12, "arcane") == 5, f"the journal: {bits(12):#x}"
    print("OK: Tainted Sun closed, the Arcane Sanctuary, the Summoner (a2q3, a2q4, a2q5)")

    # a2q6: The Seven Tombs. The Canyon (state 1); the staff in the true
    # tomb's orifice (quest 10 done), its way down to Duriel; his death (3),
    # Tyrael (bit 3, his portal home), Jerhyn (bit 4), Meshif: done, and
    # Meshif sails east.
    take_portal(4, 46)
    go(show(14, "true"))
    quest_room(460)
    chest_room = [o for o in presets()[0] if o[1] == 152]
    assert chest_room, "no orifice"
    cmd(f"debug warp {chest_room[0][2] + 1} {chest_room[0][3] + 1}")
    cmd("debug kill")
    operate(chest_room[0][0])
    assert bits(10) & 1 and "[hst " not in cmd("items"), f"the orifice didn't take the staff: {bits(10):#x}"
    take_portal(6, 73)
    cmd("debug kill")
    until("Duriel's death counts", lambda: show(14, "tombs") == 3, 10)
    talk_until("Tyrael", lambda: bits(14) & 8, "Tyrael")
    take_portal(0, 40)
    talk_until("Atma", lambda: bits(13) & 1, "no thanks for the Summoner")   # the town's 419..429
    talk_until("Jerhyn", lambda: bits(14) & 0x10, "Jerhyn's reward")
    talk_until("Meshif", lambda: bits(14) & 1, "Meshif didn't close the Seven Tombs")
    print("OK: the orifice, Duriel, Tyrael, the Seven Tombs (a2q2, a2q6)")
    open_menu("Meshif")
    menu_pick("sail east")
    until("Meshif's boat lands in the Kurast Docks", lambda: level() == 75, 30)
    print("OK: Meshif sails east")


def act3():
    # a3q7: the Dark Wanderer (MonAI 91) stands by the Spider Forest's head
    # till the player's within 20 subtiles, walks 20 north, and is gone once
    # the player's near at his next think; every player in act 3 gets quest
    # word 0x20 bit 0. Map seed 3: his marker (object 368) at cell (28.6,
    # 187.6), he at its x + 7 subtiles (drlg-dump 3 76 units).
    def wanderer():
        rows = [line.split("\t") for line in cmd("monsters").splitlines() if line.startswith("darkwanderer\t")]
        return (float(rows[0][1]), float(rows[0][2])) if rows else None
    cmd("debug goto 76")
    until("in the Spider Forest", lambda: level() == 76, 60)
    cmd("debug warp 22 190")                            # some 40 subtiles off: he waits
    until("the Dark Wanderer made", lambda: wanderer() is not None, 30)
    start = wanderer()
    time.sleep(2)
    assert wanderer() == start, f"the Dark Wanderer moved with no one near: {start} -> {wanderer()}"
    cmd(f"debug warp {int(start[0])} {int(start[1]) + 2}")
    until("the Dark Wanderer sets off north", lambda: wanderer() is None or wanderer()[1] < start[1] - 0.3, 20)
    cmd(f"debug warp {int(start[0])} {int(start[1]) - 2}")   # by his spot (the merc may have held him up: he tries 3 more times)
    until("the Dark Wanderer gone", lambda: wanderer() is None, 30)
    assert int(re.search(r"bits=0x([0-9a-f]+)", cmd("debug quest 32 show")).group(1), 16) & 1, "word 0x20 bit 0 not set"
    until("his afterglow (FUN_005bd390)", lambda: re.search(r"the Dark Wanderer's afterglow: [78]\b", log_text()), 5)
    print("OK: the Dark Wanderer walks off and is gone (a3q7)")
    # a3q0: Hratli's greeting (word 0x10 bit 0).
    go(75)
    talk_until("Hratli", lambda: bits(0x10) & 1, "no greeting")
    # a3q1: Lam Esen's Tome. Lower Kurast opens it, Alkor tells (bit 2), its
    # stand in the Ruined Temple (94) drops it, Alkor takes it: done, +5 stat points.
    go(79)
    go(75)
    talk_until("Alkor", lambda: bits(0x11) & 4, "Lam Esen's Tome not given")
    go(94)
    stand = object_index(193)
    stand_at = [o for o in presets()[0] if o[0] == stand][0]
    cmd(f"debug warp {stand_at[2]} {stand_at[3]}"); cmd("debug kill")
    operate(stand)
    pick_up("bbb")
    go(75)
    points = int(state()["statpts"])
    talk_until("Alkor", lambda: bits(0x11) & 1, "the tome not taken")
    assert int(state()["statpts"]) == points + 5, f"no 5 stat points: {points} -> {state()['statpts']}"
    print("OK: Hratli's greeting, Lam Esen's Tome (a3q0, a3q1)")
    # a3q4: The Golden Bird. The first boss made in act 3 carries the jade
    # figurine; Cain hears of it (bit 2), Meshif makes the golden bird, Cain
    # (bit 4), Alkor takes it (bit 1) and brews the Potion of Life: +20 life.
    for lid in (76, 77, 78, 80, 81):
        if "the jade figurine drops on level" in log_text(): break
        go(lid)
        time.sleep(1)
        cmd("debug kill")
    dropped = re.search(r"the jade figurine drops on level (\d+)", log_text())
    assert dropped, "no boss dropped the jade figurine"
    go(int(dropped.group(1)))
    pick_up("j34")
    go(75)
    talk_until("Cain", lambda: bits(0x14) & 4, "Cain didn't hear of the figurine")
    talk_until("Meshif", lambda: "g34" in cmd("items"), "no golden bird")
    talk_until("Cain", lambda: bits(0x14) & 0x10, "Cain didn't see the bird")
    talk_until("Alkor", lambda: bits(0x14) & 2, "Alkor didn't take the bird")
    talk_until("Alkor", lambda: bits(0x14) & 1, "no Potion of Life")
    life = int(state()["life"].split("/")[1])
    potion = [l for l in cmd("items").splitlines() if l.startswith("[xyz ")]
    assert potion, f"no Potion of Life: {cmd('items')!r}"
    cmd(f"cmd use {re.search(r'id=(\d+)', potion[0]).group(1)}")
    until("+20 life", lambda: int(state()["life"].split("/")[1]) >= life + 20, 10)
    print("OK: the Golden Bird (a3q4)")
    # a3q3: The Blade of the Old Religion. The Flayer Jungle opens it, Hratli
    # tells (bit 3), the altar (object 252) calls the guardians, their unique
    # drops the Gidbinn; Ormus takes it (bit 6) and gives a ring (bit 8),
    # Asheara a free mercenary (bit 7): done.
    go(78)
    go(75)
    talk_until("Hratli", lambda: bits(0x13) & 8, "Hratli didn't tell of the Gidbinn")
    go(78)
    cmd("debug warp 21 52"); time.sleep(1); cmd("debug kill")   # map seed 3: the altar's room (16, 48), 27 / 22 subtiles in
    altar = object_index(252)
    altar_at = [o for o in presets()[0] if o[0] == altar][0]
    cmd(f"debug warp {altar_at[2]} {altar_at[3]}"); cmd("debug kill")
    operate(altar)
    until("the guardians", lambda: "the guardians come" in log_text(), 10)
    time.sleep(1)
    cmd("debug kill")
    until("the Gidbinn drops", lambda: "the Gidbinn drops" in log_text(), 10)
    pick_up("g33")
    go(75)
    talk_until("Ormus", lambda: bits(0x13) & 0x40, "Ormus didn't take the Gidbinn")
    talk_until("Ormus", lambda: bits(0x13) & 0x100, "no ring from Ormus")
    assert re.search(r"^\[rin ", cmd("items"), re.MULTILINE), "no ring in the inventory"
    cmd("debug warp 7 18"); time.sleep(1)                # Asheara's room comes up (map seed 3: room (0, 16))
    talk_until("Asheara", lambda: bits(0x13) & 0x80, "no free mercenary")
    assert bits(0x13) & 1, f"the Blade not done: {bits(0x13):#x}"
    print("OK: the Blade of the Old Religion (a3q3)")
    # a3q2 / a3q5: Khalim's Will and the Blackened Temple. The Great Marsh
    # opens it, Cain tells (bit 2); the eye's chest (Spider Cavern, 85), the
    # brain's (Flayer Dungeon 3, 91), the heart's (sewers 2, 93, the lever in
    # 92 opening the stairs); the Council in Travincal drop the flail and,
    # dead, give bit 4; Khalim's Will in hand breaks the orb in two hits:
    # word 0x12 bit 0 and the Blackened Temple done.
    def objects(op):
        return [l.split("\t") for l in cmd("debug objects").splitlines() if l.split("\t")[3:4] == [f"op{op}"]]
    def khalim_chest(lid, op, code, near):
        go(lid)
        cmd(f"debug warp {near[0]} {near[1]}"); time.sleep(1)   # its room comes up (map seed 3)
        chest = objects(op)
        assert chest, f"no OperateFn {op} on level {lid}: {cmd('debug objects')!r}"
        cmd(f"debug warp {chest[0][1]} {chest[0][2]}"); time.sleep(1); cmd("debug kill")
        cmd(f"debug operate {chest[0][0]}")
        pick_up(code)
    go(77)
    go(75)
    talk_until("Cain", lambda: bits(0x12) & 4, "Khalim's Will not told")
    khalim_chest(85, 58, "qey", (21, 26))
    khalim_chest(91, 59, "qbr", (3, 3))
    go(92)
    cmd("debug warp 54 30"); time.sleep(1)           # the lever's room (48, 24)
    lever = objects(45)
    assert lever, "no sewers lever"
    cmd(f"debug operate {lever[0][0]}")
    until("the stairs open", lambda: "the sewers' stairs open" in log_text(), 5)
    khalim_chest(93, 57, "qhr", (8, 3))
    go(83)
    cmd("debug warp 27 19"); time.sleep(2)
    for _ in range(5):
        cmd("debug kill"); time.sleep(1)
        if "the Council is dead" in log_text(): break
    until("the Council dead", lambda: "the Council is dead" in log_text(), 10)
    assert bits(0x15) & 0x10, f"no bit 4 for the Council: {bits(0x15):#x}"
    pick_up("qf1")
    go(75)
    talk_until("Cain", lambda: bits(0x12) & 0x20, "Cain didn't see the flail")
    cmd("debug equip qf2 4")                         # the Cube's made it (cubemain, generic)
    go(83)
    orb = objects(53)
    assert orb, "no compelling orb"
    cmd(f"debug warp {orb[0][1]} {orb[0][2]}")
    cmd(f"debug operate {orb[0][0]}"); cmd(f"debug operate {orb[0][0]}")
    until("the orb broken", lambda: "the compelling orb broken" in log_text(), 5)
    assert bits(0x12) & 1 and bits(0x15) & 1, f"Khalim's Will {bits(0x12):#x}, the Temple {bits(0x15):#x}"
    print("OK: Khalim's Will, the Blackened Temple (a3q2, a3q5)")
    # a3q6: Mephisto dead (FUN_005bc8b0) sets word 0x16 bits 0, 0xb, 0xd and
    # opens the hellgate; a player by the bridge (within 18) raises it, up
    # the next check (FUN_005bcac0). Map seed 3: it's by the level's middle.
    cmd("debug goto 102")
    until("in the Durance of Hate 3", lambda: level() == 102, 60)
    until("Mephisto made", lambda: any(l.startswith("mephisto\t") for l in cmd("monsters").splitlines()), 30)
    cmd("debug kill")
    until("the hellgate opens", lambda: "Mephisto is dead, the hellgate opens" in log_text(), 10)
    assert int(re.search(r"bits=0x([0-9a-f]+)", cmd("debug quest 22 show")).group(1), 16) & 0x2801 == 0x2801, "word 0x16 bits 0 / 0xb / 0xd not set"
    until("the bridge is up", lambda: "the bridge is up" in log_text(), 10)
    assert bits(0x16) & 0x800, f"no report due: {bits(0x16):#x}"
    go(75)
    talk_until("Ormus", lambda: not bits(0x16) & 0x800, "the report not taken")
    print("OK: Mephisto dead, the hellgate open, the bridge up, the report (a3q6)")


def act4():
    # The hellgate (OperateFn 46, FUN_00584750): Mephisto dead (word 0x16
    # bit 0 here) lets the player through; from the Durance's third level
    # word 0x17 gets bits 0 and 13 (FUN_00546ac0), the fortress's waypoint,
    # and the player stands at the Pandemonium Fortress's start.
    cmd("debug quest 22")
    go(102)
    gate = [l.split("\t") for l in cmd("debug objects").splitlines() if l.split("\t")[3:4] == ["op46"]]
    assert gate, f"no hellgate on level 102: {cmd('debug objects')!r}"
    cmd(f"debug warp {gate[0][1]} {gate[0][2]}")
    cmd(f"debug operate {gate[0][0]}")
    until("in the Pandemonium Fortress", lambda: level() == 103, 10)
    assert bits(0x17) & 0x2001 == 0x2001, f"word 0x17: {bits(0x17):#x}"
    print("OK: through the hellgate to the Pandemonium Fortress")

    def objects(op):
        return [l.split("\t") for l in cmd("debug objects").splitlines() if l.split("\t")[3:4] == [f"op{op}"]]
    def kill_until(what, done, seconds=30):
        deadline = time.time() + seconds
        while time.time() < deadline and not done():
            cmd("debug kill"); time.sleep(1)
        assert done(), f"{what}: {log_text()[-2000:]}"
    # a4q0: Tyrael's welcome (664) sets word 0x18 bit 0.
    talk_until("Tyrael", lambda: bits(0x18) & 1, "the welcome not heard")
    # a4q1: Tyrael gives it (670, bit 2); Izual dies on the Plains of Despair
    # (bits 13 and 1), his ghost speaks (675, bit 5); Tyrael's reward (676):
    # bit 0 and 2 skill points.
    talk_until("Tyrael", lambda: bits(0x19) & 4, "the Fallen Angel not given")
    cmd("debug goto 105")
    until("on the Plains of Despair", lambda: level() == 105, 60)
    cmd("debug warp 20 14")                              # his room (16, 8), him at 18, 18 in it (map seed 3: drlg-dump 3 105 units)
    until("Izual made", lambda: any(l.startswith("izual\t") for l in cmd("monsters").splitlines()), 30)
    izual = [l.split("\t") for l in cmd("monsters").splitlines() if l.startswith("izual\t")][0]
    cmd(f"debug warp {int(float(izual[1]))} {int(float(izual[2])) + 3}"); time.sleep(1)
    # His first think (FUN_005b4390): the Fallen Angel's log sent again (the Quest Log button).
    until("Izual's first think", lambda: "Izual's first think" in log_text(), 10)
    kill_until("Izual dead", lambda: "The Fallen Angel: Izual is dead" in log_text())
    assert bits(0x19) & 0x2002 == 0x2002, f"word 0x19: {bits(0x19):#x}"
    until("his ghost", lambda: "Izual's ghost" in log_text(), 10)
    talk_until("Izual", lambda: bits(0x19) & 0x20, "the ghost not heard")
    go(103)
    points = int(state()["skillpts"]) if "skillpts" in state() else None
    talk_until("Tyrael", lambda: bits(0x19) & 1, "the Fallen Angel not rewarded")
    assert "+2 skill points" in log_text(), "no skill points"
    print("OK: the welcome, the Fallen Angel (a4q0, a4q1)")
    # a4q3: Cain's soulstone (679); Hephasto drops the hammer in the River of
    # Flame; the forge takes the stone, then three blows of the hammer in hand;
    # Cain's reward (680).
    talk_until("Cain", lambda: "[mss " in cmd("items"), "no soulstone")
    cmd("debug goto 107")
    until("on the River of Flame", lambda: level() == 107, 60)
    cmd("debug warp 35 84"); time.sleep(1)               # the forge's room (32, 80), Hephasto there too (map seed 3)
    forge = objects(49)
    assert forge, f"no Hellforge: {cmd('debug objects')!r}"
    cmd(f"debug warp {forge[0][1]} {forge[0][2]}"); time.sleep(1)
    kill_until("Hephasto dead", lambda: "Hephasto drops the hammer" in log_text())
    cmd("debug equip hfh 4")
    cmd(f"debug warp {forge[0][1]} {forge[0][2]}")
    for _ in range(4): cmd(f"debug operate {forge[0][0]}")
    until("the forge struck", lambda: "the soulstone is shattered" in log_text(), 5)
    # The gems (FUN_005b6710): 22 frames on, then a round each 20 frames,
    # 4 (perfect) down to 1 (plain), on the quest seed; at the forge even
    # with the player gone.
    go(103)
    until("the gems", lambda: "The Hellforge: round 1," in log_text(), 10)
    assert [int(r) for r in re.findall(r"The Hellforge: round (\d)", log_text())] == [4, 3, 2, 1], "the gems' rounds"
    assert bits(0x1b) & 0x2002 == 0x2002, f"word 0x1b: {bits(0x1b):#x}"
    talk_until("Cain", lambda: bits(0x1b) & 1, "the Hellforge not rewarded")
    print("OK: the Hellforge (a4q3)")
    # a4q2: Tyrael (681); the five seals, their three bosses dead release
    # Diablo; his death sets bits 13 and 0; LoD, Tyrael's portal (20000, bit 9).
    talk_until("Tyrael", lambda: bits(0x1a) & 4, "Terror's End not given")
    cmd("debug goto 108")
    until("in the Chaos Sanctuary", lambda: level() == 108, 60)
    for arm in ((55, 32), (36, 60), (84, 60), (60, 60)):   # the seals' rooms (map seed 3: drlg-dump 3 108 units): N, W, E, the heart
        cmd(f"debug warp {arm[0]} {arm[1]}"); time.sleep(0.5); cmd("debug kill")
    seals = [o for op in (52, 54, 55, 56) for o in objects(op)]
    assert len(seals) == 5, f"seals: {seals}"
    for seal in seals:
        cmd(f"debug warp {seal[1]} {seal[2]}"); time.sleep(0.5)
        cmd(f"debug operate {seal[0]}")
        kill_until("its boss dead", lambda: True, 1)
    kill_until("the seals broken", lambda: "the seals are broken" in log_text())
    until("Diablo comes", lambda: "Terror's End: Diablo" in log_text(), 10)
    kill_until("Diablo dead", lambda: "Diablo is dead" in log_text())
    assert bits(0x1a) & 0x2001 == 0x2001, f"word 0x1a: {bits(0x1a):#x}"
    go(103)
    talk_until("Tyrael", lambda: bits(0x1a) & 0x200, "Tyrael's portal not given")
    until("the portal", lambda: "Tyrael opens the way" in log_text(), 5)
    portal = objects(73)                                    # Tyrael's last portal (OperateFn 73) to Harrogath
    assert portal, "no portal to Harrogath"
    cmd(f"debug warp {portal[0][1]} {portal[0][2]}"); time.sleep(0.5)
    cmd(f"debug operate {portal[0][0]}")
    until("in Harrogath", lambda: level() == 109, 30)
    assert bits(0x1c) & 0x2001 == 0x2001, f"word 0x1c: {bits(0x1c):#x}"
    print("OK: Terror's End (a4q2)")


def a5(key):
    """Act 5's record states (s) or logs (l) from `debug quest ... show`: [a5q1 .. a5q5]."""
    shown = cmd("debug quest 35 show")
    return [int(v) for v in re.search(rf"{'a5' if key == 's' else 'logs'}=(\S+)", shown)[1].split(",")]


def act5():
    # Harrogath, as Tyrael's portal leaves it (word 0x1c bits 0 and 13).
    cmd("debug quest 28")
    go(109)
    # a5q0: Malah's introduction (20039 to the barbarian) sets her intro bit and
    # opens Siege on Harrogath (a5q1 state 1).
    until("Siege shut", lambda: a5("s")[0] == 0, 2)
    talk("Malah")
    until("Siege opens", lambda: a5("s")[0] == 1, 5)
    # a5q1: Larzuk tells it (20077: state 2, bit 2); out of town state 3;
    # Shenk dies on the Bloody Foothills (bits 13 and 1); Larzuk's reward
    # (20090: bit 5) opens Rescue on Mount Arreat (a5q2 state 1).
    talk_until("Larzuk", lambda: bits(0x23) & 4, "the siege not given")
    go(110)
    assert a5("s")[0] == 3, f"a5q1 out of town: {a5('s')}"
    width = int(float(cmd("debug level").split()[4]))
    for x in range(4, width, 8):
        cmd(f"debug warp {x} 24"); time.sleep(0.2)
        shenk = [l.split("\t") for l in cmd("monsters").splitlines() if l.split("\t")[7:8] == ["superunique"]]
        if shenk: break
    assert shenk, "no Shenk on the Bloody Foothills"
    cmd(f"debug warp {int(float(shenk[0][1]))} {int(float(shenk[0][2])) + 2}"); time.sleep(0.5)
    deadline = time.time() + 30
    while time.time() < deadline and "Shenk is dead" not in log_text():
        cmd("debug kill"); time.sleep(0.5)
    assert bits(0x23) & 0x2002 == 0x2002, f"word 0x23: {bits(0x23):#x}"
    go(109)
    talk_until("Larzuk", lambda: bits(0x23) & 0x20, "the siege not rewarded")
    assert a5("s")[:2] == [5, 1], f"a5q1 rewarded, a5q2 open: {a5('s')}"
    # Larzuk's reward: his menu's socket entry (0x58dc) sockets the shield (bit 0).
    shield = service("Larzuk", 5, lambda: bits(0x23) & 1, "Larzuk socketed")
    assert f"Larzuk put 1 sockets in the {shield}" in log_text(), "not the shield, or not its one socket"
    print("OK: the introductions, Siege on Harrogath (a5q0, a5q1)")
    # a5q2: Qual-Kehk (20096, bit 2); the Frigid Highlands' three cages come
    # up with five prisoners each; a door broken frees its five, the player
    # by them opens its portal; the third, with all fifteen freed, sets bits
    # 13, 1 and 5. Qual-Kehk's reward (20110): three runes, bit 0.
    talk_until("Qual-Kehk", lambda: bits(0x24) & 4, "the rescue not given")
    cmd("debug goto 111")
    until("in the Frigid Highlands", lambda: level() == 111, 60)
    cages = [(39, 38), (23, 86), (39, 118)]             # object 473 (map seed 3: drlg-dump 3 111 units), its door 2 east
    for n, cage in enumerate(cages):
        cmd(f"debug warp {cage[0]} {cage[1] + 2}"); time.sleep(1)
        until(f"cage {n} up", lambda: log_text().count("Rescue on Mount Arreat: cage") >= n + 1, 10)
        cmd("debug kill spare act5pow within 12")
        until(f"cage {n}'s portal", lambda: log_text().count("'s portal (") >= n + 1, 10)
    assert bits(0x24) & 0x2022 == 0x2022, f"word 0x24: {bits(0x24):#x} {a5('s')} {a5('l')}"
    until("the last cage's prisoners through", lambda: log_text().count("a prisoner through the portal") >= 5, 30)
    go(109)
    runes = cmd("items").count("[r0")
    talk_until("Qual-Kehk", lambda: bits(0x24) & 1, "the rescue not rewarded")
    assert cmd("items").count("[r0") >= runes + 3 or "Qual-Kehk's 3 runes" in log_text(), "no runes"
    assert a5("s")[1:3] == [5, 1], f"a5q2 rewarded, a5q3 open: {a5('s')}"
    print("OK: Rescue on Mount Arreat (a5q2)")
    # a5q3: Malah (20116, bit 2); the Frozen River's marker makes frozen Anya
    # (fana) 25 frames on; without the potion she says 20131 (state 4);
    # Malah's potion (20127); poured on her, she thaws (bits 13, 1) and
    # Betrayal of Harrogath opens; Malah's scroll (20132, bit 8) sends Anya
    # home; Anya's item (20136, bit 9) closes it (bit 0).
    def fana():
        found = [l.split("\t") for l in cmd("debug objects").splitlines() if l.split("\t")[3:4] == ["op67"]]
        assert found, f"no frozen Anya: {cmd('debug objects')!r}"
        cmd(f"debug warp {found[0][1]} {float(found[0][2]) + 1}"); time.sleep(0.5)
        cmd(f"debug operate {found[0][0]}"); time.sleep(0.5)
    talk_until("Malah", lambda: bits(0x25) & 4, "the prison not given")
    cmd("debug goto 114")
    until("on the Frozen River", lambda: level() == 114, 60)
    cmd("debug warp 36 13"); time.sleep(0.5); cmd("debug kill within 15")
    until("frozen Anya", lambda: "Prison of Ice: frozen Anya" in log_text(), 10)
    fana()
    assert a5("s")[2] == 4, f"a5q3 no potion: {a5('s')}"
    go(109)
    talk_until("Malah", lambda: "[ice " in cmd("items"), "no thawing potion")
    cmd("debug goto 114")
    until("on the Frozen River", lambda: level() == 114, 60)
    fana()
    until("Anya thaws", lambda: "Prison of Ice: Anya thaws" in log_text(), 10)
    assert bits(0x25) & 0x2002 == 0x2002, f"word 0x25: {bits(0x25):#x}"
    assert a5("s")[2:4] == [5, 1], f"a5q3 thawed, a5q4 open: {a5('s')}"
    go(109)
    talk_until("Malah", lambda: bits(0x25) & 0x100, "no scroll")
    until("Anya home", lambda: "Anya goes home" in log_text(), 5)
    talk_until("Anya", lambda: bits(0x25) & 1, "Anya's item not given")
    print("OK: Prison of Ice (a5q3)")
    # a5q4: Anya (20137, bit 2) opens a red portal to Nihlathak's Temple as
    # her talk closes; Nihlathak in the Halls of Vaught (superunique 60 at
    # the marker) dies (bits 13, 1); Anya's 20148: bit 4.
    talk_until("Anya", lambda: bits(0x26) & 4, "the betrayal not given")
    until("Anya's portal", lambda: "Anya opens the way" in log_text(), 5)
    cmd("debug goto 124")
    until("in the Halls of Vaught", lambda: level() == 124, 60)
    cmd("debug warp 4 43"); time.sleep(1)
    until("Nihlathak comes", lambda: "(superunique 60)" in log_text(), 10)
    deadline = time.time() + 30
    while time.time() < deadline and "Nihlathak is dead" not in log_text():
        cmd("debug kill within 20"); time.sleep(0.5)
    assert bits(0x26) & 0x2002 == 0x2002, f"word 0x26: {bits(0x26):#x}"
    go(109)
    talk_until("Anya", lambda: bits(0x26) & 0x10, "the betrayal not rewarded")
    # Her personalizing (0x58dd, bit 0): the weapon takes the player's name.
    service("Anya", 4, lambda: bits(0x26) & 1, "Anya personalized")
    assert "Anya personalized the " in log_text() and "Quester's " in cmd("items"), "no personalizing"
    print("OK: Betrayal of Harrogath (a5q4)")
    # a5q5 (of level 20 on Normal): Qual-Kehk (20153, bit 2); the Summit's
    # altar has the Ancients speak; 20 frames on the statues are the three
    # Ancients; all dead: bits 0 and 13, the experience; the stairs lead to
    # the Worldstone Keep.
    def objects(op):
        return [l.split("\t") for l in cmd("debug objects").splitlines() if l.split("\t")[3:4] == [f"op{op}"]]
    cmd("debug stat 12 25")
    talk_until("Qual-Kehk", lambda: bits(0x27) & 4, "the rite not given")
    cmd("debug goto 120")
    until("on the Arreat Summit", lambda: level() == 120, 60)
    cmd("debug warp 10 17"); time.sleep(1); cmd("debug kill within 30")
    altar = objects(65)
    assert altar, f"no altar: {cmd('debug objects')!r}"
    cmd(f"debug warp {altar[0][1]} {float(altar[0][2]) + 1}"); time.sleep(0.5)
    cmd(f"debug operate {altar[0][0]}")
    until("the Ancients", lambda: all(f"(superunique {n})" in log_text() for n in (43, 44, 45)), 10)
    deadline = time.time() + 30
    while time.time() < deadline and "the Ancients are dead" not in log_text():
        cmd("debug kill within 30"); time.sleep(0.5)
    assert bits(0x27) & 0x2001 == 0x2001, f"word 0x27: {bits(0x27):#x} {a5('s')}"
    assert "Rite of Passage: +" in log_text(), "no experience"
    until("the statues back", lambda: log_text().count("Rite of Passage: statue ") == 3, 5)
    stairs = objects(66)
    assert stairs, "no stairs"
    cmd(f"debug warp {stairs[0][1]} {float(stairs[0][2]) + 1}"); time.sleep(0.5)
    cmd(f"debug operate {stairs[0][0]}"); time.sleep(0.5); cmd(f"debug operate {stairs[0][0]}")
    until("in the Worldstone Keep", lambda: level() == 128, 30)
    print("OK: Rite of Passage (a5q5)")
    # a5q6: open from the Rite (state 2); the Throne of Destruction cleared,
    # Baal sends his five waves, then walks into the Chamber's portal (log
    # 3); through it, Baal dies (bits 13 and 0, the next difficulty); Tyrael
    # comes; his talk opens the last portal home (bit 10).
    assert a5("s")[4] == 5 and bits(0x28) & 4, f"a5q6 open: {a5('s')} {bits(0x28):#x}"
    cmd("debug goto 131")
    until("on the Throne of Destruction", lambda: level() == 131, 60)
    cmd("debug warp 18 12"); time.sleep(1)
    deadline = time.time() + 240
    while time.time() < deadline and "Baal goes into the Worldstone Chamber" not in log_text():
        cmd("debug kill spare baalthrone within 60"); time.sleep(1)
    assert log_text().count("Eve of Destruction: wave") == 5, "five waves"
    portal = objects(70)
    assert portal, "no Chamber portal"
    cmd(f"debug warp {portal[0][1]} {float(portal[0][2]) + 1}"); time.sleep(0.5)
    cmd(f"debug operate {portal[0][0]}")
    until("in the Worldstone Chamber", lambda: level() == 132, 30)
    cmd("debug warp 27 34"); time.sleep(1)
    deadline = time.time() + 30
    while time.time() < deadline and "Baal is dead" not in log_text():
        cmd("debug kill within 40"); time.sleep(0.5)
    assert bits(0x28) & 0x2001 == 0x2001, f"word 0x28: {bits(0x28):#x}"
    assert "gold for Baal's killer, 1 pile" in log_text(), "no gold for Baal's killer"
    until("Tyrael comes", lambda: "Tyrael comes" in log_text(), 40)
    talk_until("Tyrael", lambda: "Tyrael opens the way home" in log_text(), "no last portal")
    last = objects(72)
    assert last, "no last portal"
    cmd(f"debug warp {last[0][1]} {float(last[0][2]) + 1}"); time.sleep(0.5)
    cmd(f"debug operate {last[0][0]}")
    until("home", lambda: level() == 109, 30)
    assert bits(0x28) & 0x400, f"word 0x28: {bits(0x28):#x}"
    print("OK: Eve of Destruction (a5q6)")


try:
    until("d2d starts", lambda: os.path.exists(sock_path), 60)
    cmd("debug statpts 400")
    cmd("cmd stat 3 400")
    until("the vitality", lambda: int(state()["life"].split("/")[1]) > 1000)

    for act in acts:
        globals()[f"act{act}"]()
finally:
    if proc.poll() is None:
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
    log.close()
