# SPDX-License-Identifier: GPL-3.0-or-later
"""Run four small game.exe 1.14d unit helpers in the emulator on synthetic state.

    uv run python items_objects_helpers.py --exe /path/to/Game.exe [--mutation-check]

Runs game.exe's own code for FUN_00622e70 (stat 12 times 10000), FUN_0063a130
(any shared bit between a unit's state words and a set, EAX = unit, EBX = set),
FUN_0062a0a0 (type-4 item guard that forwards its class to FUN_0062a060) and
FUN_00554120 (store an interaction id and type on an idle unit). Hooked, not
claimed: FUN_00625480 (stat getter), FUN_00625bb0 (state word list), FUN_0062a060
(item flag), FUN_00408090 / FUN_00408a60 (fatal-error path, recorded),
FUN_006221a0 (player name), the `_sprintf` at 0x6823fd (recorded) and the security
cookie check FUN_00681a48. The hooks record their arguments and the checks assert
them. d2d is not compared.

`--mutation-check` patches one instruction of each target in a fresh emulator
(a wrong multiplier, word count, type constant or stored byte, plus a `ret` at the
entry) and requires that function's check to fail.
"""
import argparse
import sys

import emu

if not __debug__:
    sys.exit("items_objects_helpers.py requires assertions; do not use python -O")

STATE_TABLES = 0x744304
STATE_COUNT = 0xC4
CALL_LIMIT = 100000      # instruction cap so a looping mutation fails instead of hanging


class Fatal(Exception):
    """Raised by the hooked fatal-error exit so a guard failure ends the call."""


def check_gold(engine):
    """FUN_00622e70 asks FUN_00625480 for (unit, 12, 0) and multiplies by 10000 in 32 bits."""
    stat = [0]
    seen = []

    def get_stat(machine):
        """Record the getter's arguments and answer the case's stat."""
        seen.append((machine.arg(0), machine.arg(1), machine.arg(2)))
        return stat[0]

    engine.hook(0x625480, get_stat, 3)
    values = (0, 1, 99, 2147483647, 0xFFFFFFFF, 0x80000000, 214749)
    for value in values:
        stat[0] = value
        assert engine.call(0x622E70, 0x12345678, limit=CALL_LIMIT) == (value * 10000) & 0xFFFFFFFF, value
    assert seen == [(0x12345678, 12, 0)] * len(values), seen
    return len(values)


def check_state(engine):
    """FUN_0063a130 scans (count + 31) / 32 words for a shared bit, 0 when the unit has no list."""
    unit = engine.alloc(0x100)
    tables = engine.alloc(0xC8)
    bits = engine.alloc(16)
    mask = engine.alloc(16)
    engine.w32(STATE_TABLES, tables)
    state_ptr = [bits]
    asked = []

    def words(machine):
        """FUN_00625bb0 stand-in: note the unit asked about and return the case's word list."""
        asked.append(machine.arg(0))
        return state_ptr[0]

    engine.hook(0x625BB0, words, 1)
    cases = (
        # count, state words, set words, expected
        (0, (1, 1, 1), (1, 1, 1), 0),
        (1, (1, 0, 0), (1, 0, 0), 1),
        (1, (2, 0, 0), (1, 0, 0), 0),
        (31, (0x40000000, 0, 0), (0x40000000, 0, 0), 1),
        (32, (0, 1, 0), (1, 1, 0), 0),
        (32, (0x80000000, 1, 0), (0x80000000, 0, 0), 1),
        (33, (0, 1, 0), (1, 1, 0), 1),
        (63, (0, 1, 0), (0, 1, 0), 1),
        (64, (0, 0, 1), (0, 0, 1), 0),
        (65, (0, 0, 1), (0, 0, 1), 1),
        (65, (0, 0, 2), (0, 0, 1), 0),
    )
    for count, state, wanted, expected in cases:
        engine.w32(tables + STATE_COUNT, count)
        for index in range(3):
            engine.w32(bits + 4 * index, state[index])
            engine.w32(mask + 4 * index, wanted[index])
        assert engine.call(0x63A130, regs={"eax": unit, "ebx": mask}, limit=CALL_LIMIT) == expected, (count, state, wanted)
    state_ptr[0] = 0
    engine.w32(tables + STATE_COUNT, 33)
    assert engine.call(0x63A130, regs={"eax": unit, "ebx": mask}, limit=CALL_LIMIT) == 0
    assert asked == [unit] * (len(cases) + 1), asked
    return len(cases) + 1


def check_item(engine):
    """FUN_0062a0a0 returns FUN_0062a060's EAX for a type-4 unit and is fatal for null or other types."""
    item = engine.alloc(8)
    classes = []
    fatal = []

    def item_flag(machine):
        """FUN_0062a060 stand-in: record the class and answer a full 32-bit word."""
        classes.append(machine.arg(0))
        return 0x87654321

    def message(machine):
        """FUN_00408090 stand-in: remember the assert line, return it as the message."""
        fatal.append(machine.arg(0))
        return machine.arg(0)

    def die(machine):
        """FUN_00408a60 stand-in: record the call and end the emulated call."""
        fatal.append(("report", machine.arg(1)))
        raise Fatal

    engine.hook(0x62A060, item_flag, 1)
    engine.hook(0x408090, message, 1)
    engine.hook(0x408A60, die, 0)
    for unit_class in (123, 0, 0xFFFFFFFF):
        engine.w32(item, 4)
        engine.w32(item + 4, unit_class)
        assert engine.call(0x62A0A0, item, limit=CALL_LIMIT) == 0x87654321
    assert classes == [123, 0, 0xFFFFFFFF] and not fatal, (classes, fatal)
    guards = ((0, 0x1162), (item, 0x1163))
    engine.w32(item, 1)
    for unit, line in guards:
        try:
            engine.call(0x62A0A0, unit, limit=CALL_LIMIT)
        except Fatal:
            pass
        else:
            raise AssertionError("guard did not fire")
        assert fatal[-2:] == [line, ("report", line)], fatal
    assert classes == [123, 0, 0xFFFFFFFF]
    return 3 + len(guards)


