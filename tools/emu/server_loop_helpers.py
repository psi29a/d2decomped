# SPDX-License-Identifier: GPL-3.0-or-later
"""Run three small game.exe 1.14d server-loop and party-gate helpers on synthetic state.

    uv run python server_loop_helpers.py --exe /path/to/Game.exe [--mutation-check]

Runs game.exe's own code for:
- FUN_0052df80 (stdcall, 1 stack arg): stores fps in DAT_00731014 and signed
  1000 / fps (truncated toward zero) in DAT_00883d60.
- FUN_0052d310 (EDI = game): when game->frame (+0xa8) is a multiple of 0x5dc
  (1500), calls FUN_00558b90 and FUN_00542ac0 in order for acts 0..4 with
  ECX = game, EDX = act; skips both on any other frame.
- FUN_0053fd10 (ECX = game): reads *(game + 0x1d28); on null calls
  FUN_00408090(0x27), FUN_00408a60(0x6cc837, caller), FUN_00681e09(-1);
  otherwise returns bit 16 of *(ptr + 8).

Hooked, not claimed: FUN_00558b90, FUN_00542ac0, FUN_00408090, FUN_00408a60,
FUN_00681e09. Their arguments are recorded and asserted. d2d is not compared.

`--mutation-check` patches one instruction of each target in a fresh emulator
(plus a `ret` at the entry) and requires that function's check to fail.
"""
import argparse
import sys

import emu

if not __debug__:
    sys.exit("server_loop_helpers.py requires assertions; do not use python -O")

FPS_GLOBAL = 0x731014
FRAME_MS_GLOBAL = 0x883D60
PARTY_CONFIG_OFFSET = 0x1D28
ASSERT_EMPTY_STR = 0x6CC837
CALL_LIMIT = 100000


class Fatal(Exception):
    """Raised by the hooked fatal-error exit so a null guard ends the call."""


