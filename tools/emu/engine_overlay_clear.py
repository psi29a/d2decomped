# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Gfx.cpp overlay list clearing FUN_0046f170 in stock game.exe 1.14d.

Run: uv run python engine_overlay_clear.py [--mutation-check]
Removal is hooked and simulates destroying each node's next pointer. The
oracle checks traversal, arguments and fatal behavior, not the real removal.
"""
import argparse

import _dungeon_rig as rig

MUT = set()


def suite(test):
    """Exercise empty and populated lists, including missing graphics."""
    unit, gfx = test.slot(0), test.slot(1)
    nodes = [test.slot(i) for i in (2, 3, 4)]

    def remove(emu):
        """Destroy the next pointer as a stand-in for freeing the node."""
        emu.w32(emu.arg(1) + 0xa4, 0)
        return 0

    test._callee("remove", 0x46e040, 2, (), remove)

    def put(img, off, value):
        """Store a dword in the arena image."""
        img[off:off + 4] = value.to_bytes(4, "little")

    def build(img, _rng):
        """Cycle through absent graphics and lists with zero to three nodes."""
        index = test.cases % 5
        put(img, 0x54, 0 if index == 0 else gfx)
        count = max(index - 1, 0)
        put(img, 0x12c, nodes[0] if count else 0)
        for i, node in enumerate(nodes):
            put(img, node - test.arena + 0xa4, nodes[i + 1] if i + 1 < count else 0)
        return {"ecx": unit}

    def model(m, args):
        """Capture next before each removal, continuing until the list ends."""
        graphics = m.r(args["ecx"] + 0x54)
        if not graphics:
            raise rig.Halt(0xccf if "fatal" not in MUT else 0xcce)
        node = m.r(graphics + 0x2c)
        while node:
            next_node = m.r(node + 0xa4)
            m.call("remove", args["ecx"], node + (4 if "pointer" in MUT else 0))
            m.w(node + 0xa4, 0)
            node = 0 if "one" in MUT else next_node

    test.run("FUN_0046f170", 0x46f170, build, model, 0, ret=False)
    return test.cases


def main():
    """Run the oracle or its intentional model defects."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("fatal", "pointer", "one")}
    rig.run_suite(suite, mutations, "ENGINE overlay clear", args.mutation_check)


if __name__ == "__main__":
    main()
