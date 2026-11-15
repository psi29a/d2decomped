# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the screen dispatcher FUN_00455ae0 and the trade-close wait FUN_004b9180 in stock game.exe 1.14d.

    uv run python ui_dispatch.py --exe /path/to/game.exe [--mutation-check]

FUN_00455ae0 (ECX = a screen id, `jmp [ecx*4 + 0x455e80]` for 0..0x25) opens or
closes one UI screen. Its calls come in pairs that the decompile reads
wrongly: `FUN_004f59a0` only returns the global at 0x7c8cbc (the pushed word is
left on the stack), then `FUN_00451e10` is called with ECX = that global, EDX = a
string address and the pushed word as its one stack argument (`ret 4`).
Every callee is hooked and logged; the model below was read from the jump
table's disassembly (call target, the constant ECX / EDX each call gets, the
pushed word): one row per screen id, in call order. Ids 0xd, 0x1b..0x1e and 0x20
do nothing, as does any id above 0x25. Screen 5 also needs nine globals to be
zero for its `FUN_0046aa20` call and 0x7a27e8 non-zero for its `FUN_00451db0`;
screen 0x17 returns early when `FUN_004b9180` says so; screen 0xa clears 0x7a2864.

FUN_004b9180 (the trade-close wait) runs for real with GetTickCount scripted: its
30 s limit is `start + 30000` in an unsigned 32-bit add, so a start within 30 s
of the wrap times out at once (docs/research/re/bugs.md 109). A start of
0xffffffff is "none armed", and a state other than 7 returns 0.

