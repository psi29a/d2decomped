#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The pad's A on what the field offers besides monsters (Steam Deck playtest 2026-10-21).

Usage: pad_use_d2d.py <path-to-d2d>

A headless d2d with a synthetic save and devctl's virtual pad: A takes a
town portal cast in the Blood Moor back to camp, A opens the camp's
waypoint, the d-pad steps its rows from the first (the cursor lands
there) and A travels to the Cold Plains. Skips without the MPQs.
"""
import atexit, os, platform, socket, struct, subprocess, sys, tempfile, time

d2d = sys.argv[1]
controller_ux = "--controller-ux" in sys.argv[2:]
touch_ux = "--touch-ux" in sys.argv[2:]
corpse_ux = "--corpse-ux" in sys.argv[2:]
skill_buttons = "--skill-buttons" in sys.argv[2:] or controller_ux
data = os.environ.get("D2_MPQ_DIR") or os.path.expanduser("~/Workspace/private/diablo2")
if not os.path.exists(os.path.join(data, "d2data.mpq")):
    print(f"SKIP: {data}/d2data.mpq not found")
    sys.exit(77)   # ctest's SKIP_RETURN_CODE (tests/CMakeLists.txt)

if skill_buttons and not os.path.isfile(os.environ.get("D2_PATCH_INSTALLER", "")):
    print("SKIP: skill-button check needs D2_PATCH_INSTALLER (1.14d)")
    sys.exit(77)

home = tempfile.mkdtemp(prefix="d2d_pad_use_")
user = os.path.join(home, "Library/Preferences/d2d" if platform.system() == "Darwin" else ".config/d2d")
saves = os.path.join(user, "save")
os.makedirs(saves)
for index, name in enumerate(["Alpha"]):   # expansion softcore, newest first: Alpha
    b = bytearray(0x2FD)
    struct.pack_into("<III", b, 0, 0xAA55AA55, 96, len(b))
    b[0x14:0x14 + len(name)] = name.encode()
    b[0x24], b[0x28], b[0x2B] = 0x20, (3 if controller_ux else 2 if corpse_ux else 1), 10
    struct.pack_into("<I", b, 0x30, 1_000_000 - index)   # last played
    with open(os.path.join(saves, name + ".d2s"), "wb") as f:
        f.write(b)
patch = os.environ.get("D2_PATCH_INSTALLER")
with open(os.path.join(user, "d2d.cfg"), "w") as f:
    if patch and os.path.exists(patch):
        f.write(f"patch = {patch}\n")
    if skill_buttons:
        f.write("pad_skill_buttons = 1\n")
    if controller_ux:
        f.write("pad_skill_right = left_shoulder\npad_skill_left = right_shoulder\n")
    if touch_ux:
        f.write("touch_controls = 1\n")

sock_path = os.path.join(tempfile.gettempdir(), f"d2d_pad_use_{os.getpid()}.sock")
proc = subprocess.Popen([d2d, "--headless", "--no-save", "--seed", "3", "--data", data, "--devctl", sock_path, "--start-screen", "charselect"],
                        env=dict(os.environ, HOME=home))
atexit.register(lambda: proc.poll() is None and proc.kill())

def cmd(line):
    with socket.socket(socket.AF_UNIX) as s:
        s.settimeout(10)
        s.connect(sock_path)
        s.sendall((line + "\n").encode())
        out = ""
        while not out.endswith("\n") or not out.rsplit("\n", 2)[-2].startswith(("ok", "err")):
            chunk = s.recv(65536)
            assert chunk, f"{line!r}: connection closed after {out!r}"
            out += chunk.decode()
    assert not out.rsplit("\n", 2)[-2].startswith("err"), f"{line!r} -> {out!r}"
    return out

def frames(n):
    frame = lambda: int(cmd("info").split("frame=")[1].split()[0])
    want, deadline = frame() + n, time.time() + 20 + n / 10
    while frame() < want:
        assert time.time() < deadline, f"stuck waiting for frame {want}"
        time.sleep(0.005)

def players():
    rows = [line.split("\t") for line in cmd("debug players").splitlines() if "\t" in line]
    return {row[1]: (float(row[2]), float(row[3]), int(row[4])) for row in rows}
def modes():
    return {row.split("\t")[1]: int(row.split("\t")[5]) for row in cmd("debug players").splitlines() if "\t" in row}
def ghosts():
    return {row.split("\t")[1]: row.split("\t")[6] == "1" for row in cmd("debug players").splitlines() if "\t" in row}
def ids():
    return {row.split("\t")[1]: int(row.split("\t")[0]) for row in cmd("debug players").splitlines() if "\t" in row}

def walk(pad, frames_held=40):
    cmd(f"pad {pad} axis leftx 1"); frames(frames_held)
    cmd(f"pad {pad} axis leftx 0"); frames(10)

for _ in range(400):
    if os.path.exists(sock_path):
        break
    assert proc.poll() is None, "d2d exited during startup"
    time.sleep(0.1)
frames(1)
cmd("pad 0 plug"); frames(5)
cmd("key Return"); frames(40)

def press(button):
    cmd(f"pad 0 {button} down"); frames(3)
    cmd(f"pad 0 {button} up"); frames(3)
def level():
    return int(cmd("debug level").split()[1])
def until(what, test, seconds=20):
    deadline = time.time() + seconds
    while not test():
        assert time.time() < deadline, f"timed out: {what}"
        frames(5)
def face(x, y):
    """The aiming stick toward world (x, y) for a moment: A's target is there."""
    px, py = players()["Alpha"][:2]
    dx, dy = x - px, y - py
    sx, sy = dx - dy, (dx + dy) / 2
    n = max((sx * sx + sy * sy) ** 0.5, 1e-3)
    cmd(f"pad 0 axis rightx {sx / n:.2f}"); cmd(f"pad 0 axis righty {sy / n:.2f}"); frames(3)
    cmd("pad 0 axis rightx 0"); cmd("pad 0 axis righty 0"); frames(2)

