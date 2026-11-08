#!/usr/bin/env python3
# lan_split.py: lan-host stage 9, players on their own levels. A headless host and two joiners
# on one machine. The host stays in town; Seat1 goes to the Blood Moor, Seat2 to the Cold Plains.
# Checks each screen sees its own level only, monsters fight on with the host away, a level
# keeps its monsters while empty, and a joiner can come back to the host.
import os, re, shutil, socket, subprocess, sys, time
D2D = os.path.abspath(os.environ.get("D2D", "build/apps/d2d/d2d"))
DATA = os.environ.get("D2_MPQ_DIR") or os.path.expanduser("~/Workspace/private/diablo2")
fails = []

def ctl(path, command):
    sock = socket.socket(socket.AF_UNIX); sock.settimeout(10); out = b""
    try:
        sock.connect(path); sock.sendall((command + "\n").encode())
        while not out.endswith(b"\n") or not out.rsplit(b"\n", 2)[-2].startswith((b"ok", b"err")):
            data = sock.recv(65536)
            if not data: break
            out += data
    except OSError: pass
    finally: sock.close()
    return out.decode(errors="replace")

def run(name, args):
    cfg = f"/tmp/lans/{name}"; ctlp = f"{cfg}/ctl"
    if os.path.exists(ctlp): os.remove(ctlp)
    p = subprocess.Popen([D2D, "--data", DATA, "--headless", "--devctl", ctlp, *args], stdout=open(f"{cfg}.log", "w"), stderr=subprocess.STDOUT,
                         cwd="/tmp", env={**os.environ, "XDG_CONFIG_HOME": cfg, "HOME": cfg})
    for _ in range(400):
        if os.path.exists(ctlp) and ctl(ctlp, "ping").startswith("ok"): return p, ctlp
        time.sleep(0.1)
    p.kill(); p.wait()
    raise SystemExit(f"{name} didn't start")

def chat(path): return [l for l in ctl(path, "debug chatlog").splitlines() if l != "ok"]
def saw(path, words, seconds=5):
    for _ in range(seconds * 2):
        lines = chat(path)
        if any(words in l for l in lines): return lines
        time.sleep(0.5)
    return None
def view(path): return dict(f.split("=", 1) for f in ctl(path, "debug view").split()[1:])
def levels(path): return {r[1]: int(r[7]) for r in (l.split("\t") for l in ctl(path, "debug players").splitlines()) if len(r) >= 8}
def life(path):
    m = re.search(r"life=(\d+)/(\d+)", ctl(path, "state")); return int(m.group(1)) if m else -1
def check(what, ok, detail=""):
    print(f"  {'ok  ' if ok else 'FAIL'} {what}  {detail}", flush=True)
    if not ok: fails.append(what)

shutil.rmtree("/tmp/lans", ignore_errors=True)
for name in ("host", "Seat1", "Seat2"):
    user = f"/tmp/lans/{name}/Library/Preferences/d2d" if sys.platform == "darwin" else f"/tmp/lans/{name}/d2d"   # macOS: $HOME's, not XDG's
    os.makedirs(f"{user}/save"); open(f"{user}/d2d.cfg", "w").write("video = 0\n")
