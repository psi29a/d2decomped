# SPDX-License-Identifier: GPL-3.0-or-later
"""When a frozen monster thinks again, by game.exe's own code.

    uv run python freeze_resume.py gen N SEED > cases.tsv
    uv run python freeze_resume.py game cases.tsv > game.tsv
    ../../build/tests/test_freeze_resume --freeze-resume cases.tsv > d2d.tsv
    uv run python freeze_resume.py compare cases.tsv game.tsv d2d.tsv

Run for real: FUN_0057b230 (the freeze: state 1 to frame + length, its
callback 0x57b170, timer 0xc at the end, the pending AI events
removed and one filed at frame + length + 1), FUN_005a7f80 (a monster's
timer event: types 0, 1, 2, 6, 7, 9 and 10 are dropped while state 1 is
on), its type 12 handler 0x5a7ef0 -> FUN_00627460 (the lists whose end
is due expire) and the callback 0x57b170 (state 1 off; a monster's AI
events removed and event 2 filed at frame + MonStats aidel, +0x4f plus the
difficulty when game +0x6a or +0x74 is set, 15 when 0), with FUN_0057af30
(coldeffect), FUN_0063e9f0, FUN_005a0180 and FUN_0063ee90.

Hooked (not the claim): the event queue (FUN_005417d0 files, FUN_00540e60
removes by type), the state bits (FUN_00639db0 / FUN_00639df0), the state
lists' allocation, lookup and end (FUN_006251f0, FUN_006256b0,
FUN_00625330 / FUN_00625310, FUN_00626e10; laid out as FUN_00627460 reads
them), FUN_00626c00 (a list's expiry: unlinked, its callback run as
FUN_006269f0 does: ECX owner, EDX state, the list pushed), the chill
FUN_0057af80, the MonStats row (MonStats.txt's aidel / coldeffect columns
for the case's rows) and the AI think (0x5b1740 through 0x6e2490[2]: its
frame is the output; it waits aidel and thinks again).

A case: id difficulty row kind length pending prior second_at second_length.
row names the MonStats row whose aidel the monster carries (the d2d side
puts it on a zombie1); kind 0 plain, 1 champion, 2 unique, 3 superunique,
4 minion, 5 MonStats boss column; length / second_length are the frozen
frames (the raw length is length x MonsterFreezeDivisor); pending is the
frame of its next think before the freeze; prior an earlier freeze's
frames left (0: none); second_at the frame of a second freeze (0: none).
Prints `id first second`: frames from the freeze to the first think, and
to the first from second_at on.
"""
import os
import struct
import sys

import emu as emu_module

FRAME = 2000
HORIZON = 1200
ZOMBIE = 2                       # MonStats row of zombie1: its coldeffect
# monster data +0x16 per d2d Boss kind (FUN_005a48c0 champions | 0xd,
# uniques | 8, superuniques | 0xa, minions | 0x10); kind 5 is the boss byte.
KIND_FLAGS = (0, 0xD, 0x8, 0xA, 0x10, 0)
KINDS = ("plain", "champion", "unique", "superunique", "minion", "boss column")
ROWS = ("zombie1", "skeleton1", "fallen1", "andariel", "bloodraven", "quillrat6", "sandraider6", "megademon1",
        "diablo", "mephisto", "izual", "baalclone", "valkyrie", "roguehire", "chicken", "chaoshorde1")
AI_THINK, LIST_EXPIRE = 0x5B1740, 0x626C00


def gen(n, seed):
    state = seed & 0xFFFFFFFF

    def draw(k):
        nonlocal state
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        return (state >> 8) % k

    rows = []
    for diff in range(3):
        for row in ROWS:
            for kind in range(6):
                for length in (1, 2, 13, 50, 250):
                    for pending in (1, 14, 60):
                        rows.append((diff, row, kind, length, pending, 0, 0, 0))
                for prior in (1, 30, 300):
                    rows.append((diff, row, kind, 30, 5, prior, 0, 0))
                for second_at, second_length in ((10, 5), (10, 40), (30, 1), (31, 20), (35, 20), (44, 3), (45, 3), (46, 3), (60, 25)):
                    rows.append((diff, row, kind, 30, 5, 0, second_at, second_length))
    while len(rows) < n + 3 * len(ROWS) * 6 * 27:
        length = draw(250) + 1
        second_at = draw(length + 60) + 1 if draw(3) == 0 else 0
        rows.append((draw(3), ROWS[draw(len(ROWS))], draw(6), length, draw(80) + 1, draw(300) + 1 if draw(4) == 0 else 0,
                     second_at, draw(120) + 1 if second_at else 0))
    for i, r in enumerate(rows):
        print(i, *r, sep="\t")


def mpq_table(e, name):
    text = e.mpq_file(f"data\\global\\excel\\{name}.txt").decode("latin-1").splitlines()
    head = text[0].split("\t")
    return [dict(zip(head, line.split("\t"), strict=False)) for line in text[1:]]


def num(text):
    try:
        return int(text)
    except ValueError:
        return 0


