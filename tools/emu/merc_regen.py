# SPDX-License-Identifier: GPL-3.0-or-later
"""A merc's own life regeneration in game.exe as the oracle: its stat 0x4a and its unit event 3, frame by frame.

    uv run python merc_regen.py gen 100000 1 > cases.txt     # `hid level gear hpregen town kind value frames`
    uv run python merc_regen.py game < cases.txt > game.txt   # each case and `regen life_after filed dead`
    ../../build/tests/test_merc_regen --merc-regen < cases.txt | grep -E '^[0-9]+( -?[0-9]+){10}$' > d2d.txt
    uv run python merc_regen.py compare game.txt d2d.txt      # `N cases, M differ`; exit 1 on a difference or a bad input

A case: Hireling.txt Id `hid` (an expansion row) at merc level `level`, `gear` more max life (whole points)
than its level-up life and `hpregen` more stat 0x4a (its items' Replenish Life, 256ths a frame; may be
negative or cancel the level-up's exactly), in a town room or not (`town`), its life (256ths), then `frames`
frames of its event 3, which the case starts filed (the merc has been hurt). Both run for real:
- FUN_00572840, the merc's level-up (its callers: the hire, the save's merc, its experience): stat 0x4a :=
  max(0, the base stat 7 it has just set (256ths) / 2000), 0x572ac2..0x572af2;
- FUN_005a6920, a monster's event 3 (a merc is unit type 1: FUN_005417d0 files it in list 1, FUN_00541060
  -> FUN_005a7f80 runs table 0x6e2490[3]): the total stat 0x4a; it files itself for the next frame; a total
  of 0 removes it; a negative total under one point of life does nothing in a town room (FUN_00620bb0,
  FUN_0061ab00); else life (stat 6) += it, over the max life (FUN_00625d10) the max and the event removed,
  under 1 life 0 and, out of mode 0 / 12, the kill (FUN_0057ccb0).
The loop stops once the event is removed or the merc is killed. `filed` is 1 while the event is still
filed after the run, `dead` 1 if the kill ran.
Hooked (inputs and side effects, not the claim): the stat getters FUN_00625480 / FUN_006253b0 (the level,
stat 6, the total 0x4a = the level-up's + `hpregen`, 0x160, the base stat 7 the level-up set), the max
life FUN_00625d10 ((level-up life + gear) << 8), the setter FUN_00627260 (recorded), FUN_00574bd0 (the
Hireling.txt record: its Id), FUN_005de330 (the level-up speech), FUN_0056deb0 (skill levels), the state
lookups FUN_006256b0 / FUN_00639df0 (no state: none of 0x34, 0x36, 2, 0x3e), FUN_0063a750 (no state of the
States group +0x14c, which would take the level-up's part off), the unit's room FUN_00620bb0 and town
test FUN_0061ab00 (`town`), the event list FUN_005417d0 / FUN_00540e60 (recorded), the life-percent byte
FUN_005a5650 and message FUN_00571a10, and the kill FUN_0057ccb0 / FUN_005c0c30 (recorded).
"""
import random
import struct
import sys

USAGE = "usage: merc_regen.py gen N SEED | game < CASES | compare GAME D2D"
EXPANSION_VERSION = 100
NU = 1                                   # the merc's mode in the cases (FUN_005a6920 reads none but the kill's 0 / 12)


def s32(value):
    return struct.unpack("<i", struct.pack("<I", value & 0xffffffff))[0]


def boot():
    import drlg
    return drlg.boot()


def hireling_ids(e):
    """The expansion rows' Ids, as Hireling.txt holds them (0x96bdd0, 0x118 bytes a row: version, Id)."""
    base, count = e.r32(0x96bdd0), e.r32(0x96bdd4)
    ids = set()
    for row in range(count):
        version, hid = struct.unpack_from("<Hxxi", e.read(base + row * 0x118, 8), 0)
        if version == EXPANSION_VERSION:
            ids.add(hid)
    return sorted(ids)


