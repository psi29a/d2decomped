# SPDX-License-Identifier: GPL-3.0-or-later
"""Storm's event-handler register, SEVT.CPP: FUN_00426420 with its body LAB_0042644a, against a model.

Run: .venv/bin/python storm_event_register.py [--exe /path/to/game.exe] [--mutation-check]

# gaps-runs: FUN_00426420 FUN_0042644a

FUN_00426420(tag, window, code, flags, handler) (`RET 0x14`) is one function
from 0x426420 to 0x42670e; Ghidra splits it at 0x42644a (the checked body),
so `FUN_0042644a` in game-functions.tsv is a label inside it, not an entry.
Each case registers a random run of handlers on game.exe's own code with
the allocator, the free and the critical section hooked or stubbed, then
compares, with a model written from the disassembly: every return value,
the ordered SMemAlloc / SMemFree calls (size, source line, flags, and which
earlier block a free takes back), `DAT_007790f4` (the last error), `DAT_007792f8`
(the dispatcher's "handlers changed" flag), and the whole table walked out
of emulator memory: the node hash (`0x779300` buckets, `0x779304` size,
`0x779308` count; node = tag, window, handler counter, handler table, next)
and each node's handler table (buckets, size, count; record = code,
sequence, handler, next), chain by chain, in order. Checks game.exe only:
d2d keeps no Storm event table (storm-file.md).
"""
import argparse
import hashlib
import random
from pathlib import Path

import emu

SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
M32 = 0xFFFFFFFF
REGISTER, ALLOC, FREE = 0x426420, 0x413020, 0x412650
NODES, SIZE, COUNT, DIRTY, LAST_ERROR = 0x779300, 0x779304, 0x779308, 0x7792F8, 0x7790F4
FILE = ".\\SOURCE\\SEVT.CPP"


def power_above(value):
    """The loop at 0x42648b / 0x42659d: 1 doubled while it is <= value (an unsigned 32-bit compare)."""
    result = 1
    value &= M32
    if value >= result:
        while True:
            result = result * 2 & M32
            if result == 0 or result > value:
                break
    return result


