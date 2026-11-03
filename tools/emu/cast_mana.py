# SPDX-License-Identifier: GPL-3.0-or-later
"""When game.exe pays a self cast's mana, as the oracle: FUN_005539b0 and FUN_0056bfe0.

    python3 cast_mana.py gen [SEED] > cases.tsv
    python3 cast_mana.py game cases.tsv > game.tsv
    ../../build/tests/test_cast_mana --cast-mana cases.tsv | grep -E '^[0-9]+ ' | diff game.tsv -

A case is `id skill lvl mana fcr cut`: skill 36 / 38 / 44 / 40 (Fire Bolt,
Charged Bolt, Frost Nova, Frozen Armor), its level, the mana stat at the
command (256ths), faster cast rate, and `cut`: -1, or the tick (counted from
the command's tick, 0) after which the cast is cut off by a get-hit. The
answer is `id began paid_tick paid fired_tick`: began 1 when the skill start
(FUN_0056c160) lets the cast begin, the tick the mana left (-1 never), how
much (256ths), the tick the action frame ran (-1 never).

What runs for real (V, `python3 cast_mana.py anim`): the cast rate branch of
FUN_00623f50, the animation scheduler FUN_005539b0 that the player mode start
FUN_0057fe90 calls with the unit's frames (+0x48) and rate (+0x4c) and the
AnimData record's event markers, the mana check FUN_0056c160 and the payment
FUN_0056bfe0. What is read, not run: FUN_00580460 runs the do (FUN_0056fc50,
which pays) on an event of value 1 or 2 unless the unit's missile-made flag
(+0xc4 bit 6) is set, and the skill start pays nothing for a row without a
srvstfunc (FUN_0056f640); a mode replaced before the event leaves the do
unrun. The events filed at a mode start are tick numbers from that start.

Limits: the AnimData record is the first Sorceress SC one (SOSC), so the
action-frame claim is a Sorceress cast's, U for the other classes. The cut
rule is V read (a get-hit start calls FUN_0057fe30 -> FUN_00553990 ->
FUN_00540e60, which removes the queued event), but modelled here, not run.
"""
import random
import struct
import sys

SKILLS = (36, 38, 44, 40)
RECORD = 0x23C
FAKE = 0x30000000
NOW = 1000
FCRS = (0, 9, 20, 30, 48, 75, 105, 125, 200)
POOLS = 6
LEVELS = range(1, 21)


def animdata_records(blob, prefix):
    """The 160-byte AnimData records whose name starts with prefix: (name, frames, speed, markers)."""
    at, found = 0, []
    while at + 4 <= len(blob):
        count = struct.unpack_from("<I", blob, at)[0]
        at += 4
        for _ in range(count):
            raw = blob[at:at + 160]
            at += 160
            if len(raw) < 160:
                break
            name = raw[:8].split(b"\0", 1)[0].decode()
            if name.startswith(prefix):
                frames, speed = struct.unpack_from("<II", raw, 8)
                found.append((name, frames, speed, raw))
    return found


def pay_tick(events, now):
    """Ticks (from the mode start) of the action events: type 0, value 1 or 2 (FUN_00580460)."""
    return [tick - now for kind, tick, value, _ in events if kind == 0 and value in (1, 2)]


def cut_cases(action, cuts=(-3, -2, -1, 1, 2)):
    """The cut ticks of one cast: none, and some before and after the event (never on it)."""
    return [-1] + sorted({action + d for d in cuts if action + d >= 0 and action + d != action})


