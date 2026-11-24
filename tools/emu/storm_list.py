# SPDX-License-Identifier: GPL-3.0-or-later
"""Storm's intrusive list helpers on local game.exe 1.14d, against a model.

Run: uv run python storm_list.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_0041c560 FUN_0041c630 FUN_0041c690 FUN_0041c700 FUN_0041c770 FUN_0041c7b0 FUN_0041c8a0 FUN_004203a0 FUN_004203f0 FUN_00420520 FUN_00420540 FUN_004205f0 FUN_00420650 FUN_004206e0 FUN_00420750 FUN_00420790 FUN_00420820 FUN_00420880

Each case builds a well-formed list in emulator memory: the list (`+0` the
link offset, `+4` its terminator link: `+4` the last link, `+8` the first
node, `~terminator` when empty) and 0 to 6 nodes whose links (`+0` the
previous link, `+4` the next node, or `~terminator` after the last) sit at
the link offset in each node. One helper then runs on game.exe (`ECX` the
list, a node or a link) and on the model, and the arena's bytes, the return
value, the SMemAlloc / SMemFree calls (hooked) and the stack arguments
popped are compared. `--mutation-check` plants
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
FREE, GLOBAL, ALLOC = 0x412650, 0x7792F4, 0x413020
NEW_NODE_AT = 0x300   # where the hooked SMemAlloc puts a new node in the arena
# address: stack dwords popped (the RET)
POPS = {0x41C560: 1, 0x41C840: 3, 0x420540: 3, 0x420820: 3, 0x41C630: 1, 0x41C690: 1, 0x41C700: 1, 0x41C770: 0, 0x41C7B0: 0, 0x41C8A0: 1, 0x4203A0: 1, 0x4203F0: 1, 0x420520: 1, 0x4205F0: 1, 0x420650: 1, 0x4206E0: 1, 0x420750: 0, 0x420790: 0, 0x420880: 1}

TWINS = {   # a second copy of the same code: address: its twin above
    0x41C560: 0x420520,
    0x41C630: 0x4205F0,
    0x41C690: 0x420650,
    0x41C700: 0x4206E0,
    0x41C770: 0x420750,
    0x41C7B0: 0x420790,
    0x41C8A0: 0x420880,
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


def insert(mem, lst, node, mode, anchor, m=None):
    """Links node (its own link, offset 0) after anchor for mode 1, else before it."""
    link = node or lst + 4
    if mem.r(link):
        unlink(mem, link)
    anchor = anchor or lst + 4
    if mode - 1 & M32 and m != "insert_always_after":
        prev = mem.r(anchor)
        mem.w(link, prev)
        mem.w(link + 4, mem.r(prev + 4))
        mem.w(prev + 4, node)
        mem.w(anchor, link)
        return
    mem.w(link, anchor)
    mem.w(link + 4, mem.r(anchor + 4))
    nxt = mem.r(anchor + 4)
    if signed(nxt) < 0:
        next_link = ~nxt & M32
    else:
        offset = mem.r(lst)
        if signed(offset) < 0:
            offset = anchor - mem.r(mem.r(anchor) + 4)
        next_link = (nxt + offset) & M32
    mem.w(next_link, link)
    mem.w(anchor + 4, node)


class Model:
    def __init__(self, mutation=None):
        self.m = mutation
        self.allocs = []

    def run(self, address, mem, ecx, args):
        """Returns (EAX or None, freed calls, the global)."""
        m, freed, glob = self.m, [], None
        tag = 0x708434 if address == 0x41C840 else 0x708850   # the SMemAlloc tag of FUN_0041c840 / FUN_00420820
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
        if address == 0x420540:
            insert(mem, ecx, args[0], args[1], args[2], m)
            return None, freed, glob
        if address == 0x420650:
            unlink(mem, args[0] or ecx + 4, m)
            return None, freed, glob
        if address == 0x4206E0:
            return (mem.r(args[0] + 4 * (m != "raw_next_prev")) if args[0] else mem.r(ecx + 8)), freed, glob
        if address in (0x420750, 0x420790):
            while signed(mem.r(ecx + 8)) > 0:
                unlink(mem, mem.r(ecx + 8))
                if m == "clear_one":
                    break
            if address == 0x420790 and m != "destroy_keeps_terminator":
                unlink(mem, ecx + 4)
            return None, freed, glob
        if address == 0x420820:
            mode, size, flags = args
            self.allocs.append(((size + 0xC + 4 * (m == "new_size_plus_8")) & M32, tag, 0xFFFFFFFE, flags | 8))
            node = self.base + NEW_NODE_AT
            mem.w(node, 0)
            mem.w(node + 4, 0)
            if mode:
                insert(mem, ecx, node, mode, 0)
            return node, freed, glob
        if address == 0x420880:
            unlink(mem, ecx)
            if args[0] & 1 and m != "delete_never_frees":
                freed.append((ecx, 0x6CC824, M32, 0))
            return ecx, freed, glob
        raise AssertionError(address)


MUTATIONS = {
    0x420540: ["insert_always_after"],
    0x420820: ["new_size_plus_8"],
    0x41C560: ["link_or_wrong"],
    0x41C630: ["next_node_unclamped"],
    0x41C690: ["unlink_keeps_link"],
    0x41C700: ["raw_next_prev"],
    0x41C770: ["clear_one"],
    0x41C7B0: ["destroy_keeps_terminator"],
    0x41C8A0: ["delete_never_frees"],
    0x4203A0: ["set_returns_0"],
    0x4203F0: ["next_link_ignores_offset"],
    0x420520: ["link_or_wrong"],
    0x4205F0: ["next_node_unclamped"],
    0x420650: ["unlink_keeps_link"],
    0x4206E0: ["raw_next_prev"],
    0x420750: ["clear_one"],
    0x420790: ["destroy_keeps_terminator"],
    0x420880: ["delete_never_frees"],
}


class Rig:
    def __init__(self, exe):
        self.machine = emu.Emu(exe)
        self.base = self.machine.alloc(ARENA)
        self.freed = []
        self.machine.hook(FREE, lambda e: self.freed.append(tuple(e.arg(i) for i in range(4))) or 1, 4)
        self.machine.hook(ALLOC, lambda e: self.allocs.append(tuple(e.arg(i) for i in range(4))) or self.base + NEW_NODE_AT, 4)

    def run(self, address, data, ecx, args):
        machine = self.machine
        machine.mu.mem_write(self.base, bytes(data))
        machine.w32(GLOBAL, 0xDEADBEEF)
        self.freed, self.allocs = [], []
        eax = machine.call(address, *args, ecx=ecx, limit=100_000)
        if machine.mu.reg_read(UC_X86_REG_EIP) != emu.RET_MAGIC:
            raise SystemExit(f"FUN_{address:08x} did not return within the budget")
        popped = (machine.mu.reg_read(UC_X86_REG_ESP) - (emu.STACK + emu.STACK_SIZE - 0x1000) + 4 * len(args)) // 4
        return machine.read(self.base, ARENA), eax, list(self.freed) + list(self.allocs), machine.r32(GLOBAL), popped


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
    offset = 0 if address in (0x420540, 0x420750, 0x420790, 0x420820, 0x420880) else rng.choice((0, 4, 8, 0xC, 0x10))
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
    elif address == 0x420540:
        fresh = base + 0x30
        mem.w(fresh, 0)
        node = rng.choice(nodes + [fresh]) if nodes and rng.random() < 0.6 else fresh
        anchor = rng.choice([n for n in nodes if n != node] + [0])
        args = [node, rng.choice((0, 1, 1, 2, rng.getrandbits(32))), anchor]
    elif address == 0x420820:
        args = [rng.choice((0, 1, 2, rng.getrandbits(32))), rng.randint(0, 0x40), rng.choice((0, 8, rng.getrandbits(32)))]
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
    model.base = rig.base
    for number in range(count):
        for address in sorted(MUTATIONS) if only is None else [only]:
            mem, ecx, args = case(rng, rig.base, address)
            data = bytes(mem.data)
            got_data, eax, freed, glob, popped = rig.run(address, data, ecx, args)
            model.allocs = []
            want_eax, want_freed, want_glob = model.run(address, mem, ecx, args)
            want_freed = want_freed + model.allocs
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
