# SPDX-License-Identifier: GPL-3.0-or-later
"""Where Nihlathak's marker makes him, as the oracle: FUN_005459a0's tries.

    uv run python nihlathak_tries.py 2000 7 > g.txt
    ../../build/tests/test_marker_npc --marker-npc g.txt   # d2d's rules::marker_npc: 0 differ

InitFn 68 (LAB_0058a610, call at 0x58a666) passes the marker's spot, its
room, 1 (made: unit +0xc4 |= 0x3000000) and 0x202. Run for real: FUN_005459a0
and the room rectangle read (FUN_00619730, room +0x4c x, +0x50 y, +0x54 w,
+0x58 h in subtiles). Hooked, driven by the case: the free-spot search
FUN_00545340 (size 2, mask 0x100, 100 rings), the room-near lookup
FUN_00463740 and the maker FUN_005b2f20.

A case line, then the calls game.exe made, then the end:

    case N room RX RY W H marker MX MY free FMASK DX DY near NMASK make K
    free ROOM X Y          free-spot search k fails when bit k of FMASK is set,
                           else gives (X+DX, Y+DY) in the room it was called with
    near ROOM X Y          room-near call k gives 0 when bit k of NMASK is set,
                           else a new room numbered k+1 (0 is the marker's room)
    make ROOM X Y RETRY    maker call k makes the unit when k == K (-1: none)
    made K FLAG | none

A failed room-near call (0x545aa3) starts over from the marker: the free-spot
search runs in the marker's room at the marker's spot. A failed free-spot
search makes at the marker's spot in the marker's room (0x545ad5). The next
try steps (+1, +1) from the spot last used, from the room last used.
"""
import random
import sys

import emu

GAME = 0x1000


def signed(value):
    return value - (1 << 32) if value >= 0x80000000 else value


class Tries:
    def __init__(self):
        self.e = e = emu.Emu()
        self.rooms = [e.alloc(0x100) for _ in range(24)]
        self.unit = e.alloc(0x200)
        e.hook(0x545340, self.free, 5)
        e.hook(0x463740, self.near, 1)
        e.hook(0x5B2F20, self.make, 6)

    def room_no(self, room):
        return self.rooms.index(room)

    def free(self, h):
        room = h.mu.reg_read(emu.UC_X86_REG_ECX)
        spot = h.mu.reg_read(emu.UC_X86_REG_EDX)
        x, y = h.s32(spot), h.s32(spot + 4)
        out = h.arg(2)
        k = self.free_calls
        self.free_calls += 1
        self.trace.append(f"free {self.room_no(room)} {x} {y}")
        if self.fmask >> k & 1:
            h.w32(out, 0)
            return 0
        h.w32(spot, x + self.dx)
        h.w32(spot + 4, y + self.dy)
        h.w32(out, room)
        return 1

    def near(self, h):
        room = h.mu.reg_read(emu.UC_X86_REG_ECX)
        x, y = signed(h.mu.reg_read(emu.UC_X86_REG_EDX)), signed(h.arg(0))
        k = self.near_calls
        self.near_calls += 1
        self.trace.append(f"near {self.room_no(room)} {x} {y}")
        return 0 if self.nmask >> k & 1 else self.rooms[k + 1]

    def make(self, h):
        room = h.mu.reg_read(emu.UC_X86_REG_EDX)
        x, y, retry = h.s32(h.mu.reg_read(emu.UC_X86_REG_ESP) + 4), signed(h.arg(1)), signed(h.arg(4))
        k = self.make_calls
        self.make_calls += 1
        self.trace.append(f"make {self.room_no(room)} {x} {y} {retry}")
        if k == self.make_ok:
            self.made = k
            return self.unit
        return 0

    def run(self, rect, marker, fmask, delta, nmask, make_ok):
        e = self.e
        room = self.rooms[0]
        for i, v in enumerate(rect):
            e.w32(room + 0x4C + 4 * i, v)
        e.w32(self.unit + 0xC4, 0)
        self.fmask, (self.dx, self.dy), self.nmask, self.make_ok = fmask, delta, nmask, make_ok
        self.free_calls = self.near_calls = self.make_calls = 0
        self.made, self.trace = None, []
        e.call(0x5459A0, marker[1], room, 1, 0x202, ecx=GAME, edx=marker[0])
        flag = e.r32(self.unit + 0xC4)
        end = f"made {self.made} {flag:#x}" if self.made is not None else "none"
        return self.trace + [end]


def case(rng):
    rx, ry = rng.randrange(0, 400, 5), rng.randrange(0, 400, 5)
    width, height = rng.choice([5, 10, 15, 20, 40]), rng.choice([5, 10, 15, 20, 40])
    mx, my = rx + rng.randrange(-25, width + 5), ry + rng.randrange(-25, height + 5)
    fmask = rng.choice([0, (1 << 21) - 1, rng.getrandbits(21)])
    nmask = rng.choice([0, (1 << 20) - 1, rng.getrandbits(20)])
    make_ok = rng.choice([-1, 0, 1, rng.randrange(2, 23)])
    return (rx, ry, width, height), (mx, my), fmask, (rng.randrange(-2, 3), rng.randrange(-2, 3)), nmask, make_ok


def main(count, seed):
    tries, rng = Tries(), random.Random(seed)
    for number in range(count):
        rect, marker, fmask, delta, nmask, make_ok = case(rng)
        print(f"case {number} room {' '.join(map(str, rect))} marker {marker[0]} {marker[1]} "
              f"free {fmask:#x} {delta[0]} {delta[1]} near {nmask:#x} make {make_ok}")
        for line in tries.run(rect, marker, fmask, delta, nmask, make_ok):
            print(line)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(int(sys.argv[1]), int(sys.argv[2]))
