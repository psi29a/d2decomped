#!/usr/bin/env python3
"""End-to-end smoke test: drive a headless d2d over devctl.

Usage: smoke_d2d.py <path-to-d2d>

Runs d2d on SDL's dummy driver with a throwaway $HOME (so the per-user
save/screenshot dirs are sandboxed), seeds two synthetic .d2s saves — or
copies every .d2s from $D2_SAVES_DIR when set — then clicks through
CharSelect -> InGame and pans left (the old infinite-loop direction).
Skips when the MPQ dir isn't reachable, like the other asset tests.
"""
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

sock_path = os.path.join(tempfile.gettempdir(), f"d2d_smoke_{os.getpid()}.sock")
env = dict(os.environ, HOME=home)
env.pop("D2_MPQ_DIR", None)   # --data wins anyway; keep the env honest
# --scale 2: every click below is in 800x600 game pixels, so the whole run
# also checks window<->game coordinate conversion.
proc = subprocess.Popen([d2d, "--headless", "--data", data, "--devctl", sock_path,
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
    kv = dict(tok.split("=", 1) for tok in cmd("state").split("\n")[0].split())
    print("state:", kv)
    return kv

def frames(n=6):
    time.sleep(n / 60)

try:
    for _ in range(100):
        if os.path.exists(sock_path):
            break
        assert proc.poll() is None, "d2d exited during startup"
        time.sleep(0.1)
    frames()

    st = state()
    assert st["screen"] == "charselect" and st["save"] == "-1"
    n = int(st["saves"])
    assert n == (len([f for f in os.listdir(saves) if f.endswith(".d2s")]))

    cmd("click 690 555")              # OK with nothing picked: stays put
    frames()
    assert state()["screen"] == "charselect"

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

    cmd("click 690 555")              # OK -> InGame as that save
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

    cmd("quit")
    assert proc.wait(timeout=10) == 0
    print("OK")
finally:
    if proc.poll() is None:
        proc.kill()
    shutil.rmtree(home, ignore_errors=True)
    if os.path.exists(sock_path):
        os.remove(sock_path)