def check_interaction(engine):
    """FUN_00554120 stores id, type and busy=1 on an idle unit and leaves a busy one alone."""
    unit = engine.alloc(0x100)
    names = []
    prints = []
    engine.stub(0, {0x681A48: 0})

    def player_name(machine):
        """FUN_006221a0 stand-in: record the unit and answer the case's name pointer."""
        names.append(machine.arg(0))
        return name_ptr[0]

    def format_text(machine):
        """_sprintf stand-in: record the format string and name argument."""
        prints.append((engine.cstr(machine.arg(1)), machine.arg(2)))
        return 0

    name_ptr = [0]
    engine.hook(0x6221A0, player_name, 1)
    engine.hook(0x6823FD, format_text, 0)
    cases = 0
    for unit_type in (1, 0):
        for busy in (0, 1, 255):
            engine.w32(unit, unit_type)
            engine.w32(unit + 0x64, 0x11111111)
            engine.w32(unit + 0x68, 0x22222222)
            engine.mu.mem_write(unit + 0x6C, bytes((busy,)))
            engine.call(0x554120, unit, 2, 42, limit=CALL_LIMIT)
            expected = (42, 2, 1) if not busy else (0x11111111, 0x22222222, busy)
            actual = (engine.r32(unit + 0x64), engine.r32(unit + 0x68), engine.read(unit + 0x6C, 1)[0])
            assert actual == expected, (unit_type, busy, actual)
            cases += 1
    # Idle units never reach the name lookup; only a busy player (type 0) does.
    assert names == [unit] * 2 and prints == [], (names, prints)
    name = engine.put(b"Hero\0")
    name_ptr[0] = name
    engine.mu.mem_write(unit + 0x6C, b"\x01")
    engine.call(0x554120, unit, 2, 42, limit=CALL_LIMIT)
    assert prints == [("Player %s attempted to operate while busy\n", name)], prints
    assert engine.r32(unit + 0x64) == 0x11111111
    return cases + 1


CHECKS = (
    ("FUN_00622e70", 0x622E70, check_gold),
    ("FUN_0063a130", 0x63A130, check_state),
    ("FUN_0062a0a0", 0x62A0A0, check_item),
    ("FUN_00554120", 0x554120, check_interaction),
)

# One real defect per target: (address, original bytes, broken bytes).
MUTATIONS = {
    "FUN_00622e70": ((0x622E82, b"\x10\x27", b"\x11\x27"), (0x622E70, None, b"\xc3")),
    "FUN_0063a130": ((0x63A157, b"\x05", b"\x04"), (0x63A14D, b"\x1f", b"\x1e"), (0x63A130, None, b"\xc3")),
    "FUN_0062a0a0": ((0x62A0CB, b"\x04", b"\x03"), (0x62A0AB, b"\x62", b"\x63"), (0x62A0A0, None, b"\xc3")),
    "FUN_00554120": ((0x554179, b"\x01", b"\x02"), (0x554136, b"\x00", b"\x01"), (0x554120, None, b"\xc3")),
}


def run_check(executable, check, patches=()):
    """Run one check in a fresh emulator after applying byte patches; return its case count."""
    engine = emu.Emu(exe=executable)
    for address, _, broken in patches:
        engine.mu.mem_write(address, broken)
        engine.mu.ctl_remove_cache(address, address + len(broken))
    return check(engine)


def main():
    """Run every check, or with --mutation-check require each patched target to fail."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="local game.exe 1.14d")
    parser.add_argument("--mutation-check", action="store_true", help="require every patched target to fail")
    options = parser.parse_args()
    if not options.mutation_check:
        total = 0
        for name, _, check in CHECKS:
            cases = run_check(options.exe, check)
            total += cases
            print(f"ok: {name}: {cases} cases")
        print(f"ok: {total} cases over {len(CHECKS)} functions")
        return
    caught = 0
    pristine = emu.Emu(exe=options.exe)
    for name, _, check in CHECKS:
        for address, original, _ in MUTATIONS[name]:
            assert original is None or pristine.read(address, len(original)) == original, hex(address)
        for patch in MUTATIONS[name]:
            try:
                run_check(options.exe, check, (patch,))
            except (AssertionError, RuntimeError, Fatal):
                caught += 1
                print(f"mutation {name} @{patch[0]:x}: caught")
            except SystemExit as stop:
                if str(stop).startswith("emu:"):
                    raise SystemExit(f"mutation {name} @{patch[0]:x}: emulator fault, not a check failure: {stop}")
                caught += 1
                print(f"mutation {name} @{patch[0]:x}: caught")
            else:
                raise SystemExit(f"mutation {name} @{patch[0]:x}: NOT caught")
    print(f"ok: {caught} target mutations caught")


if __name__ == "__main__":
    main()