class Game:
    def __init__(self):
        import emu                              # not at the top: the helpers above run without the emulator
        import skill_tips
        self.emu = emu
        self.e = e = skill_tips.boot()
        tables = e.r32(0x744304)
        self.skills, self.count = e.r32(tables + 0xB98), e.r32(tables + 0xBA0)
        self.unit = e.alloc(0x200)
        self.sched = e.alloc(0x200)
        self.skill = self.lvl = self.mana = self.fcr = 0
        self.current, self.added, self.events = FAKE, None, []
        e.hook(0x620250, lambda h: self.current, 1)
        e.hook(0x643AD0, lambda h: 0xFFFFFFFF, 1)
        e.hook(0x644140, lambda h: self.skills + self.skill * RECORD, 0)
        e.hook(0x6442A0, lambda h: self.lvl, 3)
        e.hook(0x625480, lambda h: self.mana if h.arg(1) == 8 else 0, 3)
        e.hook(0x63A400, lambda h: 0, 1)
        e.hook(0x639DF0, lambda h: 0, 2)
        e.hook(0x6272B0, self.add, 4)
        e.hook(0x625500, lambda h: self.fcr, 3)           # the stat getter of the rate: stat 105 is the FCR
        e.hook(0x623B10, lambda h: 0, 1)                  # the start frame of a cast (SC: 0)
        e.hook(0x5417D0, self.file, 4)                    # the event queue: noted
        blob = self.emu.mpq_cache().read(r"data\global\animdata.d2")
        self.records = [(n, f, s, e.put(raw)) for n, f, s, raw in animdata_records(blob, "SOSC")]

    def add(self, h):
        if h.arg(1) == 8:
            v = h.arg(2)
            self.added = v - (1 << 32) if v & 0x80000000 else v
        return 0

    def file(self, h):
        self.events.append(tuple(h.arg(i) for i in range(4)))
        return 0

    def row(self, skill):
        e, r = self.e, self.skills + skill * RECORD
        s16 = lambda a: int.from_bytes(e.read(a, 2), "little", signed=True)
        return {"minmana": s16(r + 0x186), "manashift": e.read(r + 0x188, 1)[0], "mana": s16(r + 0x18A), "lvlmana": s16(r + 0x18C),
                "srvst": s16(r + 0x2C), "srvdo": s16(r + 0x2E), "srvmissile": s16(r + 0x46)}

    def cost(self, skill, lvl):
        r = self.row(skill)
        return (r["mana"] + r["lvlmana"] * max(lvl - 1, 0)) << (r["manashift"] & 31)

    def schedule(self, record, fcr):
        """The events FUN_005539b0 files for a cast (SC, no sequence) at this FCR: (type, tick, value, index)."""
        e = self.e
        _name, frames, _speed, rec = record
        self.fcr, self.events = fcr, []
        e.mu.mem_write(self.unit, b"\0" * 0x200)
        e.mu.reg_write(self.emu.UC_X86_REG_EDI, self.unit)
        e.mu.reg_write(self.emu.UC_X86_REG_ESI, rec)
        e.mu.reg_write(self.emu.UC_X86_REG_ESP, self.emu.STACK + self.emu.STACK_SIZE - 0x1000)
        e.mu.emu_start(0x623FCB, 0x624015)                # FUN_00623f50's cast branch
        speed = e.r16(self.unit + 0x4C)
        e.w32(self.unit + 0x48, frames << 8)
        e.w32(self.unit + 0x50, rec)
        e.w32(self.sched + 0xA8, NOW)
        e.call(0x5539B0, ecx=self.sched, edx=self.unit, limit=100000)
        return speed, list(self.events)

    def run(self, skill, lvl, mana, fcr, cut, record):
        """`began paid_tick paid fired_tick` for one case."""
        self.skill, self.lvl, self.mana = skill, lvl, mana
        self.current = FAKE
        began = self.e.call(0x56C160, ecx=self.unit) & 0xFFFFFFFF
        if not began:
            return 0, -1, 0, -1
        row = self.row(skill)
        assert row["srvst"] == 0, "a start function pays at the command"
        _speed, events = self.schedule(record, fcr)
        paid_tick, paid, fired, made = -1, 0, -1, False
        for n in pay_tick(events, NOW):
            if cut >= 0 and n > cut:
                break                                     # the mode was replaced: the event runs no do
            if made:
                continue
            self.current, self.added = 0, None
            self.e.call(0x56BFE0, skill, lvl, ecx=0, edx=self.unit)
            got = -self.added if self.added is not None else 0
            if fired < 0:
                fired = n
            if got and paid_tick < 0:
                paid_tick = n
            paid += got
            self.mana -= got
            made = made or row["srvmissile"] >= 0
        return 1, paid_tick, paid, fired


def gen(seed):
    g, rng = Game(), random.Random(seed)
    cid = 0
    rec = g.records[0]
    for skill in SKILLS:
        for lvl in LEVELS:
            cost = g.cost(skill, lvl)
            pools = sorted({0, max(cost - 1, 0), cost, cost + 1, 2 * cost, 100 * 256})
            for fcr in sorted(set(FCRS) | {rng.randrange(401) for _ in range(3)}):
                _speed, events = g.schedule(rec, fcr)
                action = (pay_tick(events, NOW) or [0])[0]
                for mana in pools:
                    for cut in cut_cases(action):
                        print(cid, skill, lvl, mana, fcr, cut, sep="\t")
                        cid += 1
    dense(g, cid, rec)


def dense(g, cid, rec):
    """Every FCR 0..400 for each skill at levels 1 and 20 with mana to spare: the action tick is the claim."""
    for skill in SKILLS:
        for lvl in (1, 20):
            for fcr in range(401):
                _speed, events = g.schedule(rec, fcr)
                for cut in cut_cases((pay_tick(events, NOW) or [0])[0]):
                    print(cid, skill, lvl, 100 * 256, fcr, cut, sep="\t")
                    cid += 1
    return cid


def anim():
    g = Game()
    for skill in SKILLS:
        print("row", skill, " ".join(f"{k}={v}" for k, v in g.row(skill).items()))
    for name, frames, speed, ptr in g.records:
        for fcr in (0, 9, 20, 30, 48, 75, 105, 125, 200, 400):
            rate, events = g.schedule((name, frames, speed, ptr), fcr)
            print("anim", name, frames, speed, "fcr", fcr, "rate", rate, "events", events)


def game(path):
    g = Game()
    rec = g.records[0]
    for line in open(path):
        f = line.split()
        if f:
            cid, skill, lvl, mana, fcr, cut = map(int, f)
            print(cid, *g.run(skill, lvl, mana, fcr, cut, rec))


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "gen" and len(sys.argv) <= 3:
        gen(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    elif cmd == "game" and len(sys.argv) == 3:
        game(sys.argv[2])
    elif cmd == "anim":
        anim()
    else:
        sys.exit(__doc__)
