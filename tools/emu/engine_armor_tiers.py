# SPDX-License-Identifier: GPL-3.0-or-later
"""Check GfxUtil armor-tier output FUN_004daab0 against stock game.exe 1.14d.

Run: uv run python engine_armor_tiers.py [--mutation-check]
The equipment, item usability and item-table lookups are hooked; only their
call arguments and this function's output behavior are checked. Synthetic
callee results may include combinations the real callees never return.
"""
import argparse
import itertools

import _dungeon_rig as rig

MUT = set()


def suite(test):
    """Exercise each combination of inventory, bypass, item and table state."""
    state = {}
    test._callee("equipped", 0x63bde0, 2, (), lambda _emu: state["item"])
    test._callee("usable", 0x62a4e0, 1, (), lambda _emu: state["usable"])
    test._callee("row", 0x6335f0, 1, (), lambda _emu: state["row"])
    cases = itertools.cycle(itertools.product((0, 1), repeat=5))
    item, row, output = test.slot(0), test.slot(1), test.slot(4)

    def build(img, _rng):
        """Set synthetic lookup outputs and independent input/output bytes."""
        inventory, bypass, have_item, usable, have_row = next(cases)
        state.update(item=item if have_item else 0, usable=usable, row=row if have_row else 0)
        img[4:8] = (123).to_bytes(4, "little")
        return {"edx": test.slot(5) if inventory else 0, "stack": [bypass, output]}

    def model(m, args):
        """Copy six row bytes, zero on an unusable item, or leave output intact."""
        inventory = args["edx"]
        if not inventory:
            return
        equipped = m.call("equipped", inventory, 3)
        if args["stack"][0] or not m.call("usable", equipped):
            for offset in range(6):
                m.w(output + offset, 1 if "zero" in MUT else 0, 1)
            return
        index = m.r(equipped + 4) if equipped else 0xffffffff
        record = m.call("row", index)
        if record:
            for offset in range(6):
                source = 0x116 + offset + (1 if "offset" in MUT else 0)
                m.w(output + offset, m.r(record + source, 1), 1)
        elif "missing" in MUT:
            m.w(output, 0, 1)

    test.run("FUN_004daab0", 0x4daab0, build, model, 2, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and optional mutation checks."""
    rig.ROUNDS = 128
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("zero", "offset", "missing")}
    rig.run_suite(suite, mutations, "ENGINE armor tiers", args.mutation_check)


if __name__ == "__main__":
    main()
