# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_0058e120's prisoner packet against stock game.exe 1.14d.

Run: python quest_prisoner_packet.py [--mutation-check]
Client and quest lookups and the packet sender are hooked; the sender's
five-byte stack packet is read at entry, not sent over a network.
"""
import argparse

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import _dungeon_rig as rig_mod

MUTATIONS = set()
REC, DATA, GAME, PLAYER, CLIENT = 0x100, 0x200, 0x400, 0x500, 0x600


def suite(rig):
    """Compare packet contents, lookup arguments, and stack cleanup."""
    base = rig.arena
    rig.callee("client", 0x5531c0, 1, ("ecx", "edx"))
    rig.callee("quest", 0x543640, 0, ("ecx", "edx"))
    rig.custom["client"] = lambda emu: base + CLIENT
    rig.custom["quest"] = lambda emu: base + REC

    def packet(emu):
        """Log client and five bytes of the local packet at sender entry."""
        client = emu.mu.reg_read(UC_X86_REG_ECX)
        pointer = emu.mu.reg_read(UC_X86_REG_EDX)
        payload = emu.read(pointer, 5)
        rig.log.append(("send", client, *payload))
        rig.outs.append(0)
        return 0

    rig.m.hook(0x53d7e0, packet, 0)

    def build(img, rng):
        """Put a random count in the quest record's data block."""
        img[REC + 0x18:REC + 0x1c] = (base + DATA).to_bytes(4, "little")
        count = rng.choice((0, 1, 0xffff, rng.randrange(0x10000)))
        img[DATA + 0xa4:DATA + 0xa6] = count.to_bytes(2, "little")
        return {"ecx": base + PLAYER, "regs": {"esi": base + GAME}}

    def model(m, args):
        """Build 0x50, quest 0x24 and the little-endian prisoner count."""
        client = m.call("client", args["ecx"], 0x6e1af0, 0x243)
        record = m.call("quest", args["regs"]["esi"], 0x24)
        count = m.r(m.r(record + 0x18) + 0xa4, 2)
        m.call("send", client, 0x51 if "opcode" in MUTATIONS else 0x50,
               0x25 if "quest" in MUTATIONS else 0x24, 0, count & 0xff,
               0 if "high" in MUTATIONS else count >> 8)

    rig.run("FUN_0058e120", 0x58e120, build, model, 0, ret=False)
    return rig.cases


def mutation(name):
    """Set or clear a deliberate model defect."""
    return lambda on: MUTATIONS.add(name) if on else MUTATIONS.discard(name)


def main():
    """Run comparisons or demonstrate detection of deliberate defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutation(name) for name in ("opcode", "quest", "high")},
                      "quest prisoner packet", args.mutation_check)


if __name__ == "__main__":
    main()
