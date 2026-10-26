#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Smoke-test a d2d build: start it, take a screenshot, quit.

d2d runs with its own XDG_CONFIG_HOME (default: build/smoke-config)
and --no-save, so your real ~/.config/d2d and character saves are never
touched. The MPQs are read in place.

Usage:
  d2d_smoke.py [--d2d PATH] [--data MPQ_DIR] [--config DIR]
               [--screen ingame|title|charselect|...] [--class 0-6] [--wait SECONDS]
               [--windowed] [--out PNG] [-- extra d2d args]

Example:
  tools/d2d_smoke.py --data /path/to/mpqs --screen ingame --wait 20
Exit code: 0 if d2d answered ping and wrote the screenshot, else 1.
The d2d log is in <config>/d2d/d2d.log.
"""
import argparse
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_D2D = ROOT / "build/apps/d2d/d2d"
DEFAULT_DATA = Path(os.environ.get("D2_MPQ_DIR", Path.home() / "Workspace/private/diablo2"))


def send(sock_path, line, timeout=30.0):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(timeout)
        s.connect(str(sock_path))
        s.sendall((line + "\n").encode())
        # Replies are payload lines, then a final line "ok[ ...]" or "err ...".
        reply = b""
        while True:
            last = reply.rstrip(b"\n").rsplit(b"\n", 1)[-1]
            if reply.endswith(b"\n") and (last.startswith(b"ok") or last.startswith(b"err")):
                break
            chunk = s.recv(4096)
            if not chunk:
                break
            reply += chunk
        return reply.decode(errors="replace").strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--d2d", type=Path, default=DEFAULT_D2D)
    ap.add_argument("--data", type=Path, default=DEFAULT_DATA)
    ap.add_argument("--config", type=Path, default=ROOT / "build/smoke-config")
    ap.add_argument("--screen", default="ingame")
    ap.add_argument("--class", dest="cls", type=int, default=0,
                    help="0 Amazon, 1 Sorceress, 2 Necromancer, 3 Paladin, 4 Barbarian, 5 Druid, 6 Assassin (5-6: LoD)")
    ap.add_argument("--wait", type=float, default=15.0, help="seconds after ping before the screenshot")
    ap.add_argument("--windowed", action="store_true", help="open a real window instead of --headless")
    ap.add_argument("--out", type=Path, default=ROOT / "build/smoke.png")
    ap.add_argument("extra", nargs="*")
    a = ap.parse_args()
    if a.wait < 0:
        ap.error("--wait must be nonnegative")
    a.out = a.out.resolve()  # d2d resolves relative paths against its own screenshots dir
    if any(c.isspace() for c in str(a.out)):
        ap.error(f"--out can't hold whitespace (devctl splits its arguments on it): {a.out}")

    for p, what in ((a.d2d, "d2d binary"), (a.data / "d2data.mpq", "d2data.mpq")):
        if not p.exists():
            sys.exit(f"missing {what}: {p}")
    a.config.mkdir(parents=True, exist_ok=True)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    sock = a.config / "devctl.sock"
    sock.unlink(missing_ok=True)
    a.out.unlink(missing_ok=True)

    cmd = [str(a.d2d), "--data", str(a.data), "--devctl", str(sock), "--no-save", "--no-video",
           "--start-screen", a.screen, "--start-class", str(a.cls), "--start-name", "Smoke"]
    if not a.windowed:
        cmd.append("--headless")
    cmd += a.extra
    env = dict(os.environ, XDG_CONFIG_HOME=str(a.config))
    print("running:", " ".join(cmd))
    proc = subprocess.Popen(cmd, env=env)
    ok = False
    try:
        deadline = time.time() + 60
        while time.time() < deadline and proc.poll() is None:
            if sock.exists():
                try:
                    if send(sock, "ping", 5).endswith("ok"):
                        break
                except OSError:
                    pass
            time.sleep(0.5)
        else:
            print("d2d did not answer ping (exit code %s)" % proc.poll())
            return 1
        time.sleep(a.wait)
        print("info:", send(sock, "info"))
        print("screenshot:", send(sock, f"screenshot {a.out}"))
        ok = a.out.exists()
        print("quit:", send(sock, "quit"))
        proc.wait(timeout=20)
    finally:
        if proc.poll() is None:
            try:
                send(sock, "quit", 2)
                proc.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait(timeout=5)
        sock.unlink(missing_ok=True)
    print("d2d exit code:", proc.returncode, "| screenshot:", a.out if ok else "none")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
