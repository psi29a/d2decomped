# SPDX-License-Identifier: GPL-3.0-or-later
"""One ctest oracle run: game.exe's own code (this folder's script under the
emu .venv) against the d2d test's oracle mode on the same cases.

    python3 oracle_test.py NAME TEST_BINARY

NAME is a key of RECIPES. Exits 0 when the two agree, 1 when they differ
(the differing lines go to stdout), 77 (ctest's skip) without the emu .venv
(`uv sync` here) or without bin/game.exe beside the MPQs. ctest hands it
D2_MPQ_DIR, D2_PATCH_INSTALLER and D2_BUILD (emu.py's mpq-cat). Each run takes
CASES cases, so a ctest run stays a few minutes; the README rows run more.
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SKIPPED = 77
CASES = "2000"

# Each recipe: the steps in order (`{py}` the oracle's python, `{d2d}` the
# test binary, `{dir}` the scratch folder, `>name` sends stdout to that file),
# then how it passes: ("diff", game file, d2d file, d2d line prefix regex) or
# ("line", the compare step's last output, a regex it must match).
RECIPES = {
    "armor_hit": (["{py} armor_hit.py gen " + CASES + " 1 >cases", "{py} armor_hit.py game {dir}/cases >game",
                   "{d2d} --armor-hit {dir}/cases >d2d", "{py} armor_hit.py compare {dir}/cases {dir}/game {dir}/d2d >out"],
                  ("exit",)),
    "boss_level": (["{py} boss_level.py gen " + CASES + " 1 >cases", "{py} boss_level.py game {dir}/cases >game",
                    "{d2d} --boss-level {dir}/cases >d2d", "{py} boss_level.py compare {dir}/cases {dir}/game {dir}/d2d >out"],
                   ("line", r"mismatching cases 0, no d2d row 0, missing answers 0")),
    "cold_length": (["{py} chill_length.py " + CASES + " 1 >game", "{d2d} --cold-length 1 " + CASES + " >d2d"],
                    ("diff", "game", "d2d", r"[BR][0-9]")),
    "freeze_resume": (["{py} freeze_resume.py gen " + CASES + " 1 >cases", "{py} freeze_resume.py game {dir}/cases >game",
                       "{d2d} --freeze-resume {dir}/cases >d2d", "{py} freeze_resume.py compare {dir}/cases {dir}/game {dir}/d2d >out"],
                      ("exit",)),
    "freeze_rules": (["{py} freeze_rules.py gen " + CASES + " 1 >cases", "{py} freeze_rules.py game {dir}/cases >game",
                      "{d2d} --freeze-rules {dir}/cases >d2d", "{py} freeze_rules.py compare {dir}/cases {dir}/game {dir}/d2d >out"],
                     ("line", r"mismatches 0 resfail 0")),
    "hammer_hit": (["{py} hammer_hit.py gen " + CASES + " 1 >cases", "{py} hammer_hit.py game {dir}/cases >game",
                    "{d2d} --hammer-hit {dir}/cases >d2d"],
                   ("diff", "game", "d2d", r"[0-9]+ ")),
    "light_pierce": (["{d2d} --light-hit 1 400 >d2d", "{py} light_pierce.py {dir}/d2d >game"],
                     ("diff", "game", "d2d", r"[HT]")),
    "missile_hit": (["{py} missile_hit.py gen " + CASES + " 1 >cases", "{py} missile_hit.py game {dir}/cases >game",
                     "{d2d} --missile-hit {dir}/cases >d2d"],
                    ("diff", "game", "d2d", r"[0-9]+ ")),
    "next_hit": (["{d2d} --next-hit 1 400 >d2d", "{py} next_hit.py {dir}/d2d >game"],
                 ("diff", "game", "d2d", r"[HT]")),
    "sk_resist": (["{py} sk_resist.py gen " + CASES + " 1 >cases", "{py} sk_resist.py game {dir}/cases >game",
                   "{d2d} --resist {dir}/cases >d2d", "{py} sk_resist.py cmp {dir}/cases {dir}/game {dir}/d2d >out"],
                  ("line", r"adj differ 0, effres differ 0, dealt differ 0")),
    "static_field": (["{py} static_field.py " + CASES + " 1 >game", "{d2d} --static-field 1 " + CASES + " >d2d"],
                     ("diff", "game", "d2d", r"[BR][0-9]")),
    "umod_stats": (["{py} umod_stats.py " + CASES + " 1 >game", "{d2d} --umod-stats {dir}/game >d2d",
                    "{py} umod_stats.py --compare {dir}/game {dir}/d2d >out"],
                   ("line", r"differ \(any field\): 0 keys")),
    "walk_velocity": (["{py} walk_velocity.py gen " + CASES + " 9191 >cases", "{py} walk_velocity.py game {dir}/cases >game",
                       "{d2d} --walk-velocity {dir}/cases >d2d", "{py} walk_velocity.py compare {dir}/cases {dir}/game {dir}/d2d >out"],
                      ("exit",)),
}


def run(name, binary):
    steps, check = RECIPES[name]
    binary = Path(binary).resolve()
    venv = HERE / ".venv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not venv.exists():
        print(f"SKIP: no emu .venv: `uv sync` in {HERE}")
        return SKIPPED
    mpq_dir = os.environ.get("D2_MPQ_DIR", "")
    if not (Path(mpq_dir) / "bin" / "game.exe").exists():
        print(f"SKIP: no bin/game.exe in D2_MPQ_DIR ({mpq_dir or 'unset'})")
        return SKIPPED
    with tempfile.TemporaryDirectory() as scratch:
        for number, step in enumerate(steps, 1):
            command, _, out = step.partition(" >")
            argv = [str(venv) if word == "{py}" else str(binary) if word == "{d2d}" else word.replace("{dir}", scratch)
                    for word in command.split()]
            with open(Path(scratch) / out, "w") as sink:
                status = subprocess.run(argv, cwd=HERE, stdout=sink, stderr=subprocess.DEVNULL, check=False).returncode
            if status != 0:
                if out == "out":                                    # a compare that found a difference
                    print((Path(scratch) / out).read_text())
                print(f"{name}: step {number} `{command}` exited {status}")
                return 1
        if check[0] == "exit":
            print((Path(scratch) / "out").read_text())
            return 0
        if check[0] == "line":
            text = (Path(scratch) / "out").read_text()
            print(text)
            return 0 if re.search(check[1], text) else 1
        _, game_file, d2d_file, prefix = check
        game = (Path(scratch) / game_file).read_text().splitlines()
        d2d = [line for line in (Path(scratch) / d2d_file).read_text().splitlines() if re.match(prefix, line)]
        if not game:
            print(f"{name}: game.exe gave no lines")
            return 1
        differ = [(index, theirs, ours) for index, (theirs, ours) in enumerate(zip(game, d2d)) if theirs != ours]
        for index, theirs, ours in differ[:20]:
            print(f"line {index}: game {theirs!r} d2d {ours!r}")
        print(f"{name}: {len(game)} game lines, {len(d2d)} d2d lines, {len(differ)} differ")
        return 1 if differ or len(game) != len(d2d) else 0


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in RECIPES:
        sys.exit(__doc__)
    sys.exit(run(sys.argv[1], sys.argv[2]))
