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

--audit checks the files instead of d2d's runs, LoD's included: every
animation above whose COF layer has no DCC at all, or a DCC with fewer
frames than the mode runs (an object's Objects.txt FrameCnt,
FUN_004bcbb0; else the COF's), and every missile, overlay or item sprite
the data names that no MPQ holds. What's known (bugs.md) is listed
apart and doesn't fail the run; anything else exits 1. (Fewer directions than the COF is fine: a layer takes its
own DCC's, FUN_005ff760.)
"""
import collections, os, re, subprocess, sys, tempfile
from pathlib import Path

kBase = ("d2data", "d2char") + (("d2exp",) if "--audit" in sys.argv else ())
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
token_of = {}                                                 # (kind, COF stem) -> its token (the folder)
for token in kClasses:
    for stem in cofs[("chars", token)]: shows[("chars", stem)] = stem[2:4].upper(); token_of[("chars", stem)] = token

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
        if named: shows[(root, stem)] = mode; token_of[(root, stem)] = token
        else: unused[root] += 1

missing = []                                                  # --audit: sprites the data names that no MPQ holds
def sprites(kind, rows, column, folder, ext):
    for row in rows:
        name = row.get(column, "").lower()
        if name and any(f"data\\global\\{folder}\\{name}.{e}" in files for e in (ext, "dc6")): shows[(kind, name)] = ""   # mephisto's overlay is a DC6
        elif name: missing.append(f"{kind} {name}")
sprites("missiles", table("Missiles"), "CelFile", "missiles", "dcc")
sprites("overlays", table("Overlay"), "Filename", "overlays", "dcc")
for name in ("armor", "weapons", "misc"): sprites("items", table(name), "flippyfile", "items", "dc6")

if "--audit" in sys.argv:
    kLayers = ("hd", "tr", "lg", "ra", "la", "rh", "lh", "sh", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8")
    frame_cnt = collections.defaultdict(int)                  # (token, mode) -> its longest FrameCnt
    for row in table("objects"):
        for i, mode in enumerate(kObjectModes):
            if row.get(f"Mode{i}") == "1":
                key = (row.get("Token", "").lower(), mode)
                frame_cnt[key] = max(frame_cnt[key], int(row.get(f"FrameCnt{i}") or 0))
    dccs = collections.defaultdict(list)                      # (root, token, layer, mode + weapon) -> DCC paths
    for path in files:
        if m := re.fullmatch(r"data\\global\\(chars|monsters|objects)\\(\w+)\\(\w\w)\\(\w+)\.dc[c6]", path):   # a few are DC6 (Mephisto's)
            root, token, layer, stem = m.groups()
            if stem.startswith(token + layer) and len(stem) >= len(token) + len(layer) + 4:
                dccs[(root, token, layer, stem[-5:])].append(path)
    with tempfile.TemporaryDirectory() as heads:
        def fetch(paths, head):
            for at in range(0, len(paths), 500):
                subprocess.run([mpq_cat, "-d", heads] + (["-n", str(head)] if head else []) + [mpq_dir] + paths[at:at + 500], check=True)
        def read(path):
            try: return Path(heads, path.replace("\\", "/")).read_bytes()
            except FileNotFoundError: return b""
        cof_path = {key: f"data\\global\\{key[0]}\\{token_of[key]}\\cof\\{key[1]}.cof" for key in shows if key[0] in ("chars", "monsters", "objects")}
        fetch(sorted(cof_path.values()), 0)
        wanted, problems, known = {}, collections.defaultdict(list), collections.defaultdict(list)
        for (root, stem), path in sorted(cof_path.items()):
            cof = read(path)
            if len(cof) < 28 or len(cof) < 28 + 9 * cof[0]: problems[f"{root}: COF unreadable"].append(stem); continue   # its layer records too
            layer_count, frames = cof[0], cof[1]
            token = path.split("\\")[3]
            mode_weapon = stem[len(token):]
            need = frame_cnt.get((token, mode_weapon[:2].upper()), 0) if root == "objects" else frames
            for k in range(layer_count):
                kind, weapon = cof[28 + 9 * k], cof[28 + 9 * k + 5:28 + 9 * k + 8].decode("latin-1").lower()
                if kind >= 16: continue
                found = dccs.get((root, token, kLayers[kind], mode_weapon[:2] + weapon))
                if not found:
                    if root != "chars": known[f"{root}: a layer in no MPQ, the others drawn (game.exe: fatal if asked for, FUN_005fe990; bugs.md #31)"].append(f"{stem}/{kLayers[kind]}")
                    continue
                for dcc in found: wanted[dcc] = (root, need or frames)
        fetch(sorted(wanted), 24)
        for dcc, (root, need) in sorted(wanted.items()):
            head = read(dcc)
            if len(head) < 24: problems[f"{root}: DCC unreadable"].append(dcc); continue
            if dcc.endswith(".dc6"): have_frames = int.from_bytes(head[20:24], "little")
            else: have_frames = int.from_bytes(head[3:7], "little")
            if have_frames < need:
                line = f"{dcc.split(chr(92))[-1]} ({have_frames} frames, the mode runs {need})"
                if root == "objects": known[f"{root}: DCC short of FrameCnt, blank past its last frame as in game.exe (FUN_005feb80; bugs.md #30)"].append(line)
                else: known[f"{root}: DCC short of its mode, blank past its last frame as in game.exe (FUN_005feb80; bugs.md #29)"].append(line)
    unused = {"items flptail": "item tal \"Not used\", never spawns"}           # bugs.md #32
    for name in missing:
        if name in unused: known[name.split()[0] + f": named, in no MPQ, {unused[name]} (bugs.md #32)"].append(name.split()[1])
        else: problems[name.split()[0] + ": named, in no MPQ"].append(name.split()[1])
    print(f"audited {len(cof_path)} COFs, {len(wanted)} DCCs: {sum(map(len, problems.values()))} to look at, {sum(map(len, known.values()))} known")
    for title, buckets in (("to look at", problems), ("known (bugs.md)", known)):
        if buckets: print(title + ":")
        for bucket, names in sorted(buckets.items(), key=lambda kv: -len(kv[1])):
            shown = names if "--all" in sys.argv else names[:8]
            print(f"{len(names):5d}  {bucket}: {' '.join(shown)}{' ...' if len(shown) < len(names) else ''}")
    sys.exit(1 if problems else 0)

# The player modes d2d can draw (gamedata.hpp kModeCode).
player_modes = set(re.findall(r'"([A-Z0-9]{2})"', re.search(r"kModeCode\[\d+\] = \{([^}]*)\}",
                                                            Path("components/game/gamedata.hpp").read_text()).group(1)))

drawn = set()
for log in logs:
    for line in Path(log).read_text(encoding="latin-1").splitlines():
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
