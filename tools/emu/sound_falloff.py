"""game.exe's 2D sound mix (FUN_00516830): a world sound's distance volume and pan.

Usage: uv run python sound_falloff.py

FUN_004dfc20 hands it the sound's offset from the player in client pixels
(dx, 2*dy, and 640 up) times 1/320; out come a volume and a pan, 0..255. One
line per offset: dx dy volume pan, for tests/test_sound_mix.cpp. Then
FUN_005165f0 (0, 127, v), the hundredths of a dB SetVolume and SetPan get.
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
# FUN_005165f0 leaves its answer in ST0: a thunk calls it and stores it.
out_db, thunk = e.alloc(4), e.alloc(64)
push = lambda value: b"\x68" + struct.pack("<I", value)
for value in (0.0, 1.0, 12.7, 63.5, 100.0, 127.0):
    code = push(f32(value)) + push(f32(127.0)) + push(f32(0.0))
    code += b"\xe8" + struct.pack("<i", 0x5165f0 - (thunk + len(code) + 5))
    code += b"\xd9\x1d" + struct.pack("<I", out_db) + b"\xc3"
    e.mu.mem_write(thunk, code)
    e.mu.ctl_remove_cache(thunk, thunk + len(code))
    e.call(thunk)
    print("db", value, struct.unpack("<f", e.read(out_db, 4))[0])   # 2000 log10(v / 127); -10000 at 0