class Rig:
    def __init__(self, e):
        self.e = e
        self.sets, self.stats = {}, {}
        self.level = 1
        self.hid = 0
        self.max_life = 0
        self.town = 0
        self.filed = True
        self.dead = False
        self.events = []
        self.mode = "regen"
        self.game, self.player, self.merc, self.room = e.alloc(0x400), e.alloc(0x400), e.alloc(0x400), e.alloc(0x100)
        e.w32(self.game + 0x70, 1)                                     # an expansion game
        e.w32(self.merc, 1)                                            # unit type 1, a monster
        e.hook(0x625480, self.get, 3)
        e.hook(0x6253b0, lambda _e: self.sets.get(7, 0), 3)           # the base stat 7, as the level-up set it
        e.hook(0x625d10, lambda _e: self.max_life, 1)
        e.hook(0x627260, self.put, 4)
        e.hook(0x574bd0, self.record, 0)
        e.hook(0x5de330, lambda _e: 0, 3)
        e.hook(0x56deb0, lambda _e: 0, 2)
        e.hook(0x6256b0, lambda _e: 0, 2)
        e.hook(0x639df0, lambda _e: 0, 2)
        e.hook(0x63a750, lambda _e: 0, 1)
        e.hook(0x620bb0, lambda _e: self.room, 1)
        e.hook(0x61ab00, lambda _e: self.town, 1)
        e.hook(0x5417d0, lambda _e: self.events.append(("file", _e.arg(0), _e.arg(1))) or 0, 4)
        e.hook(0x540e60, lambda _e: self.events.append(("remove", _e.arg(0))) or 0, 2)
        e.hook(0x5a5650, lambda _e: self.stats.get(0x160, 0), 0)
        e.hook(0x571a10, lambda _e: 0, 0)
        e.hook(0x57ccb0, self.kill, 2)
        e.hook(0x5c0c30, lambda _e: 0, 3)

    def get(self, e):
        unit, stat = e.arg(0), e.arg(1)
        if unit != self.merc:
            return 0
        if self.mode == "levelup":
            return self.level - 1 if stat == 0xc else 0
        return self.stats.get(stat, 0) & 0xffffffff

    def put(self, e):
        unit, stat, value = e.arg(0), e.arg(1), e.arg(2)
        if unit == self.merc:
            (self.sets if self.mode == "levelup" else self.stats)[stat] = s32(value)
        return 0

    def record(self, e):
        rec = e.alloc(0x20)
        e.w32(rec + 8, self.hid)
        return rec

    def kill(self, _e):
        self.dead = True
        return 0

    def regen_of(self, hid, level):
        """FUN_00572840 for this merc: the base stat 7 and stat 0x4a it sets."""
        self.mode, self.hid, self.level = "levelup", hid, level
        self.sets.clear()
        self.e.call(0x572840, self.merc, level, ecx=self.game, edx=self.player)
        return self.sets.get(7, 0), self.sets.get(0x4a, 0)

    def run(self, total, life, max_life, town, frames):
        """FUN_005a6920 once a frame, while the event is filed: (life after in 256ths, filed, dead)."""
        self.mode = "regen"
        self.stats = {6: life, 0x4a: total}
        self.max_life, self.town = max_life, town
        self.filed, self.dead = True, False
        self.e.w32(self.merc + 0x10, NU)
        for frame in range(frames):
            if not self.filed or self.dead:
                break
            self.e.w32(self.game + 0xa8, 1000 + frame)
            self.events.clear()
            self.e.call(0x5a6920, 0, 0, ecx=self.game, edx=self.merc)
            filed = [ev for ev in self.events if ev[0] == "file"]
            if filed != [("file", 3, 1001 + frame)]:
                sys.exit(f"event 3 not filed for the next frame: {self.events}")
            if ("remove", 3) in self.events:
                self.filed = False
        return self.stats[6], int(self.filed), int(self.dead)


