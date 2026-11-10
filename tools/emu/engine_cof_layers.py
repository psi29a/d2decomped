# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the monster mode COF preload FUN_0046f650 in stock 1.14d.

Run: uv run python engine_cof_layers.py [--mutation-check]
FUN_0046f650 (EBX request, stack: MonStats row, mode, flag) builds the
monster's COF path (FUN_0046f490), finds or loads the entry in the player
cache 0x7a78f0 (fatal 0xec3), checks the entry data (0xec9), its header
(FUN_004db040, 0xecb) and its layer order table (FUN_004db110, 0xed0), then
for each COF layer in order (a layer id of 16 or more is fatal 0xed7) fills a
request with FUN_004dbcf0 for each of the request's weapon-class count (byte
+3; one fill with 0 when it is 0) and starts the cel load: FUN_006001f0 (1, 1
and FUN_006004e0 when the load is not done, when the flag is set; 0, 0
otherwise). The path builder, the cache find / load, the header and order
getters, the request fill and the cel calls are hooked. The claim: the
lookup order, the fatal lines, the layer loop, the argument lists of the fill
and the cel calls and the bytes popped. (The player cache is used for a
monster's COF: stock does that.)
"""
import argparse

import _dungeon_rig as rig
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESI

MUT = set()


def suite(test):
    """Run FUN_0046f650 over every branch."""
    sel = {"locals": []}
    request, entry, header, order, row = (test.slot(i) for i in range(5))

    def path(e):
        """FUN_0046f490: note where the token goes, write it."""
        sel["tokout"] = e.mu.reg_read(UC_X86_REG_ESI)
        sel["pbuf"] = e.arg(1)
        e.w32(sel["tokout"], sel["token"])
        return 0

    def fill(e):
        """FUN_004dbcf0: note the request buffer, give the next outcome."""
        sel["locals"].append(e.mu.reg_read(UC_X86_REG_ECX))
        return sel["fills"].pop(0)

    test._callee("path", 0x46F490, 2, ("eax", "esi"), path, nlog=2)
    test._callee("find", 0x46DC40, 2, (), lambda _e: sel["found"], nlog=1)
    test._callee("loadfile", 0x46DCC0, 1, ("eax",), lambda _e: sel["loaded"], nlog=1)
    test._callee("header", 0x4DB040, 0, ("ecx",), lambda _e: sel["header"])
    test._callee("order", 0x4DB110, 1, ("ecx", "edx"), lambda _e: sel["order"])
    test._callee("fill", 0x4DBCF0, 8, ("ecx", "edx"), fill)
    test._callee("cel", 0x6001F0, 3, (), lambda _e: sel["cel"].pop(0))
    test.callee("celfree", 0x6004E0, 1, ())

    def build(img, rng):
        """Random rows, layers and outcomes."""
        sel.update(token=rng.randrange(1 << 32), found=rng.choice((0, entry)), loaded=rng.choice((0, entry, entry)),
                   header=header if rng.randrange(8) else 0, order=order if rng.randrange(8) else 0, locals=[])
        sel["fills"] = [rng.choice((0, 1, 1)) for _ in range(12)]
        sel["cel"] = [rng.choice((0, 1)) for _ in range(12)]
        img[entry - test.arena + 0x14:entry - test.arena + 0x18] = rng.choice((0, 0x3000 + test.arena)).to_bytes(4, "little")
        img[header - test.arena] = rng.choice((0, 1, 2, 3))
        for i in range(4):
            img[order - test.arena + i] = rng.choice((0, 1, 5, 0xF, 0xF, 0x10, 0x80))
        img[request - test.arena + 3] = rng.choice((0, 1, 2, 3))
        return {"regs": {"ebx": request}, "stack": [row, rng.randrange(1 << 32), rng.choice((0, 1))]}

    def model(m, args):
        """Preload every layer of the mode's COF."""
        mode, flag = args["stack"][1], args["stack"][2]
        cls = m.r(request, 2)
        cls = cls - 0x10000 if cls & 0x8000 else cls
        m.call("path", cls & rig.M32, sel["tokout"], mode, sel["pbuf"])
        found = m.call("find", 0x7A78F0 if "cache" not in MUT else 0x7A8100)
        if not found:
            found = m.call("loadfile", sel["pbuf"], 0x7A78F0)
            if not found:
                raise rig.Halt(0xEC3)
        data = m.r(found + 0x14)
        if not data:
            raise rig.Halt(0xEC9)
        hdr = m.call("header", data)
        if not hdr:
            raise rig.Halt(0xECB)
        ords = m.call("order", data, 0, 0)
        if not ords:
            raise rig.Halt(0xED0)
        total = m.r(hdr, 1)
        count = m.r(request + 3, 1)
        calls = [0]

        def one(layer, step, state):
            """One request fill and its cel call."""
            rig.require(calls[0] < len(sel["locals"]), "model made more fills than stock")
            local = sel["locals"][calls[0]]
            calls[0] += 1
            if m.call("fill", local, 0, 0, layer, mode, sel["token"], found, request, args["stack"][0], step):
                if flag:
                    if not m.call("cel", local, 1, 1 if "cel" not in MUT else 0):
                        m.call("celfree", local)
                else:
                    m.call("cel", local, 0, 0)
        for k in range(total):
            layer = m.r(ords + k, 1)
            if layer >= (0x11 if "layer" in MUT else 0x10):
                raise rig.Halt(0xED7)
            if count:
                for step in range(count):
                    one(layer, step, None)
            else:
                one(layer, 0, None)
    test.run("FUN_0046f650", 0x46F650, build, model, 3, ret=False)
    return test.cases


def main():
    """Run the stock comparisons and the planted-defect check."""
    rig.ROUNDS = 800
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    mutations = {name: (lambda on, name=name: MUT.add(name) if on else MUT.discard(name))
                 for name in ("cache", "layer", "cel")}
    rig.run_suite(suite, mutations, "ENGINE COF layers", args.mutation_check)


if __name__ == "__main__":
    main()
