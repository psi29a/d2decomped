# SPDX-License-Identifier: GPL-3.0-or-later
"""Run four small skill and missile helpers of game.exe 1.14d on synthetic tables.

Run: uv run python skills_combat_helpers.py [--exe /path/to/Game.exe] [--mutation-check]

The helpers run for real; the tables and units are built by hand in the
emulator and each result (return value, memory written, arguments handed to
callees) is compared with a Python model of the decompile.

  FUN_00663270  missile range: byte 0x9a + byte 0x9b * level / 8 (signed, 32-bit
                multiply, truncating division) from the missile table at
                0x96c794 (count 0x96c79c, rows of 0x1a4). Nothing is hooked.
  FUN_00645c10  Skills.txt row +0x158 / +0x15c forwarded to FUN_00645b20 with
                the level. HOOKED: FUN_00645b20 (logs its arguments).
  FUN_0056e170  adds min + rand to damage +8. The real seed draw FUN_0045c3e0
                runs on the unit's seed at +0x20 (n = max - min, n&(n-1) mask or
                % n, 0 and no seed step for n < 1). HOOKED: the minimum and
                maximum physical damage FUN_00647bc0 and FUN_00647d00 (log
                their arguments, return scripted values).
  FUN_005c6cc0  the six aurastats slots of a Skills.txt row. HOOKED: the calc
                evaluator FUN_00646ca0, the stat-list add FUN_006270b0 and set
                FUN_00627150, and the unit check FUN_00623f50 (all log their
                arguments).

Not covered: the hooked callees themselves, and any d2d implementation (no
parity claim).

`--mutation-check` plants deliberate defects in the model (and, for each
function, replaces its first instruction by `ret`) and requires every one to
be caught.
"""
import argparse
import sys

import emu

MISSILE_TABLE, MISSILE_COUNT = 0x96C794, 0x96C79C
SKILL_TABLES = 0x744304
SEED_MULTIPLIER = 0x6AC690C5
SKILL_ROW = 0x23C
STAT_ROW = 0x144
ASSERT_FILE = 0x6E30AC           # ".\SKILLS\SkillDruid.cpp"

mutation = None


class Mismatch(Exception):
    """The emulated function and the model disagree."""


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def expect(actual, expected, what):
    """Raise Mismatch (also under python -O) when actual differs from expected."""
    if actual != expected:
        raise Mismatch(f"{what}: game.exe {actual!r}, model {expected!r}")


def signed32(value):
    """Interpret the low 32 bits as an x86 signed integer."""
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value >= 0x80000000 else value


def new_engine(exe, target, ret_mutation):
    """A fresh emulator; with ret_mutation the target's first byte becomes `ret`."""
    engine = emu.Emu(exe=exe)
    if ret_mutation:
        engine.mu.mem_write(target, b"\xc3")
        engine.mu.ctl_remove_cache(target, target + 1)
    return engine


def skill_tables(engine, rows, stat_rows=0, stat_count=0):
    """Install a skill table struct at 0x744304 with `rows` Skills.txt rows; returns (tables, rows address)."""
    tables = engine.alloc(0xC00)
    base = engine.alloc(SKILL_ROW * max(rows, 1))
    engine.w32(SKILL_TABLES, tables)
    engine.w32(tables + 0xB98, base)
    engine.w32(tables + 0xBA0, rows)
    if stat_rows:
        engine.w32(tables + 0xBCC, engine.alloc(STAT_ROW * stat_rows))
    engine.w32(tables + 0xBD4, stat_count)
    return tables, base


# ---------------------------------------------------------------- FUN_00663270

def range_model(rows, count, ident, level):
    """Model of FUN_00663270: rows is a list of (byte 0x9a, byte 0x9b)."""
    if mut("range_bound_le"):
        valid = 0 <= ident <= count
    else:
        valid = 0 <= ident < count
    if not valid:
        return 0
    base, scale = rows[ident % len(rows)]
    if mut("range_swap_bytes"):
        base, scale = scale, base
    product = scale * level
    if not mut("range_no_wrap"):
        product = signed32(product)
    quotient = abs(product) // 8 * (-1 if product < 0 else 1)
    if mut("range_floor_div"):
        quotient = product // 8
    return signed32(base + quotient + (1 if mut("range_plus_one") else 0))


