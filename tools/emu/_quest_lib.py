# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers for the quest_*.py oracles: not run by itself.

Small conveniences over _dungeon_rig: little-endian stores into the arena
image, a mutation switchboard, a hook that logs a callee which reads its
argument record through EDI, and the usual command line.
"""
import argparse

from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX,
                               UC_X86_REG_ESI)

import _dungeon_rig as rig_mod


def put(img, offset, value, size=4):
    """Store an unsigned little-endian integer at an arena offset."""
    img[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")


def record_hook(rig, name, addr, nstack, offsets, reg=UC_X86_REG_EDI, custom=None):
    """Hook a callee that takes a record pointer in `reg`; log its dword fields at `offsets` and stack args."""
    def hook(emu):
        """Log the record fields and stack arguments, return the case's scripted result."""
        pointer = emu.mu.reg_read(reg)
        fields = tuple(emu.r32(pointer + off) for off in offsets)
        stack = tuple(emu.arg(i) for i in range(nstack))
        rig.log.append((name,) + fields + stack)
        out = custom(emu) if custom else 0
        rig.outs.append(out & 0xffffffff)
        return out
    rig.m.hook(addr, hook, nstack)


REGS = {"eax": UC_X86_REG_EAX, "ebx": UC_X86_REG_EBX, "ecx": UC_X86_REG_ECX, "edx": UC_X86_REG_EDX,
        "esi": UC_X86_REG_ESI, "edi": UC_X86_REG_EDI}


def bytes_hook(rig, name, addr, nstack, regs, buf, size, nlog=None, custom=None):
    """Hook a callee; log registers `regs`, `size` bytes at register `buf`'s pointer, then stack args."""
    def hook(emu):
        """Log the registers, the buffer's bytes and the stack arguments."""
        values = tuple(emu.mu.reg_read(REGS[r]) for r in regs)
        data = bytes(emu.read(emu.mu.reg_read(REGS[buf]), size)) if buf else b""
        stack = tuple(emu.arg(i) for i in range(nstack if nlog is None else nlog))
        rig.log.append((name,) + values + ((data,) if buf else ()) + stack)
        out = custom(emu) if custom else 0
        rig.outs.append(out & 0xffffffff)
        return out
    rig.m.hook(addr, hook, nstack)


class Mutations:
    """The set of model defects switched on for one mutation run."""

    def __init__(self, *names):
        """Start with every defect in `names` off."""
        self.names = names
        self.on = set()

    def __contains__(self, name):
        """True when defect `name` is active."""
        return name in self.on

    def setter(self, name):
        """Function that switches defect `name` on or off."""
        return lambda flag: self.on.add(name) if flag else self.on.discard(name)


def main(doc, description, suite, mutations):
    """Run `suite` or, with --mutation-check, require each model defect in `mutations` to be caught."""
    parser = argparse.ArgumentParser(description=doc)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    rig_mod.run_suite(suite, {name: mutations.setter(name) for name in mutations.names},
                      description, args.mutation_check)
