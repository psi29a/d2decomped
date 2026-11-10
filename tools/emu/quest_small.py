# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ten small QUESTS.CPP helpers against stock game.exe 1.14d.

Run: python quest_small.py [--mutation-check]
FUN_00543b90, 00543ce0, 00543de0 (event records), 00544470 (quest-data accessor),
00545290 and 00545300 (id list), 00545760 (respec flag), 00545830, 00545990
and 00545d40 (record constructor). The event dispatchers, the memset and the
other callees are hooked and logged; their bodies are not exercised here.
"""
import _dungeon_rig as rig_mod
from _quest_lib import Mutations, main, put, record_hook

MUT = Mutations("kind3", "kind0", "count_has", "count_clear", "tag", "flags", "ctor")
GAME, REC, LIST = 0x100, 0x200, 0x400


def suite(rig):
    """Run every helper on random arenas against its model."""
    base = rig.arena
    record_hook(rig, "event", 0x5438e0, 2, (0, 4, 0xc, 0x14, 0x18))
    record_hook(rig, "unit_event", 0x5439a0, 1, (0, 4, 8, 0xc))
    rig.callee("handler", 0x59dfd0, 1, ("ecx", "edx"))
    rig.callee("walk", 0x5537d0, 2, ("ecx", "edx"))
    rig.callee("memset", 0x681ef0, 0, (), nlog=3)
    rig.custom["memset"] = lambda emu: emu.mu.mem_write(emu.arg(0), bytes(emu.arg(2))) or 0

    def plain(img, rng):
        """Registers and stack words that are random, with no pointers."""
        return {"ecx": rng.randrange(1 << 32), "edx": rng.randrange(1 << 32),
                "stack": [rng.randrange(1 << 32), rng.randrange(1 << 32)]}

    def model_b90(m, args):
        """FUN_00543b90: record kind 3 with the two stack words, dispatched with (1, 0)."""
        a, b = args["stack"]
        m.call("event", args["ecx"], 2 if "kind3" in MUT else 3, b, args["edx"], a, 1, 0)
    rig.run("FUN_00543b90", 0x543b90, plain, model_b90, 2, ret=False)

    def model_ce0(m, args):
        """FUN_00543ce0: record kind 0 carrying the stack word and EDX, dispatched with 0."""
        a = args["stack"][0]
        m.call("unit_event", args["ecx"], 1 if "kind0" in MUT else 0, a, args["edx"], 0)
    rig.run("FUN_00543ce0", 0x543ce0, plain, model_ce0, 1, ret=False)

    def model_de0(m, args):
        """FUN_00543de0: record kind 6 carrying EDX, dispatched with 0."""
        m.call("unit_event", args["ecx"], 6, args["edx"], 0, 0)
    rig.run("FUN_00543de0", 0x543de0, plain, model_de0, 0, ret=False)

    def build_544470(img, rng):
        """Cover the null game assert, a missing quest block and a present one."""
        index = rig.cases % 24
        put(img, GAME + 0x10f4, 0 if index % 3 == 1 else base + REC)
        put(img, REC + 8, rng.randrange(1 << 32))
        return {"stack": [0 if index % 8 == 7 else base + GAME]}

    def model_544470(m, args):
        """FUN_00544470: assert on a null game, else the data pointer's +8 or 0."""
        game = args["stack"][0]
        if not game:
            raise rig_mod.Halt(0x7b3)
        block = m.r(game + 0x10f4)
        return m.r(block + 8) if block else 0
    rig.run("FUN_00544470", 0x544470, build_544470, model_544470, 1)

    def build_list(img, rng):
        """An id list of 0..9 entries; half the cases plant the searched id."""
        count = rig.cases % 10
        put(img, LIST + 0x80, count, 2)
        put(img, LIST + 0x82, rng.randrange(1 << 16), 2)        # the high word of the dword: not part of the count
        target = rng.randrange(1 << 32)
        for index in range(count):
            put(img, LIST + 4 * index, rng.randrange(1 << 32))
        if count and rig.cases % 4 < 2:
            put(img, LIST + 4 * rng.randrange(count), target)
        return {"ecx": base + LIST, "edx": target}

    def model_545290(m, args):
        """FUN_00545290: 1 when the id is among the first `count` words, else 0."""
        count = m.r(args["ecx"] + 0x80, 2)
        limit = count - 1 if "count_has" in MUT else count
        return int(any(m.r(args["ecx"] + 4 * i) == args["edx"] for i in range(max(limit, 0))))
    rig.run("FUN_00545290", 0x545290, build_list, model_545290, 0, mask=0xff)

    def model_545300(m, args):
        """FUN_00545300: zero the list count."""
        m.w(args["ecx"] + 0x80, 1 if "count_clear" in MUT else 0, 2)
    rig.run("FUN_00545300", 0x545300, build_list, model_545300, 0, ret=False)

    def build_760(img, rng):
        """A game with a quest-data block and a random EDX low byte."""
        put(img, GAME + 0x10f4, base + REC)
        return {"ecx": base + GAME, "edx": rng.randrange(1 << 32)}

    def model_760(m, args):
        """FUN_00545760: store DL at data +0x20 and walk with the sender callback 0x5456f0."""
        m.w(m.r(args["ecx"] + 0x10f4) + 0x20, args["edx"] & (0xfe if "flags" in MUT else 0xff), 1)
        return m.call("walk", args["ecx"], 0, 0, 0x5456f0)
    rig.run("FUN_00545760", 0x545760, build_760, model_760, 0)

    def model_830(m, args):
        """FUN_00545830: forward the second word to FUN_0059dfd0 only for tag 0x49."""
        tag, word = args["stack"]
        if tag != (0x48 if "tag" in MUT else 0x49):
            return 0
        return m.call("handler", args["ecx"], args["edx"], word)

    def build_830(img, rng):
        """Random words with the tag forced to 0x49 half the time."""
        out = plain(img, rng)
        if rig.cases & 1:
            out["stack"][0] = 0x49
        return out
    rig.run("FUN_00545830", 0x545830, build_830, model_830, 2)

    def model_990(m, args):
        """FUN_00545990: returns with no effect."""
    rig.run("FUN_00545990", 0x545990, plain, model_990, 1, ret=False)

    def build_d40(img, rng):
        """A record at REC and three random constructor arguments."""
        return {"regs": {"esi": base + REC}, "stack": [rng.randrange(1 << 32) for _ in range(3)]}

    def model_d40(m, args):
        """FUN_00545d40: memset 0xf8 then +0 = arg1, +4 = arg0, +8 = low byte of arg2, +9 = 1."""
        record = base + REC
        a, b, c = args["stack"]
        m.call("memset", record, 0, 0xf8)
        m.region(record, 0xf8)[0][REC:REC + 0xf8] = bytes(0xf8)
        m.w(record, a if "ctor" in MUT else b)
        m.w(record + 4, a)
        m.w(record + 0xa, 0, 1)
        m.w(record + 0xb, 0, 1)
        m.w(record + 9, 1, 1)
        m.w(record + 8, c, 1)
        m.w(record + 0x9c, 0, 2)
    rig.run("FUN_00545d40", 0x545d40, build_d40, model_d40, 3, ret=False)
    return rig.cases


if __name__ == "__main__":
    main(__doc__, "small quest helper", suite, MUT)