def check_range(engine):
    """FUN_00663270 on three synthetic missile rows; returns the case count."""
    rows = [(3, 0), (7, 1), (255, 255)]
    table = engine.alloc(len(rows) * 0x1A4)
    engine.w32(MISSILE_TABLE, table)
    engine.w32(MISSILE_COUNT, len(rows))
    cases = 0
    for index, row in enumerate(rows):
        engine.mu.mem_write(table + index * 0x1A4 + 0x9A, bytes(row))
    levels = (-2147483648, -100, -9, -8, -7, -1, 0, 1, 7, 8, 9, 100, 2147483647)
    for ident in (0, 1, 2):
        for level in levels:
            actual = signed32(engine.call(0x663270, ident, level))
            expect(actual, range_model(rows, len(rows), ident, level), f"range id {ident} level {level}")
            cases += 1
    for ident in (-1, 3, 2147483647):
        expect(engine.call(0x663270, ident, 10), range_model(rows, len(rows), ident, 10), f"range id {ident}")
        cases += 1
    engine.w32(MISSILE_TABLE, 0)          # a null table makes row 0 a null row
    expect(engine.call(0x663270, 0, 10), 0, "range null row")
    return cases + 1


# ---------------------------------------------------------------- FUN_00645c10

def check_interp(engine):
    """FUN_00645c10 forwarding to a logged FUN_00645b20; returns the case count."""
    _, base = skill_tables(engine, 2)
    for index, (first, second) in enumerate(((11, 22), (33, 44))):
        engine.w32(base + index * SKILL_ROW + 0x158, first)
        engine.w32(base + index * SKILL_ROW + 0x15C, second)
    calls = []

    def interpolate(machine):
        """Log FUN_00645b20(level, +0x158, +0x15c) and return a level-derived value."""
        args = tuple(machine.arg(index) for index in range(3))
        calls.append(args)
        return (args[0] * 3 + 1) & 0xFFFFFFFF

    engine.hook(0x645B20, interpolate, 3)
    cases = 0
    for skill, pair in ((0, (11, 22)), (1, (33, 44))):
        for level in (1, 2, 20, 2147483647):
            calls.clear()
            actual = engine.call(0x645C10, level, skill)
            expected_pair = (22, 11) if mut("interp_swap_fields") else pair
            expect(actual, (level * 3 + 1) & 0xFFFFFFFF, f"interp return skill {skill} level {level}")
            expect(calls, [(level, *expected_pair)], f"interp args skill {skill} level {level}")
            cases += 1
    for level, skill in ((0, 0), (-1, 0), (-2147483648, 1), (1, -1), (1, 2), (1, 2147483647)):
        calls.clear()
        guarded = mut("interp_allow_zero") and level == 0
        actual = engine.call(0x645C10, level, skill)
        if guarded:
            expect(calls, [(0, 11, 22)], "interp mutated zero level")
        expect((actual, calls), (0, []), f"interp reject level {level} skill {skill}")
        cases += 1
    engine.w32(engine.r32(SKILL_TABLES) + 0xB98, 0)     # null row table: id 0 is a null row
    calls.clear()
    expect((engine.call(0x645C10, 5, 0), calls), (0, []), "interp null row")
    return cases + 1


# ---------------------------------------------------------------- FUN_0056e170

def seed_draw(seed, bound):
    """Model of FUN_0045c3e0: returns (result, new seed); 0 and no step for bound < 1."""
    if bound < 1:
        return 0, seed
    low, high = seed
    product = low * SEED_MULTIPLIER + high
    new = (product & 0xFFFFFFFF, (product >> 32) & 0xFFFFFFFF)
    if bound & (bound - 1) == 0:
        return new[0] & (bound - 1), new
    return new[0] % bound, new


