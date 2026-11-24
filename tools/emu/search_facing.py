# SPDX-License-Identifier: GPL-3.0-or-later
"""The facing a shift-click's search takes from the player, FUN_006238c0, as the oracle.

Run: export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
     uv run python search_facing.py [--mutation-check]
     uv run python search_facing.py inc --out ../../tests/search_facing_cases.inc

FUN_006238c0 (stack: the unit, `ret 4`) runs natively with a player (type 0)
whose dynamic path holds its subtile (+2, +6) and its target (+0x10, +0x12,
SP1) and its new direction (+0x65): the whole of FUN_006238c0, FUN_00621dc0,
FUN_0064fdc0 and the direction math under it run as game.exe has them.
The claim, checked against rules::search_facing (tests/test_search_facing.cpp):
  - at its target (both subtiles equal) the answer is path +0x65, the facing
    the path turns to;
  - anywhere else it is the 64-way direction from the unit's subtile to the
    target (FUN_0064fdc0), whatever +0x65 holds.
`inc` prints game.exe's answers for that test to stdout; `inc --out FILE` writes
them to FILE only once the whole run has passed.
"""
import argparse
import hashlib
import random
import sys
import tempfile
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
FACING = 0x6238C0
MUT = set()
KSTEP = (13, 26, 39, 53, 68, 85, 105)


def direction64(x, y, target_x, target_y):
    """rules::direction64 re-stated (FUN_0064fdc0 -> FUN_0064fc60): 0 faces +x+y, 64 ways."""
    across, down = abs(target_x - x), abs(target_y - y)
    steep = across <= down
    major, minor = (down, across) if steep else (across, down)
    eighth = 0 if major == 0 else min(max(((minor << 16) * 0x7F & 0xFFFFFFFF) // (major << 16), 0), 127)
    turn = sum(1 for step in KSTEP if step <= eighth)
    if not steep:
        turn = (-turn - 1) & 0xF
    if target_y < y:
        turn = (-turn - 1) & 0x1F
    return (turn + 8) & 0x3F if target_x < x else (((-turn - 1) & 0x3F) + 8) & 0x3F


def model(x, y, target_x, target_y, new_dir):
    """rules::search_facing re-stated: the path's +0x65 at its target, else the way to it."""
    if "x_only" in MUT:
        at = x == target_x
    else:
        at = x == target_x and y == target_y
    if "always_path" in MUT or at:
        return new_dir
    if "reversed" in MUT:
        return direction64(target_x, target_y, x, y)
    return direction64(x, y, target_x, target_y)


class Game:
    """game.exe with a player and its path in the fake heap."""

    def __init__(self, exe):
        """Load the exe and lay out one player."""
        self.machine = machine = emu.Emu(str(exe))
        self.unit, self.path = machine.alloc(0x200), machine.alloc(0x200)
        view = machine.view("unit", self.unit)
        view.type = 0
        view.path = self.path

    def facing(self, x, y, target_x, target_y, new_dir):
        """Run FUN_006238c0 on a player at (x, y) walking to the target."""
        machine = self.machine
        machine.mu.mem_write(self.path, bytes(0x80))
        for offset, value in ((2, x), (6, y), (0x10, target_x), (0x12, target_y)):
            machine.mu.mem_write(self.path + offset, value.to_bytes(2, "little"))
        machine.mu.mem_write(self.path + 0x65, bytes([new_dir]))
        return machine.call(FACING, self.unit, limit=20000) & 0xFF


def cases():
    """(x, y, target x, target y, new direction): the target on the unit, one off, a few away, far."""
    rng = random.Random(0x6238C0)
    out = []
    for _ in range(4000):
        x, y = rng.randrange(60, 4000), rng.randrange(60, 4000)
        pick = rng.random()
        if pick < 0.2:
            tx, ty = x, y
        elif pick < 0.3:
            tx, ty = x + rng.choice((0, 0, 1, -1)), y + rng.choice((1, -1, 0, 0))
        elif pick < 0.8:
            tx, ty = x + rng.randint(-12, 12), y + rng.randint(-12, 12)
        else:
            tx, ty = x + rng.randint(-250, 250), y + rng.randint(-250, 250)
        out.append((x, y, max(tx, 0), max(ty, 0), rng.randrange(64)))
    return out


def run(mutation):
    """Compare game.exe to the model; return (bad, results)."""
    MUT.clear()
    MUT.update(mutation)
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    game = Game(exe)
    bad, results = 0, []
    for case in cases():
        got = game.facing(*case)
        results.append((case, got))
        want = model(*case)
        if got != want:
            bad += 1
            if bad <= 10:
                print(f"{case}: game {got} model {want}", file=sys.stderr)
    return bad, results


def write_fixture(out, text):
    """Write `text` to `out` through a staging file in the same directory, removed on failure."""
    handle = tempfile.NamedTemporaryFile("w", dir=Path(out).parent, prefix=Path(out).name + ".", suffix=".tmp", delete=False)
    try:
        with handle:
            handle.write(text)
        Path(handle.name).replace(out)
    except BaseException:
        Path(handle.name).unlink(missing_ok=True)
        raise


def main():
    """Run the oracle; `inc` prints the C++ fixture; --mutation-check proves the mutants are caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", default="check", choices=("check", "inc"))
    parser.add_argument("--mutation-check", action="store_true")
    parser.add_argument("--out", help="with inc: write the fixture here, after the run has passed")
    args = parser.parse_args()
    if args.mode == "inc" and args.mutation_check:
        parser.error("inc and --mutation-check are separate runs")
    if args.out and args.mode != "inc":
        parser.error("--out goes with inc")
    bad, results = run(())
    if bad:
        print(f"FAIL: {bad} of {len(results)} differ", file=sys.stderr)
        sys.exit(1)
    if args.mode == "inc":
        lines = ["// Stock game.exe 1.14d, FUN_006238c0; generated by tools/emu/search_facing.py inc.",
                 "// unit x, y (subtiles), path target x, y (+0x10 / +0x12), path +0x65, the answer"]
        for (x, y, tx, ty, new_dir), got in results[:1500]:
            lines.append(f"    {{ {x}, {y}, {tx}, {ty}, {new_dir}, {got} }},")
        text = "\n".join(lines) + "\n"
        if args.out:
            write_fixture(args.out, text)
        else:
            sys.stdout.write(text)
        return
    print(f"{len(results)} cases, 0 differ")
    if args.mutation_check:
        for name in ("x_only", "always_path", "reversed"):
            missed, _ = run((name,))
            if not missed:
                print(f"mutation {name}: NOT caught", file=sys.stderr)
                sys.exit(1)
            print(f"mutation {name}: caught ({missed} differ)")
    print("ok")


if __name__ == "__main__":
    main()
