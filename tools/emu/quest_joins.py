# SPDX-License-Identifier: GPL-3.0-or-later
"""What each quest's join does, as the oracle: every record's +0xd4.

    uv run python quest_joins.py            # every quest
    uv run python quest_joins.py 0x5 0x21   # the records named (record id)
    uv run python quest_joins.py --d8 [ids] # the later players' +0xd8 instead

A player coming into a game runs each quest record's +0xd4 (FUN_00546270),
fastcall ECX record, EDX the event {+0 game, +0xc player}. The records are
made as game.exe makes them: each row of the init table 0x731510 (24 bytes:
init fn, act, 0x64, flag, record id, quest word), the record's +0 the id,
+4 the game, then the init run for real (its +0xd4, +0x18 data block, the
other slots). The record as FUN_00545d80 makes it: 0xf8 bytes zeroed, +0
the id, +4 the game, +8 the act, +9 = 1 (open), then the init (ECX the
record). Then each +0xd4 runs for real, explored over its inputs.

Inputs (each a yes / no the join asks, explored: the first run answers no to
everything, and every question it asked opens a run answering yes to it as
well; up to LIMIT runs a record):
  p W.B    the player's quest word W bit B (FUN_0065c310 on the player's
           flags, *(FUN_006221a0(player) + 0x10 + difficulty * 4))
  g W.B    the game's copy (FUN_0065c310 on game +0x10f4 -> +0xc)
  item C   the player carries item code C (FUN_00558110)
  wp L     the player's waypoint for level L is on (FUN_00660e00 /
           FUN_00660e50, the waypoint flags at pcdata +0x1c)
  call A   a yes / no from FUN_A (FUN_005723c0)
Only the questions a run asked count: a line's yes answers are the ones on its
path, so each line is one path through the join.
The record's own fields come from its init (state +0xc, +9 open, +0xb log);
a join reading other records gets theirs (FUN_00543640, hooked to the made
records).

--d8: a later player (the quest list's +8 already set) gets only each
record's +0xd8 (FUN_00546270's first loop), same arguments. A +0xd8 reads
what the game has done so far, so each runs once for every record state
+0xc = 0..6 (a block line `state N`, the record's +0xb log left as the init
made it); the data block is still the init's.

Output, a block a record (`rec ID word W init FN join FN`), then a line a run:
the yes answers, `->`, then what it did in order: `p+ W.B` / `p- W.B` (set /
clear a player bit: FUN_0065c360 / FUN_0065c3a0), `g+ W.B` (FUN_00544720,
the game's copy), `state N` (FUN_00544350), `log N` (FUN_00544300), `dark`
(FUN_0059a570), `send N` (FUN_00538680, the client update), `players FN`
(FUN_005537d0 over the players with FN), then the bytes it changed: `rID+OFF=V`
in a record (its 0xf8 bytes), `dID+OFF=V` in a record's data block
(+0x18, its size as the init allocated it).
"""
import struct
import sys

import emu

LIMIT = 256
TABLE, ROWS = 0x731510, 0x25
REG = emu


def edx(h):
    return h.mu.reg_read(REG.UC_X86_REG_EDX)


def ecx(h):
    return h.mu.reg_read(REG.UC_X86_REG_ECX)


