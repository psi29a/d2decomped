# SPDX-License-Identifier: GPL-3.0-or-later
"""Check five small client frontier functions in stock game.exe 1.14d.

Run: uv run python client_frontier.py --exe /path/to/game.exe [--mutation-check]

The executable entry points run for real.  Only FUN_004a7080's font-width and
glyph-draw callees are hooked, so the check can supply widths and record the
draw without loading the game's fonts.  The other four functions operate on
synthetic globals, unit records, or playing-sound list records.  No MPQs are
read.

# gaps-runs: 00453a90 00457460 004a7080 004b3500 004b9590
"""
import argparse

import emu

MASK32 = (1 << 32) - 1
UI_FLAGS = 0x7A27C0
GAMBLE_MODE = 0x7C0DB0
SOUND_HEAD = 0x7C5458


def signed(value):
    """Interpret a Python integer as game.exe's signed 32-bit value."""
    value &= MASK32
    return value - (1 << 32) if value & (1 << 31) else value


def check_centered_draw(exe, mutation):
    """Run FUN_004a7080 across both placement branches and argument forwarding."""
    machine = emu.Emu(exe)
    text = machine.alloc(16)
    machine.mu.mem_write(text, b"x\0\0\0")
    state = {"width": 0}
    calls = []

    def width(inner):
        calls.append(("width", inner.mu.reg_read(emu.UC_X86_REG_ECX)))
        return state["width"]

    def draw(inner):
        calls.append(("draw", inner.mu.reg_read(emu.UC_X86_REG_ECX),
                      signed(inner.mu.reg_read(emu.UC_X86_REG_EDX)),
                      inner.arg(0), inner.arg(1), inner.arg(2)))
        return 0

    machine.hook(0x501820, width, 0)
    machine.hook(0x502320, draw, 3)
    cases = []
    for left, right in ((0, 0), (10, 20), (-20, 20), (319, 384), (500, 450)):
        span = signed(right - left + 1)
        cases.extend((left, right, width_value) for width_value in
                     (0, 1, max(span - 1, 0), max(span, 0), max(span + 1, 1), 127))
    cases.extend(((-100, 99, 55), (17, 82, 14), (17, 82, 15), (17, 83, 15)))
    for index, (left, right, width_value) in enumerate(cases):
        colour = (index * 7 + 3) & 0xFF
        final = (index * 0x10203) & MASK32
        state["width"] = width_value
        calls.clear()
        machine.call(0x4A7080, right, text, final, ecx=left, edx=colour)
        span = signed(right - left + (0 if mutation == "exclusive-range" else 1))
        expected_x = left
        if signed(width_value) < span:
            expected_x = signed(left + ((span - signed(width_value)) >> 1))
        expected = [("width", text), ("draw", text, expected_x, colour, final, 0)]
        assert calls == expected, (left, right, width_value, calls, expected)
    return len(cases)


def check_getters(exe, mutation):
    """Run the indexed UI flag getter and the exact-value gamble-mode getter."""
    machine = emu.Emu(exe)
    cases = 0
    values = (0, 1, 2, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF)
    machine.mu.mem_write(UI_FLAGS, bytes(38 * 4))
    for index in (0, 1, 9, 21, 25, 37):
        for value in values:
            machine.w32(UI_FLAGS + index * 4, value)
            actual = machine.call(0x453A90, ecx=index)
            expected = int(value != 0) if mutation == "boolean-ui" else value
            assert actual == expected, (index, value, actual, expected)
            cases += 1
    for value in values:
        machine.w32(GAMBLE_MODE, value)
        actual = machine.call(0x4B3500)
        expected = int(value != 0) if mutation == "boolean-gamble" else value
        assert actual == expected, (value, actual, expected)
        cases += 1
    return cases


def check_unit_flags(exe, mutation):
    """Run FUN_00457460 and check its sole allowed data effect at unit +0xc4."""
    machine = emu.Emu(exe)
    unit = machine.alloc(0xD0)
    original = bytes((index * 29 + 11) & 0xFF for index in range(0xD0))
    cases = 0
    machine.call(0x457460, 1, ecx=0, edx=0xFFFFFFFF)
    for flags in (0, 1, 0x55555555, 0xAAAAAAAA, 0xFFFFFFFF):
        for mask in (0, 1, 2, 0x80000000, 0x55555555, 0xFFFFFFFF):
            for enabled in (0, 1, 2, 0xFFFFFFFF):
                machine.mu.mem_write(unit, original)
                machine.w32(unit + 0xC4, flags)
                before = bytearray(machine.read(unit, 0xD0))
                machine.call(0x457460, enabled, ecx=unit, edx=mask)
                use_enabled = not enabled if mutation == "invert-unit-toggle" else enabled
                expected_flags = (flags | mask) if use_enabled else (flags & ~mask)
                before[0xC4:0xC8] = (expected_flags & MASK32).to_bytes(4, "little")
                assert machine.read(unit, 0xD0) == bytes(before), (flags, mask, enabled)
                cases += 1
    return cases + 1


def check_sound_state(exe, mutation):
    """Run FUN_004b9590 over empty, missing, duplicate, and exact-state cases."""
    machine = emu.Emu(exe)
    arena = machine.alloc(3 * 8 + 3 * 0x40)
    nodes = [arena + index * 8 for index in range(3)]
    records = [arena + 3 * 8 + index * 0x40 for index in range(3)]
    scenarios = (
        (0, ()),
        (7, ()),
        (0, ((0, 1),)),
        (7, ((7, 0),)),
        (7, ((7, 1),)),
        (7, ((7, 2),)),
        (7, ((3, 1), (7, 1))),
        (7, ((7, 0), (7, 1))),
        (7, ((7, 1), (7, 0))),
        (0xFFFFFFFF, ((1, 1), (0xFFFFFFFF, 1), (2, 1))),
    )
    for wanted, entries in scenarios:
        machine.w32(SOUND_HEAD, nodes[0] if entries else 0)
        for index, (handle, state) in enumerate(entries):
            machine.w32(nodes[index], records[index])
            machine.w32(nodes[index] + 4, nodes[index + 1] if index + 1 < len(entries) else 0)
            machine.w32(records[index] + 8, handle)
            machine.w32(records[index] + 0x30, state)
        actual = machine.call(0x4B9590, ecx=wanted)
        matches = [state for handle, state in entries if wanted != 0 and handle == wanted]
        if mutation == "any-live-duplicate":
            expected = int(any(state == 1 for state in matches))
        else:
            expected = int(bool(matches) and matches[0] == 1)
        assert actual == expected, (wanted, entries, actual, expected)
    return len(scenarios)


def run_suite(exe, mutation=None):
    """Return the number of stock calls checked against the five models."""
    return (check_centered_draw(exe, mutation) + check_getters(exe, mutation)
            + check_unit_flags(exe, mutation) + check_sound_state(exe, mutation))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    cases = run_suite(args.exe)
    print(f"ok: {cases} client frontier cases over 5 functions")
    if args.mutation_check:
        mutations = ("exclusive-range", "boolean-ui", "boolean-gamble",
                     "invert-unit-toggle", "any-live-duplicate")
        for mutation in mutations:
            try:
                run_suite(args.exe, mutation)
            except AssertionError:
                print(f"mutation {mutation}: failed as required")
            else:
                raise AssertionError(f"mutation {mutation} was not detected")


if __name__ == "__main__":
    main()
