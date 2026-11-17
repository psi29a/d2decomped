# SPDX-License-Identifier: GPL-3.0-or-later
"""Check MonUMod 34 (ai_after_death), FUN_005a3840 and FUN_005a3910, in stock game.exe 1.14d.

Run: export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
     uv run python ai_after_death.py [--mutation-check]
     uv run python ai_after_death.py inc > ../../tests/ai_after_death_cases.inc

Both hooks (ECX the game, EDX the unit, stack: mod id, unique flag; `ret 8`)
run as game.exe has them, with their callees hooked and logged: the event
list (FUN_00540e60 drop, FUN_005417d0 queue), alignment (FUN_006259b0), dead
test (FUN_005541b0), hidden corpse (FUN_0063a770), the spot test
(FUN_0064d870), the AI scratch (FUN_0058ec50 get, FUN_0058ec00 set), the
raise test (FUN_00645510) and the skill start (FUN_005dead0). The MonStats
row is a fake table behind game.exe's own pointer at 0x744304. The seed, the
frame arithmetic and the branch order are game.exe's. The claim:
  - hook 1: mode 0 and alignment 0 drop event 7, then rand(100) on the seed
    (unit +0x20 / +0x24) under aip8 (+0x80 + difficulty x 2) queues event 7
    at frame + aip1 (+0x56 + difficulty x 2) x 10 + 1; anything else does
    nothing and leaves the seed;
  - hook 2: the order of rules::ai_after_death_rise (tests/test_ai_after_death.cpp).
`inc` prints game.exe's answers for that test.
"""
import argparse
import hashlib
import random
import sys
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
HOOK1, HOOK2 = 0x5A3840, 0x5A3910
GLOBAL, FRAME = 0x744304, 1000
MUT = set()


def model_queue(mode, alignment, chance, aip1, seed):
    """rules::ai_after_death_queue re-stated: (cleared, frames or -1, new seed low, new seed high)."""
    if mode != 0 or alignment != 0:
        return False, -1, seed[0], seed[1]
    product = seed[0] * 0x6AC690C5 + seed[1]
    low, high = product & 0xFFFFFFFF, product >> 32
    hit = low % (100 if "bound" not in MUT else 99) < chance
    return True, (aip1 * 10 + 1 if hit else -1), low, high


def model_rise(dead, hidden, unique, blocked, can_raise, alignment, scratch, aip1):
    """rules::ai_after_death_rise re-stated: (kind 0 nothing / 1 again / 2 rise, frames)."""
    if not dead or hidden or unique or alignment == 2:
        return 0, 0
    if blocked:
        return 1, aip1 * 10 + 1
    limit = 2 if "limit" not in MUT else 3
    if scratch >= limit or not can_raise or (alignment != 0 and "evil" not in MUT):
        return 0, 0
    return 2, 0x33