def state():
    return dict(word.split("=", 1) for word in cmd("state").split() if "=" in word)

def skill_panel(skill):
    cmd("key t"); frames(5)
    page, pixel_x, pixel_y = map(int, cmd(f"debug skillat {skill}").split()[1:])
    cmd(f"click 675 {430 if page == 1 else 325 if page == 2 else 220}"); frames(3)
    cmd(f"move {pixel_x} {pixel_y}"); frames(3)
    return pixel_x, pixel_y

def screenshot(name):
    directory = os.environ.get("D2_SCREENSHOT_DIR")
    if directory:
        os.makedirs(directory, exist_ok=True)
        cmd(f"screenshot {os.path.join(directory, name)}")

if controller_ux:
    assert state()["pad_south"] == "0", "a fresh layout needs Attack/Use"
    cmd("debug stat 12 99"); cmd("debug points 98 20"); frames(5)
    skill_panel(98)
    press("y")
    assert state()["picker"] == "3", "Assign from the skill panel did not open the editor"
    screenshot("controller-assign.png")
    press("leftshoulder")
    assert "LB . Might" in cmd("state"), "assignment feedback did not use the remapped destination"
    cmd("pad 0 lefttrigger down"); frames(3)
    press("leftshoulder")
    screenshot("controller-bank-II.png")
    cmd("pad 0 lefttrigger up"); frames(3)
    press("back"); press("b")
    for key, field in (("c", "character_panel"), ("t", "tree"), ("q", "quests"), ("h", "help"), ("i", "inventory")):
        cmd(f"key {key}"); frames(5)
        assert state()[field] != "0", (key, cmd("state"))
        press("b")
        assert state()[field] == "0", (key, cmd("state"))
    cmd("debug goto 2"); frames(20)
    start = players()["Alpha"][:2]
    cmd("pad 0 leftshoulder down"); frames(35)
    assert state()["aura"] == "98", "Might did not activate"
    assert players()["Alpha"][:2] == start and state()["walking"] == "0", "holding Might chased an enemy"
    cmd("pad 0 leftshoulder up"); frames(3)
    press("leftshoulder")
    assert state()["aura"] == "0", "Might did not toggle off"
    # Hold Attack across an enemy death and an item appearing underneath.
    # The kill is fixture setup; the held/released inputs take the real path.
    cmd("debug stat 7 256000"); cmd("debug stat 6 256000")
    cmd("debug kill 1"); frames(5)
    foes = [line.split("\t") for line in cmd("monsters").splitlines() if "\t" in line and int(line.split("\t")[5].split("/")[0]) > 0]
    assert foes, "held-attack test needs a living target"
    foe = foes[0]
    cmd(f"debug warp {float(foe[1]) + 1} {foe[2]}"); frames(5)
    face(float(foe[1]), float(foe[2]))
    before = int(state()["items"])
    cmd("pad 0 a down"); frames(3)
    cmd("debug kill"); frames(40)
    cmd("debug give r01"); frames(5)
    drop = next(line.split("\t") for line in cmd("ground").splitlines() if line.startswith("r01\t"))
    face(float(drop[5]), float(drop[6])); frames(15)
    assert int(state()["items"]) == before, "held Attack turned into pickup after the kill"
    cmd("pad 0 a up"); frames(3)
    press("a")
    until("fresh Attack/Use picks up the rune", lambda: int(state()["items"]) == before + 1)
    shrines = [line.split("\t") for line in cmd("debug objects").splitlines() if "\tshrine\t" in line]
    if not shrines:
        cmd("debug goto 3"); frames(20)
        shrines = [line.split("\t") for line in cmd("debug objects").splitlines() if "\tshrine\t" in line]
    if not shrines:
        extent = cmd("debug level").split()
        for grid_y in range(4, int(extent[5]), 12):
            for grid_x in range(4, int(extent[4]), 12):
                cmd(f"debug warp {grid_x} {grid_y}"); frames(3)
                shrines = [line.split("\t") for line in cmd("debug objects").splitlines() if "\tshrine\t" in line]
                if shrines:
                    break
            if shrines:
                break
    if not shrines:
        print("SKIP: this generated level supplied no shrine fixture; shrine input remains unverified")
    else:
        shrine = shrines[0]
        shrine_x, shrine_y = float(shrine[1]), float(shrine[2])
        for offset_x, offset_y in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            cmd(f"debug warp {shrine_x + offset_x} {shrine_y + offset_y}"); frames(5)
            cmd("debug kill"); frames(5)
            face(shrine_x, shrine_y); press("a"); frames(10)
            mode = next(line.split("\t")[-1] for line in cmd("debug objects").splitlines() if line.startswith(shrine[0] + "\t"))
            if mode != shrine[-1]:
                break
        assert mode != shrine[-1], "Attack/Use did not activate the shrine"
    # Clear is explicit and survives subsequent assignments to other slots.
    press("back"); press("dpright"); press("a"); press("back")
    assert state()["pad_south"] == "-1", "Clear was not preserved"
    print("ok: panel assignment, remapped glyphs, both banks, B closes panels, stationary held Might, held attack across kill, shrine use, explicit clear")
    sys.exit(0)