`--mutation-check` breaks the model one defect at a time and requires each to fail.
"""
import argparse
import hashlib
import random
from pathlib import Path

import unicorn.x86_const as uc

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
MUTATIONS = ("wrong-edx", "wrong-push", "screen-5-one-global", "screen-17-no-early-return", "screen-a-keeps-flag",
             "tick-signed", "tick-no-wrap", "wait-armed-at-ffffffff", "wait-state-any")
mutation = None
GLOBAL_UI = 0x7C8CBC
SENTINEL = 0x00BEEF00
SCREEN5_ZERO = (0x7A27F0, 0x7A281C, 0x7A2824, 0x7A2828, 0x7A282C, 0x7A2830, 0x7A2834, 0x7A2838, 0x7A2840)
NO_ARG = {0x4F59A0}
# screen id: ((callee, ECX or "eax" (the global) or None = unconstrained, EDX or None, pushed word or None), ...)
TABLE = {
    0x0: ((0x4f59a0, None, None, 0x4), (0x451e10, 'eax', 0x6d5f28, None),),
    0x1: ((0x453ee0, None, None, None), (0x4f59a0, None, None, 0x4), (0x451e10, 'eax', 0x6d5f58, None), (0x453ae0, None, None, None),),
    0x2: ((0x453ee0, None, None, None), (0x4f59a0, None, None, 0x2), (0x451e10, 'eax', 0x6d5fd0, None), (0x453ae0, None, None, None),),
    0x3: ((0x4f59a0, None, None, 0x1), (0x451e10, 'eax', 0x6d5fc4, None),),
    0x4: ((0x4f59a0, None, None, 0x2), (0x451e10, 'eax', 0x6d5fac, None), (0x453ae0, None, None, None), (0x4abf10, None, None, None),),
    0x5: ((0x47b2e0, None, None, None), (0x47b4e0, None, None, None), (0x46aa20, 0x1, 0x1, None), (0x4f59a0, None, None, 0x5), (0x451db0, 'eax', 0x6d6084, None),),
    0x6: ((0x4f59a0, None, None, 0x2), (0x451e10, 'eax', 0x6d5fe8, None),),
    0x7: ((0x4f59a0, None, None, 0x2), (0x451e10, 'eax', 0x6d600c, None),),
    0x8: ((0x453ae0, None, None, None), (0x4b3f10, None, None, None),),
    0x9: ((0x4f59a0, None, None, 0x7), (0x451e10, 'eax', 0x6d6030, None), (0x466fe0, None, None, None),),
    0xa: ((0x4f59a0, None, None, 0x5), (0x451e10, 'eax', 0x6d6084, None),),
    0xb: ((0x4f59a0, None, None, 0x1), (0x451e10, 'eax', 0x6d6024, None), (0x4f59a0, None, None, 0xd), (0x451e10, 'eax', 0x6d60c0, None), (0x4a5200, 0x0, None, None),),
    0xc: ((0x4f59a0, None, None, 0x8), (0x451e10, 'eax', 0x70f878, None), (0x4b3f10, None, None, None), (0x46aa20, 0x1, 0x1, None),),
    0xd: (),
    0xe: ((0x453ae0, None, None, None), (0x4b3f10, None, None, None),),
    0xf: ((0x4f59a0, None, None, 0x7), (0x451e10, 'eax', 0x70f8d8, None), (0x4a28d0, None, None, None), (0x453ae0, None, None, None),),
    0x10: ((0x4f59a0, None, None, 0x2), (0x451e10, 'eax', 0x6d61b0, None), (0x49ff00, None, None, None), (0x453ae0, None, None, None),),
    0x11: ((0x4f59a0, None, None, 0x4), (0x451e10, 'eax', 0x6d615c, None), (0x4a3000, None, None, None),),
    0x12: ((0x453ae0, None, None, None),),
    0x13: ((0x4f59a0, None, None, 0x1), (0x451e10, 'eax', 0x6d6000, None),),
    0x14: ((0x4f59a0, None, None, 0x5), (0x451e10, 'eax', 0x6d61c8, None), (0x49cf50, None, None, None), (0x453ae0, None, None, None),),
    0x15: ((0x4f59a0, None, None, 0x2), (0x451e10, 'eax', 0x6d6204, None),),
    0x16: ((0x4f59a0, None, None, 0x2), (0x451e10, 'eax', 0x6d621c, None), (0x49a020, None, None, None), (0x453ae0, None, None, None),),
    0x17: ((0x4b9180, None, None, None), (0x45aea0, 0x0, None, None), (0x453ee0, None, None, None), (0x4f59a0, None, None, 0xd), (0x451e10, 'eax', 0x70f930, None), (0x453ae0, None, None, None), (0x4b8940, 0x0, None, None), (0x46aa20, 0x1, 0x1, None),),
    0x18: ((0x4a01a0, None, None, None),),
    0x19: ((0x453ee0, None, None, None), (0x45aea0, 0x0, None, None), (0x489ee0, None, None, None), (0x4f59a0, None, None, 0x8), (0x451e10, 'eax', 0x70f9d0, None), (0x453ae0, None, None, None), (0x46aa20, 0x1, 0x1, None),),
    0x1a: ((0x45aea0, 0x0, None, None), (0x48a500, None, None, None), (0x4f59a0, None, None, 0x7), (0x451e10, 'eax', 0x70fa30, None), (0x453ae0, None, None, None), (0x46aa20, 0x1, 0x1, None),),
    0x1b: (),
    0x1c: (),
    0x1d: (),
    0x1e: (),
    0x1f: ((0x4f59a0, None, None, 0x2), (0x451e10, 'eax', 0x70fa84, None),),
    0x20: (),
    0x21: ((0x494fc0, None, None, None), (0x4f59a0, None, None, 0x5), (0x451e10, 'eax', 0x6d62c4, None),),
    0x22: ((0x4f59a0, None, None, 0x2), (0x451e10, 'eax', 0x6d6300, None),),
    0x23: ((0x4f59a0, None, None, 0x3), (0x451e10, 'eax', 0x6d6318, None),),
    0x24: ((0x4f59a0, None, None, 0x3), (0x451e10, 'eax', 0x6d633c, None), (0x487990, None, None, None),),
    0x25: ((0x4f59a0, None, None, 0x2), (0x451e10, 'eax', 0x6d6360, None),),
}
TWO_ARG = {0x451E10, 0x451DB0}      # stdcall with one pushed word
CALLEES = (0x451DB0, 0x451E10, 0x453AE0, 0x453EE0, 0x45AEA0, 0x466FE0, 0x46AA20, 0x47B2E0, 0x47B4E0, 0x487990, 0x489EE0,
           0x48A500, 0x494FC0, 0x49A020, 0x49CF50, 0x49FF00, 0x4A01A0, 0x4A28D0, 0x4A3000, 0x4A5200, 0x4ABF10, 0x4B3F10,
           0x4B8940, 0x4B9180)
WAIT_CALLS = ((0x48A6A0, None), (0x4B8940, 0), (0x477EE0, None), (0x44B880, 0), (0x44C860, 1), (0x44D520, None))
TICK = [0]


def w_get_tick_count(_machine):
    """GetTickCount as the harness scripts it (stdcall, no arguments)."""
    return TICK[0], 0


class Rig:
    """The hooked callees and their log of (address, ECX, EDX, pushed word)."""

    def __init__(self, machine, addresses=CALLEES):
        """Hook each callee; FUN_004f59a0 stays game.exe's own."""
        self.m = machine
        self.log = []
        self.wait = 0
        for address in addresses:
            machine.hook(address, self._make(address), 1 if address in TWO_ARG else 0)

    def _make(self, address):
        """A hook for one callee."""
        def hook(current):
            """Log the call; FUN_004b9180 answers the scripted value."""
            word = current.arg(0) if address in TWO_ARG else None
            self.log.append((address, current.mu.reg_read(uc.UC_X86_REG_ECX), current.mu.reg_read(uc.UC_X86_REG_EDX), word))
            return self.wait if address == 0x4B9180 else 0
        return hook


