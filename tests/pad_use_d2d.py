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
data = os.environ.get("D2_MPQ_DIR") or os.path.expanduser("~/Workspace/private/diablo2")
if not os.path.exists(os.path.join(data, "d2data.mpq")):
    print(f"SKIP: {data}/d2data.mpq not found")
    sys.exit(0)

home = tempfile.mkdtemp(prefix="d2d_pad_use_")
user = os.path.join(home, "Library/Preferences/d2d" if platform.system() == "Darwin" else ".config/d2d")
saves = os.path.join(user, "save")
os.makedirs(saves)
for index, name in enumerate(["Alpha"]):   # expansion softcore, newest first: Alpha
    b = bytearray(0x2FD)
    struct.pack_into("<III", b, 0, 0xAA55AA55, 96, len(b))
    b[0x14:0x14 + len(name)] = name.encode()
    b[0x24], b[0x28], b[0x2B] = 0x20, index + 1, 10
    struct.pack_into("<I", b, 0x30, 1_000_000 - index)   # last played
    with open(os.path.join(saves, name + ".d2s"), "wb") as f:
        f.write(b)
patch = os.environ.get("D2_PATCH_INSTALLER")
if patch and os.path.exists(patch):
    with open(os.path.join(user, "d2d.cfg"), "w") as f:
        f.write(f"patch = {patch}\n")

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

# A town portal: read in the Blood Moor, A at its mouth takes it to camp.
cmd("debug goto 2"); frames(20)
cmd("debug equip tsc 12")
scroll = cmd("items").split("[tsc ")[1].split(" id=")[1].split()[0].rstrip("]")
cmd(f"cmd use {scroll}")
until("the portal opens", lambda: any(line.startswith("0\t") for line in cmd("debug portals").splitlines()))
portal = [line.split("\t") for line in cmd("debug portals").splitlines() if line.startswith("0\t")][0]
cmd(f"debug warp {int(float(portal[2])) + 2} {int(float(portal[3]))}"); frames(10)
face(float(portal[2]), float(portal[3]))
press("a")
until("through the portal to camp", lambda: level() == 1)

# The waypoint: A opens it, the cursor on its first row; down and A travel.
cmd("debug wp 1")                                   # the Cold Plains' is on
waypoint = [line.split("\t") for line in cmd("debug presets").splitlines() if line.endswith("\tWaypoint")][0]
wp_x, wp_y = float(waypoint[2]), float(waypoint[3])
cmd(f"debug warp {int(wp_x) + 2} {int(wp_y)}"); frames(10)
time.sleep(10)                                      # game.exe's 10 s from arriving (pcdata+0x160)
face(wp_x, wp_y)
press("a"); frames(20)
assert " waypoint=9 " in cmd("state"), cmd("state")[:400]
press("dpdown"); press("a")
until("by waypoint to the Cold Plains", lambda: level() == 3)
print("ok: pad A takes portals and waypoints")