if touch_ux:
    def finger(identity, phase, pixel_x, pixel_y):
        cmd(f"touch {identity} {phase} {pixel_x} {pixel_y}"); frames(3)
    def tap(pixel_x, pixel_y):
        finger(2, "down", pixel_x, pixel_y); finger(2, "up", pixel_x, pixel_y)
    aimed = 70 if corpse_ux else 54
    cmd("debug stat 12 99"); cmd(f"debug points {aimed} 20")
    cmd("debug stat 9 256000"); cmd("debug stat 8 256000")
    cmd("debug stat 7 256000"); cmd("debug stat 6 256000"); frames(5)
    pixel_x, pixel_y = skill_panel(aimed)
    tap(pixel_x, pixel_y)
    tap(500, 565)
    tap(565, 470)
    screenshot("touch-assignment-corpse.png" if corpse_ux else "touch-assignment.png")
    tap(720, 32); tap(720, 32)  # Done, then Back from the tree
    cmd("debug goto 2"); frames(20)
    cmd("debug kill"); frames(40)
    if corpse_ux:
        until("death animations become selectable corpses", lambda: any("\tDD\t" in line for line in cmd("monsters").splitlines()))
        bodies = [line.split("\t") for line in cmd("monsters").splitlines() if "\t" in line and line.split("\t")[6] == "DD"]
        assert bodies, "corpse test has no dead monsters"
        cmd(f"debug warp {float(bodies[0][1]) + 2} {bodies[0][2]}"); frames(10)
    # Aiming and cancellation travel through injected SDL finger events.
    mana = state()["mana"]
    finger(1, "down", 100, 430); finger(1, "move", 160, 430)
    finger(2, "down", 565, 470)
    assert state()["touch_aiming"] == "1" and state()["touch_skill"] == str(aimed), cmd("state")
    if corpse_ux:
        assert int(state()["touch_target"]) >= 0, "corpse skill did not select a corpse"
    finger(2, "move", 630, 450)
    screenshot("touch-corpse-aim.png" if corpse_ux else "touch-teleport-aim.png")
    finger(2, "cancel", 630, 450)
    assert state()["touch_aiming"] == "0" and state()["touch_move"] != "0.000000,0.000000", "cancellation interrupted movement"
    assert state()["mana"] == mana, "cancelled skill spent mana"
    finger(3, "down", 680, 280); finger(3, "up", 680, 280)
    assert state()["touch_page"] == "1" and state()["touch_move"] != "0.000000,0.000000", "page switch interrupted movement"
    finger(1, "up", 160, 430)
    tap(680, 280)
    finger(2, "down", 565, 470)
    cmd("key i"); frames(5)
    finger(2, "up", 630, 450)
    assert state()["touch_aiming"] == "0" and state()["mana"] == mana, "opening a panel committed the pending skill"
    screenshot("touch-inventory.png")
    tap(720, 32)
    finger(2, "down", 565, 470)
    if not corpse_ux:
        finger(2, "move", 610, 480)
    preview = tuple(map(float, state()["touch_point"].split(",")))
    before_cast = players()["Alpha"][:2]
    finger(2, "up", 610 if not corpse_ux else 565, 480 if not corpse_ux else 470)
    frames(35)
    if corpse_ux:
        assert state()["pets"] != "-", "touch corpse skill did not raise a minion"
    else:
        assert players()["Alpha"][:2] != before_cast, "touch Teleport did not cast"
        position = players()["Alpha"][:2]
        assert abs(position[0] - preview[0]) < .3 and abs(position[1] - preview[1]) < .3, (position, preview)
    if not corpse_ux:
        def carried(code, column, row):
            cmd(f"debug equip {code} 12"); frames(5)
            item = cmd("items").split(f"[{code} ")[1].split(" id=")[1].split()[0]
            cmd(f"cmd hand {item}"); frames(4)
            cmd(f"cmd grid {column} {row}"); frames(4)
            return item
        def item_point(item):
            words = cmd(f"debug itemat {item}").split()
            return int(words[1]), int(words[2])
        cmd("debug clearinv"); frames(4)
        identify = carried("ibk", 0, 0)
        target = carried("cap", 2, 0)
        portal = carried("tbk", 4, 0)
        cmd("debug unid"); frames(4)
        cmd("key i"); frames(5)
        before = int(state()["unid"])
        tap(*item_point(identify))
        assert int(state()["identify"]) >= 0, "touch did not activate the identify tome"
        tap(*item_point(target))
        assert int(state()["unid"]) == before - 1, "touch identify target was not applied"
        tap(*item_point(portal))
        until("touch opened a portal", lambda: any(line.startswith("0\t") for line in cmd("debug portals").splitlines()))
        tap(720, 32)
        cmd("debug goto 1"); frames(20)
        akara = next(line.split("\t") for line in cmd("npcs").splitlines() if line.startswith("Akara\t"))
        cmd(f"debug warp {float(akara[5]) + 1} {akara[6]}"); frames(6)
        cmd("debug stat 14 100000"); cmd(f"cmd trade {akara[4]}"); frames(8)
        before = int(state()["items"])
        tap(110, 137)
        assert int(state()["items"]) == before + 1, "touch stock tap did not buy"
        bought = [line for line in cmd("items").splitlines() if line.startswith("[") and "panel=1 " in line][-1]
        bought_id = bought.split(" id=")[1].split()[0]
        pixel_x, pixel_y = item_point(bought_id)
        finger(2, "down", pixel_x, pixel_y)
        held_from = time.time()
        while time.time() - held_from < .8:
            frames(3)
        finger(2, "up", pixel_x, pixel_y)
        assert int(state()["items"]) == before, "touch hold did not sell"
        tap(720, 32)
    print("ok: touch assignment, independent page/movement, aim/cancel, actual cast, tomes and separate buy/sell where applicable")
    sys.exit(0)

