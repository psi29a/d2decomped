"""game.exe's 2D sound mix (FUN_00516830): a world sound's distance volume and pan.

Usage: uv run python sound_falloff.py

FUN_004dfc20 hands it the sound's offset from the player in client pixels
(dx, 2*dy, and 640 up) times 1/320; out come a volume and a pan, 0..255. One
line per offset: dx dy volume pan, for tests/test_sound_mix.cpp. Also
FUN_005165f0, the 0..255 -> DirectSound hundredths of a dB the volume goes
through (0, 255, v).
"""
import struct
import emu

e = emu.Emu()
out_volume, out_pan = e.alloc(4), e.alloc(4)
f32 = lambda value: struct.unpack("<I", struct.pack("<f", value))[0]
for dx in (0, 100, -100, 400, -400, 1000, 3000, 20000):
    for dy in (0, 50, -200, 500):
        e.call(0x516830, f32(dx / 320), f32(2 * dy / 320), f32(640 / 320), ecx=out_volume, edx=out_pan)
        print(dx, dy, e.s32(out_volume), e.s32(out_pan))
for value in (0, 1, 64, 128, 200, 254, 255):
    e.call(0x5165f0, f32(0), f32(255), f32(value))
    st0 = e.mu.reg_read(emu.UC_X86_REG_FP0) if hasattr(emu, "UC_X86_REG_FP0") else None
    print("db", value, st0)