def check_damage(engine):
    """FUN_0056e170 with the real seed draw and hooked minimum/maximum; returns the case count."""
    skill_tables(engine, 3)
    actor = engine.alloc(0x100)
    damage = engine.alloc(0x40)
    bounds = [0, 0]
    calls = []

    def minimum(machine):
        """FUN_00647bc0: log its four stack arguments, return the scripted minimum."""
        calls.append(("min", tuple(machine.arg(index) for index in range(4))))
        return bounds[0] & 0xFFFFFFFF

    def maximum(machine):
        """FUN_00647d00: log its four stack arguments, return the scripted maximum."""
        calls.append(("max", tuple(machine.arg(index) for index in range(4))))
        return bounds[1] & 0xFFFFFFFF

    engine.hook(0x647BC0, minimum, 4)
    engine.hook(0x647D00, maximum, 4)
    seed = (0x12345678, 0x9ABCDEF0)
    cases = 0
    for skill in (0, 2):
        for low, high in ((7, 15), (7, 14), (10, 10), (10, 5), (0, 1), (-3, 4), (5, 6), (0, 1000)):
            for start in (100, 0xFFFFFFF0):
                bounds[:] = [low, high]
                calls.clear()
                engine.w32(actor + 0x20, seed[0])
                engine.w32(actor + 0x24, seed[1])
                engine.w32(damage + 8, start)
                engine.w32(damage + 4, 0xAAAAAAAA)
                engine.call(0x56E170, skill, 55, ecx=actor, edx=damage)
                bound = high - low + (1 if mut("damage_inclusive") else 0)
                rolled, new_seed = seed_draw(seed, bound)
                base = 0 if mut("damage_no_min") else low
                expect(engine.r32(damage + 8), (start + base + rolled) & 0xFFFFFFFF, f"damage skill {skill} {low}..{high}")
                expect((engine.r32(actor + 0x20), engine.r32(actor + 0x24)), new_seed, f"seed {low}..{high}")
                expect(engine.r32(damage + 4), 0xAAAAAAAA, "damage other field")
                args = (actor, skill, 55, 0)
                expect(calls, [("min", args), ("max", args)], "damage callee order and arguments")
                cases += 1
    for skill in (-1, 3, 2147483647):
        calls.clear()
        engine.w32(actor + 0x20, seed[0])
        engine.w32(actor + 0x24, seed[1])
        engine.w32(damage + 8, 100)
        engine.call(0x56E170, skill, 55, ecx=actor, edx=damage)
        expect((engine.r32(damage + 8), calls, engine.r32(actor + 0x20)), (100, [], seed[0]), f"damage reject {skill}")
        cases += 1
    engine.w32(engine.r32(SKILL_TABLES) + 0xB98, 0)     # null row table: id 0 is a null row
    engine.w32(damage + 8, 100)
    engine.call(0x56E170, 0, 55, ecx=actor, edx=damage)
    expect((engine.r32(damage + 8), calls), (100, []), "damage null row")
    return cases + 1


# ---------------------------------------------------------------- FUN_005c6cc0

def check_aura(engine):
    """FUN_005c6cc0 with hooked evaluator, stat-list writers and unit check; returns the case count."""
    tables, base = skill_tables(engine, 3, stat_rows=70, stat_count=70)
    actor = engine.alloc(0x100)
    stats = engine.alloc(0x40)
    events = []
    values = {100: 9, 101: 0, 102: 7, 103: 5, 104: 6, 105: 11}

    def put_row(address, ids):
        """Fill a Skills.txt row's six aurastat ids (+0x54, words) and calc offsets (+0x68, dwords 100..105)."""
        for slot, ident in enumerate(ids):
            engine.mu.mem_write(address + 0x54 + 2 * slot, (ident & 0xFFFF).to_bytes(2, "little"))
            engine.w32(address + 0x68 + 4 * slot, 100 + slot)

    def evaluate(machine):
        """FUN_00646ca0(unit, calc offset, skill, level): log, answer from `values`."""
        args = tuple(machine.arg(index) for index in range(4))
        events.append(("calc", args))
        return values[args[1]]

    def add(machine):
        """FUN_006270b0(list, stat, value, layer): log."""
        events.append(("add", tuple(machine.arg(index) for index in range(4))))
        return 0

    def put(machine):
        """FUN_00627150(list, stat, value, layer): log."""
        events.append(("set", tuple(machine.arg(index) for index in range(4))))
        return 0

    def check_unit(machine):
        """FUN_00623f50(unit, file, line): log."""
        events.append(("unit", tuple(machine.arg(index) for index in range(3))))
        return 0

    engine.hook(0x646CA0, evaluate, 4)
    engine.hook(0x6270B0, add, 4)
    engine.hook(0x627150, put, 4)
    engine.hook(0x623F50, check_unit, 3)
    explicit = engine.alloc(SKILL_ROW)
    # slots: stat 68 (also sets 69), stat 2 evaluating to 0 (skipped), -1, 70 (= count), -32768, stat 3
    put_row(explicit, (68, 2, -1, 70, -32768, 3))
    cases = 0
    expected = [("calc", (actor, 100, 7, 4)), ("add", (stats, 68, 9, 0)), ("set", (stats, 69, 9, 0)),
                ("calc", (actor, 101, 7, 4)), ("calc", (actor, 105, 7, 4)), ("add", (stats, 3, 11, 0)),
                ("unit", (actor, ASSERT_FILE, 0xF8))]
    if mut("aura_set_all"):
        expected.insert(3, ("set", (stats, 69, 9, 0)))
    if mut("aura_skip_unit_check"):
        expected.pop()
    for ident in (7, 1):                 # the row argument wins; the id is only passed to the evaluator
        events.clear()
        engine.call(0x5C6CC0, explicit, ident, 4, ecx=actor, edx=stats)
        want = [(kind, (args[0], args[1], ident, args[3]) if kind == "calc" else args) for kind, args in expected]
        expect(events, want, f"aura explicit row id {ident}")
        cases += 1
    # no row: the row of skill id 1 from the Skills.txt table
    put_row(base + SKILL_ROW, (3, -1, -1, -1, -1, 68))
    events.clear()
    engine.call(0x5C6CC0, 0, 1, 6, ecx=actor, edx=stats)
    expect(events, [("calc", (actor, 100, 1, 6)), ("add", (stats, 3, 9, 0)),
                    ("calc", (actor, 105, 1, 6)), ("add", (stats, 68, 11, 0)), ("set", (stats, 69, 11, 0)),
                    ("unit", (actor, ASSERT_FILE, 0xF8))], "aura table row")
    cases += 1
    for ident in (-1, 3, 2147483647):
        events.clear()
        engine.call(0x5C6CC0, 0, ident, 6, ecx=actor, edx=stats)
        expect(events, [], f"aura reject id {ident}")
        cases += 1
    for unit, target in ((0, stats), (actor, 0)):
        events.clear()
        engine.call(0x5C6CC0, explicit, 1, 6, ecx=unit, edx=target)
        expect(events, [], f"aura null unit {unit} stats {target}")
        cases += 1
    events.clear()                       # a zero evaluation adds nothing, even for stat 68
    values[100] = 0
    put_row(explicit, (68, -1, -1, -1, -1, -1))
    engine.call(0x5C6CC0, explicit, 1, 6, ecx=actor, edx=stats)
    zero = [("calc", (actor, 100, 1, 6)), ("unit", (actor, ASSERT_FILE, 0xF8))]
    if mut("aura_zero_adds"):
        zero.insert(1, ("add", (stats, 68, 0, 0)))
    expect(events, zero, "aura zero value")
    cases += 1
    events.clear()                       # a null stat table makes stat row 0 a null row
    put_row(explicit, (0, -1, -1, -1, -1, -1))
    engine.w32(tables + 0xBCC, 0)
    engine.call(0x5C6CC0, explicit, 1, 6, ecx=actor, edx=stats)
    expect(events, [("unit", (actor, ASSERT_FILE, 0xF8))], "aura null stat row")
    cases += 1
    events.clear()                       # stat id 69: below the count, written to a row of the real table
    engine.w32(tables + 0xBCC, engine.alloc(STAT_ROW * 70))
    put_row(explicit, (69, -1, -1, -1, -1, -1))
    values[100] = 3
    engine.call(0x5C6CC0, explicit, 1, 6, ecx=actor, edx=stats)
    expect(events[:2], [("calc", (actor, 100, 1, 6)), ("add", (stats, 69, 3, 0))], "aura stat 69 alone")
    return cases + 1