class Game:
    """game.exe with both hooks' callees hooked once."""

    def __init__(self, exe):
        """Load the exe, the fake MonStats table, and hook the callees."""
        self.machine = machine = emu.Emu(str(exe))
        self.log = []
        self.game = machine.alloc(0x200)
        self.unit = machine.alloc(0x200)
        self.data = machine.alloc(0x40)
        self.row = machine.alloc(0x1A8)
        table = machine.alloc(0x10)
        machine.w32(GLOBAL, table)
        machine.w32(table + 0xA78, self.row - 0x1A8 * 5)        # class 5 is the row
        machine.w32(table + 0xA80, 100)
        self.cfg = {}
        machine.w32(self.game + 0xA8, FRAME)
        machine.w32(self.unit, 1)
        machine.w32(self.unit + 4, 5)
        machine.w32(self.unit + 0x14, self.data)
        machine.hook(0x540E60, lambda e: self.log.append(("drop", e.arg(0), e.arg(1))) or 0, 2)
        machine.hook(0x5417D0, lambda e: self.log.append(("queue", e.arg(0), e.arg(1))) or 0, 4)
        machine.hook(0x6259B0, lambda e: self.cfg["alignment"], 1)
        machine.hook(0x5541B0, lambda e: self.cfg["dead"], 0)
        machine.hook(0x63A770, lambda e: self.cfg["hidden"], 1)
        machine.hook(0x45AE20, lambda e: 11, 0)
        machine.hook(0x45ADF0, lambda e: 12, 0)
        machine.hook(0x620BB0, lambda e: 0x1000, 1)
        machine.hook(0x64D870, lambda e: self.cfg["blocked"], 5)
        machine.hook(0x58EC50, lambda e: self.cfg["scratch"], 0)
        machine.hook(0x58EC00, self.set_scratch, 1)
        machine.hook(0x645510, lambda e: self.cfg["can_raise"], 2)
        machine.hook(0x5DEAD0, self.skill, 5)

    def set_scratch(self, current):
        """Hook FUN_0058ec00: the scratch word put back."""
        self.log.append(("scratch", current.arg(0)))
        return 0

    def skill(self, current):
        """Hook FUN_005dead0: the skill start (mode, skill id)."""
        self.log.append(("skill", current.arg(0), current.arg(1)))
        return 1

    def setup(self, difficulty, chance, aip1, unique=False):
        """Put the difficulty, the MonStats words and the unique flag in the fake records."""
        machine = self.machine
        machine.mu.mem_write(self.game + 0x6D, bytes([difficulty]))
        machine.mu.mem_write(self.row + 0x80 + difficulty * 2, (chance & 0xFFFF).to_bytes(2, "little"))
        machine.mu.mem_write(self.row + 0x56 + difficulty * 2, (aip1 & 0xFFFF).to_bytes(2, "little"))
        machine.mu.mem_write(self.data + 0x16, bytes([8 if unique else 0]))

    def queue(self, mode, alignment, chance, aip1, seed, difficulty):
        """Run hook 1; return (cleared, frames or -1, seed low, seed high)."""
        self.setup(difficulty, chance, aip1)
        self.cfg = {"alignment": alignment}
        self.log = []
        self.machine.w32(self.unit + 0x10, mode)
        self.machine.w32(self.unit + 0x20, seed[0])
        self.machine.w32(self.unit + 0x24, seed[1])
        self.machine.call(HOOK1, 34, 1, ecx=self.game, edx=self.unit, limit=5000)
        cleared = any(entry[0] == "drop" for entry in self.log)
        frames = -1
        for entry in self.log:
            if entry[0] == "queue":
                assert entry[1] == 7
                frames = entry[2] - FRAME
        if cleared:
            assert self.log[0] == ("drop", 7, 0)
        return cleared, frames, self.machine.r32(self.unit + 0x20), self.machine.r32(self.unit + 0x24)

    def rise(self, dead, hidden, unique, blocked, can_raise, alignment, scratch, aip1, difficulty=1):
        """Run hook 2; return (kind, frames, drops of event 2, scratch put, skill, flags after)."""
        self.setup(difficulty, 0, aip1, unique)
        self.cfg = {"alignment": alignment, "dead": int(dead), "hidden": int(hidden), "blocked": int(blocked),
                    "scratch": scratch, "can_raise": int(can_raise)}
        self.log = []
        self.machine.w32(self.unit + 0xC4, 0xFFFFFFFF)
        self.machine.call(HOOK2, 34, 1, ecx=self.game, edx=self.unit, limit=5000)
        flags = self.machine.r32(self.unit + 0xC4)
        drops = [entry for entry in self.log if entry[0] == "drop"]
        assert all(entry[1:] == (2, 0) for entry in drops)
        kind, frames, skill, put = 0, 0, None, None
        for entry in self.log:
            if entry[0] == "queue" and entry[1] == 7:
                kind, frames = 1, entry[2] - FRAME
            elif entry[0] == "queue" and entry[1] == 2:
                assert kind != 1
                kind, frames = 2, entry[2] - FRAME
            elif entry[0] == "skill":
                skill = entry[1:]
            elif entry[0] == "scratch":
                put = entry[1]
        return kind, frames, len(drops), put, skill, flags


