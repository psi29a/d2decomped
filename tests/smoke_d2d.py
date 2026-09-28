#!/usr/bin/env python3
"""End-to-end smoke test: drive a headless d2d over devctl.

Usage: smoke_d2d.py <path-to-d2d>

Runs d2d on SDL's dummy driver with a throwaway $HOME (so the per-user
save/screenshot dirs are sandboxed), seeds two synthetic .d2s saves — or
copies every .d2s from $D2_SAVES_DIR when set — then clicks through
CharSelect -> InGame and pans left (the old infinite-loop direction).
Skips when the MPQ dir isn't reachable, like the other asset tests.
"""
import re
import os, platform, shutil, socket, struct, subprocess, sys, tempfile, time

d2d = sys.argv[1]
data = os.environ.get("D2_MPQ_DIR") or os.path.expanduser("~/Workspace/private/diablo2")
if not os.path.exists(os.path.join(data, "d2data.mpq")):
    print(f"SKIP: {data}/d2data.mpq not found")
    sys.exit(0)

home = tempfile.mkdtemp(prefix="d2d_smoke_")
user = os.path.join(home, "Library/Preferences/d2d" if platform.system() == "Darwin"
                    else ".config/d2d")
saves = os.path.join(user, "save")
os.makedirs(saves)

def make_save(name, status, cls, level):
    b = bytearray(0x2FD)
    struct.pack_into("<III", b, 0, 0xAA55AA55, 96, len(b))
    b[0x14:0x14 + len(name)] = name.encode()
    b[0x24], b[0x28], b[0x2B] = status, cls, level
    with open(os.path.join(saves, name + ".d2s"), "wb") as f:
        f.write(b)

real = os.environ.get("D2_SAVES_DIR")
if real:
    for f in os.listdir(real):
        if f.endswith(".d2s"):
            shutil.copy(os.path.join(real, f), saves)
else:
    make_save("Alpha", 0x24, 0, 10)   # hardcore expansion Amazon
    make_save("Beta",  0x20, 3, 20)   # expansion Paladin
    for i in range(3, 12):            # 11 saves total: 6 rows -> scrollbar
        make_save(f"Char{i:02}", 0x20, i % 7, i)

# The 1.14d patch installer (D2_PATCH_INSTALLER) brings the town objects
# the stash step needs; without it that step is skipped.
patch = os.environ.get("D2_PATCH_INSTALLER")
if patch and os.path.exists(patch):
    with open(os.path.join(user, "d2d.cfg"), "w") as f:
        f.write(f"patch = {patch}\n")
else:
    patch = None

sock_path = os.path.join(tempfile.gettempdir(), f"d2d_smoke_{os.getpid()}.sock")
env = dict(os.environ, HOME=home)
env.pop("D2_MPQ_DIR", None)   # --data wins anyway; keep the env honest
# --scale 2: every click below is in 800x600 game pixels, so the whole run
# also checks window<->game coordinate conversion.
proc = subprocess.Popen([d2d, "--headless", "--seed", "3", "--data", data, "--devctl", sock_path,
                         "--start-screen", "charselect", "--scale", "2"], env=env)

def cmd(line):
    # Replies end with a line starting "ok" (maybe "ok <payload>") or "err".
    with socket.socket(socket.AF_UNIX) as s:
        s.settimeout(5)               # a stuck main loop fails fast
        s.connect(sock_path)
        s.sendall((line + "\n").encode())
        out = ""
        while not out.endswith("\n") or not out.rsplit("\n", 2)[-2].startswith(("ok", "err")):
            chunk = s.recv(4096)
            assert chunk, f"{line!r}: connection closed after {out!r}"
            out += chunk.decode()
    assert not out.rsplit("\n", 2)[-2].startswith("err"), f"{line!r} -> {out!r}"
    return out

def state():
    kv = dict(re.findall(r"(\w+)=(.*?)(?= \w+=|$)", cmd("state").split("\n")[0]))   # a name may hold a space
    print("state:", kv)
    return kv

def frame():
    return int(cmd("info").split("frame=")[1].split()[0])

def frames(n=6):
    # n frames actually drawn, not n/60 s: a slow frame (or a slow machine)
    # would otherwise leave input unprocessed when the assert looks.
    want = frame() + n
    deadline = time.time() + 10 + n / 10
    while frame() < want:
        assert time.time() < deadline, f"stuck waiting for frame {want}"
        time.sleep(0.005)