class Model:
    """FUN_00426420 in Python: blocks are numbered in allocation order, as the log records them."""

    def __init__(self, mutation=None):
        self.m = mutation
        self.log = []          # ('alloc', id, size, line, flags) / ('free', id, line, flags)
        self.next_id = 0
        self.size = self.count = 0
        self.buckets = None    # (id, [chain, ...]); a chain is a list of nodes, head first
        self.dirty = 0
        self.last_error = None

    def alloc(self, size, line, flags=8):
        """SMemAlloc: logged, numbered."""
        self.next_id += 1
        self.log.append(("alloc", self.next_id, size, line, flags))
        return self.next_id

    def free(self, ident, line):
        """SMemFree: logged by the block it takes back."""
        self.log.append(("free", ident, line, 0))

    def register(self, tag, window, code, flags, handler):
        """Returns EAX; the table is the model's own."""
        m = self.m
        if not (handler != 0 and (flags == 0 or m == "ignore_flags")):
            self.last_error = 0x57
            return 0
        node = self.lookup(tag, window)
        if node is None:
            if self.size >> 1 <= self.count:
                size = power_above(2 * self.count + 2)
                ident = self.alloc(size * 4, 0x143)
                chains = [[] for _ in range(size)]
                if self.buckets:
                    for chain in self.buckets[1][:self.size]:
                        for old in chain:
                            chains[(old["tag"] ^ old["window"]) & (size - 1)].insert(0 if m != "node_rehash_keeps_order" else len(chains[(old["tag"] ^ old["window"]) & (size - 1)]), old)
                    self.free(self.buckets[0], 0x154)
                self.size, self.buckets = size, (ident, chains)
            node = {"tag": tag, "window": window, "counter": 0}
            node["id"] = self.alloc(0x14, 0x15D)
            node["table"] = {"id": self.alloc(0x10, 0x160), "size": 0, "count": 0, "buckets": None}
            self.count += 1
            self.buckets[1][(tag ^ window) & (self.size - 1)].insert(0, node)
        table = node["table"]
        if not table["size"] or table["size"] >> 1 <= table["count"] - (m == "grow_one_late"):
            size = power_above(2 * table["size"] + 2)
            ident = self.alloc(size * 4, 0x16E)
            tails = self.alloc(size * 4, 0x16F)
            chains = [[] for _ in range(size)]
            if table["buckets"] and table["size"]:
                for chain in table["buckets"][1][:table["size"]]:
                    for old in chain:
                        chains[old["code"] & (size - 1)].append(old)
            self.free(tails, 0x182)
            if table["buckets"]:
                self.free(table["buckets"][0], 0x184)
            table["size"], table["buckets"] = size, (ident, chains)
        record = {"code": code, "handler": handler}
        record["id"] = self.alloc(0x10, 0x18D)
        node["counter"] += 1
        record["sequence"] = node["counter"] if m != "sequence_zero" else 0
        table["buckets"][1][(table["size"] - 1) & code].insert(0 if m != "append_handler" else len(table["buckets"][1][(table["size"] - 1) & code]), record)
        table["count"] += 1
        self.dirty = 1
        return 1

    def lookup(self, tag, window):
        """FUN_00426270: the node of (tag, window), or None."""
        if not self.buckets or not self.size:
            return None
        for node in self.buckets[1][(tag ^ window) & (self.size - 1)]:
            if node["tag"] == tag and node["window"] == window:
                return node
        return None

    def dump(self):
        """The table as the emulator walk reports it."""
        if not self.buckets:
            return (0, 0, 0, None)
        nodes = []
        for chain in self.buckets[1]:
            nodes.append([(n["id"], n["tag"], n["window"], n["counter"], self.table_dump(n["table"])) for n in chain])
        return (self.size, self.count, self.buckets[0], nodes)

    def table_dump(self, table):
        """One node's handler table: id, size, count, chains of (id, code, sequence, handler)."""
        if not table["buckets"]:
            return (table["id"], 0, table["count"], 0, [])
        return (table["id"], table["size"], table["count"], table["buckets"][0],
                [[(r["id"], r["code"], r["sequence"], r["handler"]) for r in chain] for chain in table["buckets"][1]])


class Rig:
    """game.exe's FUN_00426420, one machine reused: the globals are zeroed between cases."""

    def __init__(self, exe):
        self.machine = emu.Emu(exe, real=(REGISTER, 0x426270, 0x41C340))
        self.ids = {}
        self.log = []
        machine = self.machine

        def allocate(current):
            """FUN_00413020 (size, file, line, flags): a fresh zeroed block, numbered and logged."""
            assert bytes(current.read(current.arg(1), len(FILE) + 1)) == FILE.encode() + b"\0"
            address = current.alloc(max(current.arg(0), 1))
            self.ids[address] = len(self.ids) + 1
            self.log.append(("alloc", self.ids[address], current.arg(0), current.arg(2), current.arg(3)))
            return address

        def release(current):
            """FUN_00412650 (pointer, file, line, flags): logged by the block it takes back."""
            assert bytes(current.read(current.arg(1), len(FILE) + 1)) == FILE.encode() + b"\0"
            self.log.append(("free", self.ids[current.arg(0)], current.arg(2), current.arg(3)))
            return 1

        machine.hook(ALLOC, allocate, 4)
        machine.hook(FREE, release, 4)

    def reset(self):
        """A fresh table: the five globals zeroed, the logs emptied."""
        for address in (NODES, SIZE, COUNT, DIRTY, LAST_ERROR):
            self.machine.w32(address, 0)
        self.ids, self.log = {}, []

    def run(self, tag, window, code, flags, handler):
        """One register call; returns EAX."""
        self.machine.w32(DIRTY, 0)
        return self.machine.call(REGISTER, tag, window, code, flags, handler, limit=2_000_000)

    def dump(self):
        """Walks the table out of memory; the same shape as Model.dump."""
        machine, ids = self.machine, self.ids
        size, count, array = machine.r32(SIZE), machine.r32(COUNT), machine.r32(NODES)
        if not array:
            return (0, 0, 0, None)
        nodes = []
        for index in range(size):
            chain, node = [], machine.r32(array + 4 * index)
            while node:
                chain.append((ids[node], machine.r32(node), machine.r32(node + 4), machine.r32(node + 8), self.table_dump(machine.r32(node + 12))))
                node = machine.r32(node + 16)
            nodes.append(chain)
        return (size, count, ids[array], nodes)

    def table_dump(self, table):
        """The handler table at `table`: the same shape as Model.table_dump."""
        machine, ids = self.machine, self.ids
        array, size, count = machine.r32(table), machine.r32(table + 4), machine.r32(table + 8)
        if not array:
            return (ids[table], 0, count, 0, [])
        chains = []
        for index in range(size):
            chain, record = [], machine.r32(array + 4 * index)
            while record:
                chain.append((ids[record], machine.r32(record), machine.r32(record + 4), machine.r32(record + 8)))
                record = machine.r32(record + 12)
            chains.append(chain)
        return (ids[table], size, count, ids[array], chains)