if skill_buttons:
    # Issue #144: assign Inventory from the wheel, then open it without Start.
    press("back")
    press("dpright")  # Actions tab
    press("dpdown"); press("dpdown"); press("dpdown")  # Clear -> Interact -> Show Items -> Inventory
    press("a")
    press("back")
    press("a")
    cmd("debug equip cap 1"); frames(5)
    cap = cmd("items").split("[cap ")[1].split(" id=")[1].split()[0].rstrip("]")
    words = cmd(f"debug itemat {cap}").split()
    cmd(f"move {words[1]} {words[2]}"); frames(3)
    press("a")
    assert state()["held"] == "cap", "the assigned Inventory button did not open the panel"
    cmd("cmd grid 0 0"); frames(5)
    cmd("key Escape"); frames(5)
    # RT remains Attack (skill 0), which must also use things on a fresh press.
    attack_button = "righttrigger"
else:
    attack_button = "a"

# A town portal: read in the Blood Moor, A at its mouth takes it to camp.
cmd("debug goto 2"); frames(20)
cmd("debug equip tsc 12")
until("the scroll in items", lambda: "[tsc " in cmd("items"))   # the client's copy, published on a tick
scroll = cmd("items").split("[tsc ")[1].split(" id=")[1].split()[0].rstrip("]")
if skill_buttons:
    def bag_item(code, column, row):
        cmd(f"debug equip {code} 12"); frames(5)
        item = cmd("items").split(f"[{code} ")[1].split(" id=")[1].split()[0].rstrip("]")
        cmd(f"cmd hand {item}"); frames(5)
        cmd(f"cmd grid {column} {row}"); frames(5)
        return item

    def point_item(item):
        words = cmd(f"debug itemat {item}").split()
        cmd(f"move {words[1]} {words[2]}"); frames(3)

    # RS uses tomes via the inventory's right-click path, including the
    # two-step Identify selection (FUN_004917c0 / FUN_00487740).
    cmd("debug clearinv"); frames(5)
    identify = bag_item("ibk", 0, 0)
    target = bag_item("cap", 2, 0)
    portal_book = bag_item("tbk", 4, 0)
    cmd("debug unid"); frames(5)
    press("a")
    before = int(state()["unid"])
    point_item(identify); press("rightstick")
    point_item(target); press("a")
    assert int(state()["unid"]) == before - 1, cmd("state")
    point_item(portal_book); press("rightstick")
    cmd("key Escape"); frames(5)