class Joins:
    def __init__(self):
        self.e = e = emu.Emu()
        self.game = e.alloc(0x2000)
        e.w32(self.game + 0x1C, e.alloc(0x40))                # memory pool (only passed to the allocator)
        self.flags_game = e.alloc(0x10)                       # game +0x10f4 -> {+8 set, +0xc flags handle}
        e.w32(self.flags_game + 8, 1)
        e.w32(self.flags_game + 0xC, e.alloc(8))
        e.w32(self.game + 0x10F4, self.flags_game)
        self.player = e.alloc(0x200)
        e.w32(self.player + 0xC, 1)                           # unit id
        self.pcdata = e.alloc(0x100)
        self.flags_player = e.alloc(8)
        self.waypoints = e.alloc(8)
        for diff in range(3):
            e.w32(self.pcdata + 0x10 + 4 * diff, self.flags_player)
            e.w32(self.pcdata + 0x1C + 4 * diff, self.waypoints)
        self.event = e.alloc(0x10)
        e.w32(self.event, self.game)
        e.w32(self.event + 0xC, self.player)
        self.sizes = {}
        e.hook(0x40B430, self.allocate, 3)
        self.records = {}
        self.rows = []
        for row in range(ROWS):
            init, act, _, _, rid, word = struct.unpack("<6I", e.read(TABLE + 24 * row, 24))
            rec = e.alloc(0xF8)                               # as FUN_00545d80 makes it
            e.w32(rec, rid)
            e.w32(rec + 4, self.game)
            e.mu.mem_write(rec + 8, bytes([act, 1, 0, 0]))    # +8 act, +9 open, +0xa 0, +0xb 0
            e.call(init, ecx=rec)
            self.records[rid] = rec
            self.rows.append((rid, word, init, rec))
        self.init_state = {rid: e.read(rec + 0xC, 1)[0] for rid, rec in self.records.items()}
        e.hook(0x6221A0, lambda h: self.pcdata, 1)
        e.hook(0x543640, lambda h: self.records.get(edx(h), 0), 0)
        e.hook(0x65C310, self.test, 3)
        e.hook(0x65C360, lambda h: self.change(h, "+"), 3)
        e.hook(0x65C3A0, lambda h: self.change(h, "-"), 3)
        e.hook(0x544720, self.game_set, 1)
        e.hook(0x558110, lambda h: self.ask(f"item {h.arg(0).to_bytes(4, 'little').decode('latin1').strip()}") and self.dummy, 1)
        e.hook(0x660E00, self.wp_index, 2)
        e.hook(0x660E50, lambda h: self.ask(f"wp {self.wp_level.get(h.arg(1), h.arg(1))}"), 2)
        e.hook(0x5723C0, lambda h: self.ask(f"call 5723c0 {h.arg(0):#x}"), 1)
        e.hook(0x59A570, lambda h: self.did("dark"), 1)
        e.hook(0x544350, lambda h: self.did(f"state {edx(h)}"), 2)
        e.hook(0x544300, lambda h: self.did(f"log {edx(h)}"), 3)
        e.hook(0x5531C0, lambda h: self.dummy, 1)
        e.hook(0x538680, lambda h: self.did(f"send {edx(h)}"), 1)
        e.hook(0x5537D0, lambda h: self.did(f"players {h.arg(1):x}"), 2)
        self.dummy = e.alloc(0x200)
        self.wp_level = {}

    def allocate(self, h):
        size = max(edx(h), 4)
        block = h.alloc(size)
        self.sizes[block] = size
        return block

    # ---- inputs ----
    def ask(self, fact):
        answer = int(fact in self.yes)
        if fact not in [asked for asked, _ in self.asked]:
            self.asked.append((fact, answer))
        return answer

    def who(self, handle):
        if handle == self.flags_player:
            return "p"
        if handle == self.e.r32(self.flags_game + 0xC):
            return "g"
        return f"?{handle:x}"

    def test(self, h):
        who, word, bit = self.who(h.arg(0)), h.arg(1), h.arg(2)
        fact = f"{who} {word:#x}.{bit}"
        if fact in self.set_now:
            return 1
        if fact in self.cleared_now:
            return 0
        return self.ask(fact)

    def change(self, h, sign):
        fact = f"{self.who(h.arg(0))} {h.arg(1):#x}.{h.arg(2)}"
        (self.set_now if sign == "+" else self.cleared_now).add(fact)
        (self.cleared_now if sign == "+" else self.set_now).discard(fact)
        return self.did(f"{fact.split()[0]}{sign} {fact.split()[1]}")

    def game_set(self, h):
        fact = f"g {edx(h):#x}.{h.arg(0)}"
        self.set_now.add(fact)
        return self.did(f"g+ {fact.split()[1]}")

    def wp_index(self, h):
        level = h.arg(0)
        self.wp_level[level] = level
        self.e.w32(h.arg(1), level)
        return 1

    def did(self, text):
        self.effects.append(text)
        return 0

    # ---- one run ----
    def snapshot(self):
        e = self.e
        out = {}
        for rid, rec in self.records.items():
            out[("r", rid)] = e.read(rec, 0xF8)
            data = e.r32(rec + 0x18)
            if data:
                out[("d", rid)] = e.read(data, self.sizes[data])
        return out

    def run(self, rid, join, yes, state=None):
        self.yes, self.asked, self.effects = set(yes), [], []
        self.set_now, self.cleared_now = set(), set()
        if state is not None:
            self.e.mu.mem_write(self.records[rid] + 0xC, bytes([state]))
        before = self.snapshot()
        try:
            self.e.call(join, ecx=self.records[rid], edx=self.event, limit=2000000)
        except Exception as error:                            # noqa: BLE001 - reported, not hidden
            self.effects.append(f"FAULT {str(error)[:160]}")
        after = self.snapshot()
        for key in before:
            old, new = before[key], after[key]
            for offset in range(len(old)):
                if old[offset] != new[offset]:
                    self.effects.append(f"{key[0]}{key[1]:#x}+{offset:#x}={new[offset]:#x}")
        for key, value in before.items():                     # put every record back for the next run
            rec = self.records[key[1]]
            self.e.mu.mem_write(rec if key[0] == "r" else self.e.r32(rec + 0x18), value)
        if state is not None:
            self.e.mu.mem_write(self.records[rid] + 0xC, bytes([self.init_state[rid]]))
        return list(self.asked), list(self.effects)

    def explore(self, rid, join, state=None):
        """Every path through the join: a path's yes answers are its input; each no on it opens another."""
        todo, seen, lines = [frozenset()], set(), []
        while todo and len(seen) < LIMIT:
            yes = todo.pop(0)
            asked, effects = self.run(rid, join, yes, state)
            path = frozenset(fact for fact, answer in asked if answer)
            if path in seen:
                continue
            seen.add(path)
            lines.append(f"  {' + '.join(sorted(path)) or '-'} -> {', '.join(effects)}")
            todo.extend(path | {fact} for fact, answer in asked if not answer)
        if todo:
            lines.append(f"  (stopped at {LIMIT} runs)")
        return lines


def main(wanted, later):
    joins = Joins()
    for rid, word, init, rec in joins.rows:
        join = joins.e.r32(rec + (0xD8 if later else 0xD4))
        if not join or (wanted and rid not in wanted):
            continue
        print(f"rec {rid:#x} word {word:#x} init {init:#x} {'d8' if later else 'join'} {join:#x}")
        for state in (range(7) if later else (None,)):
            if state is not None:
                print(f" state {state}")
            for line in joins.explore(rid, join, state):
                print(line)


if __name__ == "__main__":
    args = sys.argv[1:]
    later = "--d8" in args
    try:
        names = {int(arg, 16) for arg in args if arg != "--d8"}
    except ValueError:
        sys.exit(__doc__)
    main(names, later)
