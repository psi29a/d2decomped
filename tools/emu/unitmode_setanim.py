# SPDX-License-Identifier: GPL-3.0-or-later
"""Setting a unit's mode and loading its cof, as the oracle: FUN_00480e30
(UNITMODE_SetModeAndLoadCof), FUN_00480e70 (UNITMODE_SetAnimMode) and
FUN_00480ec0 (UNITMODE_SetAnimModeSilent).

    uv run python unitmode_setanim.py

docs/research/re/collision.md cites FUN_00480e70 several times ("the modes
are set by FUN_00480e70(unit, mode)") but never its two siblings. All three
share one shape: clear UNITFLAG_SKSRVDOFUNC on the unit (`unit+0xc4 &=
~0x40`, at 0x480e3e / 0x480ec8 and the fastcall pair's own copy), call
FUN_00624690 (SetupUpdateFlag); on a true answer, call
FUN_00470610 (GFX_LoadUnitModeCof, unit, 0) and return 1, otherwise return 0
without loading a cof. FUN_00480e70 alone goes on to call FUN_00480d20
(UNITMODE_ShowElementalHitOverlay) and FUN_004cc5b0
(UNITSND_DispatchModeChangeSound) after the cof loads; the other two never
call either. This oracle hooks SetupUpdateFlag to answer a given 0/1, and
the three downstream callees to record whether they ran, over both answers
and a null unit, and diffs each real run against that shape: no
disagreement.

FUN_00480e30 has no caller anywhere in game.exe (0 xrefs to it in the
Ghidra database); FUN_00480ec0 has exactly one, MonsterMessage's unique
death case (monster base id 0xbe) going silent instead of playing the
overlay/sound when the monster is already in Skill2. Neither claim is
checked by this oracle; it only runs the two functions' own bodies.
"""
import emu

SETUP_UPDATE_FLAG = 0x624690
LOAD_COF = 0x470610
SHOW_OVERLAY = 0x480D20
DISPATCH_SOUND = 0x4CC5B0
FLAG_SKSRVDOFUNC = 0x40
FLAG_OFFSET = 0xC4


def setup(e, answer):
    """Hook the four callees: SetupUpdateFlag answers `answer`; the rest just record a call."""
    seen = []
    e.hook(SETUP_UPDATE_FLAG, lambda ev: (seen.append(("SetupUpdateFlag", ev.arg(0), ev.arg(1))), answer)[1], 2)
    e.hook(LOAD_COF, lambda ev: (seen.append(("LoadCof", ev.mu.reg_read(emu.UC_X86_REG_ECX))), 0)[1], 0)
    e.hook(SHOW_OVERLAY, lambda ev: (seen.append(("ShowOverlay", ev.mu.reg_read(emu.UC_X86_REG_ECX),
                                                   ev.mu.reg_read(emu.UC_X86_REG_EDX))), 0)[1], 0)
    e.hook(DISPATCH_SOUND, lambda ev: (seen.append(("DispatchSound", ev.mu.reg_read(emu.UC_X86_REG_ECX),
                                                     ev.mu.reg_read(emu.UC_X86_REG_EDX), ev.arg(0))), 0)[1], 1)
    return seen


def run_case(e, name, addr, convention, unit, mode, answer):
    seen = setup(e, answer)
    if unit:
        e.w32(unit + FLAG_OFFSET, 0xFFFFFFFF)   # every bit set, so clearing 0x40 is visible
    if convention == "esi_stack":               # FUN_00480e30: nAnimMode on the stack, pUnit in ESI
        result = e.call(addr, mode, regs={"ESI": unit})
    else:                                        # fastcall: pUnit ECX, nAnimMode EDX
        result = e.call(addr, ecx=unit, edx=mode)

    calls = [s[0] for s in seen]
    expect_calls = ["SetupUpdateFlag"]
    if answer:
        expect_calls.append("LoadCof")
        if name == "SetAnimMode":
            expect_calls += ["ShowOverlay", "DispatchSound"]
    flag_ok = (not unit) or (e.r32(unit + FLAG_OFFSET) & FLAG_SKSRVDOFUNC) == 0
    setup_args_ok = seen[0][1:] == (unit, mode)
    # Only FUN_00480e30 returns BOOL; FUN_00480e70/00480ec0 are void (their EAX is whatever
    # SetupUpdateFlag's hook left behind, not a claim this oracle makes).
    result_ok = result == (1 if answer else 0) if name == "SetModeAndLoadCof" else True
    extra_ok = all(s[0] not in ("LoadCof", "ShowOverlay", "DispatchSound") or s[0] in expect_calls for s in seen)
    ok = calls == expect_calls and flag_ok and setup_args_ok and result_ok and extra_ok
    print(name, bool(unit), mode, answer,
          "OK" if ok else f"DISAGREE calls={calls} expect={expect_calls} flag_ok={flag_ok} result={result}")


def main():
    e = emu.Emu()
    unit = e.alloc(0x200)
    funcs = [
        ("SetModeAndLoadCof", 0x480E30, "esi_stack"),
        ("SetAnimMode", 0x480E70, "fastcall"),
        ("SetAnimModeSilent", 0x480EC0, "fastcall"),
    ]
    for name, addr, convention in funcs:
        for test_unit in (0, unit):
            for mode in (0, 3, 0x13):
                for answer in (0, 1):
                    run_case(e, name, addr, convention, test_unit, mode, answer)


if __name__ == "__main__":
    main()
