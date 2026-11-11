# SPDX-License-Identifier: GPL-3.0-or-later
"""The click's own path onto the ground, as the oracle: game.exe's own
PLAYER_CalculateMovementTarget (FUN_004623c0) and CLIENT_MovePlayerToPosition
(FUN_00461840), run natively, with their pathing and network sub-calls hooked
so each is checked on its own glue, the way cursor-units.md and collision.md
cite it.

    uv run python click_move.py

## FUN_004623c0: the held-frame throttle and the minimum-step clamp

Runs natively: the delta/clamp arithmetic, WEATHER_ApproximateDistance
(0x474080, pure) and the float-to-int helper (0x682fd0, pure).
GetEntryIndex (0x44da90) reads the global clock `0x7a0498` directly, so the
case sets it instead of hooking; the throttle's own memory,
`gnLastMovementTargetEntryIndex` at `0x7a5268`, is read back after the call.
GetXPos/GetYPos (0x6488c0/0x648900) run natively on a real D2DynamicPathStrc
(`structs.tsv` path+2 = nPosX, path+6 = nPosY).

`test_throttle` sweeps every (mode, held, delta, flag) combination collision.md
"How often" / "How near" describes and checks the branch (1 keep-going, 0
nothing-sent) and, when it returns 1, that the clock `0x7a5268` was stamped.

## FUN_00461840: path then send

Hooked (not the claim; PATH_CalculatePath's own search is `drlg.md` "Bounded
path expansion", already traced): SetSP1 (0x648ad0), PATH_CalculatePath
(0x649970, answers found/not-found), GetLastPathPointX/Y (0x648a40/0x648a60,
a fixed point). The claim is the glue: a failed path returns 0 and never
calls UNITMODE_ExecuteSkillAction (0x481030, the LAB_00461879 site collision.md
cites); a found one calls it with the path's last point and the mode passed
in, and returns 1.
"""
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

MOUSE_CTX = 0x40          # flags(u32) ... nX +0xc, nY +0x10 (D2UnderMouseStrc, read from the decompile)
CLOCK = 0x7A0498          # GAMEDATA_GetGlobalTickCount's sibling: GetEntryIndex's global (0x44da90)
LAST_INDEX = 0x7A5268     # gnLastMovementTargetEntryIndex, stamped at 0x462545


def test_throttle(e):
    """FUN_004623c0 (stack pPlayerUnit, EBX pMouseCtx): the held-frame throttle and step clamp."""
    unit = e.alloc(0x100)
    path = e.alloc(0x200)
    ctx = e.alloc(MOUSE_CTX)
    e.view("unit", unit).type = 0                 # UNIT_PLAYER: dynamic path
    e.view("unit", unit).path = path
    cases = 0
    for mode in (0, 1, 2, 3, 6, 7):                # < Walk(2): stopped; Walk..Run/TownWalk(2,3,6): moving; 7: past Run, not TownWalk
        e.view("unit", unit).mode = mode
        for dx, dy in ((0, 0), (0, 1), (300, 0), (5, 0), (1, 1), (2, 0)):
            for flags in (0, 4, 0x44, 8, 0x48, 0x10):     # none, press, press+run, held, held+run, release
                for last_index, now in ((0, 0), (0, 10), (5, 10), (5, 11)):
                    cases += 1
                    e.view("path", path).x, e.view("path", path).y = dx & 0xFFFF, dy & 0xFFFF
                    e.w32(ctx, flags)
                    e.w32(ctx + 0xC, 0)
                    e.w32(ctx + 0x10, 0)
                    e.w32(CLOCK, now)
                    e.w32(LAST_INDEX, last_index)
                    ret = e.call(0x4623C0, unit, regs={"ebx": ctx}) & 0xFFFFFFFF
                    throttled = bool(flags & 8) and 2 <= mode and (mode <= 3 or mode == 6)
                    stamped = e.r32(LAST_INDEX)
                    # the model: nothing moves under the dead zone or past 0x100 on an axis; a held,
                    # walking/running player is throttled to one send per 7 frames (280 ms at 25/s)
                    if (dx, dy) == (0, 0) or max(abs(dx), abs(dy)) >= 0x100:
                        expect = 0
                    elif throttled and now - last_index < 7:
                        expect = 0
                    else:
                        expect = 1
                    if ret != expect:
                        raise AssertionError(f"mode {mode} dx{dx} dy{dy} flags {flags:#x} clock {last_index}->{now}: game {ret} model {expect}")
                    if ret == 1 and stamped != now:
                        raise AssertionError(f"mode {mode} dx{dx} dy{dy} flags {flags:#x}: 0x7a5268 not stamped ({stamped} != {now})")
                    if ret == 0 and stamped != last_index:
                        raise AssertionError(f"mode {mode} dx{dx} dy{dy} flags {flags:#x}: 0x7a5268 moved on a no-op ({stamped} != {last_index})")
    print(f"ok: {cases} FUN_004623c0 throttle/clamp cases")


