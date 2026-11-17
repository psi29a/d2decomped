# SPDX-License-Identifier: GPL-3.0-or-later
"""Larzuk's marker as the oracle: FUN_00587840 (InitFn 71, object 543, quest record 0x1f).

    uv run python larzuk_init.py                      # game.exe against the model; ok
    uv run python larzuk_init.py --mutation-check     # the model broken six ways, each caught
    uv run python larzuk_init.py --cases > ../../tests/larzuk_marker_cases.inc
    ../../build/tests/test_larzuk_marker              # d2d's rules::larzuk_marker on those

FUN_00587840 runs for real. Hooked, driven by the case: the record lookup
FUN_00543640, the free-spot search FUN_00545340 (size 2, mask 0x100, 100
rings; it moves the spot and hands out the room holding it, or 0), the maker
FUN_005b2f20, and the two helpers run for the made unit (FUN_0058f000,
FUN_00666120). A case line, then the calls game.exe made, then the end:

    case N made M free F room R make K link L
    free ROOM X Y              the spot search: F 0 fails, 1 moves the spot by (+3, +1)
                               in the room it was called with, 2 hands out room 1
    make ROOM X Y RADIUS       the maker: made when K is 1 (the unit's flags |= 0x3000000)
    end MADE FLAGS             the record's made byte +0x15 and the unit's flags, or `end none`
                               when the maker was never reached

M is the record's made byte before the call. The maker is asked once, at radius 5:
the spot itself (radius -1) isn't tried first. L: whether the record's list
pointer is set, which runs FUN_0058f000 and FUN_00666120 once (flag byte +0x17).
"""
import argparse
import itertools
import sys

import emu

GAME = 0x1000
MAKER_CLASS, MAKER_MODE = 0x1FF, 1


def signed(value):
    """A dword as a signed int."""
    return value - (1 << 32) if value >= 0x80000000 else value


class Larzuk:
    """FUN_00587840 under emu, its callees hooked as the module doc says."""

    def __init__(self):
        """Set up the hooks and the structs one call reads."""
        self.e = e = emu.Emu()
        self.rooms = [e.alloc(0x100) for _ in range(2)]
        self.record = e.alloc(0x40)
        self.data = e.alloc(0x40)
        self.link = e.alloc(0x40)
        self.unit = e.alloc(0x200)
        self.marker = e.alloc(0x40)
        e.hook(0x543640, self.lookup, 0)
        e.hook(0x545340, self.free, 5)
        e.hook(0x5B2F20, self.make, 6)
        e.hook(0x58F000, self.owner, 1)
        e.hook(0x666120, self.listed, 0)

    def room_no(self, room):
        """Which of the two rooms a pointer is."""
        return self.rooms.index(room)

    def lookup(self, h):
        """FUN_00543640(game, id): the quest record, whose +0x18 is its data."""
        assert h.mu.reg_read(emu.UC_X86_REG_EDX) == 0x1F
        return self.record

    def free(self, h):
        """FUN_00545340: the spot moved (+3, +1) in the room asked, or in room 1, or none."""
        room = h.mu.reg_read(emu.UC_X86_REG_ECX)
        spot = h.mu.reg_read(emu.UC_X86_REG_EDX)
        x, y, out = h.s32(spot), h.s32(spot + 4), h.arg(2)
        assert (h.arg(0), h.arg(1), h.arg(3), h.arg(4)) == (2, 0x100, 0x10, 100)
        self.trace.append(f"free {self.room_no(room)} {x} {y}")
        if self.free_mode == 0:
            h.w32(out, 0)
            return 0
        h.w32(spot, x + 3)
        h.w32(spot + 4, y + 1)
        h.w32(out, self.rooms[1] if self.free_mode == 2 else room)
        return 0

    def make(self, h):
        """FUN_005b2f20(game, room, x, y, class, mode, radius, flags): made when asked to."""
        room = h.mu.reg_read(emu.UC_X86_REG_EDX)
        x, y, cls, mode, radius, flags = h.s32(h.mu.reg_read(emu.UC_X86_REG_ESP) + 4), h.s32(h.mu.reg_read(emu.UC_X86_REG_ESP) + 8), h.arg(2), h.arg(3), signed(h.arg(4)), h.arg(5)
        assert (cls, mode, flags) == (MAKER_CLASS, MAKER_MODE, 0)
        self.trace.append(f"make {self.room_no(room)} {x} {y} {radius}")
        return self.unit if self.make_ok else 0

    def owner(self, h):
        """FUN_0058f000(unit): the made unit's id, noted."""
        self.helpers.append("owner")
        return 0x77

    def listed(self, h):
        """FUN_00666120: run for the record's list pointer."""
        self.helpers.append("listed")
        return 0

    def run(self, made, free_mode, make_ok, link):
        """One call; the trace and the end line."""
        e = self.e
        for room in self.rooms:
            e.w32(room + 0x4C, 0)
        e.w32(self.record + 0x18, self.data)
        e.w32(self.data, self.link if link else 0)
        e.w32(self.link + 4, 1)
        e.mu.mem_write(self.data + 0x10, b"\0" * 12)
        e.mu.mem_write(self.data + 0x15, bytes([made]))
        e.mu.mem_write(self.data + 0x17, b"\0")
        e.w32(self.unit + 0xC, 0x5555)
        e.w32(self.unit + 0xC4, 0)
        e.w32(self.marker, GAME)
        e.w32(self.marker + 8, self.rooms[0])
        e.w32(self.marker + 0x14, 184)
        e.w32(self.marker + 0x18, 104)
        self.free_mode, self.make_ok, self.trace, self.helpers = free_mode, make_ok, [], []
        e.call(0x587840, ecx=self.marker)
        made_byte = e.read(self.data + 0x15, 1)[0]
        flags = e.r32(self.unit + 0xC4)
        reached = any(line.startswith("make") for line in self.trace)
        end = f"end {made_byte} {flags:#x}" if reached and made_byte == 1 and make_ok else "end none"
        extra = [f"helpers {' '.join(self.helpers) or '-'}"] if reached and make_ok else []
        return self.trace + [end] + extra


