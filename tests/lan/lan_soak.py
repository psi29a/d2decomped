#!/usr/bin/env python3
# lan_soak.py MINUTES [JOINERS] [netem]: a headless d2d host and JOINERS headless joiners (own profiles),
# walking and casting in the Blood Moor; every minute: host RSS, players in the host's World,
# Views that didn't apply, resyncs. netem (Linux tc): 50 ms +- 20 ms, 2% loss on loopback for the run.
import os, random, shutil, socket, subprocess, sys, time
D2D = os.path.abspath(os.environ.get("D2D", "build/apps/d2d/d2d"))
DATA = os.environ.get("D2_MPQ_DIR") or os.path.expanduser("~/Workspace/private/diablo2")
MINUTES = float(sys.argv[1]) if len(sys.argv) > 1 else 2
JOINERS = int(sys.argv[2]) if len(sys.argv) > 2 else 3
NETEM = "netem" in sys.argv

def ctl(path, command):
    sock = socket.socket(socket.AF_UNIX); sock.settimeout(5); out = b""
    try:
        sock.connect(path); sock.sendall((command + "\n").encode())
        while not out.endswith(b"ok\n") and b"err" not in out:
            data = sock.recv(65536)
            if not data: break
            out += data
    except OSError: pass
    finally: sock.close()
    return out.decode(errors="replace")

def wait_for(path, seconds=40):
    for _ in range(int(seconds * 10)):
        if os.path.exists(path) and ctl(path, "ping").startswith("ok"): return True
        time.sleep(0.1)
    return False

def players(path):
    rows = [l.split("\t") for l in ctl(path, "debug players").splitlines() if "\t" in l]
    return {r[1]: (float(r[2]), float(r[3])) for r in rows}

def grep(log, word): return sum(1 for l in open(log, errors="replace") if word in l)

def run(name, args, env_extra=None):
    cfg = f"/tmp/lans/{name}"; ctlp = f"{cfg}/ctl"
    if os.path.exists(ctlp): os.remove(ctlp)
    env = {**os.environ, "XDG_CONFIG_HOME": cfg, "HOME": cfg, **(env_extra or {})}
    p = subprocess.Popen([D2D, "--data", DATA, "--headless", "--devctl", ctlp, *args], stdout=open(f"{cfg}.log", "w"), stderr=subprocess.STDOUT, cwd="/tmp", env=env)
    if not wait_for(ctlp):
        p.kill(); p.wait()
        raise AssertionError(f"{name} didn't start")
    return p, ctlp

def ps(pid, field): return subprocess.run(["ps", "-o", f"{field}=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()   # Linux and macOS
def rss(pid): return int(ps(pid, "rss") or 0) // 1024

def cpu_seconds(pid):                                # ps's TIME: [[dd-]hh:]mm:ss[.ss]
    days, _, clock = ps(pid, "time").rpartition("-")
    seconds = 0.0
    for part in clock.split(":"): seconds = seconds * 60 + float(part or 0)
    return seconds + 86400 * int(days or 0)

names = [f"Seat{n + 1}" for n in range(JOINERS)]
shutil.rmtree("/tmp/lans", ignore_errors=True)
for name in ["host", *names]:
    user = f"/tmp/lans/{name}/Library/Preferences/d2d" if sys.platform == "darwin" else f"/tmp/lans/{name}/d2d"   # macOS: $HOME's, not XDG's
    os.makedirs(f"{user}/save"); open(f"{user}/d2d.cfg", "w").write("video = 0\n")
procs = []
netem_on = False
try:
    for name in names:                                   # each joiner's own character, made once
        p, c = run(name, ["--start-screen", "ingame", "--start-class", str(random.choice([0, 2, 3, 4])), "--start-name", name]); time.sleep(2)
        ctl(c, "quit"); p.wait(15)
    if NETEM: subprocess.run(["sudo", "-n", "tc", "qdisc", "add", "dev", "lo", "root", "netem", "delay", "50ms", "20ms", "loss", "2%"], check=True); netem_on = True
    host, H = run("host", ["--no-save", "--start-screen", "ingame", "--start-class", "4", "--start-name", "Host", "--host", "6113"]); procs.append(host); time.sleep(3)
    ctl(H, "debug stat 12 30"); ctl(H, "debug points 64 20"); ctl(H, "debug goto 2"); time.sleep(3)
    clients = []
    for name in names:
        p, c = run(name, ["--start-screen", "charselect", "--connect", "127.0.0.1"]); procs.append(p); time.sleep(2)
        ctl(c, "click 689 555"); time.sleep(6); clients.append((name, p, c))
    print("in the host's World:", sorted(players(H)), flush=True)
    start = time.time(); last = start; casts = moves = 0; ticks = cpu_seconds(host.pid)
    while time.time() - start < MINUTES * 60:
        for name, _proc, c in clients:
            here = players(H).get(name)
            if not here: continue
            x, y = here
            if random.random() < 0.3: ctl(c, f"cmd skill 0 {x + random.uniform(-4, 4):.1f} {y + random.uniform(-4, 4):.1f}"); casts += 1
            else: ctl(c, f"cmd move {x + random.uniform(-8, 8):.1f} {y + random.uniform(-8, 8):.1f}"); moves += 1
        hx, hy = players(H).get("Host", (0, 0)); ctl(H, f"cmd skill 64 {hx + 3:.1f} {hy + 2:.1f}")
        time.sleep(0.5)
        if time.time() - last >= 60:
            now = time.time(); t = cpu_seconds(host.pid); cpu = 100 * (t - ticks) / (now - last); ticks = t; last = now
            bad = sum(grep(f"/tmp/lans/{n}.log", "didn't apply") for n in names)
            print(f"{(last - start) / 60:4.1f} min: host RSS {rss(host.pid)} MB CPU {cpu:.0f}% of a core, players {len(players(H))}, joiners alive {sum(p.poll() is None for _, p, _ in clients)},"
                  f" apply failures {bad}, resyncs {grep('/tmp/lans/host.log', 'whole View')}, moves {moves} casts {casts}", flush=True)
    print("host frames/5s, last 6:", [l.split("alive: ")[1].split(" ")[0] for l in open("/tmp/lans/host.log") if "alive:" in l][-6:])
    print("left/lost lines:", grep("/tmp/lans/host.log", "left ("), "| joiner warnings:", sum(grep(f"/tmp/lans/{n}.log", "WARNING] lan") for n in names))
finally:
    if netem_on: subprocess.run(["sudo", "-n", "tc", "qdisc", "del", "dev", "lo", "root"], check=False)   # only the one this run put there
    for p in procs:
        if p.poll() is None: p.terminate()
    time.sleep(1)
    for p in procs:
        if p.poll() is None: p.kill()
