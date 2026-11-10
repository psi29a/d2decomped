# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Gfx.cpp record allocation FUN_0046ebb0 against stock 1.14d.

Run: uv run python engine_gfx_alloc.py [--mutation-check]
The pool allocator is hooked and can fail; its own behavior is not claimed.
The returned block starts nonzero so that zero-initialization is observable.
"""
import argparse

import _dungeon_rig as rig

MUT = set()


def suite(test):
    """Compare successful allocation and fatal allocation failure."""
    fail = [False]

    def alloc(emu):
        """Provide a dirty 0x3c-byte block or a synthetic allocation failure."""
        if fail[0]:
            return 0
        pointer = emu.alloc(0x3c)
        emu.mu.mem_write(pointer, b'\x55' * 0x3c)
        return pointer

    test._callee("pool", 0x40b380, 2, ("ecx", "edx"), alloc)

    def build(_img, _rng):
        """Alternate allocator success and failure."""
        fail[0] = test.cases % 2 == 0
        return {}

    def model(m, _args):
        """Initialize the record to zero with the two defaults at its end."""
        pointer = m.call("pool", 0x3c, 0x6d6c1c, 0x804, 0)
        if not pointer:
            raise rig.Halt(0x804 if "fatal" in MUT else 0x805)
        block = bytearray(0x3c)
        if "zero" in MUT:
            block[0] = 0x55
        block[0x34:0x38] = (1 if "default" in MUT else 2).to_bytes(4, "little")
        block[0x38:0x3c] = (2).to_bytes(4, "little")
        m.blocks.append((pointer, block))
        return pointer

    test.run("FUN_0046ebb0", 0x46ebb0, build, model, 0)
    return test.cases


def main():
    """Run the stock oracle or check representative model defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("fatal", "zero", "default")}
    rig.run_suite(suite, mutations, "ENGINE graphics allocation", args.mutation_check)


if __name__ == "__main__":
    main()
