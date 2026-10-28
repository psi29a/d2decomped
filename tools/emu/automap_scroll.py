"""game.exe's automap scroll (FUN_00459700, after FUN_0045b440 and FUN_00457640).

Usage: uv run python automap_scroll.py

The cels and marks draw at (px / div - scroll_x, py / div - scroll_y)
(0x7a5198 / 0x7a519c). One line per case: client_x client_y mini on_left
portraits side scroll_x scroll_y, for tests/test_automap_scroll.cpp. Only
the player lookup, the side panel test, the portraits test and the cel
draws are stubbed; the rest is game.exe's own code at 800 x 600.
"""
import emu

e = emu.Emu()
state = {}
e.hook(0x463dd0, lambda e: 1, 0)                      # the player unit: any non-null
e.hook(0x620650, lambda e: state["x"], 1)             # its client px
e.hook(0x6206b0, lambda e: state["y"], 1)
e.hook(0x476ac0, lambda e: 0, 0)                      # no screen shake offset
e.hook(0x492c10, lambda e: state["portraits"], 0)
e.hook(0x45ae90, lambda e: state["side"], 0)          # 0 none, 1 / 2 a side panel open
e.hook(0x459440, lambda e: 0, 0)                      # the cel lists' draw

W, H = 800, 600
for addr, value in ((0x71146c, W), (0x711470, H), (0x7a5220, W), (0x7a521c, H - 40), (0x7a5164, e.alloc(0x1c))):   # 0x7a5164: the layer's map
    e.w32(addr, value)
for client_x, client_y in ((16000, 8000), (16005, 8003), (-1234, 98765), (77, 13)):
    for mini in (0, 1):
        for on_left in (0, 1):
            for portraits in (0, 1):
                for side in (0, 1, 2):
                    state.update(x=client_x, y=client_y, portraits=portraits, side=side)
                    left = 1 if side == 1 else 0 if side == 2 else on_left
                    e.w32(0x7a5150, mini)
                    e.w32(0x711254, 20 if mini else 10)
                    for addr in (0x7a51e0, 0x7a51e4, 0x7a51e8, 0x7a51ec):
                        e.w32(addr, left)
                    e.w32(0x7a51f0, portraits)
                    e.call(0x457640, ecx=1)
                    e.call(0x45b440)
                    e.call(0x459700)
                    print(client_x, client_y, mini, left, portraits, side, e.s32(0x7a5198), e.s32(0x7a519c))
