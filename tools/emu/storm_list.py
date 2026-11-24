# SPDX-License-Identifier: GPL-3.0-or-later
"""Storm's intrusive list helpers on local game.exe 1.14d, against a model.

Run: uv run python storm_list.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_004203a0 FUN_004203f0 FUN_00420520 FUN_004205f0 FUN_00420650

Each case builds a well-formed list in emulator memory: the list (`+0` the
link offset, `+4` its terminator link: `+4` the last link, `+8` the first
node, `~terminator` when empty) and 0 to 6 nodes whose links (`+0` the
previous link, `+4` the next node, or `~terminator` after the last) sit at
the link offset in each node. One helper then runs on game.exe (`ECX` the
list, a node or a link) and on the model, and the arena's bytes, the return
value and the stack arguments popped are compared. `--mutation-check` plants
defects in the model and requires each to be caught. Checks game.exe only:
d2d has no counterpart (source-list.md).
"""
import argparse
import hashlib
import random
import struct
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
M32 = 0xFFFFFFFF
ARENA = 0x400
FREE, GLOBAL = 0x412650, 0x7792F4
# address: stack dwords popped (the RET)
POPS = {0x41C560: 1, 0x41C630: 1, 0x41C690: 1, 0x41C700: 1, 0x41C770: 0, 0x41C7B0: 0, 0x41C8A0: 1, 0x4203A0: 1, 0x4203F0: 1, 0x420520: 1, 0x4205F0: 1, 0x420650: 1, 0x4206E0: 1, 0x420750: 0, 0x420790: 0, 0x420880: 1}

TWINS = {   # a second copy of the same code: address: its twin above
}


def signed(value):
    return value - (1 << 32) if value & 0x80000000 else value


class Memory:
    """Dwords of the arena, by address."""

    def __init__(self, base, data):
        self.base, self.data = base, bytearray(data)

    def r(self, address):
        return struct.unpack_from("<I", self.data, address - self.base)[0]

    def w(self, address, value):
        struct.pack_into("<I", self.data, address - self.base, value & M32)


def unlink(mem, link, m=None):
    """Takes a link out of its list (nothing when its previous link is 0)."""
    prev = mem.r(link)
    if not prev:
        return
    nxt = mem.r(link + 4)
    if signed(nxt) < 0:
        next_link = ~nxt & M32
    else:
        next_link = (nxt + link - mem.r(prev + 4)) & M32
    mem.w(next_link, prev)
    mem.w(prev + 4, mem.r(link + 4))
    if m != "unlink_keeps_link":
        mem.w(link, 0)
        mem.w(link + 4, 0)


class Model:
    def __init__(self, mutation=None):
        self.m = mutation

    def run(self, address, mem, ecx, args):
        """Returns (EAX or None, freed calls, the global)."""
        m, freed, glob = self.m, [], None
        address = TWINS.get(address, address)
        if address == 0x4203A0:
            return (0 if m == "set_returns_0" else 1), freed, args[0]
        if address == 0x4203F0:
            nxt = mem.r(ecx + 4)
            if signed(nxt) < 0:
                return ~nxt & M32, freed, glob
            offset = args[0]
            if signed(offset) < 0 and m != "next_link_ignores_offset":
                offset = ecx - mem.r(mem.r(ecx) + 4)
            return (nxt + offset) & M32, freed, glob
        if address == 0x420520:
            return (args[0] or (ecx + 4 + 4 * (m == "link_or_wrong"))) & M32, freed, glob
        if address == 0x4205F0:
            link = args[0] or ecx + 4
            nxt = mem.r(link + 4)
            return (nxt if signed(nxt) > 0 or m == "next_node_unclamped" else 0), freed, glob
        if address == 0x420650:
            unlink(mem, args[0] or ecx + 4, m)
            return None, freed, glob
        raise AssertionError(address)


