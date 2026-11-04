# SPDX-License-Identifier: GPL-3.0-or-later
"""Which items a quest's item events answer, as the oracle: the ones bound to it.

    uv run python quest_item_events.py

Event 4 (an item into a player's inventory, 0x543d80(ECX game, EDX player,
item)) and event 5 (an item dropped, 0x543db0) build {game, n, item, player}
and FUN_005439a0 walks the ITEM's own quest list (unit +0x74: {record, next}
nodes, linked by FUN_005436b0 -> FUN_005435c0 when a quest binds the unit),
calling each record's slot +0xa0 + 4 n (records with +0xa == 1). An item no
quest bound has an empty list: no handler runs. The handlers never read the
item themselves (only event +0xc, the player). Run for real: the two senders,
FUN_005439a0 and the handlers a3q1's init FUN_005b7e90 sets, FUN_005b7970
(+0xb0) and LAB_005b7a00 (+0xb4), and a4q3's LAB_005b5a90 (+0xb0,
FUN_005b6610). Hooked (noted, not the claim): the quest state set
FUN_00544350 (EDX the state), the log send FUN_00544300 (EDX the log),
FUN_00544070, the holder list add / remove FUN_00545200 / FUN_00545240, the
player's quest words FUN_006221a0 and the bit test FUN_0065c310 (the case's
bits).

Prints, per case, the calls made: `state N`, `log N`, `holder+ ID`,
`holder- ID`. Expected:

    a3q1 open, its bound item in: log 2, state 4, holder+ 7
    a3q1 open, its bound item dropped: holder- 7, state 3, log 1
    a3q1 open, an unbound item in:
    a3q1 open, an unbound item dropped:
    a3q1 closed, its bound item in:
    a3q1 closed, its bound item dropped: holder- 7
    a4q3 open, word 0x1b clear, state 0, its bound item in: state 1
    a4q3 open, word 0x1b clear, state 0, an unbound item in:
    a4q3 open, word 0x1b bit 13, its bound item in:
"""
import sys

import emu


class Events:
    def __init__(self):
        self.e = e = emu.Emu()
        self.calls = []
        self.bits = set()
        self.game = e.alloc(0x200)
        self.player = e.alloc(0x100)
        e.w32(self.player + 0xC, 7)                      # unit id
        self.words = e.alloc(0x40)
        self.data = e.alloc(0x100)
        self.item = e.alloc(0x100)
        e.hook(0x544350, lambda h: self.note(f"state {h.mu.reg_read(emu.UC_X86_REG_EDX)}"), 2)
        e.hook(0x544300, lambda h: self.note(f"log {h.mu.reg_read(emu.UC_X86_REG_EDX)}"), 3)
        e.hook(0x544070, lambda h: 0, 2)
        e.hook(0x545200, lambda h: self.note(f"holder+ {h.mu.reg_read(emu.UC_X86_REG_EDX)}"), 0)
        e.hook(0x545240, lambda h: self.note(f"holder- {h.mu.reg_read(emu.UC_X86_REG_EDX)}"), 0)
        e.hook(0x6221A0, lambda h: self.words, 1)
        e.hook(0x65C310, lambda h: int(h.arg(2) in self.bits and h.arg(1) == 0x1B), 3)

    def note(self, text):
        self.calls.append(text)
        return 0

    def record(self, slots, open_):
        e = self.e
        rec = e.alloc(0x100)
        e.w32(rec + 4, self.game)
        e.mu.mem_write(rec + 9, bytes([int(open_), 1, 0, 0]))   # +9 open, +0xa 1, +0xb 0, +0xc state 0
        e.w32(rec + 0x18, self.data)
        for offset, handler in slots.items():
            e.w32(rec + offset, handler)
        return rec

    def send(self, sender, slots, open_, bound=True, bits=()):
        e = self.e
        node = e.alloc(8)
        e.w32(node, self.record(slots, open_))
        e.w32(self.item + 0x74, node if bound else 0)
        self.bits, self.calls = set(bits), []
        e.call(sender, self.item, ecx=self.game, edx=self.player)
        return ", ".join(self.calls)


def main():
    events = Events()
    a3q1 = {0xB0: 0x5B7970, 0xB4: 0x5B7A00}
    a4q3 = {0xB0: 0x5B5A90}
    print("a3q1 open, its bound item in:", events.send(0x543D80, a3q1, True))
    print("a3q1 open, its bound item dropped:", events.send(0x543DB0, a3q1, True))
    print("a3q1 open, an unbound item in:", events.send(0x543D80, a3q1, True, False))
    print("a3q1 open, an unbound item dropped:", events.send(0x543DB0, a3q1, True, False))
    print("a3q1 closed, its bound item in:", events.send(0x543D80, a3q1, False))
    print("a3q1 closed, its bound item dropped:", events.send(0x543DB0, a3q1, False))
    print("a4q3 open, word 0x1b clear, state 0, its bound item in:", events.send(0x543D80, a4q3, True))
    print("a4q3 open, word 0x1b clear, state 0, an unbound item in:", events.send(0x543D80, a4q3, True, False))
    print("a4q3 open, word 0x1b bit 13, its bound item in:", events.send(0x543D80, a4q3, True, True, (13,)))


if __name__ == "__main__":
    if sys.argv[1:]:
        sys.exit(__doc__)
    main()