def calls(rng, number):
    """A random run of register calls: a few (tag, window) pairs, codes spread over a wide range; some invalid."""
    pairs = [(rng.choice((0x534D5347, 0x534D5348, 0x534D5349, rng.getrandbits(32))), rng.choice((0x1234, 0x1238, 0, rng.getrandbits(32))))
             for _ in range(rng.choice((1, 2, 5, 40)))]
    codes = [rng.choice((0x100, 0x102, 0x200, 0x201, 0x202)) for _ in range(3)] + [rng.getrandbits(32) for _ in range(4)]
    out = []
    for _ in range(number):
        tag, window = rng.choice(pairs)
        code = rng.choice(codes) if rng.random() < 0.5 else rng.getrandbits(rng.choice((4, 8, 32)))
        flags = 0 if rng.random() < 0.93 else rng.choice((1, 0x8000, rng.getrandbits(32)))
        handler = rng.getrandbits(32) | 1 if rng.random() < 0.95 else 0
        out.append((tag, window, code, flags, handler))
    return out


def compare(rig, seed, count, mutation=None):
    """Runs the cases on game.exe and the model; returns the first difference or None."""
    rng = random.Random(seed)
    for number in range(count):
        rig.reset()
        model = Model(mutation)
        run = calls(rng, rng.choice((1, 3, 10, 40, 150)) if number % 10 else 600)
        for position, args in enumerate(run):
            model.dirty = 0
            got = rig.run(*args)
            want = model.register(*args)
            where = f"case {number} call {position} {[hex(a) for a in args]}"
            if got != want:
                return f"{where}: eax {got} != {want}"
            if rig.machine.r32(DIRTY) != model.dirty:
                return f"{where}: DAT_007792f8 {rig.machine.r32(DIRTY)} != {model.dirty}"
            if rig.log != model.log:
                return f"{where}: allocations {rig.log[-3:]} != {model.log[-3:]}"
        error = rig.machine.r32(LAST_ERROR)
        if error != (model.last_error or 0):
            return f"case {number}: last error {error:#x} != {(model.last_error or 0):#x}"
        if rig.dump() != model.dump():
            return f"case {number}: the table differs after {len(run)} calls"
    return None


def main():
    """Runs the oracle; --mutation-check plants defects in the model and requires each to be caught."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, help="game.exe 1.14d (default: bin/game.exe in the data folder emu.py finds)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--count", type=int, default=60)
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    exe = args.exe or Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != SHA256:
        raise SystemExit(f"{exe} is not game.exe 1.14d")
    rig = Rig(str(exe))
    difference = compare(rig, args.seed, args.count)
    if difference:
        raise SystemExit(f"FAIL {difference}")
    print(f"ok: {args.count} registration runs (up to 600 calls each) match the model")
    if args.mutation_check:
        for mutation in ("ignore_flags", "grow_one_late", "node_rehash_keeps_order", "sequence_zero", "append_handler"):
            caught = compare(rig, args.seed, args.count, mutation)
            if not caught:
                raise SystemExit(f"FAIL mutation {mutation} not caught")
            print(f"caught {mutation}: {caught[:140]}")


if __name__ == "__main__":
    main()