def model(made, free_mode, make_ok, link, mutation=None):
    """What FUN_00587840 is claimed to do, which d2d's rules::larzuk_marker and objgroups.cpp do."""
    trace = []
    if made and mutation != "ignores-made":
        return trace + ["end none"]
    trace.append("free 0 184 104")
    if free_mode == 0 and mutation != "makes-without-spot":
        return trace + ["end none"]
    x, y = (187, 105) if free_mode else (184, 104)
    room = 1 if free_mode == 2 and mutation != "marker-room" else 0
    if mutation == "radius-minus-one-first":
        trace.append(f"make {room} {x} {y} -1")
        if make_ok:
            return trace + ["end 1 0x3000000", "helpers owner listed" if link else "helpers -"]
    trace.append(f"make {room} {x} {y} {10 if mutation == 'radius-ten' else 5}")
    if not make_ok:
        return trace + ["end none"]
    flags = "0x0" if mutation == "no-flags" else "0x3000000"
    return trace + [f"end 1 {flags}", "helpers owner listed" if link else "helpers -"]


def cases():
    """Every combination of the case axes."""
    return list(itertools.product((0, 1), (0, 1, 2), (0, 1), (0, 1)))


def check(oracle, mutation=None):
    """Compare game.exe with the model over every case; the mismatches."""
    bad = []
    for made, free_mode, make_ok, link in cases():
        got = oracle.run(made, free_mode, make_ok, link)
        want = model(made, free_mode, make_ok, link, mutation)
        if got != want:
            bad.append(((made, free_mode, make_ok, link), got, want))
    return bad


def main():
    """Run the check, the mutation check, or print the case lines."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    parser.add_argument("--cases", action="store_true")
    args = parser.parse_args()
    oracle = Larzuk()
    if args.cases:
        for number, (made, free_mode, make_ok, link) in enumerate(cases()):
            print(f'    "case {number} made {made} free {free_mode} make {make_ok} link {link}",')
            for line in oracle.run(made, free_mode, make_ok, link):
                if not line.startswith("helpers"):
                    print(f'    "{line}",')
        return
    bad = check(oracle)
    if bad:
        for case, got, want in bad[:5]:
            print(f"case {case}: game {got} != model {want}")
        sys.exit(f"{len(bad)} of {len(cases())} cases differ")
    print(f"ok: {len(cases())} FUN_00587840 cases (made byte, free spot none / same room / next room, maker, list pointer)")
    if args.mutation_check:
        names = ("ignores-made", "makes-without-spot", "marker-room", "radius-minus-one-first", "radius-ten", "no-flags")
        for name in names:
            if not check(oracle, name):
                sys.exit(f"mutation {name} was not caught")
            print(f"mutation {name}: failed as required")
        print(f"mutation check ok ({len(names)} ways)")


if __name__ == "__main__":
    main()