else:
    cmd(f"cmd use {scroll}")
until("the portal opens", lambda: any(line.startswith("0\t") for line in cmd("debug portals").splitlines()))
portal = [line.split("\t") for line in cmd("debug portals").splitlines() if line.startswith("0\t")][0]
cmd(f"debug warp {int(float(portal[2])) + 2} {int(float(portal[3]))}"); frames(10)
face(float(portal[2]), float(portal[3]))
press(attack_button)
until("through the portal to camp", lambda: level() == 1)

if skill_buttons:
    # The vague trade report in #144: exercise the documented skill-preset
    # controls too. X buys, holding A sells, RS on our tome never leaves shop.
    akara = next(line.split("\t") for line in cmd("npcs").splitlines() if line.startswith("Akara\t"))
    cmd(f"debug warp {float(akara[5]) + 1} {akara[6]}"); frames(10)
    cmd("debug stat 14 100000")
    cmd(f"cmd trade {akara[4]}"); frames(10)
    assert state()["store"] != "-1", cmd("state")
    before = int(state()["items"])
    cmd("move 90 137"); frames(3)
    press("dpright"); press("x"); frames(5)
    assert int(state()["items"]) == before + 1, "X did not buy stock"
    bought = [line for line in cmd("items").splitlines() if line.startswith("[") and "panel=1 " in line][-1]
    bought_id = bought.split(" id=")[1].split()[0]
    point_item(bought_id)
    cmd("pad 0 a down"); frames(3)
    held_from = time.time()
    while time.time() - held_from < 0.8:
        frames(3)
    cmd("pad 0 a up"); frames(5)
    assert int(state()["items"]) == before and state()["held"] == "-", "hold A did not sell"
    point_item(identify); press("rightstick")
    assert state()["store"] != "-1", "using our tome closed the store"
    press("rightstick")   # cancel the identify cursor
    assert state()["identify"] == "-1", cmd("state")
    press("b"); frames(5)
    assert state()["store"] == "-1", "B did not leave the store"
    press("back")
    assert state()["picker"] == "3", "field stayed blocked after trading: " + cmd("state")
    press("back")

