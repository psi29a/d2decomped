# SPDX-License-Identifier: GPL-3.0-or-later
"""A missile's physical from its owner's weapon, FUN_0064b860 -> FUN_0064ac60 -> FUN_005a89a0, as the oracle.

Run: uv run python missile_record.py [--mutation-check]
     uv run python missile_record.py inc --out ../../tests/missile_record_cases.inc

game.exe's own code, on synthetic units and one synthetic Missiles row (no Skill, no flags, SrcDamage the case's):
FUN_0064b860 builds the record from a player (type 0) holding a weapon (type 4), FUN_0064ac60 puts it on the missile
as its stats, and FUN_005a89a0 rolls the hit on a monster. The claim, checked against rules::missile_physical and
rules::missile_scaled (tests/test_missile_record.cpp):
  - the weapon's stats 21 / 22 x 256 x SrcDamage / 128 toward zero, the row's own physical added after, unscaled;
  - the record's +0x78: the unit's stat 25, the mastery %, the str / dex bonus, the missile's stat 25 and the larger
    of the unit's stats 18 / 17, at least -90; SrcDamage 0 takes none of the unit's;
  - the hit: the lower end + rand(the gap), none unless both ends are over 0; its %: +0x78, + stat 121 against a
    demon, + stat 122 (+ 50 with a Blunt weapon) against the undead, at least -90; the roll + roll x % / 100.
Hooked (inputs and side effects, not the claim): the stat getter FUN_00625480 and setter FUN_00627260 (a dict per
unit), the weapon getters FUN_00623990 and FUN_00622830 (a monster's), the mastery FUN_00645830, the item's StrBonus / DexBonus FUN_00629860 /
FUN_006298a0, the item-type test FUN_00629bb0 (Blunt), the throwable test FUN_0062ba80 (no), the two-hander tests
FUN_006289c0 / FUN_0063d340 (no), the hireling test FUN_0063ee90 (no), the crit chances
FUN_0064a850 (none), the row's own physical FUN_0064af20 / FUN_0064aff0 (the case's) and element FUN_0064b100 /
FUN_0064b1d0 / FUN_0064b2a0 (none), the demon / undead tests FUN_0063e940 / FUN_0063e990, the seed draw FUN_0045c3e0
(the case's draw mod the gap), the stat-list switches FUN_00623c80 / FUN_00622e90 and FUN_0064a930 / FUN_0064aa60.
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
RECORD, PUT, HIT = 0x64B860, 0x64AC60, 0x5A89A0
DATA_TABLES = 0x744304
LIMIT = 200000
MASK = 0xFFFFFFFF
MUT = set()
FIELDS = ("wmin", "wmax", "srcdam", "row_lo", "row_hi", "s25", "mastery", "strength", "str_bonus", "dex", "dex_bonus",
          "missile25", "s18", "s17", "s121", "s122", "blunt", "demon", "undead", "draw")


def signed(value):
    """A 32-bit register as a signed int."""
    value &= MASK
    return value - (1 << 32) if value & 0x80000000 else value


def toward_zero(numerator, denominator):
    """C's int division."""
    quotient = abs(numerator) // abs(denominator)
    return quotient if (numerator < 0) == (denominator < 0) else -quotient


def model(case):
    """rules::missile_physical and missile_scaled re-stated: (low, high, record %, damage)."""
    c = dict(zip(FIELDS, case))
    low, high, record_pct, demon_pct, undead_pct = c["row_lo"], c["row_hi"], c["missile25"], 0, 0
    if c["srcdam"] > 0:
        share = (lambda value: toward_zero(value * c["srcdam"], 128)) if "srcdam_all" not in MUT else (lambda value: value)
        low += share(c["wmin"] << 8)
        high += share(c["wmax"] << 8)
        if "srcdam_all" in MUT:
            low, high = toward_zero(low * c["srcdam"], 128), toward_zero(high * c["srcdam"], 128)
        phys_pct = c["s25"] + c["mastery"] + toward_zero(c["strength"] * c["str_bonus"], 100) + toward_zero(c["dex"] * c["dex_bonus"], 100)
        record_pct += phys_pct + (0 if "no_unit_pct" in MUT else max(c["s18"], c["s17"]))
        demon_pct = 0 if "no_demon" in MUT else c["s121"]
        undead_pct = c["s122"] + (50 if c["blunt"] else 0)
        if "undead_floor" in MUT:
            undead_pct = max(undead_pct, 0)
    record_pct = max(record_pct, -90)
    hit_pct = max(record_pct + (demon_pct if c["demon"] else 0) + (undead_pct if c["undead"] else 0), -90)
    if low > 0 and high > 0:
        low, high = (min(low, high), max(low, high)) if "no_swap" not in MUT else (min(low, high), high)
    else:
        low = high = 0
    roll = low + c["draw"] % (high - low) if high > low else low
    return low, high, record_pct, toward_zero(roll * hit_pct, 100) + roll


