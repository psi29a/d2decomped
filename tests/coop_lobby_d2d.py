#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Local co-op's lobby (docs/design/local-coop.md "The lobby").

Usage: coop_lobby_d2d.py <path-to-d2d>

Two virtual pads on character select: the second presses A for its
roster and picks a save (player 1's greyed), its Start doesn't start the
game; player 1 starts, and both are in it. Skips without the MPQs.
"""
import atexit, os, platform, socket, struct, subprocess, sys, tempfile, time

d2d = sys.argv[1]
data = os.environ.get("D2_MPQ_DIR") or os.path.expanduser("~/Workspace/private/diablo2")
if not os.path.exists(os.path.join(data, "d2data.mpq")):
    print(f"SKIP: {data}/d2data.mpq not found")
    sys.exit(0)

home = tempfile.mkdtemp(prefix="d2d_lobby_")
user = os.path.join(home, "Library/Preferences/d2d" if platform.system() == "Darwin" else ".config/d2d")
saves = os.path.join(user, "save")
os.makedirs(saves)
for index, name in enumerate(["Alpha", "Beta", "Gamma"]):   # expansion softcore, newest first: Alpha
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

sock_path = os.path.join(tempfile.gettempdir(), f"d2d_lobby_{os.getpid()}.sock")
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

def press(pad, button):
    cmd(f"pad {pad} {button} down"); frames(3)
    cmd(f"pad {pad} {button} up"); frames(3)

for _ in range(400):
    if os.path.exists(sock_path):
        break
    assert proc.poll() is None, "d2d exited during startup"
    time.sleep(0.1)
frames(1)
cmd("pad 0 plug")
cmd("pad 1 plug")
frames(5)
press(1, "start")                  # a partner's Start: its roster, not the game
assert "screen=charselect" in cmd("state"), "a partner started the game"
press(1, "b")                      # back out of the roster
press(1, "a")                      # A: the roster again; A on Beta (Alpha, player 1's, greyed)
press(1, "a")
frames(5)
cmd("screenshot " + os.environ.get("LOBBY_SHOT", os.path.join(home, "lobby.png")))   # LOBBY_SHOT: where to keep it
cmd("key Return")                  # player 1 starts, as Alpha
frames(40)
assert "screen=ingame" in cmd("state")
started = players()
assert set(started) == {"Alpha", "Beta"} and started["Beta"][2] == 1, started
# Beta came from the lobby, not the in-game roster: no roster is open.
assert not [line for line in cmd("debug roster").splitlines() if "\t" in line], cmd("debug roster")
print("OK: co-op lobby: a partner picks Beta on character select, only player 1 starts, both play")