# The waypoint: A opens it, the cursor on its first row; down and A travel.
cmd("debug wp 1")                                   # the Cold Plains' is on
waypoint = [line.split("\t") for line in cmd("debug presets").splitlines() if line.endswith("\tWaypoint")][0]
wp_x, wp_y = float(waypoint[2]), float(waypoint[3])
cmd(f"debug warp {int(wp_x) + 2} {int(wp_y)}"); frames(10)
face(wp_x, wp_y)
assert state()["pad_field"] == "1", "field before waypoint: " + cmd("state")
press(attack_button); frames(20)
assert " waypoint=9 " in cmd("state"), cmd("state")[:400]
press("dpdown"); press("a")
until("by waypoint to the Cold Plains", lambda: level() == 3)
if skill_buttons:
    # Holding Attack/Use must finish walking to a drop, not replace the
    # queued pickup with an attack (FUN_00462930's held-unit gate).
    cmd("debug kill"); frames(5)
    before = int(state()["items"])
    cmd("debug give r01"); frames(5)
    drop = next(line.split("\t") for line in cmd("ground").splitlines() if line.startswith("r01\t"))
    drop_x, drop_y = float(drop[5]), float(drop[6])
    cmd(f"debug warp {drop_x + 3} {drop_y}"); frames(5)
    face(drop_x, drop_y)
    cmd("pad 0 righttrigger down"); frames(50)
    cmd("pad 0 righttrigger up"); frames(5)
    assert int(state()["items"]) == before + 1, "held Attack/Use abandoned its pickup"

print("ok: pad Attack/Use takes portals and waypoints" + ("; wheel inventory and both tomes work" if skill_buttons else ""))
