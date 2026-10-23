#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Which of the base install's animations d2d has drawn, and which not yet.

Usage: anim_coverage.py <mpq dir> <anim log>... [--all]   (from the repo root, after a build)

The animations are what game.exe's data can show from the base install
(d2data / d2char; LoD's d2exp isn't read):
  - chars:    every player COF of the five classes (class, mode, weapon class);
  - monsters: a MonStats Code's COFs whose mode MonStats2 flags (mA1, mS1, ...)
              and whose weapon class is a BaseW of its rows;
  - objects:  an objects.txt Token's COFs whose mode its Mode0..7 flags;
  - missiles: Missiles.txt CelFile DCCs;
  - overlays: Overlay.txt Filename DCCs;
  - items:    armor / weapons / misc FlippyFile DC6s (the drop).
A file of the base install that no row names isn't counted (shown as
"unused by the data").

What d2d has drawn comes from its own runs: with D2D_ANIM_LOG=<file> set,
d2d appends each animation the first time it draws it (Scene::note_drawn).
Several runs can share one file, e.g.
    D2D_ANIM_LOG=$PWD/build/anims.log ctest --test-dir build -R quests_act
A player mode d2d has no code for (not in gamedata.hpp kModeCode) is
"no path": no run can draw it. What's left is printed by kind and mode:
the animations no run has reached yet, or that d2d never asks for.
Needs build/tools/mpq-cat/mpq-cat (and D2_PATCH_INSTALLER for 1.14d's
excel tables, as mpq-cat reads them).
"""
import collections, os, re, subprocess, sys, tempfile

kBase = ("d2data", "d2char")
kClasses = ("am", "ba", "ne", "pa", "so")                     # the base game's five
kMonModes = ("DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN")
kObjectModes = ("NU", "OP", "ON", "S1", "S2", "S3", "S4", "S5")   # gamedata.hpp kObjectModes

args = [a for a in sys.argv[1:] if not a.startswith("--")]
if not args: sys.exit(__doc__)
mpq_dir, logs = args[0], args[1:]
mpq_cat = os.path.join("build", "tools", "mpq-cat", "mpq-cat")

def cat(path, mpqs=mpq_dir):
    done = subprocess.run([mpq_cat, mpqs, path], capture_output=True)
    if done.returncode != 0:
        sys.exit(f"anim_coverage: mpq-cat {path}: {done.stderr.decode('latin-1').strip() or 'failed'}")
    return done.stdout.decode("latin-1")

def table(name):
    rows = [line.rstrip("\r").split("\t") for line in cat(f"data\\global\\excel\\{name}.txt").splitlines()]
    head = rows[0] if rows else []
    return [dict(zip(head, row)) for row in rows[1:]]

# The base install's files, one MPQ at a time (mpq-cat reads the first listfile it finds).
files = set()
with tempfile.TemporaryDirectory() as scratch:
    for mpq in kBase:
        os.mkdir(os.path.join(scratch, mpq))
        os.symlink(os.path.abspath(os.path.join(mpq_dir, mpq + ".mpq")), os.path.join(scratch, mpq, mpq + ".mpq"))
        files |= {line.strip().lower() for line in cat("(listfile)", os.path.join(scratch, mpq)).splitlines()}

cofs = collections.defaultdict(list)                          # (root, token) -> COF stems
for path in files:
    if m := re.fullmatch(r"data\\global\\(chars|monsters|objects)\\(\w+)\\cof\\(\w+)\.cof", path):
        cofs[(m[1], m[2])].append(m[3])

# What the data shows: (kind, name) -> its bucket's mode.
shows, unused = {}, collections.Counter()
for token in kClasses:
    for stem in cofs[("chars", token)]: shows[("chars", stem)] = stem[2:4].upper()

ms2 = {row.get("Id", ""): row for row in table("MonStats2")}
mon_modes, mon_weapons = collections.defaultdict(set), collections.defaultdict(set)
for row in table("MonStats"):
    extra = ms2.get(row.get("MonStatsEx", ""))
    token = row.get("Code", "").lower()
    if not extra or not token: continue
    mon_modes[token] |= {mode for mode in kMonModes if extra.get("m" + mode) == "1"}
    mon_weapons[token].add(extra.get("BaseW", "").lower() or "hth")

obj_modes = collections.defaultdict(set)
for row in table("objects"):
    token = row.get("Token", "").lower()
    obj_modes[token] |= {mode for i, mode in enumerate(kObjectModes) if row.get(f"Mode{i}") == "1"}

for (root, token), stems in cofs.items():
    if root == "chars": continue
    for stem in stems:
        mode, weapon = stem[len(token):len(token) + 2].upper(), stem[len(token) + 2:]
        named = (mode in mon_modes[token] and weapon in mon_weapons[token]) if root == "monsters" else mode in obj_modes[token]
        if named: shows[(root, stem)] = mode
        else: unused[root] += 1

def sprites(kind, rows, column, folder, ext):
    for row in rows:
        name = row.get(column, "").lower()
        if name and f"data\\global\\{folder}\\{name}.{ext}" in files: shows[(kind, name)] = ""
sprites("missiles", table("Missiles"), "CelFile", "missiles", "dcc")
sprites("overlays", table("Overlay"), "Filename", "overlays", "dcc")
for name in ("armor", "weapons", "misc"): sprites("items", table(name), "flippyfile", "items", "dc6")

# The player modes d2d can draw (gamedata.hpp kModeCode).
player_modes = set(re.findall(r'"([A-Z0-9]{2})"', re.search(r"kModeCode\[\d+\] = \{([^}]*)\}",
                                                            open("components/game/gamedata.hpp").read()).group(1)))

drawn = set()
for log in logs:
    for line in open(log, encoding="latin-1"):
        if len(parts := line.split()) == 2: drawn.add((parts[0], parts[1].lower()))

todo = collections.defaultdict(list)
by_kind = collections.Counter()
covered = 0
for (kind, name), mode in sorted(shows.items()):
    by_kind[kind] += 1
    if (kind, name) in drawn: covered += 1; continue
    bucket = f"{kind} {mode}".strip()
    if kind == "chars" and mode not in player_modes: bucket += " (no path)"
    todo[bucket].append(name)

print(f"base install animations: {len(shows)} ({', '.join(f'{k} {n}' for k, n in sorted(by_kind.items()))})")
if not logs: print("no anim log given: nothing drawn to compare (see --help)")
print(f"d2d has drawn {covered}, not yet {len(shows) - covered}"
      + (f" ({len(logs)} log{'s' if len(logs) > 1 else ''}, {len(drawn)} lines)" if logs else ""))
for bucket, names in sorted(todo.items(), key=lambda kv: -len(kv[1])):
    shown = names if "--all" in sys.argv else names[:8]
    print(f"{len(names):5d}  {bucket}: {' '.join(shown)}{' ...' if len(shown) < len(names) else ''}")
if unused: print("unused by the data: " + ", ".join(f"{root} {n} COFs" for root, n in sorted(unused.items())))
