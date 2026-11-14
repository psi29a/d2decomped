# SPDX-License-Identifier: GPL-3.0-or-later
"""Bound oracle calls and reject runs that stop before the return sentinel.

Use CheckedEmu in place of emu.Emu when each top-level call should return;
pass an explicit limit to override the default instruction budget.
"""
import emu
from unicorn.x86_const import UC_X86_REG_EIP


class CheckedEmu(emu.Emu):
    """An emulator that treats an exhausted instruction budget as failure."""

    def call(self, addr, *args, limit=0, **kwargs):
        """Run a bounded call and verify execution reached its return address."""
        result = super().call(addr, *args, limit=limit or 20_000_000, **kwargs)
        instruction = self.mu.reg_read(UC_X86_REG_EIP)
        if instruction != emu.RET_MAGIC:
            raise RuntimeError(f'{addr:#x} did not return (stopped at {instruction:#x})')
        return result
