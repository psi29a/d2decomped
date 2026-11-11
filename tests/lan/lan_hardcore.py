#!/usr/bin/env python3
# lan_hardcore.py: hardcore over a d2d LAN host, headless on 127.0.0.1:6113 (docs/design/lan-host.md).
# Softcore can't join hardcore and back, the dead can't join (game.exe's texts), a joiner's death
# reaches its own save and drops it to char select, a dead host keeps the game up. Throwaway heroes
# under a temp HOME. Usage: D2D=build/apps/d2d/d2d D2_MPQ_DIR=<mpqs> python3 tests/lan/lan_hardcore.py
import os, platform, shutil, socket, subprocess, sys, tempfile, time
D2D = os.path.abspath(os.environ.get("D2D", "build/apps/d2d/d2d"))
DATA = os.environ.get("D2_MPQ_DIR") or os.path.expanduser("~/Workspace/private/diablo2")
ROOT = tempfile.mkdtemp(prefix="d2d_lanhc_")
USER = "Library/Preferences/d2d" if platform.system() == "Darwin" else ".config/d2d"
fails, procs = [], []

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
    return out.decode(errors="replace").strip()

def home(name):
    h = f"{ROOT}/{name}"; shutil.rmtree(h, ignore_errors=True)
    os.makedirs(f"{h}/{USER}/save"); open(f"{h}/{USER}/d2d.cfg", "w").write("video = 0\n")

def save(name, hero=None): return f"{ROOT}/{name}/{USER}/save/{hero or name}.d2s"
def status(path): return open(path, "rb").read()[0x24]   # 0x04 hardcore, 0x08 died, 0x20 expansion
def state(c): return dict(f.split("=", 1) for f in ctl(c, "state").split() if "=" in f)
def players(c): return {r[1]: r[0] for r in (l.split("\t") for l in ctl(c, "debug players").splitlines()) if len(r) >= 7}
def log(name): return open(f"{ROOT}/{name}.log", errors="replace").read()

def run(name, args):
    h = f"{ROOT}/{name}"; c = f"{tempfile.gettempdir()}/d2hc-{os.getpid()}-{name}.ctl"   # unix socket paths: 104 bytes at most
    if os.path.exists(c): os.remove(c)
    env = {k: v for k, v in os.environ.items() if k != "XDG_CONFIG_HOME"}; env["HOME"] = h
    p = subprocess.Popen([D2D, "--data", DATA, "--headless", "--devctl", c, *args], stdout=open(f"{h}.log", "a"), stderr=subprocess.STDOUT, cwd=h, env=env)
    for _ in range(400):
        if os.path.exists(c) and ctl(c, "ping").startswith("ok"): procs.append((p, c)); return c
        if p.poll() is not None: break
        time.sleep(0.1)
    if p.poll() is None: p.kill(); p.wait()
    raise SystemExit(f"{name} didn't start, see {h}.log")

def stop_all():
    for p, c in procs:
        ctl(c, "quit")
        try: p.wait(15)
        except subprocess.TimeoutExpired: p.kill()
    procs.clear()

def check(what, ok, detail=""):
    print(f"  {'ok  ' if ok else 'FAIL'} {what}  {detail}", flush=True)
    if not ok: fails.append(what)

def make(name, hc, dead=False, cls="4"):
    home(name)
    c = run(name, ["--start-screen", "ingame", "--start-class", cls, "--start-name", name] + (["--start-hardcore"] if hc else []))
    time.sleep(2); ctl(c, "save")
    if dead: ctl(c, "debug die"); time.sleep(4); ctl(c, "cmd resurrect"); time.sleep(2)
    stop_all()
    return save(name)

def lan():
    print("LAN (127.0.0.1:6113)")
    def join(name):
        c = run(name, ["--start-screen", "charselect", "--connect", "127.0.0.1"]); time.sleep(1.5); ctl(c, "key Return"); time.sleep(4); return c
    files = {n: make(n, hc, dead) for n, hc, dead in (("JHc", True, False), ("JHc2", True, False), ("JSc", False, False), ("JDead", True, True))}
    home("HostHc")
    H = run("HostHc", ["--start-screen", "ingame", "--start-class", "1", "--start-name", "HostHc", "--start-hardcore", "--host", "6113"]); time.sleep(3)
    S = join("JSc")
    check("softcore joiner refused by a hardcore host", state(S).get("screen") == "charselect" and "JSc" not in players(H), players(H))
    check("told why (game.exe's text)", "normal character cannot join" in log("JSc"))
    D = join("JDead")
    check("a dead hardcore hero can't join", state(D).get("screen") == "charselect" and "JDead" not in players(H))
    J = join("JHc"); J2 = join("JHc2")
    check("hardcore joiners in", {"JHc", "JHc2"} <= set(players(H)), players(H))
    ctl(H, f"debug die {players(H)['JHc']}"); time.sleep(5)
    ctl(J, "cmd resurrect"); time.sleep(4)
    check("joiner's death reaches its own save", status(files["JHc"]) == 0x2c, hex(status(files["JHc"])))
    check("dead joiner back at char select, the host dropped it", state(J).get("screen") == "charselect" and "JHc" not in players(H), players(H))
    ctl(J, "key Return"); time.sleep(4)
    check("dead joiner can't rejoin", "JHc" not in players(H))
    ctl(H, "debug die"); time.sleep(5); ctl(H, "cmd resurrect"); time.sleep(2); ctl(H, "save"); s = state(H)
    check("dead host: save dead, game menu, still hosting", status(save("HostHc")) == 0x2c and not s.get("gamemenu", "0").startswith("0") and s.get("screen") == "ingame", s.get("gamemenu"))
    check("JHc2 plays on", state(J2).get("screen") == "ingame" and "JHc2" in players(H))
    def at(name): return next((tuple(float(v) for v in l.split("\t")[2:4]) for l in ctl(H, "debug players").splitlines() if f"\t{name}\t" in l), None)
    was = at("JHc2"); ctl(J2, f"cmd move {was[0] + 4:.1f} {was[1]:.1f}"); time.sleep(3)
    check("JHc2 walks with the host's game menu up (the hosted game goes on)", at("JHc2") != was, (was, at("JHc2")))
    stop_all()
    home("HostSc")
    H = run("HostSc", ["--start-screen", "ingame", "--start-class", "1", "--start-name", "HostSc", "--host", "6113"]); time.sleep(3)
    J2 = join("JHc2")
    check("hardcore joiner refused by a softcore host", state(J2).get("screen") == "charselect" and "JHc2" not in players(H), players(H))
    S = join("JSc")
    check("softcore joins softcore", "JSc" in players(H), players(H))
    ctl(H, f"debug die {players(H)['JSc']}"); time.sleep(5); ctl(S, "cmd resurrect"); time.sleep(3)
    check("softcore joiner dies, respawns, stays", state(S).get("screen") == "ingame" and "JSc" in players(H))
    stop_all()

try:
    lan()
finally:
    stop_all()
print("FAILS:", ", ".join(fails) if fails else "none")
sys.exit(1 if fails else 0)