CHECKS = (
    ("FUN_00663270", 0x663270, check_range, ("range_bound_le", "range_swap_bytes", "range_no_wrap",
                                              "range_floor_div", "range_plus_one")),
    ("FUN_00645c10", 0x645C10, check_interp, ("interp_swap_fields", "interp_allow_zero")),
    ("FUN_0056e170", 0x56E170, check_damage, ("damage_inclusive", "damage_no_min")),
    ("FUN_005c6cc0", 0x5C6CC0, check_aura, ("aura_set_all", "aura_skip_unit_check", "aura_zero_adds")),
)


def main():
    """Run every check, or with --mutation-check prove each defect is caught."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", help="game.exe 1.14d (default: <data>/bin/game.exe)")
    parser.add_argument("--mutation-check", action="store_true", help="require every deliberate defect to fail")
    options = parser.parse_args()
    if options.mutation_check:
        caught = 0
        for name, address, check, defects in CHECKS:
            for defect in (None,) + defects:
                mutation = defect
                try:
                    check(new_engine(options.exe, address, defect is None))
                except Mismatch:
                    print(f"caught: {name} {defect or 'target replaced by ret'}")
                    caught += 1
                except RuntimeError as err:          # emulator fault (a `ret` leaves the stack unbalanced)
                    print(f"caught: {name} {defect or 'target replaced by ret'} ({str(err)[:40]})")
                    caught += 1
                else:
                    raise SystemExit(f"mutation {defect or 'ret'} of {name} was not caught")
                finally:
                    mutation = None
        print(f"ok: {caught} mutations caught")
        return
    total = 0
    for name, address, check, _ in CHECKS:
        count = check(new_engine(options.exe, address, False))
        print(f"{name}: {count} cases")
        total += count
    print(f"ok: {total} synthetic cases over {len(CHECKS)} functions")


if __name__ == "__main__":
    sys.exit(main())