def expected(screen, flags):
    """The calls the model says screen `screen` makes: (address, ECX, EDX, word); ECX / EDX None = unconstrained."""
    log, pushed = [], None
    zero = all(not flags[address] for address in SCREEN5_ZERO)
    if mutation == "screen-5-one-global":
        zero = not flags[SCREEN5_ZERO[0]]
    for address, ecx, edx, word in TABLE.get(screen, ()):
        if address == 0x4F59A0:
            pushed = word
            continue
        if screen == 5 and ((address == 0x46AA20 and not zero) or (address == 0x451DB0 and not flags[0x7A27E8])):
            continue
        if address in TWO_ARG:
            edx = edx + 4 if mutation == "wrong-edx" and screen == 3 else edx
            word = pushed + 1 if mutation == "wrong-push" and screen == 9 else pushed
            log.append((address, SENTINEL, edx, word))
        else:
            log.append((address, ecx, edx, None))
        if screen == 0x17 and address == 0x4B9180 and flags["wait"] and mutation != "screen-17-no-early-return":
            return log
    return log


def same(have, want):
    """Whether two call logs agree (None in the model is a wildcard)."""
    if len(have) != len(want):
        return False
    return all(h[0] == w[0] and (w[1] is None or h[1] == w[1]) and (w[2] is None or h[2] == w[2]) and h[3] == w[3]
               for h, w in zip(have, want))


def run_screens(exe):
    """Every screen id (and a few out of range) under the flag combinations that matter; returns the case count."""
    machine = emu.Emu(str(exe))
    rig = Rig(machine)
    cases = 0
    for screen in list(range(0x30)) + [0x100, 0x7FFFFFFF]:
        variants = [{}]
        if screen == 5:
            variants = [{address: 0 for address in SCREEN5_ZERO}] + [{address: 1} for address in SCREEN5_ZERO]
            variants = [{**v, 0x7A27E8: flag} for v in variants for flag in (0, 1)]
        if screen == 0x17:
            variants = [{"wait": 0}, {"wait": 1}]
        for variant in variants:
            flags = {address: variant.get(address, 0) for address in SCREEN5_ZERO + (0x7A27E8,)}
            flags["wait"] = variant.get("wait", 0)
            for address, value in flags.items():
                if address != "wait":
                    machine.w32(address, value)
            machine.w32(GLOBAL_UI, SENTINEL)
            machine.w32(0x7A2864, 0x99)
            rig.log.clear()
            rig.wait = flags["wait"]
            top = emu.STACK + emu.STACK_SIZE - 0x1000
            machine.call(0x455AE0, ecx=screen, limit=5000)
            if machine.mu.reg_read(uc.UC_X86_REG_ESP) != top:
                raise AssertionError(f"screen {screen:#x} left the stack unbalanced")
            want = expected(screen, flags)
            if not same(rig.log, want):
                raise AssertionError(f"screen {screen:#x} {variant}: calls {[(hex(a), hex(c), hex(e), w) for a, c, e, w in rig.log]}\n"
                                     f" model {[(hex(a), c, e, w) for a, c, e, w in want]}")
            want_flag = 0x99 if screen != 0xA or mutation == "screen-a-keeps-flag" else 0
            if machine.r32(0x7A2864) != want_flag:
                raise AssertionError(f"screen {screen:#x}: 0x7a2864 is {machine.r32(0x7A2864):#x}, model {want_flag:#x}")
            cases += 1
    return cases


