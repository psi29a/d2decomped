#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Act 1 quests end to end: drive a headless d2d over devctl.

Usage: quests_d2d.py <path-to-d2d>

A new Amazon (made sturdy: 400 Vitality) plays Act 1's six quests
through the World: the NPC talks, the quest objects (Tree of Inifuss,
Cairn Stones, Cain's Gibbet, the Moldy Tome, the Malus), the kills, the
rewards (Charsi's imbue on the held item), Warriv's way east, checking
each quest's flag bits
(docs/research/re/quests.md, act1-end.md). Travel is `debug goto`; each
level's monsters go by `debug kill`. Skips without the MPQs or the 1.14d
patch (the quest objects' tables).
"""
import os, platform, re, socket, subprocess, sys, tempfile, time

d2d = sys.argv[1]
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
log = open(log_path, "w")
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


def talk(name):
    """Walk up to a camp NPC, talk, close the talk (a quest given counts as it closes)."""
    npc = [l.split("\t") for l in cmd("npcs").splitlines() if l.split("\t")[0].endswith(name)]
    assert npc, f"{name} not in camp: {[l for l in cmd('debug presets').splitlines() if 'Cain' in l]} level {level()}"
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


def open_menu(name):
    npc = [l.split("\t") for l in cmd("npcs").splitlines() if l.split("\t")[0].endswith(name)]
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


def operate(index, seconds=4):
    cmd(f"cmd interact {index}")
    time.sleep(seconds)


try:
    until("d2d starts", lambda: os.path.exists(sock_path), 60)
    cmd("debug statpts 400")
    cmd("cmd stat 3 400")
    until("the vitality", lambda: int(state()["life"].split("/")[1]) > 1000)

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
    order = re.findall(r"the stones' order (\d+) (\d+) (\d+) (\d+) (\d+)", open(log_path).read())
    assert order, "no stone order"
    for stone in order[-1]:
        operate(stones[int(stone)], 3)
    until("the way to Tristram", lambda: any(l.startswith("2\t") for l in cmd("debug portals").splitlines()))
    cmd("cmd interact -2002")
    until("Tristram", lambda: level() == 38, 30)
    cmd("debug kill")
    gibbet = object_index(26)
    gibbet_at = [o for o in presets()[0] if o[0] == gibbet][0]
    cmd(f"debug warp {gibbet_at[2] + 1.2} {gibbet_at[3] + 1.1}")
    cmd("debug kill")
    operate(gibbet)
    until("Cain's rescue counts", lambda: bits(4) & 0x2002 == 0x2002, 30)
    until("Cain walks into his portal", lambda: "Cain has gone to the camp" in open(log_path).read(), 60)   # leaving first, he stays (the level stops)
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
    assert re.search(r"^r\d\d\t", cmd("ground"), re.M), "the Countess dropped no rune"
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
    weapon = re.search(r"^\[(\w+) loc=1 slot=4 .* id=(\d+) ", cmd("items"), re.M)
    assert weapon, "no weapon worn"
    cmd(f"debug hold {weapon.group(2)}")
    open_menu("Charsi")
    menu_pick("imbue")
    until("Charsi imbued", lambda: bits(3) & 1)
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
    go(1)
    open_menu("Warriv")
    assert any(l.split("\t")[0].lower().startswith(("go east", "east")) for l in cmd("menu").splitlines()), f"no way east: {cmd('menu')!r}"
    print("OK: Sisters to the Slaughter (Warriv offers the way east)")
    cmd("quit")
finally:
    if proc.poll() is None:
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
    log.close()