def gen(count, seed):
    e = boot()
    ids = hireling_ids(e)
    rig, made = Rig(e), {}
    rng = random.Random(seed)
    for _ in range(count):
        hid = rng.choice(ids)
        level = rng.choice((1, 2, 98, rng.randint(1, 98), rng.randint(1, 98)))
        gear = rng.choice((0, 0, rng.randint(1, 40), rng.randint(1, 600)))
        if (hid, level) not in made:
            made[(hid, level)] = rig.regen_of(hid, level)[1]
        regen = made[(hid, level)]
        hpregen = rng.choice((0, 0, 0, rng.randint(1, 40), -rng.randint(1, 40), -regen, -regen - rng.randint(1, 300)))
        town = rng.choice((0, 0, 0, 1))
        frames = rng.choice((0, 1, 2, rng.randint(1, 30), rng.randint(1, 600)))
        # The life: (kind, value): kind 0 absolute 256ths, 1 max less value, 2 max plus value.
        kind = rng.choice((0, 0, 0, 1, 1, 2))
        value = (rng.choice((0x100, 0x101, rng.randint(0x100, 0x200), rng.randint(0x100, 0x40000))) if kind == 0
                 else rng.choice((0, 1, rng.randint(0, 300), rng.randint(0, 0x8000))) if kind == 1
                 else rng.choice((1, rng.randint(1, 0x2000))))
        print(hid, level, gear, hpregen, town, kind, value, frames)


def game():
    e = boot()
    rig = Rig(e)
    made = {}
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        hid, level, gear, hpregen, town, kind, value, frames = (int(f) for f in field)
        if (hid, level) not in made:
            made[(hid, level)] = rig.regen_of(hid, level)
        base, regen = made[(hid, level)]
        if base <= 0 or base % 256:
            sys.exit(f"hireling {hid} level {level}: stat 7 {base} is not whole points")
        max_life = base + (gear << 8)
        life = value if kind == 0 else max(max_life - value, 0x100) if kind == 1 else max_life + value
        after, filed, dead = rig.run(regen + hpregen, life, max_life, town, frames)
        print(hid, level, gear, hpregen, town, life, frames, regen, after, filed, dead)


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            field = line.split()
            if not field:
                continue
            if len(field) != 11 or not all(f.lstrip("-").isdigit() for f in field):
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            out.append(tuple(int(f) for f in field))
    return out


def compare(game_path, d2d_path):
    game_rows, d2d_rows = rows(game_path), rows(d2d_path)
    if not game_rows or len(game_rows) != len(d2d_rows):
        sys.exit(f"empty or unequal: {len(game_rows)} game, {len(d2d_rows)} d2d (an empty file is not a pass)")
    differ = 0
    for g, d in zip(game_rows, d2d_rows, strict=True):
        if g[:7] != d[:7]:
            sys.exit(f"cases out of step: game {g[:7]}, d2d {d[:7]}")
        if g[7:] != d[7:]:
            differ += 1
            if differ <= 20:
                print(f"DIFF hid {g[0]} level {g[1]} gear {g[2]} hpregen {g[3]} town {g[4]} life {g[5]} frames {g[6]}: "
                      f"game regen {g[7]} life {g[8]} filed {g[9]} dead {g[10]}, d2d regen {d[7]} life {d[8]} filed {d[9]} dead {d[10]}")
    print(f"{len(game_rows)} cases, {differ} differ")
    sys.exit(1 if differ else 0)


def main():
    a = sys.argv[1:]
    if not a or (a[0], len(a)) not in {("gen", 3), ("game", 1), ("compare", 3)}:
        sys.exit(USAGE)
    if a[0] == "gen":
        gen(int(a[1]), int(a[2]))
    elif a[0] == "game":
        game()
    else:
        compare(a[1], a[2])


if __name__ == "__main__":
    main()
