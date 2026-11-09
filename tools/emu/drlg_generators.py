# SPDX-License-Identifier: GPL-3.0-or-later
"""Run game.exe's five large DRLG generators and diff d2d's port of them, per generator.

    D2_BUILD=<build dir with tools/drlg-dump and tools/mpq-cat> uv run python drlg_generators.py [--mutation-check]

FUN_00665950 (DS1 parse), FUN_00667970 (preset area), FUN_00675850 (act 4
outdoors), FUN_006719f0 (the Arcane Sanctuary's maze) and FUN_00677880 (act 3
outdoor placement) are too large and call too much for a callee-log model, so
this oracle runs them for real in the emulator instead: it counts each
entry (a code hook on the function's first instruction), requires every
generator to have been entered during the run (FUN_00665950 parses the DS1s
at table load, so its entries are the boot's), and diffs game.exe's
level dump and tiles with `build/tools/drlg-dump ... tiles` (d2d's C++ port)
through diff_drlg.compare(). Equal output on every row is equivalence of the
port's end result for those seeds, not of the function body step by step:
the function's own intermediate state is not compared. `--mutation-check`
corrupts d2d's side three ways (a changed value, a dropped line, two lines
swapped) and requires each to be reported.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from unicorn import UC_HOOK_CODE

import diff_drlg
import drlg
import emu

# generator -> (entry address, [(level, first seed, last seed)])
GENERATORS = {
    "FUN_00665950": (0x665950, [(2, 1, 8), (8, 1, 8)]),
    "FUN_00667970": (0x667970, [(2, 1, 8), (8, 1, 8), (40, 1, 8), (79, 1, 8)]),
    "FUN_00675850": (0x675850, [(2, 1, 8), (104, 1, 8), (106, 1, 8)]),
    "FUN_006719f0": (0x6719F0, [(74, 1, 8)]),
    "FUN_00677880": (0x677880, [(76, 1, 8), (79, 1, 8), (83, 1, 8)]),
}
COUNTS = {name: 0 for name in GENERATORS}


def install_counters():
    """Make every Emu created from now on count entries into the generators."""
    original = emu.Emu.__init__
    by_addr = {addr: name for name, (addr, _) in GENERATORS.items()}

    def counted(self, *args, **kwargs):
        """Emu.__init__ plus one code hook per generator entry."""
        original(self, *args, **kwargs)

        def hit(uc, address, size, user):
            """Count one entry."""
            COUNTS[by_addr[address]] += 1
        for addr in by_addr:
            self.mu.hook_add(UC_HOOK_CODE, hit, None, addr, addr)
    emu.Emu.__init__ = counted


def mutate(path, how):
    """Corrupt one of d2d's dump files: change a value, drop a line, or swap two lines."""
    lines = path.read_text().splitlines()
    if how == "value":
        i = next(k for k, line in enumerate(lines) if any(c.isdigit() for c in line) and k > 0)
        lines[i] = lines[i].replace(next(c for c in lines[i] if c.isdigit()), "9" if next(c for c in lines[i] if c.isdigit()) != "9" else "8", 1)
    elif how == "drop":
        del lines[len(lines) // 2]
    else:
        i = next(k for k in range(1, len(lines) - 1) if lines[k] != lines[k + 1])
        lines[i], lines[i + 1] = lines[i + 1], lines[i]
    path.write_text("\n".join(lines) + "\n")


def run_rows(mutation=None):
    """Run every row; returns the mismatching rows (empty when the port matches game.exe)."""
    env = dict(os.environ)
    env.setdefault("D2_PATCH_INSTALLER", str(Path.home() / "Downloads/Diablo II + LoD/patch/LODPatch_114d.exe"))
    env.setdefault("ORDER", "shuffle")
    bad_rows = []
    e = drlg.boot()
    rows = [(name, lvl, a, b) for name, (_, items) in GENERATORS.items() for lvl, a, b in items]
    done = {}
    for name, lvl, first, last in rows:
        if (lvl, first, last) in done:
            bad = done[(lvl, first, last)]
        else:
            with tempfile.TemporaryDirectory() as out:
                Path(out, "tiles").mkdir()
                dump = emu.BUILD / "tools/drlg-dump/drlg-dump"
                if subprocess.run([str(dump), emu.data_path(), f"{first}-{last}", str(lvl), str(Path(out, "tiles")), "tiles"],
                                  env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
                    raise SystemExit(f"drlg-dump failed for level {lvl}")
                if mutation:
                    mutate(Path(out, "tiles", f"{first}.txt"), mutation)
                e, bad = diff_drlg.compare(e, first, last, lvl, "tiles", Path(out, "tiles"), env)
            done[(lvl, first, last)] = bad
        if bad:
            bad_rows.append((name, lvl, bad))
    return bad_rows


def main():
    """Run the rows, require every generator entered, and report; or run the three corruptions."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    install_counters()
    if args.mutation_check:
        for how in ("value", "drop", "swap"):
            for name in COUNTS:
                COUNTS[name] = 0
            if not run_rows(how):
                raise SystemExit(f"mutation {how}: NOT caught")
            print(f"mutation {how}: caught")
        print("ok: 3 mutations all caught")
        return 0
    bad = run_rows()
    missing = [name for name, n in COUNTS.items() if n == 0]
    if bad or missing:
        raise SystemExit(f"mismatch {bad}; generators never entered: {missing}")
    print("ok: " + ", ".join(f"{name} entered {n}x" for name, n in COUNTS.items()) + "; port output equals game.exe on every row")
    return 0


if __name__ == "__main__":
    sys.exit(main())