def trunc_div(numerator, denominator):
    """Signed 32-bit x86 idiv quotient (truncated toward zero)."""
    sign = -1 if (numerator < 0) ^ (denominator < 0) else 1
    return sign * (abs(numerator) // abs(denominator))


def check_frame_rate(engine):
    """FUN_0052df80 writes fps to DAT_00731014 and signed 1000 / fps to DAT_00883d60."""
    cases = (1, 10, 25, 30, 40, 60, 100, 1000, 1001, -25, -3)
    for fps in cases:
        engine.w32(FPS_GLOBAL, 0xDEADBEEF)
        engine.w32(FRAME_MS_GLOBAL, 0xDEADBEEF)
        engine.call(0x52DF80, fps & 0xFFFFFFFF, limit=CALL_LIMIT)
        assert engine.r32(FPS_GLOBAL) == (fps & 0xFFFFFFFF), fps
        want_ms = trunc_div(1000, fps) & 0xFFFFFFFF
        assert engine.r32(FRAME_MS_GLOBAL) == want_ms, (fps, engine.r32(FRAME_MS_GLOBAL), want_ms)
    return len(cases)


def check_expiry_sweep(engine):
    """FUN_0052d310 runs FUN_00558b90 then FUN_00542ac0 for acts 0..4 iff frame % 1500 == 0."""
    game = engine.alloc(0x200)
    calls = []

    def record(tag):
        """Return a fastcall hook that records (tag, ecx, edx)."""
        def handler(machine):
            calls.append((
                tag,
                machine.mu.reg_read(emu.UC_X86_REG_ECX),
                machine.mu.reg_read(emu.UC_X86_REG_EDX),
            ))
            return 0
        return handler

    engine.hook(0x558B90, record("ground"), 0)
    engine.hook(0x542AC0, record("inactive"), 0)

    expected_sweep = []
    for act in range(5):
        expected_sweep.append(("ground", game, act))
        expected_sweep.append(("inactive", game, act))

    frames = (0, 1, 2, 749, 750, 1499, 1500, 1501, 2999, 3000, 3001, 4500, 0x5DC * 100)
    for frame in frames:
        calls.clear()
        engine.view("game", game).frame = frame
        engine.call(0x52D310, regs={"edi": game}, limit=CALL_LIMIT)
        want = expected_sweep if frame % 0x5DC == 0 else []
        assert calls == want, (frame, calls)
    return len(frames)


def check_party_gate(engine):
    """FUN_0053fd10 checks bit 16 of *(*(game + 0x1d28) + 8) and aborts if the pointer is null."""
    game = engine.alloc(0x1E00)
    config = engine.alloc(0x20)
    events = []

    def assert_caller(machine):
        events.append(("caller", machine.arg(0)))
        return 0xCAFEBABE

    def assert_msg(machine):
        events.append(("msg", machine.arg(0), machine.arg(1)))
        return 0

    def fatal_exit(machine):
        events.append(("exit", machine.arg(0)))
        raise Fatal()

    engine.hook(0x408090, assert_caller, 0)
    engine.hook(0x408A60, assert_msg, 0)
    engine.hook(0x681E09, fatal_exit, 0)

    engine.w32(game + PARTY_CONFIG_OFFSET, config)
    flags_cases = (
        0x00000000,
        0x00008000,
        0x00010000,
        0x00020000,
        0x0001FFFF,
        0xFFFEFFFF,
        0xFFFFFFFF,
    )
    for flags in flags_cases:
        events.clear()
        engine.w32(config + 8, flags)
        got = engine.call(0x53FD10, ecx=game, limit=CALL_LIMIT)
        assert got == ((flags >> 16) & 1), (hex(flags), got)
        assert events == [], events

    # Null pointer at game + 0x1d28 takes the fatal-error path.
    events.clear()
    engine.w32(game + PARTY_CONFIG_OFFSET, 0)
    try:
        engine.call(0x53FD10, ecx=game, limit=CALL_LIMIT)
    except Fatal:
        pass
    else:
        raise AssertionError("null game+0x1d28 did not abort")
    assert events == [
        ("caller", 0x27),
        ("msg", ASSERT_EMPTY_STR, 0xCAFEBABE),
        ("exit", 0xFFFFFFFF),
    ], events
    return len(flags_cases) + 1


CHECKS = (
    ("FUN_0052df80", 0x52DF80, check_frame_rate),
    ("FUN_0052d310", 0x52D310, check_expiry_sweep),
    ("FUN_0053fd10", 0x53FD10, check_party_gate),
)

# One real defect per target: (address, original bytes, broken bytes).
MUTATIONS = {
    "FUN_0052df80": ((0x52DF87, b"\xe8\x03", b"\xe9\x03"), (0x52DF80, None, b"\xc2\x04\x00")),
    "FUN_0052d310": (
        (0x52D318, b"\xdc\x05", b"\xdd\x05"),
        (0x52D33C, b"\x05", b"\x04"),
        (0x52D310, None, b"\xc3"),
    ),
    "FUN_0053fd10": (
        (0x53FD3B, b"\x10", b"\x0f"),
        (0x53FD1B, b"\x27", b"\x28"),
        (0x53FD10, None, b"\xc3"),
    ),
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
        run_check(options.exe, check)
        for address, original, _ in MUTATIONS[name]:
            assert original is None or pristine.read(address, len(original)) == original, hex(address)
        for patch in MUTATIONS[name]:
            try:
                run_check(options.exe, check, (patch,))
            except (AssertionError, Fatal):
                caught += 1
                print(f"mutation {name} @{patch[0]:x}: caught")
            except RuntimeError as fault:
                raise SystemExit(f"mutation {name} @{patch[0]:x}: emulator fault, not a check failure: {fault}") from None
            except SystemExit as stop:
                if str(stop).startswith("emu:"):
                    raise SystemExit(f"mutation {name} @{patch[0]:x}: emulator fault, not a check failure: {stop}") from None
                caught += 1
                print(f"mutation {name} @{patch[0]:x}: caught")
            else:
                raise SystemExit(f"mutation {name} @{patch[0]:x}: NOT caught")
    print(f"ok: {caught} target mutations caught")


if __name__ == "__main__":
    main()