def run_wait(exe):
    """FUN_004b9180 for real with a scripted tick counter; returns the case count."""
    emu.Emu.w_GetTickCount = w_get_tick_count
    machine = emu.Emu(str(exe))
    rig = Rig(machine, tuple(address for address, _ in WAIT_CALLS))
    rng = random.Random(0x4B9180)
    mask = 0xFFFFFFFF
    edges = [(0xFFFFFFFE, 0xFFFFFFFF), (0xFFFFFFFE, 0), (0xFFFFFFFE, 29998), (0xFFFFFFFE, 29999), (0xFFFF8AD0, 0), (0xFFFF8ACF, 0),
             (0xFFFFFFFF, 5), (0xFFFFFFFF, 0xFFFFFFFF), (0, 29999), (0, 30000), (0, 30001), (100, 30100), (100, 30101)]
    cases = edges + [(rng.choice((rng.getrandbits(32), 0xFFFF8000 + rng.randrange(0x8000))), rng.getrandbits(32)) for _ in range(1500)]
    count = 0
    for start, tick in cases:
        for state in (7, 7, 7, 0, 6, 8):
            machine.w32(0x7C0E7C, state)
            machine.w32(0x7C0E70, start)
            TICK[0] = tick
            rig.log.clear()
            got = machine.call(0x4B9180, limit=5000)
            armed = start != 0xFFFFFFFF or mutation == "wait-armed-at-ffffffff"
            total = (start + 30000) & mask
            if mutation == "tick-no-wrap":
                total = start + 30000
            late = tick > total
            if mutation == "tick-signed":
                late = (tick - (1 << 32) if tick & 0x80000000 else tick) > (total - (1 << 32) if total & 0x80000000 else total)
            runs = (state == 7 or mutation == "wait-state-any") and armed and late
            want_ret = 1 if (state == 7 or mutation == "wait-state-any") else 0
            want_calls = [(a, c) for a, c in WAIT_CALLS] if runs else []
            have_calls = [(entry[0], entry[1] if want[1] is not None else None) for entry, want in zip(rig.log, want_calls)]
            if got != want_ret or [entry[0] for entry in rig.log] != [a for a, _ in want_calls] or have_calls != want_calls:
                raise AssertionError(f"start {start:#x} tick {tick:#x} state {state}: ret {got}, calls {[hex(e[0]) for e in rig.log]}; "
                                     f"model ret {want_ret}, timed out {runs}")
            if runs and machine.r32(0x7C0E7C) != 0:
                raise AssertionError("a timed-out wait left its state set")
            count += 1
    return count


def run(exe):
    """Both checks; returns the summary line."""
    return f"ok: {run_screens(exe)} screen scenarios, {run_wait(exe)} trade-wait ticks"


def main():
    """Entry point: run the checks, or every mutation and require each to fail."""
    global mutation
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    print(run(exe))
    if args.mutation_check:
        for name in MUTATIONS:
            mutation = name
            try:
                run(exe)
            except AssertionError:
                print(f"mutation {name}: failed as required")
                continue
            finally:
                mutation = None
            raise SystemExit(f"mutation {name} was not caught")


if __name__ == "__main__":
    main()
