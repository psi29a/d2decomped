# SPDX-License-Identifier: GPL-3.0-or-later
"""A monster's curse slot in game.exe as the oracle: sequences of curse puts through the timed-state put.

    uv run python curse_put.py gen 100000 1 > cases.txt     # `seq skill level length frame`, one put a line
    uv run python curse_put.py game < cases.txt > game.txt   # each put and `accepted skill level left curses`
    ../../build/tests/test_curse_put --curse-put < cases.txt | grep -E '^[0-9]+( -?[0-9]+){9}$' > d2d.txt
    uv run python curse_put.py compare game.txt d2d.txt      # `N puts, M differ`; exit 1 on a difference or a bad input

A sequence (`seq`) is one monster, puts in frame order: a skill that puts a curse state (States.txt curse) on
it (Skills.txt srvdofunc 30, 59 Attract, 61 Confuse, 130 Howl's Terror, and 71 Taunt, 68 Battle Cry, 47 Cloak
of Shadows, 131 Overseer Whip's Bloodlust) at `level` for `length` frames at game frame `frame`. Frames are
even and lengths odd, so no put lands on the frame a curse ends (the end event's order in that frame is left
out). After each put: whether it was taken, then the curse the monster holds: its skill, level and the frames
left (-1 -1 -1: none), and how many curse states it holds.
FUN_0056e970, the timed-state put, runs for real on its request record (+4 the target, +8 skill, +0xc level,
+0x10 length, +0x1c the state: Skills.txt auratargetstate), with FUN_0063a420 (the States.txt curse bit) and
FUN_00483360 on the loaded tables: a curse state is cut by the target's curse resistance (stat 0x6d, 0 here)
and refused when the target has state 0x39 (Attract, 0x56ea40..0x56ea4a); else the target's curse node
(FUN_00625760, kind 0x20) of the same state, skill and level runs longer, a higher-level one stays (the put
refused), any other comes off and the new one goes on.
Hooked (inputs and side effects, not the claim): FUN_00554010 (the unit's game), the stat getter FUN_00625480
(0), FUN_00451fe0 (a MonStats2 row that lets states on), the unit's state-node list as a model (FUN_006251f0 /
006252d0 / 006260d0 / 006260f0 / 00626e10 / 00625310 / 006256b0 / 00625760 / 006252f0 / 006260e0 / 00626100 /
006277e0 / 00626cd0; a node is gone from its end frame on: the end event, FUN_005417d0, is recorded only), the
has-state test FUN_00639df0 (a node of that state), FUN_00639db0, FUN_0063a6b0, FUN_0064c040, FUN_00627150 and
FUN_00625ce0.
"""
import random
import sys

USAGE = "usage: curse_put.py gen N SEED | game < CASES | compare GAME D2D"
CURSE_DOS = {"30", "47", "59", "61", "68", "71", "131"}
HOWL = 130
ATTRACT_DO = "59"


def tables(e):
    """{skill id: (state id, Attract?)} for the skills above, and the curse states' ids."""
    def rows(name):
        text = e.mpq_file(f"data\\global\\excel\\{name}.txt").decode("latin-1").splitlines()
        head = text[0].split("\t")
        return [dict(zip(head, line.split("\t"))) for line in text[1:]]
    state_rows = [r for r in rows("States") if r.get("state")]
    states = {row["state"]: index for index, row in enumerate(state_rows)}
    curses = {index for index, row in enumerate(state_rows) if row.get("curse") == "1"}
    skills = {}
    for row in rows("Skills"):
        if not row.get("Id", "").isdigit():
            continue
        skill = int(row["Id"])
        if (row.get("srvdofunc") in CURSE_DOS or skill == HOWL) and row.get("auratargetstate") in states:
            if states[row["auratargetstate"]] in curses:
                skills[skill] = (states[row["auratargetstate"]], row.get("srvdofunc") == ATTRACT_DO)
    return skills, curses


def boot():
    import drlg
    return drlg.boot()