MUTATIONS = {
    0x4203A0: ["set_returns_0"],
    0x4203F0: ["next_link_ignores_offset"],
    0x420520: ["link_or_wrong"],
    0x4205F0: ["next_node_unclamped"],
    0x420650: ["unlink_keeps_link"],
}


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.base = self.machine.alloc(ARENA)
        self.freed = []
        self.machine.hook(FREE, lambda e: self.freed.append(tuple(e.arg(i) for i in range(4))) or 1, 4)

    def run(self, address, data, ecx, args):
        machine = self.machine
        machine.mu.mem_write(self.base, bytes(data))
        machine.w32(GLOBAL, 0xDEADBEEF)
        self.freed = []
        eax = machine.call(address, *args, ecx=ecx, limit=100_000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise SystemExit(f"FUN_{address:08x} did not return within the budget")
        popped = (machine.mu.reg_read(UC_X86_REG_ESP) - (emu.STACK + emu.STACK_SIZE - 0x1000) + 4 * len(args)) // 4
        return machine.read(self.base, ARENA), eax, list(self.freed), machine.r32(GLOBAL), popped


def build(rng, base, offset):
    """A list at base with 0 to 6 nodes at shuffled slots; returns memory, nodes."""
    mem = Memory(base, bytes(rng.randint(0, 255) for _ in range(ARENA)))
    lst = base
    term = lst + 4
    slots = rng.sample(range(8), rng.randint(0, 6))
    nodes = [base + 0x40 + 0x60 * s for s in slots]
    mem.w(lst, offset)
    links = [term] + [n + offset for n in nodes]
    for i, link in enumerate(links):
        prev = links[i - 1]
        nxt = nodes[i] if i < len(nodes) else ~term & M32
        mem.w(link, prev)
        mem.w(link + 4, nxt)
    return mem, nodes


def case(rng, base, address):
    address = TWINS.get(address, address)
    offset = 0 if address in (0x420750, 0x420790, 0x420880) else rng.choice((0, 4, 8, 0xC, 0x10))
    mem, nodes = build(rng, base, offset)
    links = [n + offset for n in nodes]
    ecx, args = base, []
    if address == 0x4203A0:
        args = [rng.getrandbits(32)]
    elif address == 0x4203F0:
        ecx = rng.choice(links + [base + 4])
        args = [rng.choice((offset, M32, 0x80000000 | rng.getrandbits(31)))]
    elif address in (0x420520, 0x4205F0, 0x4206E0):
        args = [rng.choice(links + [0, 0]) if links else 0]
    elif address == 0x420650:
        if links and rng.random() < 0.9:
            args = [rng.choice(links)]
        else:
            free = base + 0x30
            mem.w(free, 0)
            args = [free]
    elif address == 0x420880:
        if nodes and rng.random() < 0.8:
            ecx = rng.choice(nodes)
        else:
            ecx = base + 0x30
            mem.w(ecx, 0)
        args = [rng.choice((0, 1, 2, 3, rng.getrandbits(32)))]
    return mem, ecx, args


def compare(rig, seed, count, mutation=None, only=None):
    """Runs the cases on game.exe and the model; returns the first difference or None."""
    rng = random.Random(seed)
    model = Model(mutation)
    for number in range(count):
        for address in sorted(MUTATIONS) if only is None else [only]:
            mem, ecx, args = case(rng, rig.base, address)
            data = bytes(mem.data)
            got_data, eax, freed, glob, popped = rig.run(address, data, ecx, args)
            want_eax, want_freed, want_glob = model.run(address, mem, ecx, args)
            got = (got_data, eax if want_eax is not None else None, freed, glob if want_glob is not None else None, popped)
            want = (bytes(mem.data), want_eax, want_freed, want_glob, POPS[address])
            if got != want:
                where = next((i for i in range(ARENA) if got_data[i] != mem.data[i]), None)
                return f"case {number} FUN_{address:08x} ecx {ecx:#x} args {args}: eax {got[1]} != {want_eax}, freed {freed} != {want_freed}, popped {popped}, first byte off {where}"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, help="game.exe 1.14d (default: bin/game.exe in the data folder emu.py finds)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit(f"{exe} is not game.exe 1.14d")
    rig = Rig(str(exe))
    difference = compare(rig, args.seed, args.count)
    if difference:
        raise SystemExit(f"FAIL {difference}")
    print(f"ok: {args.count} cases each of {len(MUTATIONS)} helpers")
    if args.mutation_check:
        for address, mutations in MUTATIONS.items():
            for mutation in mutations:
                caught = compare(rig, args.seed, args.count, mutation, address)
                if not caught:
                    raise SystemExit(f"FAIL mutation {mutation} not caught")
                print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