class Game:
    def __init__(self):
        e = self.e = emu_module.Emu()
        self.divisors = [num(r["MonsterFreezeDivisor"]) for r in mpq_table(e, "DifficultyLevels")[:3]]
        monstats = {r["Id"]: r for r in mpq_table(e, "MonStats") if r.get("Id")}
        self.aidel = {row: [num(monstats[row][c]) for c in ("aidel", "aidel(N)", "aidel(H)")] for row in ROWS}
        self.cold = [num(monstats["zombie1"][c]) for c in ("coldeffect", "coldeffect(N)", "coldeffect(H)")]
        self.boss_mask = e.r32(0x6CE280) & 0xFF
        self.game = e.alloc(0x200)
        self.attacker = e.alloc(0x200)
        self.unit = e.alloc(0x200)
        self.mdata = e.alloc(0x100)
        self.statlist = e.alloc(0x80)
        self.diffrec = e.alloc(0x100)
        self.row = e.alloc(0x1A8)
        tables = e.alloc(0x1000)
        e.w32(tables + 0xA78, self.row - 0x1A8 * ZOMBIE)
        e.w32(tables + 0xA80, 0x300)
        e.w32(0x744304, tables)
        e.w32(self.attacker, 0)
        e.w32(self.unit, 1)
        e.w32(self.unit + 4, ZOMBIE)
        e.w32(self.unit + 0x14, self.mdata)
        e.w32(self.unit + 0x5C, self.statlist)
        e.w32(self.statlist + 0x10, 0x80000000)
        e.w32(self.mdata, self.row)
        e.mu.mem_write(self.game + 0x6A, bytes([3]))      # a single player game (net-join.md: 3)
        self.queue = []                    # [frame, seq, type]
        self.states = set()
        self.lists = {}                    # state -> list address
        self.thinks = []
        self.expired = []
        self._hooks()

    def frame(self):
        return self.e.r32(self.game + 0xA8)

    def _hooks(self):
        e = self.e
        seq = iter(range(1 << 62))

        def file_event(h):                 # FUN_005417d0(ECX game, EDX unit; type, frame, 0, 0)
            self.queue.append([h.arg(1), next(seq), h.arg(0)])
            return 0

        def remove_events(h):              # FUN_00540e60(ECX game, EDX unit; type, 0)
            assert h.arg(1) == 0
            self.queue = [ev for ev in self.queue if ev[2] != h.arg(0)]
            return 0

        def set_state(h):                  # FUN_00639db0(unit, state, on)
            (self.states.add if h.arg(2) else self.states.discard)(h.arg(1))
            return 0

        def new_list(h):                   # FUN_006251f0(pool, 2, end, owner type, owner id)
            a = h.alloc(0x60)
            e.w32(a + 0x10, h.arg(1))
            e.w32(a + 0x18, h.arg(2))
            return a

        def attach(h):                     # FUN_00626e10(unit, list, 1): the unit's timed lists, +0x3c / +0x2c
            unit, a = h.arg(0), h.arg(1)
            e.w32(a + 4, unit)
            e.w32(a + 0x2C, e.r32(self.statlist + 0x3C))
            e.w32(self.statlist + 0x3C, a)
            self.lists[e.r32(a + 0x14)] = a
            return 0

        def unlink(a):
            prev, at = None, e.r32(self.statlist + 0x3C)
            while at and at != a:
                prev, at = at, e.r32(at + 0x2C)
            if prev:
                e.w32(prev + 0x2C, e.r32(a + 0x2C))
            elif at:
                e.w32(self.statlist + 0x3C, e.r32(a + 0x2C))
            self.lists = {s: v for s, v in self.lists.items() if v != a}

        def expire(h):                     # FUN_00626c00(ECX list): unlinked; FUN_006269f0's callback after the walk
            import unicorn.x86_const as x86
            a = h.mu.reg_read(x86.UC_X86_REG_ECX)
            unlink(a)
            self.expired.append(a)
            return 0

        def think(h):                      # 0x5b1740: the AI turn
            self.thinks.append(self.frame())
            self.queue.append([self.frame() + self.wait, next(seq), 2])
            return 0

        for addr, n in ((0x5541B0, 0), (0x553570, 1), (0x57AF80, 2), (0x64C040, 1), (0x623F50, 3)):
            e.hook(addr, lambda h: 0, n)
        for addr, fn, n in (
            (0x5417D0, file_event, 4), (0x540E60, remove_events, 2), (0x639DB0, set_state, 3),
            (0x639DF0, lambda h: int(h.arg(1) in self.states), 2), (0x554010, lambda h: self.game, 0),
            (0x611D30, lambda h: self.diffrec, 1), (0x451F80, lambda h: self.row, 0),
            (0x6256B0, lambda h: self.lists.get(h.arg(1), 0), 2), (0x6251F0, new_list, 5),
            (0x6252D0, lambda h: e.w32(h.arg(0) + 0x14, h.arg(1)), 2), (0x626E10, attach, 3),
            (0x625330, lambda h: e.r32(h.arg(0) + 0x18), 1), (0x625310, lambda h: e.w32(h.arg(0) + 0x18, h.arg(1)), 2),
            (LIST_EXPIRE, expire, 0), (AI_THINK, think, 2),
        ):
            e.hook(addr, fn, n)

    def freeze(self, length):
        self.e.call(0x57B230, self.attacker, length * self.divisor, regs={"eax": self.unit})

    def run(self, diff, row, kind, length, pending, prior, second_at, second_length):
        e = self.e
        e.w32(self.game + 0xA8, FRAME)
        e.mu.mem_write(self.game + 0x6D, bytes([diff]))
        self.divisor = self.divisors[diff]
        e.w32(self.diffrec + 0x14, self.divisor)
        e.mu.mem_write(self.row, b"\0" * 0x1A8)
        e.mu.mem_write(self.row + 0xC, bytes([self.boss_mask if kind == 5 else 0]))
        e.mu.mem_write(self.row + 0x4F, bytes(self.aidel[row]))
        e.mu.mem_write(self.row + 0x168, struct.pack("<3b", *self.cold))
        e.mu.mem_write(self.mdata + 0x16, struct.pack("<H", KIND_FLAGS[kind]))
        self.wait = self.aidel[row][diff] or 15
        self.queue, self.states, self.lists, self.thinks, self.expired = [[FRAME + pending, -1, 2]], set(), {}, [], []
        e.w32(self.statlist + 0x3C, 0)
        if prior:                          # an earlier freeze as FUN_0057b230 left it
            e.w32(self.game + 0xA8, FRAME - 1)
            self.freeze(prior + 1)
            self.queue = [[FRAME + pending, -1, 2]] + [ev for ev in self.queue if ev[2] != 2]
            e.w32(self.game + 0xA8, FRAME)
        self.freeze(length)
        for frame in range(FRAME + 1, FRAME + HORIZON + 1):
            e.w32(self.game + 0xA8, frame)
            if frame == FRAME + second_at:
                self.freeze(second_length)
            while due := sorted(ev for ev in self.queue if ev[0] <= frame):
                ev = due[0]
                self.queue.remove(ev)
                e.call(0x5A7F80, ev[2], 0, 0, ecx=self.game, edx=self.unit)
                for a in self.expired:     # FUN_006269f0: ECX the owner, EDX the state, the list pushed
                    e.call(e.r32(a + 0x38), a, ecx=e.r32(a + 4), edx=e.r32(a + 0x14))
                self.expired = []
            if self.thinks and (not second_at or any(t >= FRAME + second_at for t in self.thinks)):
                break
        first = next((t - FRAME for t in self.thinks), None)
        second = next((t - FRAME for t in self.thinks if t >= FRAME + second_at), None) if second_at else None
        return ("-" if first is None else first), ("-" if second is None else second)


