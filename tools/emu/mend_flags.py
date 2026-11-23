# SPDX-License-Identifier: GPL-3.0-or-later
"""Check what FUN_0055f900 (a broken item mended) does to the item's flags, in stock game.exe 1.14d.

Run: export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
     /home/gui/Projects/d2decomped/tools/emu/.venv/bin/python mend_flags.py [--mutation-check]

FUN_0055f900 (ECX game, EDX owner unit, stack item unit) runs for real, with
FUN_006280d0 (the flag setter on item data +0x18) real too; every other callee
is hooked and its arguments checked. The claim, for 600 item flag words and an
owner with and without an inventory (unit +0x60) or a client:
  - item flag 0x200 comes on and 0x100 off, every other bit as it was;
  - with an inventory: FUN_0055d970, FUN_0055dbc0, FUN_0063cc70 (inventory,
    item) and FUN_00621000 (owner, 1) run, in that order, before the stat;
  - a max durability (FUN_00625e00) above 0 writes stat 0x48 (FUN_00627260),
    and tells the owner's client (FUN_0053d130) when there is an owner;
  - FUN_00663cc0 (owner, item, 0, 0) runs, then the hand-skill pass FUN_0055c5c0
    (EAX = owner) when there is an owner.
d2d's rules::mend / rules::replenish_fire: flags = (flags | 0x200) & ~0x100
(tests/test_item_repaired.cpp). The stat write and the hooks' order are
checked here only; the hand-skill pass is not ported (components/rules/items/replenish.hpp).
"""
import argparse
import random
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EAX

import emu

TARGET = 0x55F900
MUT = set()


def model_flags(flags):
    """d2d's mend flags: 0x200 on, 0x100 off."""
    if "keep100" in MUT:
        return flags | 0x200
    if "no200" in MUT:
        return flags & ~0x100 & 0xFFFFFFFF
    return (flags | 0x200) & ~0x100 & 0xFFFFFFFF


class Mend:
    """FUN_0055f900 over a hooked machine."""

    def __init__(self):
        e = self.e = emu.Emu()
        self.game, self.owner, self.inv, self.item, self.data = (e.alloc(0x200) for _ in range(5))
        self.calls = []
        self.dmost = 0
        e.w32(self.item, 4)
        e.w32(self.item + 0x14, self.data)
        e.w32(self.owner + 0x60, 0)

        def log(name, **kwargs):
            def hooked(h):
                self.calls.append((name, kwargs.get("args", lambda h: ())(h)))
                return kwargs.get("result", lambda h: 0)(h)
            return hooked

        def regs(h):
            return (h.mu.reg_read(UC_X86_REG_ECX), h.mu.reg_read(UC_X86_REG_EDX))

        e.hook(0x55D970, log("wield_in", args=lambda h: (h.arg(0),)), 1)
        e.hook(0x55DBC0, log("settle", args=lambda h: (regs(h), h.arg(0))), 1)
        e.hook(0x63CC70, log("inv_add", args=lambda h: (h.arg(0), h.arg(1))), 2)
        e.hook(0x621000, log("owner_dirty", args=lambda h: (h.arg(0), h.arg(1))), 2)
        e.hook(0x625E00, log("dmost", args=lambda h: (h.arg(0),), result=lambda h: self.dmost & 0xFFFFFFFF), 1)
        e.hook(0x627260, log("stat_set", args=lambda h: (h.arg(0), h.arg(1), h.arg(2), h.arg(3))), 4)
        e.hook(0x5531C0, log("client", args=lambda h: (regs(h), h.arg(0), h.arg(1), h.arg(2), h.arg(3), h.arg(4))), 5)
        e.hook(0x53D130, log("tell", args=lambda h: (regs(h), h.arg(0), h.arg(1), h.arg(2), h.arg(3))), 4)
        e.hook(0x663CC0, log("hands_a", args=lambda h: (h.arg(0), h.arg(1), h.arg(2), h.arg(3))), 4)
        e.hook(0x55C5C0, log("hand_skills", args=lambda h: (h.mu.reg_read(UC_X86_REG_EAX),)), 0)

    def run(self, flags, with_owner, with_inv, dmost):
        e = self.e
        e.w32(self.data + 0x18, flags)
        e.w32(self.owner + 0x60, self.inv if with_inv else 0)
        self.calls, self.dmost = [], dmost
        e.e_owner = self.owner
        e.call(TARGET, self.item, ecx=self.game, edx=self.owner if with_owner else 0, limit=10000)
        return e.r32(self.data + 0x18), self.calls


def expected(game, with_owner, with_inv, dmost):
    """The calls FUN_0055f900 makes, as the claim lists them."""
    out = []
    if with_owner and with_inv:
        out += [("wield_in", (game.game,)), ("settle", ((game.game, game.owner), 0)), ("inv_add", (game.inv, game.item)), ("owner_dirty", (game.owner, 1))]
    out.append(("dmost", (game.item,)))
    if dmost > 0:
        out.append(("stat_set", (game.item, 0x48, dmost, 0)))
        if with_owner:
            out.append(("client", None))
            out.append(("tell", None))
    out.append(("hands_a", (game.owner if with_owner else 0, game.item, 0, 0)))
    if with_owner:
        out.append(("hand_skills", (game.owner,)))
    return out


def same(got, want):
    """Compare call lists; a None argument list matches any."""
    return len(got) == len(want) and all(g[0] == w[0] and (w[1] is None or g[1] == w[1]) for g, w in zip(got, want))


def run(mutation):
    """Return (failures, cases)."""
    MUT.clear()
    MUT.update(mutation)
    game = Mend()
    rng = random.Random(7)
    bad = cases = 0
    for case in range(600):
        flags = rng.choice((0, 0x100, 0x200, 0x300, 0xFFFFFFFF)) if case < 5 else rng.randrange(1 << 32)
        with_owner, with_inv = bool(case & 1), bool(case & 2)
        dmost = rng.choice((0, 1, 40, 255, -1, rng.randrange(-5, 300)))
        if with_inv and not with_owner:
            with_inv = False                      # an inventory belongs to an owner
        got_flags, calls = game.run(flags, with_owner, with_inv, dmost)
        cases += 1
        if got_flags != model_flags(flags) or not same(calls, expected(game, with_owner, with_inv, dmost)):
            bad += 1
            if bad < 6:
                print(f"flags {flags:#x} owner {with_owner} inv {with_inv} dmost {dmost}: game {got_flags:#x} model {model_flags(flags):#x} calls {[c[0] for c in calls]}")
    return bad, cases


def main():
    """Run the oracle; --mutation-check proves a wrong flag rule is caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    bad, cases = run(())
    if bad:
        print(f"FAIL: {bad} of {cases} differ", file=sys.stderr)
        sys.exit(1)
    print(f"ok: {cases} FUN_0055f900 cases (flag words, owner with and without an inventory, durability maxima)")
    if args.mutation_check:
        for name in ("keep100", "no200"):
            bad, _ = run((name,))
            print(f"mutant {name}: {'caught' if bad else 'NOT CAUGHT'} ({bad})")
            if not bad:
                sys.exit(1)
        print("mutation check ok")


if __name__ == "__main__":
    main()
