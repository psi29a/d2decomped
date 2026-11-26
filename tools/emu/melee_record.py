# SPDX-License-Identifier: GPL-3.0-or-later
"""A melee record's enhanced damage before its build, FUN_0057b7d0 up to FUN_0057b420, as the oracle.

Run: uv run python melee_record.py [--mutation-check]
     uv run python melee_record.py inc --out ../../tests/melee_record_cases.inc

FUN_0057b7d0 (ECX game, EDX attacker; stack: target, record, -, SrcDam) runs natively for a player (type 0) on a
monster (type 1) with no MonStats row (FUN_0057b6a0's MonProp list skipped), with FUN_0057b660 under it, up to its
call of FUN_0057b420 (0x57b8a4), where the run stops and the record's +0xc is read. The claim, checked against
rules::melee_record_pct (tests/test_melee_record.cpp):
  - +0xc starts at the swing's enhanced damage;
  - against a demon (FUN_0063e940) it takes the attacker's stat 121 when that is over 0 (0x57b857..0x57b86f);
  - against the undead (FUN_0063e990) 50 with a Blunt weapon (FUN_00629bb0 type 0x39) plus stat 122, when the sum is
    over 0 (FUN_0057b660).
Hooked (inputs and side effects, not the claim): the stat getters FUN_00625480 / FUN_00625500 (the case's 121 / 122;
120 none) and setter FUN_00627260, FUN_00535d10, the alignment FUN_006259b0, the demon / undead tests, the weapon
getter FUN_00535bc0 and its type test, FUN_0057b420 (stops the run).
`inc` prints game.exe's answers for the test; `inc --out FILE` writes them only once the whole run has passed.
"""
import argparse
import hashlib
import random
import sys
import tempfile
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MELEE = 0x57B7D0
MASK = 0xFFFFFFFF
MUT = set()
FIELDS = ("ed", "s121", "s122", "blunt", "demon", "undead")


def signed(value):
    """A 32-bit word as a signed int."""
    value &= MASK
    return value - (1 << 32) if value & 0x80000000 else value


def model(case):
    """rules::melee_record_pct re-stated."""
    c = dict(zip(FIELDS, case))
    demon = c["s121"] if "demon_unguarded" in MUT else max(c["s121"], 0)
    if "no_demon" in MUT:
        demon = 0
    undead = max((50 if c["blunt"] else 0) + c["s122"], 0)
    return c["ed"] + (demon if c["demon"] else 0) + (undead if c["undead"] else 0)


class Game:
    """game.exe with a player, a monster and a record in the fake heap."""

    def __init__(self, exe):
        """Load the exe, lay out the units, hook the inputs."""
        self.machine = machine = emu.Emu(str(exe))
        self.unit, self.target, self.weapon, self.record = (machine.alloc(0x200) for _ in range(4))
        machine.w32(self.target, 1)
        machine.w32(self.target + 4, MASK)
        self.case, self.seen = {}, None
        stat = lambda e: {0x79: self.case["s121"], 0x7a: self.case["s122"]}.get(e.arg(1), 0) & MASK if e.arg(0) == self.unit else 0
        hooks = {0x625480: (stat, 3), 0x625500: (stat, 3), 0x627260: (lambda e: 0, 4), 0x6259b0: (lambda e: 0, 1),
                 0x63e940: (lambda e: self.case["demon"], 1), 0x63e990: (lambda e: self.case["undead"], 1),
                 0x535bc0: (lambda e: self.weapon, 0), 0x629bb0: (lambda e: self.case["blunt"] if e.arg(1) == 0x39 else 0, 2),
                 0x57b420: (self.stop, 6)}
        for addr, (fn, nstack) in hooks.items():
            machine.hook(addr, fn, nstack)
        machine.stub(0, {0x535d10: 0})

    def stop(self, machine):
        """FUN_0057b420 reached: read the record's +0xc and end the run."""
        self.seen = signed(machine.r32(self.record + 0xC))
        machine.mu.emu_stop()
        return 0

    def run(self, case):
        """FUN_0057b7d0 up to FUN_0057b420: the record's +0xc."""
        machine = self.machine
        self.case, self.seen = dict(zip(FIELDS, case)), None
        machine.mu.mem_write(self.record, bytes(0x200))
        machine.w32(self.record + 0xC, self.case["ed"])
        machine.call(MELEE, self.target, self.record, 0, 0x80, ecx=0, edx=self.unit, limit=200000)
        if self.seen is None:
            raise SystemExit(f"{case}: FUN_0057b420 not reached")
        return self.seen


def cases():
    """The percents round 0 on both sides, Blunt or not, each monster kind."""
    rng = random.Random(0x57B7D0)
    pct = lambda: rng.choice((0, rng.randint(-120, 120), rng.randint(-60, 60), rng.randint(1, 300)))
    return [(pct(), pct(), pct(), int(rng.random() < 0.4), int(rng.random() < 0.5), int(rng.random() < 0.5)) for _ in range(4000)]


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
        got = game.run(case)
        results.append((case, got))
        if got != model(case):
            bad += 1
            if bad <= 10 and not mutation:
                print(f"{case}: game {got} model {model(case)}", file=sys.stderr)
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
        lines = ["// Stock game.exe 1.14d, FUN_0057b7d0 up to FUN_0057b420; generated by tools/emu/melee_record.py inc.",
                 "// " + ", ".join(FIELDS) + "; then game.exe's record +0xc"]
        lines += ["    { " + ", ".join(str(value) for value in case + (got,)) + " }," for case, got in results[:1500]]
        text = "\n".join(lines) + "\n"
        if args.out:
            write_fixture(args.out, text)
        else:
            sys.stdout.write(text)
        return
    print(f"ok: {len(results)} FUN_0057b7d0 melee record cases, 0 differ")
    if args.mutation_check:
        for name in ("no_demon", "demon_unguarded"):
            missed, _ = run((name,))
            if not missed:
                print(f"mutation {name}: NOT caught", file=sys.stderr)
                sys.exit(1)
            print(f"mutation {name}: caught ({missed} differ)")
        print("mutation check ok")


if __name__ == "__main__":
    main()