def cases(path):
    for line in open(path):
        f = line.split()
        if f:
            yield [int(f[0]), int(f[1]), f[2], *map(int, f[3:])]


def game(path):
    g = Game()
    for cid, *case in cases(path):
        print(cid, *g.run(*case))


def compare(case_path, game_path, d2d_path):
    rows = {r[0]: r for r in cases(case_path)}
    g = {f[0]: f[1:3] for f in (line.split() for line in open(game_path)) if f}
    d = {f[0]: f[1:3] for f in (line.split() for line in open(d2d_path)) if f and f[0].isdigit()}
    bad = [k for k in rows if g.get(str(k)) != d.get(str(k))]
    print(f"cases {len(rows)} game {len(g)} d2d {len(d)} mismatches {len(bad)}")
    groups = {}
    for k, r in rows.items():
        _cid, diff, row, kind, *_ = r
        frozen = KIND_FLAGS[kind] & 8 == 0 and kind != 5
        aidel = f"frozen, aidel {G_AIDEL[row][diff] or 15}{' (0 -> 15)' if not G_AIDEL[row][diff] else ''}"
        for key in (f"kind {KINDS[kind]}", *((aidel,) if frozen else ()),
                    "frozen in game.exe" if frozen else "not frozen in game.exe (chilled: FUN_0057b230's special branch)"):
            t = groups.setdefault(key, [0, 0])
            t[0] += 1
            t[1] += k in bad
    for key, (n, b) in sorted(groups.items()):
        print(f"  {key:60s} {n:7d} cases {b:7d} differ")
    for k in bad[:5]:
        print("   e.g.", *rows[k], "game", g.get(str(k)), "d2d", d.get(str(k)))
    return 1 if bad else 0


G_AIDEL = {}

if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "gen" and len(sys.argv) == 4:
        gen(int(sys.argv[2]), int(sys.argv[3], 0))
    elif cmd == "game" and len(sys.argv) == 3:
        game(sys.argv[2])
    elif cmd == "compare" and len(sys.argv) == 5:
        G_AIDEL.update(Game().aidel)
        sys.exit(compare(*sys.argv[2:5]))
    else:
        sys.exit(__doc__)