try:
    for _ in range(100):
        if os.path.exists(sock_path):
            break
        assert proc.poll() is None, "d2d exited during startup"
        time.sleep(0.1)
    frames(1)                         # the first frame: the screen is up

    st = state()
    assert st["screen"] == "charselect" and st["save"] == "0"   # LoD preselects 0
    n = int(st["saves"])
    assert n == (len([f for f in os.listdir(saves) if f.endswith(".d2s")]))

    # Scrolling: one step = one row = 2 saves, clamped to the last row.
    max_scroll = max(0, (n + 1) // 2 * 2 - 8)
    cmd("click 592 450")              # scrollbar down arrow
    frames()
    assert state()["scroll"] == str(min(2, max_scroll))
    cmd("wheel -10")                  # way past the end: clamps
    frames()
    assert state()["scroll"] == str(max_scroll)
    cmd("click 142 120")              # slot 0 while scrolled
    frames()
    assert state()["save"] == str(max_scroll)
    cmd("wheel 10")                   # back to the top
    frames()
    assert state()["scroll"] == "0"

    cmd("click 142 120")              # slot 0 (saves sort by name)
    frames()
    assert state()["save"] == "0"

    # Keyboard, as LoD: Left/Right stay in the row, Up/Down move a row,
    # End/Home jump and scroll to keep the pick visible.
    for key, want in (("Left", "0"), ("Right", "1"), ("Right", "1"), ("Down", "3"),
                      ("Up", "1"), ("Home", "0")):
        cmd(f"key {key}"); frames(3)
        assert state()["save"] == want, (key, state())
    if n > 8:
        cmd("key End"); frames(3)
        st = state()
        assert st["save"] == str(n - 1) and st["scroll"] == str(max_scroll)
        cmd("key Home"); frames(3)
        assert state()["save"] == "0" and state()["scroll"] == "0"

    cmd("click 690 555")              # OK -> InGame as that save
    frames()
    assert state()["screen"] == "ingame"
    # The town start is next to the stash (townE1 special tile 30/0):
    # hover the chest, click it -> stash (and inventory) open; Esc closes.
    if patch:
        # Town music: Levels.txt 1 -> SoundEnviron 1 -> Sounds.txt 4673.
        assert state()["music"] == "4673", "no town music"
        cmd("move 270 285"); frames(6)
        cmd("click 270 285"); frames(30)
        assert state()["stash"] == "1", "stash did not open"
        cmd("key Escape"); frames()
        st = state()
        assert st["stash"] == "0" and st["screen"] == "ingame"
        # Warriv (patrols near the start): click him -> walk over -> his
        # menu (name, talk, cancel); Esc closes it.
        def npc_at(name):
            for l in cmd("npcs").splitlines():
                p = l.split("\t")
                if p[0].endswith(name):
                    return int(p[1]), int(p[2])
        def walk_to(name, done, dy=40, tries=12):
            """Click `name` (a step its way when off screen: the edge may
            be a wall), wait for the walk to end; again (it may patrol)
            until done(state)."""
            for _ in range(tries):
                x, y = npc_at(name)
                y -= dy
                f = min(1.0, 250 / max(abs(x - 400), 2 * abs(y - 340), 1))
                x, y = int(400 + (x - 400) * f), int(340 + (y - 340) * f)
                cmd(f"move {x} {y}"); frames(2); cmd(f"click {x} {y}"); frames(2)
                for _ in range(60):
                    st = state()
                    if done(st) or st["walking"] == "0":
                        break
                    frames(10)
                frames(6)
                if done(state()):
                    return
        walk_to("Warriv", lambda st: st["menu"] != "0")
        assert state()["menu"] == "3", "Warriv's menu did not open"
        # talk -> the talk submenu (talk, introduction, gossip, cancel);
        # introduction -> his speech scrolls; Esc ends it.
        def menu_line(text):
            for l in cmd("menu").splitlines():
                p = l.split("\t")
                if p[0].lower() == text:
                    return int(p[1]), int(p[2])
        x, y = menu_line("talk")
        cmd(f"move {x} {y}"); frames(2); cmd(f"click {x} {y}"); frames(6)
        assert menu_line("introduction"), "talk submenu did not open"   # 4 lines, more with quest topics
        x, y = menu_line("introduction")
        cmd(f"move {x} {y}"); frames(2); cmd(f"click {x} {y}"); frames(6)
        assert state()["speech"] != "0", "no speech"
        assert state()["voice"] != "0", "no voice (Sounds.txt / d2speech.mpq)"
        cmd("key Escape"); frames()
        st = state()
        assert st["speech"] == "0" and st["menu"] == "0" and st["screen"] == "ingame"
        frames(2)
        assert state()["voice"] == "0", "voice kept playing after the speech closed"
        # The town waypoint (OperateFn 23): walk to it -> the panel, Act I's
        # 9 rows (touching it activates the town's); Esc closes it.
        walk_to("Waypoint", lambda st: st["waypoint"] != "0", dy=20)
        assert state()["waypoint"] == "9", "waypoint panel did not open"
        cmd("key Escape"); frames()
        assert state()["waypoint"] == "0"

        # --- Town services and panels (synthetic saves: set the stage). ---
        def near(name):
            """Warp next to a town NPC, click it, wait for its menu."""
            cx, cy = (float(v) for v in state()["cam"].split(","))
            x, y = npc_at(name)
            u, v = (x - 400) / 80, (y - 340) / 40
            cmd(f"debug warp {cx + (u + v) / 2 + 1.2:.2f} {cy + (v - u) / 2 + 0.2:.2f}"); frames(6)
            for _ in range(3):
                x, y = npc_at(name)
                cmd(f"move {x} {y - 40}"); frames(2); cmd(f"click {x} {y - 40}"); frames(60)
                if state()["menu"] != "0":
                    return
            raise AssertionError(f"{name}'s menu did not open")
        def pick(text):
            x, y = menu_line(text)
            cmd(f"move {x} {y}"); frames(2); cmd(f"click {x} {y}"); frames(6)
        cmd("debug stat 12 30")                  # level 30: Kashya hires
        cmd("debug stat 15 1000000")             # a stash full of gold

        # Pathing: from the start to Charsi on foot, round the camp.
        walk_to("Charsi", lambda st: st["menu"] != "0")
        assert menu_line("trade/repair"), "didn't walk to Charsi"
        cmd("key Escape"); frames()

        # Gamble at Gheed: a rolled item lands in the inventory.
        cmd("debug clearinv"); frames(2)        # room for it, whatever the save carries
        near("Gheed")
        pick("gamble")
        n0 = int(state()["items"])
        cmd("move 110 137"); cmd("rclick 110 137"); frames(4)   # first stock cell
        assert int(state()["items"]) == n0 + 1, "gamble bought nothing"
        # The item cursor: pick it up, put it down again.
        at = [l for l in cmd("items").splitlines() if l.startswith("[") and "panel=1 " in l][-1]
        col, row = (int(v) for v in at.split("at=")[1].rstrip("]").split(","))
        w, h = (int(v) for v in at.split("size=")[1].split()[0].split("x"))
        gx, gy = 419 + 29 * col + 14, 315 + 29 * row + 14              # its top-left cell
        cx, cy = gx + (w - 1) * 29 // 2, gy + (h - 1) * 29 // 2        # held items drop by their centre
        cmd("key Escape"); frames()                             # closes the store and the inventory
        cmd("key i"); frames()
        cmd(f"move {gx} {gy}"); cmd(f"click {gx} {gy}"); frames(4)
        assert state()["held"] != "-", "didn't pick the item up"
        cmd(f"move {cx} {cy}"); cmd(f"click {cx} {cy}"); frames(4)   # back where it was: a real save's grid may be full
        assert state()["held"] == "-", "didn't put the item down"
        cmd("key i"); frames()

        # Hire at Kashya: the list, then a rogue follows.
        near("Kashya")
        pick("hire")
        rows = [l.split("\t") for l in cmd("menu").splitlines() if " - " in l]
        assert len(rows) == 5, "no hire offers"
        cmd(f"move {rows[0][1]} {rows[0][2]}"); frames(2); cmd(f"click {rows[0][1]} {rows[0][2]}"); frames(6)
        assert state()["merc"].endswith(":RG"), "hired rogue missing"

        # Cain once rescued: identify items.
        cmd("debug quest 4")
        cmd("debug unid"); frames(3)          # the World's items; the client's follow on its next tick
        assert state()["unid"] != "0"
        near("Deckard Cain")
        pick("identify items")
        assert state()["unid"] == "0", "Cain didn't identify"

        # Stat and skill points.
        cmd("debug statpts 2"); cmd("key c"); frames()
        s0 = int(state()["str"])
        cmd("move 217 155"); cmd("click 217 155"); frames(4)
        assert int(state()["str"]) == s0 + 1 and state()["statpts"] == "1", "stat point not spent"
        cmd("key c"); frames()
        cmd("debug skillpts 1"); cmd("key t"); frames()
        assert state()["tree"] != "0"
        cmd("key t"); frames()
    cmd("key Escape")                 # back to the roster
    frames()
    assert state()["screen"] == "charselect"
    cmd("click 142 120"); frames(2); cmd("click 142 120")   # double-click plays
    frames()
    st = state()
    assert st["screen"] == "ingame"
    if not real:
        assert st["class"] == "3" and st["name"] == "Alpha" and st["hardcore"] == "1"

    cam0 = st["cam"]
    cmd("click 2 300")                # mouse at left edge -> pan left
    frames(60)
    st = state()                      # still answering = main loop not stuck
    assert st["cam"] != cam0, "camera did not pan left"

    cmd("screenshot ingame.png")      # relative -> <user>/screenshots/
    assert os.path.getsize(os.path.join(user, "screenshots", "ingame.png")) > 0

    # The skill bar: a click on the right button opens its picker, Esc
    # closes it (the synthetic save's skills are Attack).
    st = state()
    if not real:
        assert st["lskill"] == "0" and st["rskill"] == "0", st
    assert st["picker"] == "0", st
    cmd("click 659 580"); frames()
    assert state()["picker"] == "2"
    cmd("key Escape"); frames()
    assert state()["picker"] == "0" and state()["screen"] == "ingame"

    # Leaving camp (default map seed 3: the Blood Moor east of the town,
    # townE1): from the west end of the town's bridge (row 16), walk east
    # across it into the Blood Moor; its west edge leads back. On normal:
    # the first real save (the last played) may be a Hell character whose
    # merc and self die to the Fallen before the attack below lands.
    cmd("debug difficulty 0"); frames(6)
    lv = cmd("debug level").split()
    assert lv[1] == "1", lv
    cmd("debug warp 44.5 16.5"); frames(6)
    # Until it crosses (a camp NPC patrolling the bridge can hold it up).
    for _ in range(40):
        cmd("move 700 490"); cmd("click 700 490"); frames(30)
        lv = cmd("debug level").split()
        if lv[1] == "2":
            break
    assert lv[1] == "2" and lv[4:6] == ["96", "56"], f"didn't walk out of camp: {lv}"
    # The Blood Moor's monsters (seed 3: 155), alive and at full life.
    mons = [m.split("\t") for m in cmd("monsters").splitlines()[:-1]]
    assert len(mons) > 50 and {m[0] for m in mons} == {"zombie1", "fallen1", "quillrat1"}, mons[:3]
    assert all(m[5].split("/")[0] == m[5].split("/")[1] for m in mons)
    # Through the protocol alone (`cmd`: what a remote client sends the
    # World): walk a few cells east, then attack a monster until it's hurt.
    x0, y0 = float(lv[2]), float(lv[3])
    cmd(f"cmd move {x0 + 3:.1f} {y0:.1f}"); frames(60)
    assert float(cmd("debug level").split()[2]) > x0 + 1, "cmd move didn't walk"
    m = min(mons, key=lambda m: (float(m[1]) - x0) ** 2 + (float(m[2]) - y0) ** 2)
    uid = m[-1].lstrip("#")
    cmd(f"debug warp {float(m[1]) - 1:.2f} {float(m[2]):.2f}"); frames(6)
    for _ in range(40):
        cur = next(r.split("\t") for r in cmd("monsters").splitlines()[:-1] if r.split("\t")[-1] == "#" + uid)
        if cur[5].split("/")[0] != cur[5].split("/")[1]:
            break
        cmd(f"cmd skill 0 {cur[1]} {cur[2]} {uid} 1"); frames(20)
    assert cur[5].split("/")[0] != cur[5].split("/")[1], f"cmd skill didn't hurt {cur}; the player: {cmd('state').splitlines()[0]}; {cmd('debug level')}"
    lv = cmd("debug level").split()
    cmd(f"debug warp -0.2 {lv[3]}"); frames(6)
    assert cmd("debug level").split()[1] == "1"

    # Saving (character_store.hpp): gold set, saved, out to the roster and
    # back in; the gold is still there, the original kept as .d2s.bak.
    name = state()["name"]
    cmd("debug stat 14 777"); frames(2)
    cmd("save")
    for _ in range(6):
        if state()["screen"] == "charselect":
            break
        cmd("key Escape"); frames(6)
    st = state()
    assert st["screen"] == "charselect" and st["save"] == "0", st
    assert os.path.exists(os.path.join(saves, name + ".d2s.bak")), "no backup of the original save"
    cmd("click 690 555"); frames(10)
    st = state()
    assert st["screen"] == "ingame" and st["name"] == name and st["gold"] == "777", st

    cmd("quit")
    assert proc.wait(timeout=10) == 0

    # A new character (the char-create screen, CharStats.txt's start): made
    # and saved at once; it enters at level 1 and its file is there.
    proc = subprocess.Popen([d2d, "--headless", "--seed", "3", "--data", data, "--devctl", sock_path, "--start-screen", "charcreate",
                             "--start-class", "1", "--start-name", "Newbie", "--scale", "2"], env=env)
    for _ in range(100):
        if os.path.exists(sock_path):
            break
        time.sleep(0.1)
    frames(1)
    cmd("click 690 555"); frames(10)          # OK
    st = state()
    assert st["screen"] == "ingame" and st["name"] == "Newbie" and st["level"] == "1", st
    assert os.path.getsize(os.path.join(saves, "Newbie.d2s")) > 0x2FD
    cmd("quit")
    assert proc.wait(timeout=10) == 0
    print("OK")
finally:
    if proc.poll() is None:
        proc.kill()
    shutil.rmtree(home, ignore_errors=True)
    if os.path.exists(sock_path):
        os.remove(sock_path)