class Game:
    """game.exe with a player, its weapon, a missile and a monster in the fake heap."""

    def __init__(self, exe):
        """Load the exe, lay out the units and a one-row Missiles table, hook the inputs."""
        self.machine = machine = emu.Emu(str(exe))
        self.unit, self.weapon, self.missile, self.target = (machine.alloc(0x200) for _ in range(4))
        self.record, self.tables, self.row = machine.alloc(0x80), machine.alloc(0x1000), machine.alloc(0x1a4)
        machine.w32(self.weapon, 4)
        machine.w32(self.missile, 3)
        machine.w32(self.target, 1)
        machine.w32(self.target + 4, MASK)                 # no MonStats row: FUN_005a89a0's MonProp list is skipped
        machine.w32(self.tables + 0xB64, self.row)
        machine.w32(self.tables + 0xB6C, 1)
        machine.w32(DATA_TABLES, self.tables)
        self.case, self.stats, self.draws = {}, {}, []
        case = lambda name: lambda e: self.case[name] & MASK
        hooks = {0x625480: (lambda e: self.stats.get((e.arg(0), e.arg(1)), 0) & MASK, 3),
                 0x627260: (self.put_stat, 4),
                 0x623990: (lambda e: self.weapon, 2), 0x622830: (lambda e: self.weapon, 1),
                 0x645830: (case("mastery"), 4), 0x629860: (case("str_bonus"), 1), 0x6298a0: (case("dex_bonus"), 1),
                 0x629bb0: (lambda e: self.case["blunt"] if e.arg(1) == 0x39 else 0, 2),
                 0x64af20: (case("row_lo"), 4), 0x64aff0: (case("row_hi"), 4),
                 0x63e940: (case("demon"), 1), 0x63e990: (case("undead"), 1),
                 0x45c3e0: (self.draw, 0)}
        for addr, (fn, nstack) in hooks.items():
            machine.hook(addr, fn, nstack)
        machine.stub(0, {0x64a850: 1, 0x6289c0: 1, 0x62ba80: 1, 0x63d340: 2, 0x63ee90: 1, 0x64b100: 4, 0x64b1d0: 4,
                         0x64b2a0: 4, 0x623c80: 2, 0x622e90: 2, 0x64a930: 0, 0x64aa60: 0})

    def call(self, address, *args):
        """machine.call with an instruction cap; a run the cap stops short of its return is an error."""
        result = self.machine.call(address, *args, limit=LIMIT)
        from unicorn.x86_const import UC_X86_REG_EIP
        if (eip := self.machine.mu.reg_read(UC_X86_REG_EIP)) != emu.RET_MAGIC:
            raise SystemExit(f"missile_record: FAIL: {address:#x} stopped at {eip:#x}, {LIMIT} instructions in")
        return result

    def put_stat(self, machine):
        """FUN_00627260(unit, id, value, layer): the dict the getter reads."""
        self.stats[(machine.arg(0), machine.arg(1))] = signed(machine.arg(2))
        return 0

    def draw(self, machine):
        """FUN_0045c3e0 (ECX seed, EDX n): the case's draw mod n, 0 under 1."""
        from unicorn.x86_const import UC_X86_REG_EDX
        bound = signed(machine.mu.reg_read(UC_X86_REG_EDX))
        self.draws.append(bound)
        return self.case["draw"] % bound if bound > 0 else 0

    def run(self, case):
        """FUN_0064b860, FUN_0064ac60 and FUN_005a89a0 on one case: (low, high, record %, damage)."""
        machine, c = self.machine, dict(zip(FIELDS, case))
        self.case, self.draws = c, []
        self.stats = {(self.unit, 0x15): c["wmin"], (self.unit, 0x16): c["wmax"], (self.unit, 0x19): c["s25"],
                      (self.unit, 0): c["strength"], (self.unit, 2): c["dex"], (self.unit, 0x12): c["s18"],
                      (self.unit, 0x11): c["s17"], (self.unit, 0x79): c["s121"], (self.unit, 0x7a): c["s122"],
                      (self.missile, 0x19): c["missile25"]}
        machine.mu.mem_write(self.unit, bytes(0x200))      # a player, no +0x60 inventory link: stats 21 / 22
        machine.mu.mem_write(self.row, bytes(0x1a4))       # no Skill, no flags, no element
        machine.mu.mem_write(self.row + 0x12D, bytes([c["srcdam"]]))
        machine.w32(self.missile + 4, 0)
        self.call(RECORD, self.record, self.unit, 0, self.missile, 1)
        record = [signed(machine.r32(self.record + 4 * index)) for index in range(31)]
        self.stats = {key: value for key, value in self.stats.items() if key[0] != self.missile}
        self.call(PUT, 0, self.missile, self.record, 1)
        out = machine.alloc(0x70)
        self.call(HIT, self.missile, self.target, out)
        damage = signed(machine.r32(out + 8))
        low, high = record[1], record[2]
        if low > 0 and high > 0:
            low, high = min(low, high), max(low, high)
            if self.draws[:1] != [high - low]:
                raise SystemExit(f"{case}: the hit drew {self.draws[:1]}, not the gap {high - low}")
        else:
            low = high = 0
        return low, high, record[0x1E], damage