def queue_cases():
    """Hook 1 inputs: (mode, alignment, chance, aip1, seed low, seed high, difficulty)."""
    rng = random.Random(34)
    out = []
    for _ in range(3000):
        out.append((rng.choice((0, 0, 1, 12)), rng.choice((0, 0, 1, 2)), rng.choice((0, 1, 25, 50, 99, 100, 150)),
                    rng.randrange(0, 12), rng.getrandbits(32), rng.choice((666, rng.getrandbits(32))), rng.randrange(3)))
    return out


def rise_cases():
    """Hook 2 inputs: every flag combination, scratch 0..3, aip1 0 and 5."""
    out = []
    for dead in (0, 1):
        for hidden in (0, 1):
            for unique in (0, 1):
                for blocked in (0, 1):
                    for can_raise in (0, 1):
                        for alignment in (0, 1, 2):
                            for scratch in (0, 1, 2, 3):
                                for aip1 in (0, 5):
                                    out.append((dead, hidden, unique, blocked, can_raise, alignment, scratch, aip1))
    return out


def run(mutation):
    """Compare game.exe to the models; return (bad, cases, queue rows, rise rows)."""
    MUT.clear()
    MUT.update(mutation)
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    game = Game(exe)
    bad = cases = 0
    queues, rises = [], []
    for mode, alignment, chance, aip1, low, high, difficulty in queue_cases():
        got = game.queue(mode, alignment, chance, aip1, (low, high), difficulty)
        want = model_queue(mode, alignment, chance, aip1, (low, high))
        cases += 1
        queues.append(((mode, alignment, chance, aip1, low, high), got))
        if got != want:
            bad += 1
            print(f"queue {(mode, alignment, chance, aip1, low, high, difficulty)}: game {got} model {want}")
    for case in rise_cases():
        got = game.rise(*case)
        want = model_rise(*case)
        cases += 1
        rises.append((case, got))
        ok = got[:2] == want
        if want[0] == 2:
            ok = ok and got[3] == case[6] + 1 and got[4] == (8, 0x125) and got[5] == 0xFBFDFFFF and got[2] == 2
        else:
            ok = ok and got[2] == 1 and got[3] is None and got[4] is None and got[5] == 0xFFFFFFFF
        if not ok:
            bad += 1
            print(f"rise {case}: game {got} model {want}")
    return bad, cases, queues, rises


def main():
    """Run the oracle; `inc` prints the C++ fixtures; --mutation-check proves the mutants are caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", default="check", choices=("check", "inc"))
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    bad, cases, queues, rises = run(())
    if bad:
        print(f"FAIL: {bad} of {cases} differ", file=sys.stderr)
        sys.exit(1)
    if args.mode == "inc":
        print("// Stock game.exe 1.14d, FUN_005a3840 / FUN_005a3910; generated by tools/emu/ai_after_death.py inc.")
        print("// queue: mode, alignment, chance, aip1, seed low, seed high, cleared, frames (-1 none), seed low, seed high after")
        for (mode, alignment, chance, aip1, low, high), (cleared, frames, new_low, new_high) in queues:
            print(f"QUEUE({mode}, {alignment}, {chance}, {aip1}, {low}u, {high}u, {int(cleared)}, {frames}, {new_low}u, {new_high}u)")
        print("// rise: dead, hidden, unique, blocked, can_raise, alignment, scratch, aip1, kind, frames, event 2 drops, scratch put (-1 none), flags after")
        for case, (kind, frames, drops, put, _skill, flags) in rises:
            print(f"RISE({', '.join(map(str, case))}, {kind}, {frames}, {drops}, {-1 if put is None else put}, {flags}u)")
        return
    print(f"ok: {len(queues)} FUN_005a3840 cases, {len(rises)} FUN_005a3910 cases")
    if args.mutation_check:
        for name in ("bound", "limit", "evil"):
            bad, _, _, _ = run((name,))
            print(f"mutant {name}: {'caught' if bad else 'NOT CAUGHT'} ({bad})")
            if not bad:
                sys.exit(1)
        print("mutation check ok")


if __name__ == "__main__":
    main()
