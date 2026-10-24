#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Which of the base install's sounds d2d can play, and which it can't.

Usage: sound_coverage.py <mpq dir> [--all]   (from the repo root, after a build)

Every Sounds.txt row whose file is in d2sfx / d2speech / d2music is put
in one bucket:
  - a data column names it (MonSounds, Skills, Missiles, ...): covered
    when d2d reads that column (kRead below; keep it in step);
  - else game.exe plays it from code: covered when d2d's sources name it
    (a string literal, a "{}" pattern, a number in sound_ids.hpp or the
    speech table).
A Group Size's members count as its first row. What's left is printed
by bucket: the sounds still to trace. --all prints every name.
Needs build/tools/mpq-cat/mpq-cat (and D2_PATCH_INSTALLER for 1.14d's
excel tables, as mpq-cat reads them).
"""
import collections, glob, os, re, subprocess, sys, tempfile

# The data columns d2d reads a sound from (components/game/gamedata_load.cpp).
kRead = {"MonSounds.Attack1", "MonSounds.Attack2", "MonSounds.Weapon1", "MonSounds.Weapon2", "MonSounds.HitSound",
         "MonSounds.DeathSound", "misc.dropsound", "misc.usesound", "armor.dropsound", "armor.usesound", "weapons.dropsound", "weapons.usesound",
         "SoundEnviron.Song", "SoundEnviron.Day Ambience", "SoundEnviron.Night Ambience",
         "SoundEnviron.Day Event", "SoundEnviron.Night Event",
         "MonSounds.Skill1", "MonSounds.Skill2", "MonSounds.Skill3", "MonSounds.Skill4", "MonSounds.Neutral", "MonSounds.Init", "MonSounds.Taunt", "MonSounds.Flee",
         "MonSounds.Footstep", "MonSounds.FootstepLayer",
         "skills.stsound", "skills.stsoundclass", "skills.ItemCastSound", "skills.dosound", "skills.tgtsound",
         "Missiles.TravelSound", "Missiles.HitSound", "Missiles.ProgSound", "states.onsound", "states.offsound"}
kTables = ["MonSounds", "skills", "Missiles", "states", "misc", "armor", "weapons", "Overlay", "SuperUniques",
           "objects", "Levels", "LvlTypes", "npc", "MonStats", "MonStats2", "shrines"]
kSoundEnviron = ("Song", "Day Ambience", "Night Ambience", "Day Event", "Night Event")
kBase = ("d2sfx", "d2speech", "d2music")

mpq_dir = sys.argv[1]
mpq_cat = os.path.join("build", "tools", "mpq-cat", "mpq-cat")

def cat(path, mpqs=mpq_dir):
    done = subprocess.run([mpq_cat, mpqs, path], capture_output=True)
    if done.returncode != 0:
        sys.exit(f"sound_coverage: mpq-cat {path}: {done.stderr.decode('latin-1').strip() or 'failed'}")
    return done.stdout.decode("latin-1")

def table(name):
    rows = [line.rstrip("\r").split("\t") for line in cat(f"data\\global\\excel\\{name}.txt").splitlines()]
    return (rows[0], rows[1:]) if rows else ([], [])

# The base install's files, one MPQ at a time (mpq-cat reads the first listfile it finds).
files = {}
with tempfile.TemporaryDirectory() as scratch:
    for mpq in kBase:
        os.mkdir(os.path.join(scratch, mpq))
        os.symlink(os.path.abspath(os.path.join(mpq_dir, mpq + ".mpq")), os.path.join(scratch, mpq, mpq + ".mpq"))
        for line in cat("(listfile)", os.path.join(scratch, mpq)).splitlines():
            files[line.strip().lower()] = mpq

head, body = table("Sounds")
sounds = [r for r in body if len(r) > 4 and r[0] and r[1].isdigit()]
by_name = {r[0]: int(r[1]) for r in sounds}
by_index = {int(r[1]): r for r in sounds}
group = {int(r[1]): int(r[4]) if r[4].isdigit() else 1 for r in sounds}
base = {int(r[1]) for r in sounds
        if any(root + r[2].lower() in files for root in ("data\\global\\sfx\\", "data\\local\\sfx\\", "data\\global\\music\\"))}

def spread(index, into):                         # a group's members with it
    for k in range(max(group.get(index, 1), 1)): into.add(index + k)

columns = collections.defaultdict(set)         # sound index -> table.column naming it
for name in kTables:
    h, rows = table(name)
    for row in rows:
        for i, value in enumerate(row[:len(h)]):
            if value in by_name:
                members = set(); spread(by_name[value], members)
                for m in members: columns[m].add(f"{name}.{h[i]}")
h, rows = table("SoundEnviron")
for row in rows:
    for col in kSoundEnviron:
        if col in h and row[h.index(col)].isdigit() and int(row[h.index(col)]) in by_index:
            members = set(); spread(int(row[h.index(col)]), members)
            for m in members: columns[m].add(f"SoundEnviron.{col}")

# What d2d's code names.
literals, patterns, numbers = set(), [], set()
for path in glob.glob("components/**/*.[ch]pp", recursive=True) + glob.glob("apps/**/*.[ch]pp", recursive=True):
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', open(path, encoding="utf-8", errors="ignore").read()):
        text = m.group(1)
        literals.add(text)
        if "{" in text: patterns.append(re.compile("^" + re.sub(r"\\\{[^}]*\\\}", "[a-z0-9]+", re.escape(text)) + "$"))
numbers |= {int(sound) for _, sound in re.findall(r"\{ (\d+), (\d+) \}", open("apps/d2d/speech_sound.hpp").read())}
ids = open("components/rules/sound_ids.hpp").read()
refs = "\n".join(open(path, encoding="utf-8", errors="ignore").read()
                   for root in ("components", "apps")
                   for path in glob.glob(root + "/**/*.[ch]pp", recursive=True))
numbers |= {int(sound) for name, sound in re.findall(r"inline constexpr int (\w+)\s*=\s*(\d+);", ids)
            if name in refs}
numbers |= {int(n) for n in re.findall(r"\{ (\d+), \d+ \}", ids)}
for name in ("kClassSelect", "kClassDeselect"):
    row = re.search(name + r"\s*=\s*\{([^}]+)\}", ids)
    if row and name in refs: numbers |= {int(n) for n in re.findall(r"\d+", row.group(1))}
# Town::class_cue builds `<class>_<line>` from the active character's class.
# Expand those literal line suffixes across the seven classic class prefixes.
class_prefixes = ("amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin")
for path in glob.glob("components/**/*.cpp", recursive=True):
    source = open(path, encoding="utf-8", errors="ignore").read()
    for line in re.findall(r'class_cue\("([^"]+)"', source):
        numbers |= {by_name[f"{prefix}_{line}"] for prefix in class_prefixes if f"{prefix}_{line}" in by_name}
# The find lines (rules::kFindLines) go to class_cue from a table.
find_lines = re.findall(r'\{ \{[^}]*\}, \d+, "(act\d_find_\w+)"', open("components/rules/quests.hpp", encoding="utf-8").read())
numbers |= {by_name[f"{prefix}_{line}"] for line in find_lines for prefix in class_prefixes if f"{prefix}_{line}" in by_name}
# Say sounds are selected by arithmetic from each class's first phrase row.
town = open("apps/d2d/town.cpp", encoding="utf-8").read()
chat = re.search(r"kChatSound\s*=\s*\{([^}]+)\}", town)
if chat:
    numbers |= {int(value, 0) + offset for value in re.findall(r"0x[0-9a-fA-F]+|\d+", chat.group(1)) for offset in range(16)}
for line in ids.splitlines():                  # the impact's element rows (hex); the other hex there are hit classes and MonStats ids
    if "out.element =" in line: numbers |= {int(n, 16) for n in re.findall(r"0x([0-9a-f]+)", line.split("out.element =", 1)[1])}
numbers |= {int(n) for n in re.findall(r"out\.impact = (\d+)", ids)}
# The object table's sound fields only: { id, { 8 mode rows }, { sound, mode, sound, mode } }, and object 0x3d's loops.
objects = open("components/rules/object_sounds.hpp").read()
for modes, loops in re.findall(r"\{ \d+, \{ ([\d, ]+) \}, \{ ([\d, ]+) \}", objects):
    loop = [int(n) for n in loops.split(",")]
    numbers |= {int(n) for n in modes.split(",")} | {loop[0], loop[2]}
numbers |= {int(n) for n in re.search(r"kObject61Loops = \{ ([\d, ]+) \}", objects).group(1).split(",")}
numbers.discard(0)
for line in ids.splitlines():                  # the voice and greeting tables' rows (their lines open with a brace), not comments
    if line.lstrip().startswith("{") or "kPlayerVoice = " in line:
        numbers |= {int(n) for n in re.findall(r"(?<![x\w])(\d{3,4})\b", line)}
numbers |= {by_name[text] for text in literals if text in by_name}
code = set()
for n in numbers: spread(n, code)
# Rows reached by an offset from one named: a footstep's floor and run
# (sound_ids::footstep, + 4 a material, + 24 running) and a greeting's time of
# day (greeting_row, + 1 / + 2).
steps = {int(n) for n in re.findall(r"\{ \d+, \d+, (\d+) \}", ids)}
steps |= {by_name[row[h_mon.index(col)]] for h_mon, rows_mon in [table("MonSounds")] for row in rows_mon
          for col in ("Footstep",) if col in h_mon and h_mon.index(col) < len(row) and row[h_mon.index(col)] in by_name}
for first in steps:
    for k in range(48): code.add(first + k)
for first in (int(t) for t in re.findall(r"\{ 0x[0-9a-f]+, \d+, \d+, (\d+), \d+ \}", ids)):
    for k in range(3): spread(first + k, code)

def in_code(index):
    name = by_index[index][0]
    stem = re.sub(r"_?\d+$", "", name)
    return index in code or stem in literals or any(p.match(name) or p.match(stem) for p in patterns)

todo = collections.defaultdict(list)
covered = 0
for index in sorted(base):
    named = columns.get(index)
    # A sound may share its name with a non-sound table field (e.g. the
    # shrine overlay/state `shrine_skill` and the sound of that name). A
    # readable sound column covers it, but an unrelated field must not hide
    # a real code cue for the same Sounds.txt row.
    ok = bool(named and named & kRead) or in_code(index)
    if ok: covered += 1; continue
    bucket = ", ".join(sorted(named)) if named else "code: " + by_index[index][0].split("_")[0]
    todo[bucket].append(by_index[index][0])
print(f"base install sounds: {len(base)}, d2d can play {covered}, not yet {len(base) - covered}")
for bucket, names in sorted(todo.items(), key=lambda kv: -len(kv[1])):
    shown = names if "--all" in sys.argv else sorted({re.sub(r"_\d+$", "", n) for n in names})[:6]
    print(f"{len(names):5d}  {bucket}: {' '.join(shown)}")