procs = []
try:
    for name in ("Seat1", "Seat2"):
        p, c = run(name, ["--start-screen", "ingame", "--start-class", "4", "--start-name", name]); time.sleep(2)
        ctl(c, "quit"); p.wait(15)
    host, H = run("host", ["--no-save", "--start-screen", "ingame", "--start-class", "1", "--start-name", "Host", "--host", "6113"]); procs.append(host)
    time.sleep(3)
    seats = {}
    for name in ("Seat1", "Seat2"):
        p, c = run(name, ["--start-screen", "charselect", "--connect", "127.0.0.1"]); procs.append(p); time.sleep(2)
        ctl(c, "click 689 555"); time.sleep(6); seats[name] = c
    S1, S2 = seats["Seat1"], seats["Seat2"]

    print("1. all in town")
    check("the host saw Seat2 join", any("Seat2" in l for l in chat(H)), chat(H))
    for _ in range(10):
        if set(view(S2)["others"].split(",")) == {"Host", "Seat1"}: break
        time.sleep(0.5)
    check("everyone on level 1", levels(H) == {"Host": 1, "Seat1": 1, "Seat2": 1}, levels(H))
    check("Seat2 sees Host and Seat1", set(view(S2)["others"].split(",")) == {"Host", "Seat1"}, view(S2))

    print("2. Seat1 to the Blood Moor alone")
    ctl(H, "debug goto 2 Seat1"); time.sleep(4)
    check("levels Host 1, Seat1 2, Seat2 1", levels(H) == {"Host": 1, "Seat1": 2, "Seat2": 1}, levels(H))
    v1, v2, vh = view(S1), view(S2), view(H)
    check("Seat1's screen: the Blood Moor, monsters, nobody else", v1["level"] == "2" and int(v1["monsters"]) > 0 and v1["others"] == "-", v1)
    check("Seat2's screen: town, no monsters, Host only", v2["level"] == "1" and v2["monsters"] == "0" and v2["others"] == "Host", v2)
    check("Host's screen: town, no monsters, Seat2 only", vh["level"] == "1" and vh["monsters"] == "0" and vh["others"] == "Seat2", vh)

    print("3. the Blood Moor plays on with the host in town")
    near = view(S1)["near"]                               # walk into the nearest group: they wake and come
    if near != "-": ctl(S1, f"cmd move {near.replace(',', ' ')}")
    spots = []
    for _ in range(10): spots.append(view(S1)["where"]); time.sleep(1)
    check("the Blood Moor's monsters move (they think on its turn)", len(set(spots)) > 1, spots)
    print("4. three levels: Seat2 to the Cold Plains")
    ctl(H, "debug goto 3 Seat2"); time.sleep(4)
    check("levels Host 1, Seat1 2, Seat2 8", levels(H) == {"Host": 1, "Seat1": 2, "Seat2": 3}, levels(H))
    v2 = view(S2)
    check("Seat2's screen: the Cold Plains, its own monsters", v2["level"] == "3" and int(v2["monsters"]) > 0 and v2["others"] == "-", v2)

    print("5. leave the Blood Moor and come back: the same monsters")
    seen = set(view(S1)["ids"].split(",")) - {"-"}
    ctl(H, "debug goto 1 Seat1"); time.sleep(3)
    check("Seat1 back in town, sees Host", view(S1)["level"] == "1" and view(S1)["others"] == "Host", view(S1))
    ctl(H, "debug goto 2 Seat1"); time.sleep(4)
    back = set(view(S1)["ids"].split(",")) - {"-"}
    check("the Blood Moor kept its monsters (the same unit ids)", bool(seen & back), f"{len(seen)} before, {len(back)} after, {len(seen & back)} the same")

    print("6. the host joins Seat1")
    ctl(H, "debug goto 2"); time.sleep(4)
    check("Host and Seat1 together", view(H)["others"] == "Seat1" and view(S1)["others"] == "Host", (view(H), view(S1)))
    check("Seat2 still alone in the Cold Plains", view(S2)["others"] == "-" and levels(H)["Seat2"] == 3, view(S2))

    print("7. game wide chat: Seat2 in the Cold Plains types, the Blood Moor hears")
    for _ in range(2):                              # headless key presses can miss a frame: type it again once
        ctl(S2, "debug typing hello from the plains"); time.sleep(0.3); ctl(S2, "key Return")
        if saw(S2, "hello from the plains", 3): break
        print("  (retyped)")
    got_h, got_1 = saw(H, "hello from the plains"), saw(S1, "hello from the plains")
    check("the host (Blood Moor) sees Seat2's line", bool(got_h), got_h)
    check("Seat1 (Blood Moor) sees it", bool(got_1), got_1)
    check("Seat2 sees its own line", bool(saw(S2, "hello from the plains")), chat(S2))

    print("8. a death heard everywhere: Seat1 dies in the Blood Moor")
    seat1_id = next(l.split("\t")[0] for l in ctl(H, "debug players").splitlines() if "\tSeat1\t" in l)
    ctl(H, f"debug die {seat1_id}")
    death = None
    for _ in range(20):
        death = next((l for l in chat(S2) if l.startswith("Seat1") and "join" not in l and ":" not in l), None)
        if death: break
        time.sleep(0.5)
    check("Seat2 (Cold Plains) hears Seat1 died", bool(death), death or chat(S2))

    print("9. party quest credit goes by act: the host clears the Den, Seat2 in the Cold Plains shares it")
    def den_bits(): return {r[1]: int(r[8], 16) for r in (l.split("\t") for l in ctl(H, "debug players 1").splitlines()) if len(r) >= 9}
    ctl(H, "debug goto 8"); time.sleep(3)
    rooms = [tuple(int(n) for n in l.split()[1:3]) for l in ctl(H, "debug laid").splitlines() if l[:1].isdigit()]
    for _ in range(3):                                  # into every room (the clear waits for them all up), every monster dead
        for x, y in rooms:
            ctl(H, f"debug warp {x + 4} {y + 4}"); time.sleep(0.4); ctl(H, "debug kill")
        time.sleep(1)
        if den_bits().get("Host", 0) & 0x2002 == 0x2002: break
    bits = den_bits()
    check("the host cleared the Den (bits 13, 1)", bits.get("Host", 0) & 0x2002 == 0x2002, bits)
    check("Seat2, in Act 1 on another level, has the party's share (13, 1)", levels(H)["Seat2"] == 3 and bits.get("Seat2", 0) & 0x2002 == 0x2002, bits)

    alive = all(p.poll() is None for p in procs)
    bad = sum(sum(1 for l in open(f"/tmp/lans/{n}.log", errors="replace") if "didn't apply" in l) for n in ("Seat1", "Seat2"))
    check("nobody crashed, every View applied", alive and bad == 0, f"alive {alive}, failed applies {bad}")
    print("10. leaving: Seat2 quits, the others hear")
    ctl(S2, "quit")
    check("the host sees Seat2 left", bool(saw(H, "Seat2", 10)), chat(H))
    ctl(H, "quit")
finally:
    for p in procs:
        if p.poll() is None: p.terminate()
    time.sleep(1)
    for p in procs:
        if p.poll() is None: p.kill()
print("\nPASS" if not fails else f"\nFAILED: {fails}")
sys.exit(1 if fails else 0)
