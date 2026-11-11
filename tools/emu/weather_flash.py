# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's lightning flash state machine, FUN_00473910 (DRAW_Overlay_Weather).

Usage: uv run python weather_flash.py > weather_flash_cases.inc

Each flash tick: a flash left (0x7a89e8) decrements; at 0 a first flash
sets a 3-tick second flash (0x7a896c), and a second flash ends it, rolls
the next strike's delay (0x7a8968, RollBetweenMinAndMax 500..1999) and,
once a strike's already been heard (0x712b4c), rolls a thunder delay
(25..74) and, if SOUND_PlaySound (0x4b9a00) answers a channel, the sound's
position: y first, then x (0x4b99a0's nPosY, nPosX — read off the pushed
stack word and EDX at the call in the disassembly, 0x473a02..0x473a2b).
One line per case: second heard sound_ok seed_low seed_high strike_left
second_after heard_after thunder thunder_x thunder_y, for
tests/test_weather_flash.cpp. Only the player lookup and the two sound
calls are stubbed; the rest is game.exe's own code.
"""
import random

import emu
from unicorn.x86_const import UC_X86_REG_EDX

LIGHTNING, FLASH, SECOND, STRIKE_LEFT, HEARD = 0x7A8A08, 0x7A89E8, 0x7A896C, 0x7A8968, 0x712B4C

e = emu.Emu()
unit = e.alloc(0x100)
state = {"sound_ok": False}
capture = {}

e.hook(0x463DE0, lambda e: 0, 0)          # GetPlayerUnit_Net: none, so the room / rain-level lookup is skipped
e.hook(0x463DD0, lambda e: unit, 0)       # GetPlayerUnit_UI: the fake unit holding the seed at +0x20 / +0x24


def s32_of(v):
    """A uint32 as game.exe's signed 32-bit int (the thunder's x / y / countdown can be negative)."""
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def play_sound(e):
    """Stub SOUND_PlaySound: records the rolled delay, answers a channel only when sound_ok."""
    capture["delay"] = e.arg(0)
    return 1 if state["sound_ok"] else 0


def play_environment_sound(e):
    """Stub SOUND_PlayEnvironmentSound: records the thunder's x (EDX) and y (the pushed stack word)."""
    # nPosX is a register arg (EDX); nPosY is the pushed stack word (arg 0). SOUND_PlayEnvironmentSound's
    # signature (redivivus): nSoundId ECX, nPosX EDX, nPosY Stack[0x4], nPosZ Stack[0x8].
    capture["x"] = s32_of(e.mu.reg_read(UC_X86_REG_EDX))
    capture["y"] = s32_of(e.arg(0))
    return 0


e.hook(0x4B9A00, play_sound, 3)              # SOUND_PlaySound(id ECX, unit EDX, delay, volume, param)
e.hook(0x4B99A0, play_environment_sound, 2)  # SOUND_PlayEnvironmentSound(id ECX, x EDX, y, z)


def run(second, heard, sound_ok, seed_low, seed_high):
    """Set the flash state machine's globals and the fake player's seed, run one DRAW_Overlay_Weather
    frame for real, and return the resulting state and any thunder it queued."""
    e.mu.mem_write(unit, b"\0" * 0x100)
    e.w32(unit + 0x20, seed_low)
    e.w32(unit + 0x24, seed_high)
    e.w32(LIGHTNING, 1)
    e.w32(FLASH, 1)
    e.w32(SECOND, 1 if second else 0)
    e.w32(STRIKE_LEFT, 0)
    e.w32(HEARD, 1 if heard else 0)
    state["sound_ok"] = sound_ok
    capture.clear()
    e.call(0x473910)
    strike_left = s32_of(e.r32(STRIKE_LEFT))
    second_after = e.r32(SECOND)
    heard_after = e.r32(HEARD)
    thunder = capture.get("delay", -1)
    thunder_x, thunder_y = capture.get("x", 0), capture.get("y", 0)
    return strike_left, second_after, heard_after, thunder, thunder_x, thunder_y


def main():
    """Print one case line per (second, heard, sound_ok) branch x 20 random seeds."""
    print("// tools/emu/weather_flash.py: second heard sound_ok seed_low seed_high ->")
    print("// strike_left second_after heard_after thunder thunder_x thunder_y (game.exe)")
    rng = random.Random(20261111)
    cases = []
    for second in (False, True):
        for heard in (False, True):
            for sound_ok in (False, True):
                if not second and (heard or sound_ok):
                    continue   # the first flash ending never reaches the heard / sound branches
                if second and not heard and sound_ok:
                    continue   # not heard yet never reaches the sound branch either
                for _ in range(20):
                    cases.append((second, heard, sound_ok, rng.getrandbits(32), rng.getrandbits(32)))
    for second, heard, sound_ok, seed_low, seed_high in cases:
        strike_left, second_after, heard_after, thunder, thunder_x, thunder_y = run(second, heard, sound_ok, seed_low, seed_high)
        print(f"{{ {int(second)}, {int(heard)}, {int(sound_ok)}, {seed_low}u, {seed_high}u, {strike_left}, "
              f"{second_after}, {heard_after}, {thunder}, {thunder_x}, {thunder_y} }},")


if __name__ == "__main__":
    main()
