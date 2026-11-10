# SPDX-License-Identifier: GPL-3.0-or-later
"""Check Gfx.cpp overlay removal selection FUN_0046f0c0 in stock 1.14d.

Run: uv run python engine_overlay_remove.py [--mutation-check]
The node-removal callee is hooked: only its selected node and arguments are
checked, not its implementation. Synthetic overlay rows and lists are used.
"""
import argparse

import _dungeon_rig as rig

MUT = set()


def suite(test):
    """Check missing graphics, invalid rows and matching overlay nodes."""
    test.callee("remove", 0x46e040, 2, ())
    unit, gfx = test.slot(0), test.slot(1)
    nodes = [test.slot(i) for i in (2, 3, 4)]
    header, rows = test.arena + 0x1000, test.arena + 0x2000
    test.m.w32(0x744304, header)

    def put(img, off, value):
        """Write an arena dword."""
        img[off:off + 4] = (value & rig.M32).to_bytes(4, "little")

    def build(img, rng):
        """Make a bounded list with multiple possible matching fields."""
        put(img, 0x54, gfx if rng.randrange(4) else 0)
        put(img, 0x12c, nodes[0] if rng.randrange(4) else 0)
        put(img, 0x1bbc, rows)
        put(img, 0x1bc0, 4)
        for i, node in enumerate(nodes):
            off = node - test.arena
            for j, value in enumerate((rng.choice((0, 4, 9)), rng.randrange(5),
                                       rng.randrange(5), rng.randrange(5))):
                put(img, off + 4 * j, value)
            put(img, off + 0xa4, nodes[i + 1] if i < 2 else 0)
        return {"ecx": unit if rng.randrange(5) else 0,
                "stack": [rng.choice((0xffffffff, 0, 1, 2, 3, 4))]}

    def model(m, args):
        """Reject invalid rows and remove the first matching overlay."""
        owner, row = args["ecx"], args["stack"][0]
        if not owner or not m.r(owner + 0x54):
            return
        if rig.s32(row) < 0 or rig.s32(row) >= 4:
            raise rig.Halt(0xca9)
        node = m.r(m.r(owner + 0x54) + 0x2c)
        while node:
            kind = m.r(node)
            match = m.r(node + 4) == row
            if kind in (4, 9):
                match |= m.r(node + 8) == row or (m.r(node + 12) == row and "third" not in MUT)
            if match:
                m.call("remove", owner, node + (4 if "node" in MUT else 0))
                if "continue" not in MUT:
                    break
            node = m.r(node + 0xa4)

    test.run("FUN_0046f0c0", 0x46f0c0, build, model, 1, ret=False)
    return test.cases


def main():
    """Run comparisons and optional mutation checks."""
    rig.ROUNDS = 256
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("third", "node", "continue")}
    rig.run_suite(suite, mutations, "ENGINE overlay selection", args.mutation_check)


if __name__ == "__main__":
    main()