def test_move(e):
    """FUN_00461840 (EDI pUnit, CX nX, AX nY, stack nSkillActionMode): path then send, or nothing."""
    unit = e.alloc(0x40)
    path = e.alloc(0x40)
    e.w32(unit + 0x2C, path)
    calls = {}

    def set_sp1(cur):
        """SetSP1(path, nX, nY) at 0x648ad0: record the args, change nothing."""
        calls["sp1"] = (cur.arg(0), cur.arg(1), cur.arg(2))
        return 0

    def calc_path(cur):
        """PATH_CalculatePath(path, unit, 0) at 0x649970: record the args, answer the case's found/not-found."""
        calls["calc_path"] = (cur.arg(0), cur.arg(1), cur.arg(2))
        return calls["path_result"]

    def last_x(cur):
        """GetLastPathPointX(path) at 0x648a40: a fixed 111, so the exec_skill hook can check it's forwarded."""
        calls["last_x"] = cur.arg(0)
        return 111

    def last_y(cur):
        """GetLastPathPointY(path) at 0x648a60: a fixed 222, so the exec_skill hook can check it's forwarded."""
        calls["last_y"] = cur.arg(0)
        return 222

    def exec_skill(cur):
        """UNITMODE_ExecuteSkillAction(ECX mode, EDX unit, nX, nY, 0) at 0x481030: record the args, change nothing."""
        ecx = cur.mu.reg_read(UC_X86_REG_ECX)
        edx = cur.mu.reg_read(UC_X86_REG_EDX)
        calls["exec"] = (ecx, edx, cur.arg(0), cur.arg(1), cur.arg(2))
        return 0

    e.hook(0x648AD0, set_sp1, 3)
    e.hook(0x649970, calc_path, 3)
    e.hook(0x648A40, last_x, 1)
    e.hook(0x648A60, last_y, 1)
    e.hook(0x481030, exec_skill, 3)

    for path_result, mode in ((0, 5), (1, 9)):
        calls.clear()
        calls["path_result"] = path_result
        ret = e.call(0x461840, mode, regs={"edi": unit, "cx": 30, "ax": 40}) & 0xFFFFFFFF
        if calls.get("sp1") != (path, 30, 40):
            raise AssertionError(f"SetSP1(path, nX, nY) args differ: {calls}")
        if calls.get("calc_path") != (path, unit, 0):
            raise AssertionError(f"PATH_CalculatePath(path, unit, 0) args differ: {calls}")
        if path_result == 0:
            if ret != 0 or "exec" in calls:
                raise AssertionError(f"a failed path must return 0 and never reach ExecuteSkillAction: {calls}, ret {ret}")
        else:
            if ret != 1 or calls.get("exec") != (mode, unit, 111, 222, 0):
                raise AssertionError(f"a found path must call ExecuteSkillAction(mode, unit, lastX, lastY, 0) and return 1: {calls}, ret {ret}")
    print("ok: 2 FUN_00461840 path/send cases")


def main():
    """Run both FUN_004623c0 and FUN_00461840 cases against stock game.exe."""
    e = emu.Emu()
    test_throttle(e)
    test_move(e)


if __name__ == "__main__":
    main()
