# SPDX-License-Identifier: GPL-3.0-or-later
"""Check ten QUESTS.CPP packet and state-list senders against stock game.exe 1.14d.

Run: python quest_packets.py [--mutation-check]
FUN_00544520 (0x29 flag dump), 00544f60 and 00544fa0 (per-kind state lists),
00545090 and 00545100 (0x91 state packet), 00545530, 005455b0, 005456f0 (0x89),
00545780 (0x27) and 00545920 (0x5d). Player-data lookup, the sender and
packet-buffer functions are hooked and logged; their bodies are not exercised.
"""
import _dungeon_rig as rig_mod
from _dungeon_rig import mix
from _quest_lib import Mutations, bytes_hook, main, put, record_hook

MUT = Mutations("assert", "flags", "table", "count", "byte", "null", "fill", "found", "send", "kind", "gate1",
          "gate2", "list", "tail", "byte89", "rank")
GAME, DATA, REC, PD, UNIT, PARAM, ARR, REC2 = 0x100, 0x1400, 0x1600, 0x1800, 0x1a00, 0x1b00, 0x1c00, 0x1d00
CONST = 0x6e0be4
TABLES = ((0x7318b0, 6), (0x7318c8, 11), (0x7318f4, 7), None, (0x731910, 7))


def suite(rig):
    """Run each packet or list function against its model."""
    base = rig.arena
    pick = {"bits": [], "flags": 0}
    rig.callee("pdata", 0x6221a0, 1)
    rig.custom["pdata"] = lambda emu: base + PD
    rig.callee("add", 0x572420, 0, ("ecx", "edx"))
    rig.callee("test", 0x572470, 0, ("ecx", "edx"))
    rig.custom["test"] = lambda emu: pick["bits"].pop(0)
    rig.callee("slot", 0x5531c0, 1, ("ecx", "edx"))
    rig.callee("flag", 0x65c310, 3)
    rig.callee("tail", 0x545240, 0, ("ecx", "edx"))
    rig.callee("pack", 0x661480, 2, (), nlog=1)
    rig.callee("alloc", 0x661240, 1)
    rig.callee("release", 0x6612a0, 1)
    rig.callee("level", 0x61a1b0, 1)
    rig.callee("owner", 0x620bb0, 1)
    rig.callee("rank", 0x6427f0, 1)
    rig.callee("f5387c0", 0x5387c0, 0, ("ecx",))
    rig.callee("f53d670", 0x53d670, 4, ("ecx", "edx"))
    record_hook(rig, "event", 0x5438e0, 2, (0, 4, 8, 0xc, 0x14))
    bytes_hook(rig, "send61", 0x53d700, 0, ("ecx",), "edx", 0x61)
    bytes_hook(rig, "send1a", 0x53e060, 0, ("ecx",), "edx", 0x1a)
    bytes_hook(rig, "send2", 0x53dfe0, 0, ("ecx",), "edx", 2)
    bytes_hook(rig, "send6", 0x53c8d0, 0, ("ecx",), "edx", 6)
    bytes_hook(rig, "send_5d", 0x53d710, 0, ("ecx",), "edx", 6)

    def flags(emu):
        """FUN_0065c560 stand-in: fill the 0x60-byte destination from its first argument."""
        data = bytes(mix("flags", emu.arg(0), i) & 0xff for i in range(0x60))
        emu.mu.mem_write(emu.arg(1), data)
        return 0
    rig.m.hook(0x65c560, lambda emu: (rig.log.append(("copy", emu.arg(0), emu.arg(2), emu.arg(3))),
                                      rig.outs.append(flags(emu)))[1] or 0, 4)

    def build_520(img, rng):
        """A game with quest data; every eighth case has a zero +8 field to trip the assert."""
        put(img, GAME + 0x10f4, base + DATA)
        put(img, DATA + 8, 0 if rig.cases % 8 == 7 else rng.randrange(1, 1 << 32))
        return {"ecx": base + GAME, "edx": rng.randrange(1 << 32)}

    def model_520(m, args):
        """FUN_00544520: assert on data +8 == 0, else send 0x29 plus the 0x60 flag bytes of data +0xc."""
        data = m.r(args["ecx"] + 0x10f4)
        if m.r(data + 8) == 0 and "assert" not in MUT:
            raise rig_mod.Halt(0x7dd)
        key = m.r(data + 0xc)
        m.call("copy", key, 0x60, 0)
        payload = bytes(mix("flags", key, i) & 0xff for i in range(0x60))
        m.call("send61", args["edx"], bytes([0x29]) + (payload[:-1] + b"\0" if "flags" in MUT else payload))
    rig.run("FUN_00544520", 0x544520, build_520, model_520, 0, ret=False)

    def build_f60(img, rng):
        """A list of -2..7 words for a record whose class byte picks a player-data slot."""
        put(img, REC2 + 0x6d, rng.randrange(256), 1)
        for i in range(8):
            put(img, ARR + 4 * i, rng.randrange(1 << 32))
        return {"regs": {"eax": rng.randrange(1 << 32), "edi": (rig.cases % 10 - 2) & 0xffffffff},
                "stack": [base + REC2, base + ARR]}

    def model_f60(m, args):
        """FUN_00544f60: for each of EDI words, add it to the player-data state list for the class byte."""
        m.call("pdata", args["regs"]["eax"])
        slot = m.r(base + PD + m.r(args["stack"][0] + 0x6d, 1) * 4 + 0x60)
        count = rig_mod.s32(args["regs"]["edi"])
        for i in range(count - 1 if "count" in MUT else count):
            m.call("add", slot, m.r(args["stack"][1] + 4 * i))
    rig.run("FUN_00544f60", 0x544f60, build_f60, model_f60, 2, ret=False)

    def build_fa0(img, rng):
        """Kinds 0..7 (with stray high bits in ECX) against a random class byte."""
        put(img, REC2 + 0x6d, rng.randrange(256), 1)
        return {"ecx": (rng.randrange(1 << 24) << 8) | (rig.cases % 24 // 3), "edx": base + REC2,
                "regs": {"eax": rng.randrange(1 << 32)}}

    def model_fa0(m, args):
        """FUN_00544fa0: kind CL adds each state of its table (kinds 0, 1, 2, 4) to the player-data list."""
        kind = args["ecx"] & 0xff
        table = TABLES[kind] if kind < 5 else None
        if table is None:
            return
        m.call("pdata", args["regs"]["eax"])
        slot = m.r(base + PD + m.r(args["edx"] + 0x6d, 1) * 4 + 0x60)
        start, count = table
        for i in range(count - 1 if "table" in MUT else count):
            m.call("add", slot, rig.m.r32(start + 4 * i))
    rig.run("FUN_00544fa0", 0x544fa0, build_fa0, model_fa0, 0, ret=False)

    def collect(m, ctx, read, count):
        """FUN_00545090's body: record each state word `read(i)` the test accepts; returns (words, any)."""
        words, found = [], 0
        for i in range(count):
            word = read(i)
            if m.call("test", ctx, word):
                words.append(word & 0xffff)
                found = 1
        return words, found

    def build_090(img, rng):
        """Random state words and scripted acceptance bits for FUN_00545090."""
        count = rig.cases % 14 - 1
        pick["bits"] = [rng.randrange(2) for _ in range(12)]
        for i in range(13):
            put(img, ARR + 4 * i, rng.randrange(1 << 32))
        return {"regs": {"eax": base + 0x1e00}, "stack": [rng.randrange(1 << 32), base + ARR, count & 0xffffffff]}

    def model_090(m, args):
        """FUN_00545090: fill 24 bytes after EAX+2 with -1, store accepted words, return whether any."""
        out = args["regs"]["eax"]
        for i in range(5 if "fill" in MUT else 6):
            m.w(out + 2 + 4 * i, 0xffffffff)
        ctx, arr, raw = args["stack"]
        words, found = collect(m, ctx, lambda i: m.r(arr + 4 * i), max(rig_mod.s32(raw), 0))
        for i, word in enumerate(words):
            m.w(out + 2 + 2 * i, word, 2)
        return int(len(words) > 1) if "found" in MUT else found
    rig.run("FUN_00545090", 0x545090, build_090, model_090, 3, mask=0xff)

    def build_100(img, rng):
        """Kinds 0..7 with scripted state acceptance; the class byte picks a player-data slot."""
        put(img, REC2 + 0x6d, rng.randrange(256), 1)
        pick["bits"] = [rng.randrange(2) if rig.cases % 3 else 0 for _ in range(12)]
        return {"ecx": base + REC2, "edx": rng.randrange(1 << 32),
                "stack": [(rng.randrange(1 << 24) << 8) | (rig.cases % 24 // 3)]}

    def model_100(m, args):
        """FUN_00545100: for kinds 0, 1, 2, 4 send the 0x91 packet of accepted states when there is one."""
        kind = args["stack"][0] & 0xff
        m.call("pdata", args["edx"])
        slot = m.r(base + PD + m.r(args["ecx"] + 0x6d, 1) * 4 + 0x60)
        table = TABLES[kind] if kind < 5 else None
        if table is None:
            return
        words, found = collect(m, slot, lambda i: rig.m.r32(table[0] + 4 * i), table[1])
        if found or "send" in MUT:
            packet = bytearray(b"\xff" * 24)
            for i, word in enumerate(words):
                packet[2 * i:2 * i + 2] = word.to_bytes(2, "little")
            target = m.call("slot", args["edx"], CONST, 0xab7)
            m.call("send1a", target, bytes([0x91, kind + 1 if "kind" in MUT else kind]) + bytes(packet))
    rig.run("FUN_00545100", 0x545100, build_100, model_100, 1, ret=False)

    def build_530(img, rng):
        """Scripted flag answers, list count word and null/set unit; the count's high word is random and ignored."""
        index = rig.cases % 72
        pick["first"] = (1, 2, 0)[index % 3]
        pick["second"] = (0, 1, 7)[index // 3 % 3]
        put(img, REC + 4, base + UNIT)
        put(img, REC + 0xe0, rng.randrange(1 << 32))
        put(img, REC + 0x9c, (0, 3, 0x8000, 0xffff)[index // 9 % 4], 2)
        put(img, REC + 0x9e, rng.randrange(1 << 16), 2)
        put(img, UNIT + 0x6d, rng.randrange(256), 1)
        put(img, PARAM + 0xc, 0 if index // 36 else base + UNIT + 0x80)
        put(img, UNIT + 0x80 + 0xc, rng.randrange(1 << 32))
        return {"ecx": base + REC, "edx": base + PARAM}

    def model_530(m, args):
        """FUN_00545530: when flag bit 1 is exactly 1 and bit 0 nonzero and the list has entries, forward to FUN_00545240."""
        rec, param = args["ecx"], args["edx"]
        unit = m.r(param + 0xc)
        m.call("pdata", unit)
        pd_flags = m.r(base + PD + m.r(m.r(rec + 4) + 0x6d, 1) * 4 + 0x10)
        quest = m.r(rec + 0xe0)
        first = m.call("flag", pd_flags, quest, 1)
        if first != 1 and not ("gate1" in MUT and first == 2):
            return
        second = m.call("flag", pd_flags, quest, 0)
        if second == (1 if "gate2" in MUT else 0):
            return
        if m.r(rec + 0x9c, 2) == 0 and "list" not in MUT:
            return
        m.call("tail", rec + 0x1c, 0 if "tail" in MUT and not unit else (m.r(unit + 0xc) if unit else 0xffffffff))
    rig.custom["flag"] = lambda emu: pick["first"] if emu.arg(2) else pick["second"]
    for _ in range(3):
        rig.run("FUN_00545530", 0x545530, build_530, model_530, 0, ret=False)

    def build_units(img, rng):
        """A game, a unit and a player-data slot for the senders that read data +0x10."""
        put(img, GAME + 0x10f4, base + DATA)
        put(img, DATA + 0x20, rng.randrange(256), 1)
        put(img, GAME + 0x6d, rng.randrange(256), 1)
        return {"ecx": base + GAME, "edx": base + UNIT, "stack": [rng.randrange(1 << 32)]}

    def model_5b0(m, args):
        """FUN_005455b0: slot 0xca7 for the unit, then FUN_0053d670 with DL = 0x28 and (6, 0, flags, 0)."""
        m.call("pdata", args["edx"])
        pd_flags = m.r(base + PD + m.r(args["ecx"] + 0x6d, 1) * 4 + 0x10)
        target = m.call("slot", args["edx"], CONST, 0xca7)
        m.call("f53d670", target, (CONST & ~0xff) | (0x27 if "byte" in MUT else 0x28), 6, 0, pd_flags, 0)
    rig.run("FUN_005455b0", 0x5455b0, build_units, model_5b0, 0, ret=False)

    def model_6f0(m, args):
        """FUN_005456f0: as FUN_005455b0, then send the two bytes 0x89, data +0x20 through slot 0xd0e."""
        m.call("pdata", args["edx"])
        pd_flags = m.r(base + PD + m.r(args["ecx"] + 0x6d, 1) * 4 + 0x10)
        target = m.call("slot", args["edx"], CONST, 0xca7)
        m.call("f53d670", target, (CONST & ~0xff) | 0x28, 6, 0, pd_flags, 0)
        second = m.call("slot", args["edx"], CONST, 0xd0e)
        m.call("send2", second, bytes([0x8a if "byte89" in MUT else 0x89, m.r(m.r(args["ecx"] + 0x10f4) + 0x20, 1)]))
        return 0
    rig.run("FUN_005456f0", 0x5456f0, build_units, model_6f0, 1)

    def build_920(img, rng):
        """Owner null or set, level null or set, and ranks around the required byte with random high bits."""
        index = rig.cases % 24
        required = (0, 1, 0x7f, 0x80, 0xff, 0x40)[index // 4]
        low = (required + (-1, 0, 1, 0x80)[index % 4]) & 0xff
        pick["owner"] = 0 if index % 8 == 7 else 1
        pick["level"] = 0 if index % 12 == 11 else 1
        pick["rank"] = (rng.randrange(1 << 24) << 8) | low
        return {"ecx": rng.randrange(1 << 32), "edx": rng.randrange(1 << 32),
                "stack": [(rng.randrange(1 << 24) << 8) | required]}
    rig.custom["owner"] = lambda emu: pick["owner"]
    rig.custom["level"] = lambda emu: pick["level"]
    rig.custom["rank"] = lambda emu: pick["rank"]

    def model_920(m, args):
        """FUN_00545920: send 0x5d, DL, 0, 0xc, 0, 0 unless the unit's level rank byte is below the stack byte (unsigned byte compare)."""
        owner = m.call("owner", args["ecx"])
        if owner:
            level = m.call("level", owner)
            if not level:
                return
            rank = m.call("rank", level)
            if (rank < args["stack"][0] if "rank" in MUT else rank & 0xff < args["stack"][0] & 0xff):
                return
        target = m.call("slot", args["ecx"], CONST, 0xd82)
        m.call("send_5d", target, bytes([0x5d, args["edx"] & 0xff, 0, 0xc, 0, 0]))
    rig.run("FUN_00545920", 0x545920, build_920, model_920, 1, ret=False)

    rig.callee("call544520", 0x544520, 0, ("ecx", "edx"))

    def build_780(img, rng):
        """A game, a unit and an optional quest record whose first dword and +0xc are random."""
        put(img, GAME + 0x1c, rng.randrange(1 << 32))
        put(img, REC, rng.randrange(1 << 32))
        put(img, REC + 0xc, rng.randrange(1 << 32))
        return {"ecx": base + GAME, "edx": rng.randrange(1 << 32), "stack": [0 if rig.cases & 1 else base + REC]}

    def model_780(m, args):
        """FUN_00545780: allocate, raise event kind 0 on the new slot, send 0x27 with kind and id, release, resend flags."""
        game, unit, rec = args["ecx"], args["edx"], args["stack"][0]
        pool = m.call("alloc", m.r(game + 0x1c))
        slot = m.call("slot", unit, CONST, 0xd22)
        head = m.call("f5387c0", slot)
        m.call("event", head, 0, rec, unit, pool, 1, 1)
        ident = m.r(rec + 0xc) if rec else 0xffffffff
        kind = m.r(rec) & 0xff if rec else 6
        m.call("pack", pool)
        m.call("send6", slot, bytes([0x27, kind]) + ident.to_bytes(4, "little"))
        m.call("release", pool)
        m.call("call544520", game, 0 if "null" in MUT else slot)
    rig.run("FUN_00545780", 0x545780, build_780, model_780, 1, ret=False)
    return rig.cases


if __name__ == "__main__":
    main(__doc__, "quest packet", suite, MUT)