def cases():
    """Ends, SrcDamage (0, half, whole, odd), a row point, the percents round their floors, monster kinds."""
    rng = random.Random(0x64B860)
    out = []
    for _ in range(4000):
        wmin = rng.choice((0, 1, rng.randint(1, 30), rng.randint(1, 400)))
        wmax = rng.choice((wmin, wmin + rng.randint(0, 60), rng.randint(0, 40)))
        srcdam = rng.choice((0, 64, 128, 128, 96, rng.randint(1, 255)))
        row_lo = rng.choice((0, 0, 256, rng.randint(-300, 3000)))
        row_hi = rng.choice((row_lo, 0, 256, rng.randint(-300, 6000)))
        pct = lambda: rng.choice((0, 0, rng.randint(-120, 400), rng.randint(-20, 60)))
        out.append((wmin, wmax, srcdam, row_lo, row_hi, pct(), rng.choice((0, rng.randint(0, 180))),
                    rng.randint(0, 300), rng.choice((0, 0, rng.randint(0, 150))), rng.randint(0, 300),
                    rng.choice((0, 0, rng.randint(0, 150))), pct(), pct(), pct(), pct(), pct(),
                    int(rng.random() < 0.3), int(rng.random() < 0.4), int(rng.random() < 0.4), rng.randint(0, 1 << 30)))
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
        got = game.run(case)
        results.append((case, got))
        want = model(case)
        if got != want:
            bad += 1
            if bad <= 10 and not mutation:
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
        lines = ["// Stock game.exe 1.14d, FUN_0064b860 -> FUN_0064ac60 -> FUN_005a89a0; generated by tools/emu/missile_record.py inc.",
                 "// " + ", ".join(FIELDS) + "; then game.exe's low, high, record +0x78, damage (256ths)"]
        for case, got in results[:1500]:
            lines.append("    { " + ", ".join(str(value) for value in case + got) + " },")
        text = "\n".join(lines) + "\n"
        if args.out:
            write_fixture(args.out, text)
        else:
            sys.stdout.write(text)
        return
    print(f"ok: {len(results)} FUN_0064b860 / FUN_005a89a0 missile record cases, 0 differ")
    if args.mutation_check:
        for name in ("srcdam_all", "no_unit_pct", "no_demon", "no_swap", "undead_floor"):
            missed, _ = run((name,))
            if not missed:
                print(f"mutation {name}: NOT caught", file=sys.stderr)
                sys.exit(1)
            print(f"mutation {name}: caught ({missed} differ)")
        print("mutation check ok")


if __name__ == "__main__":
    main()
