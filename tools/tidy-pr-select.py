#!/usr/bin/env python3
"""Pick the TUs a PR needs clang-tidy to re-run on.

Reads:
  - changed.txt: one relative .cpp / .hpp path per line, the PR's touched files
  - build/compile_commands.json: every TU the build knows about
  - `ninja -C build -t deps` output: header list per TU (from Ninja's dep db)
Writes to stdout: one absolute .cpp path per line, the intersection of the
build's TUs with "touches or includes a changed file." Empty output means
the PR needs no tidy pass.
"""
import json
import os
import subprocess
import sys


def ninja_deps(build_dir: str) -> dict[str, set[str]]:
    """Parse `ninja -t deps`. Keys are ninja target paths (relative to build);
    values are absolute header paths the TU pulls in."""
    text = subprocess.check_output(["ninja", "-C", build_dir, "-t", "deps"], text=True)
    out: dict[str, set[str]] = {}
    current: str | None = None
    for line in text.splitlines():
        if not line: continue
        if not line.startswith(" "):
            # e.g. "components/game/CMakeFiles/game.dir/fight.cpp.o: #deps N, ..."
            target = line.split(":", 1)[0].strip()
            current = os.path.abspath(os.path.join(build_dir, target)) if target.endswith(".o") else None
            if current is not None: out[current] = set()
        elif current is not None:
            path = line.strip()
            if path: out[current].add(os.path.abspath(os.path.join(build_dir, path)))
    return out


def main() -> int:
    if not os.path.exists("changed.txt"): return 0
    changed = {os.path.abspath(line.strip()) for line in open("changed.txt") if line.strip()}
    if not changed: return 0
    build_dir = os.environ.get("BUILD_DIR", "build")
    deps = ninja_deps(build_dir)                # keyed by absolute .o path
    entries = json.load(open(os.path.join(build_dir, "compile_commands.json")))
    picked: list[str] = []
    for entry in entries:
        tu = entry["file"]
        if os.path.abspath(tu) in changed:
            picked.append(tu)
            continue
        headers = deps.get(os.path.abspath(entry["output"]))
        if headers and headers & changed: picked.append(tu)
    for tu in sorted(set(picked)): print(tu)
    return 0


if __name__ == "__main__":
    sys.exit(main())