class Rig:
    def __init__(self, e, curses):
        self.e = e
        self.curses = curses
        self.frame = 0
        self.nodes = []
        self.info = {}
        self.game, self.unit, self.caster, self.request = e.alloc(0x200), e.alloc(0x200), e.alloc(0x200), e.alloc(0x40)
        self.mon2 = e.alloc(0x134)
        e.w32(self.unit, 1)                                            # a monster
        e.w32(self.unit + 4, 0)                                        # MonStats row 0 (its +0xd gate bit clear on the loaded table)
        e.mu.mem_write(self.mon2 + 5, bytes([e.r32(0x6ce26c) & 0xff])) # the MonStats2 +5 bit the put asks for
        hooks = (
            (0x554010, lambda _e: self.game, 0), (0x625480, lambda _e: 0, 3), (0x451fe0, lambda _e: self.mon2, 0),
            (0x639df0, self.has_state, 2), (0x639db0, lambda _e: 0, 3), (0x63a6b0, lambda _e: 0, 1),
            (0x625760, self.find_curse, 2), (0x6256b0, self.find_state, 2), (0x6251f0, self.new_node, 5),
            (0x6252d0, self.setter("state"), 2), (0x6260d0, self.setter("skill"), 2), (0x6260f0, self.setter("level"), 2),
            (0x626e10, self.attach, 3), (0x625310, self.setter("end"), 2), (0x6252f0, self.getter("state"), 1),
            (0x6260e0, self.getter("skill"), 1), (0x626100, self.getter("level"), 1), (0x6277e0, self.remove, 2),
            (0x626cd0, lambda _e: 0, 1), (0x64c040, lambda _e: 0, 1), (0x5417d0, lambda _e: 0, 4),
            (0x627150, lambda _e: 0, 4), (0x625ce0, lambda _e: 0, 2),
        )
        for addr, fn, nstack in hooks:
            e.hook(addr, fn, nstack)

    def live(self):
        """The end event (hooked): a node is gone from its end frame on."""
        self.nodes = [n for n in self.nodes if self.info[n]["end"] == 0 or self.info[n]["end"] > self.frame]

    def new_node(self, e):
        node = e.alloc(0x40)
        self.info[node] = {"state": None, "skill": None, "level": None, "end": e.arg(2), "flags": e.arg(1)}
        return node

    def attach(self, e):
        self.nodes.append(e.arg(1))
        return 0

    def remove(self, e):
        if e.arg(1) in self.nodes:
            self.nodes.remove(e.arg(1))
        return 0

    def find_curse(self, e):
        return next((n for n in self.nodes if self.info[n]["flags"] & e.arg(1)), 0)

    def find_state(self, e):
        return next((n for n in self.nodes if self.info[n]["state"] == e.arg(1)), 0)

    def has_state(self, e):
        return int(any(self.info[n]["state"] == e.arg(1) for n in self.nodes))

    def setter(self, key):
        def put(e):
            self.info[e.arg(0)][key] = e.arg(1)
            return 0
        return put

    def getter(self, key):
        return lambda e: self.info[e.arg(0)][key] & 0xffffffff

    def reset(self):
        self.nodes, self.info = [], {}

    def put(self, skill, state, level, length, frame):
        """FUN_0056e970 at `frame`: (taken, skill, level, frames left) of the curse after it, and its curse states."""
        e = self.e
        self.frame = frame
        e.w32(self.game + 0xa8, frame)
        self.live()
        r = self.request
        e.mu.mem_write(r, b"\0" * 0x40)
        e.w32(r, self.caster)
        e.w32(r + 4, self.unit)
        e.w32(r + 8, skill)
        e.w32(r + 0xc, level)
        e.w32(r + 0x10, length)
        e.w32(r + 0x14, 0xffffffff)
        e.w32(r + 0x1c, state)
        taken = int(e.call(0x56e970, ecx=r) != 0)
        curse = next((n for n in self.nodes if self.info[n]["flags"] & 0x20), None)
        held = sum(self.info[n]["state"] in self.curses for n in self.nodes)
        if curse is None:
            return taken, -1, -1, -1, held
        node = self.info[curse]
        return taken, node["skill"], node["level"], node["end"] - frame, held


def gen(count, seed):
    e = boot()
    skills, _ = tables(e)
    ids = sorted(skills)
    attracts = [s for s in ids if skills[s][1]]
    rng = random.Random(seed)
    seq, made = 0, 0
    while made < count:
        frame = rng.choice((0, 2 * rng.randint(0, 50)))
        for _ in range(min(rng.randint(1, 12), count - made)):
            skill = rng.choice(attracts) if rng.random() < 0.15 else rng.choice(ids)
            level = rng.choice((1, 1, 2, 3, rng.randint(1, 4), rng.randint(1, 60)))
            length = 2 * rng.choice((rng.randint(12, 200), rng.randint(12, 3000))) + 1
            print(seq, skill, level, length, frame)
            made += 1
            frame += 2 * rng.choice((0, rng.randint(0, 60), rng.randint(0, 600), rng.randint(0, 3000)))
        seq += 1


def game():
    e = boot()
    skills, curses = tables(e)
    rig = Rig(e, curses)
    last = None
    for line in sys.stdin:
        field = line.split()
        if not field:
            continue
        seq, skill, level, length, frame = (int(f) for f in field)
        if seq != last:
            rig.reset()
            last = seq
        taken, held, held_level, left, curses_held = rig.put(skill, skills[skill][0], level, length, frame)
        print(seq, skill, level, length, frame, taken, held, held_level, left, curses_held)


def rows(path):
    out = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            field = line.split()
            if not field:
                continue
            if len(field) != 10 or not all(f.lstrip("-").isdigit() for f in field):
                sys.exit(f"malformed line in {path}: {line.strip()!r}")
            out.append(tuple(int(f) for f in field))
    return out


def compare(game_path, d2d_path):
    game_rows, d2d_rows = rows(game_path), rows(d2d_path)
    if not game_rows or len(game_rows) != len(d2d_rows):
        sys.exit(f"empty or unequal: {len(game_rows)} game, {len(d2d_rows)} d2d (an empty file is not a pass)")
    differ = 0
    for g, d in zip(game_rows, d2d_rows, strict=True):
        if g[:5] != d[:5]:
            sys.exit(f"puts out of step: game {g[:5]}, d2d {d[:5]}")
        if g[5:] != d[5:]:
            differ += 1
            if differ <= 20:
                print(f"DIFF seq {g[0]} skill {g[1]} level {g[2]} length {g[3]} frame {g[4]}: "
                      f"game taken {g[5]} holds {g[6]} level {g[7]} left {g[8]} curses {g[9]}, "
                      f"d2d taken {d[5]} holds {d[6]} level {d[7]} left {d[8]} curses {d[9]}")
    print(f"{len(game_rows)} puts, {differ} differ")
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
