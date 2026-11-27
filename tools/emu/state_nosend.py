# SPDX-License-Identifier: GPL-3.0-or-later
"""Which states the server sends its client in stock game.exe 1.14d: FUN_005711d0 over every States.txt row.

Run: export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
     .venv/bin/python state_nosend.py [--mutation-check]

With every data table loaded (drlg.boot), FUN_005711d0 (ECX the unit, EDX the client) runs for real once
per state, the unit's changed-state bits (FUN_00639f70) holding that one state. Hooked: FUN_00639df0
(the state on, or off), FUN_006256b0 (no list, so an on state goes as 0xa7), and the packet builders
FUN_0053e260 (0xa7 delayed state) / FUN_0053e290 (0xa9 end state), recorded. The claim: a state goes to
the client, on or off, unless its States.txt `nosend` is set (the loader's +0x10 bit 0, the mask byte
0x6ce268 tested at 0x5712f0). Inferno (12) is nosend, so the client never holds the server's state 0xc
or a list for it: cltstfunc 15 (LAB_004e3060) only ever finds the client's own list (Fight::inferno_client_start).
"""
import argparse
import sys

import drlg
import emu

SENDER, STATE_INFERNO = 0x5711D0, 12
MUT = set()


def nosend_column():
    """States.txt's rows in order: nosend set or not."""
    text = emu.mpq_cache().read("data\\global\\excel\\states.txt").decode("latin-1").splitlines()
    head = text[0].split("\t")
    column = head.index("nosend")
    rows = []
    for line in text[1:]:
        field = line.split("\t")
        if field[0] and field[0] != "Expansion":
            rows.append(len(field) > column and field[column].strip() not in ("", "0"))
    return rows


def model(state, nosend):
    """Whether the server sends `state` to its client."""
    if "send_all" in MUT:
        return True
    if "inferno_sent" in MUT and state == STATE_INFERNO:
        return True
    return not nosend


def game_sends(e, count):
    """FUN_005711d0 once per state, on and off: {(state, on): packet id or None}."""
    unit, client, bits = e.alloc(0x200), e.alloc(0x100), e.alloc(0x100)
    e.w32(unit, 0)
    e.w32(unit + 0xC, 1)
    now = {"on": 1, "sent": []}
    e.hook(0x639F70, lambda _e: bits, 1)
    e.hook(0x639DF0, lambda _e: now["on"], 2)
    e.hook(0x6256B0, lambda _e: 0, 2)
    e.hook(0x53E260, lambda _e: now["sent"].append((0xA7, _e.arg(1))) or 0, 2)
    e.hook(0x53E290, lambda _e: now["sent"].append((0xA9, _e.arg(1))) or 0, 2)
    out = {}
    for state in range(count):
        for on in (1, 0):
            e.mu.mem_write(bits, bytes(0x40))
            e.w32(bits + 4 * (state >> 5), 1 << (state & 31))
            now.update(on=on, sent=[])
            e.call(SENDER, ecx=unit, edx=client)
            want = (0xA7 if on else 0xA9, state)
            if now["sent"] not in ([], [want]):
                sys.exit(f"state {state}: odd packets {now['sent']}")
            out[(state, on)] = bool(now["sent"])
    return out


def run(sends, nosend, mutation):
    """The model against game.exe's sends; the failure count."""
    MUT.clear()
    MUT.update(mutation)
    bad = 0
    for (state, on), sent in sends.items():
        if model(state, nosend[state]) != sent:
            bad += 1
            if bad <= 3 and not mutation:
                print(f"state {state} on {on}: game {sent}, model {model(state, nosend[state])}")
    return bad


def main():
    """Run the oracle; with --mutation-check prove each mutant of the model is caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    nosend = nosend_column()
    sends = game_sends(drlg.boot(), len(nosend))
    bad = run(sends, nosend, ())
    if bad or sends[(STATE_INFERNO, 1)] or sends[(STATE_INFERNO, 0)]:
        print(f"FAIL: {bad} cases differ; inferno sent {sends[(STATE_INFERNO, 1)]}", file=sys.stderr)
        sys.exit(1)
    print(f"ok: {len(sends)} FUN_005711d0 cases over {len(nosend)} states ({sum(nosend)} nosend); inferno (12) never sent")
    if args.mutation_check:
        for name in ("send_all", "inferno_sent"):
            caught = run(sends, nosend, (name,))
            print(f"mutant {name}: {'caught' if caught else 'NOT CAUGHT'} ({caught})")
            if not caught:
                sys.exit(1)
        print("mutation check ok")


if __name__ == "__main__":
    main()
