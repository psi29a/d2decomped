# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's ignore list account mapping and node teardown (Chat/Ignorelist.cpp).

    uv run python ignorelist.py

Runs seven functions of `.\\Chat\\Ignorelist.cpp` (the Storm hash table and
list templates it instantiates) from game.exe, with the Storm allocator and
the table lookups hooked:

* FUN_00611320 (thiscall, ECX the list; raw name, length; ret 8) splits a
  Battle.net name "account(character)": it reads the persistent-setting flag
  from vtable slot +0x20 (0: nothing happens, returns 0), scans for a '('
  and a ')' at index + 1 for index below length - 1 (so a '(' at index 0 is
  not seen), keeps the text before the '(' as the account and the text
  between as the character, and, when both are non-empty, finds or creates
  the account's entry in the table at this + 0x54, frees its old value
  (`SMemFree`, line 0x142) and stores a duplicate of "*" + character at
  entry + 0x18. It returns 1 then, else 0. Every string over the alphabet
  a ( ) up to 7 characters is run against a model, with the table shared so
  repeats hit the old-value free.
* FUN_0060edf0 (thiscall, flags; ret 4), FUN_00610690 and FUN_0060ef60 free
  a node's value (+0x18, line 0x11), run the stpl.h node cleanup FUN_0060ea10
  (frees +0x14, line 0x5e4) and free the node ("delete" / -1 for the
  scalar deleting destructor with bit 0, the node tag / -2 for the others).
  FUN_0060ef60 also returns the node's next link when positive, else 0.
* FUN_0060ef30 changes an empty list's link offset and resets its
  terminator; FUN_0060edb0 and its copy FUN_0060eef0 clear a list's
  terminator links.
No MPQs are read.
"""
import argparse
import hashlib
import itertools
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
ADD_MAPPING = 0x611320
SCALAR_DTOR = 0x60EDF0
INTERNAL_DELETE = 0x610690
LIST_DELETE = 0x60EF60
CHANGE_OFFSET = 0x60EF30
LIST_DTORS = (0x60EDB0, 0x60EEF0)
SMEM_FREE = 0x412650
SSTR_DUP = 0x414310
TABLE_PTR = 0x60F510
TABLE_NEW = 0x611170
SLOT = 0x6615E4             # int3 padding: hooked as the vtable's +0x20 method
FILE_NAME = 0x6E6290
STPL_FILE = 0x6E6218
DELETE_TAG = 0x6CC824
NODE_TAG = 0x74362C
LIMIT = 100000


def require(condition, message):
    """Stop when game.exe's code disagrees with the claim."""
    if not condition:
        raise RuntimeError(message)


def split_name(raw, length):
    """The model of the scan: (account, character) bytes, either may be empty."""
    account = bytearray(256)
    character = bytearray(256)
    open_at = 0
    index = 0
    limit = (length - 1) & 0xFFFFFFFF

    def at(position):
        """The raw byte at a position, 0 past the end."""
        return raw[position] if position < len(raw) else 0

    while index < limit:
        if at(index + 1) == 0x28:
            open_at = index + 1
            account[:open_at] = raw[:open_at]
        if at(index + 1) == 0x29:
            character[:index - open_at] = raw[open_at + 1:index + 1]
        if account[0] and character[0]:
            break
        index += 1
        if at(index) == 0:
            break
    return bytes(account).split(b"\0")[0], bytes(character).split(b"\0")[0]


class Harness:
    """game.exe with a logging SMemFree, a dict for the account table and a fake list object."""

    def __init__(self, machine):
        """Hook the Storm calls and build the list object (vtable slot +0x20 returns `persistent`)."""
        self.machine = machine
        self.frees = []
        self.entries = {}
        self.persistent = 1
        self.news = 0
        self.this = machine.alloc(0x188)
        vtable = machine.alloc(0x40)
        machine.w32(self.this, vtable)
        machine.w32(vtable + 0x20, SLOT)
        require(machine.read(SLOT, 5) == b"\xcc" * 5, "0x6615e4 is not int3 padding")
        machine.hook(SLOT, lambda current: self.persistent, 0)
        machine.hook(SMEM_FREE, self.free, 4)
        machine.hook(SSTR_DUP, self.dup, 1)
        machine.hook(TABLE_PTR, self.ptr, 1)
        machine.hook(TABLE_NEW, self.new, 3)

    def free(self, current):
        """SMemFree(pointer, file, line, flag): logged."""
        self.frees.append((current.arg(0), current.arg(1), current.arg(2), current.arg(3)))
        return 1

    def dup(self, current):
        """SStrDup: a copy in emulated memory."""
        return self.machine.put(current.cstr(current.arg(0)).encode("latin-1") + b"\0")

    def ptr(self, current):
        """Table lookup by key (ECX must be this + 0x54): the entry or 0."""
        require(current.mu.reg_read(UC_X86_REG_ECX) == self.this + 0x54, "table is not at this + 0x54")
        return self.entries.get(current.cstr(current.arg(0)), 0)

    def new(self, current):
        """Table insert by key: a zeroed entry (value pointer at +0x18)."""
        require(current.mu.reg_read(UC_X86_REG_ECX) == self.this + 0x54, "table is not at this + 0x54")
        self.news += 1
        node = current.alloc(0x40)
        self.entries[current.cstr(current.arg(0))] = node
        return node

    def run(self, address, *args, **kwargs):
        """Call a function, clear the free log first and check it returned."""
        self.frees.clear()
        result = self.machine.call(address, *args, limit=LIMIT, **kwargs)
        require(self.machine.mu.reg_read(UC_X86_REG_EIP) == emu.RET_MAGIC, f"{address:#x}: stopped at the cap")
        return result

    def values(self):
        """The table as {account: stored value}."""
        return {key: self.machine.cstr(self.machine.r32(node + 0x18)) for key, node in self.entries.items()}


def check_add(harness, raw, length, expected):
    """Run FUN_00611320 on a raw name and compare result, table and frees with the model."""
    machine = harness.machine
    text = raw.decode("latin-1")
    before = harness.values()
    old_nodes = {key: machine.r32(node + 0x18) for key, node in harness.entries.items()}
    got = harness.run(ADD_MAPPING, machine.put(raw + b"\0"), length, ecx=harness.this)
    account, character = split_name(raw, length)
    ok = bool(account and character) and harness.persistent
    require(got == (1 if ok else 0), f"{text!r} {length}: returned {got}")
    after = harness.values()
    if not ok:
        require(after == before and not harness.frees, f"{text!r} {length}: changed the table")
        return
    key = account.decode("latin-1")
    want = dict(before)
    want[key] = "*" + character.decode("latin-1")
    require(after == want, f"{text!r} {length}: table {after}, expected {want}")
    old = old_nodes.get(key, 0)
    require(harness.frees == ([(old, FILE_NAME, 0x142, 0)] if old else []), f"{text!r} {length}: frees {harness.frees}")


def node_cases(harness):
    """The three teardown functions on nodes with and without a value and cleanup pointer."""
    machine = harness.machine
    count = 0
    for value in (0, 1):
        for extra in (0, 1):
            for flags in (0, 1):
                node = machine.alloc(0x40)
                value_ptr = machine.alloc(8) if value else 0
                extra_ptr = machine.alloc(8) if extra else 0
                machine.w32(node + 0x18, value_ptr)
                machine.w32(node + 0x14, extra_ptr)
                inner = ([(value_ptr, FILE_NAME, 0x11, 0)] if value else []) + ([(extra_ptr, STPL_FILE, 0x5E4, 0)] if extra else [])
                got = harness.run(SCALAR_DTOR, flags, ecx=node)
                want = inner + ([(node, DELETE_TAG, 0xFFFFFFFF, 0)] if flags else [])
                require(got == node and harness.frees == want, f"scalar dtor {flags}: {harness.frees}")
                machine.w32(node + 0x18, value_ptr)
                machine.w32(node + 0x14, extra_ptr)
                harness.run(INTERNAL_DELETE, node, ecx=0x1234)
                require(harness.frees == inner + [(node, NODE_TAG, 0xFFFFFFFE, 0)], f"internal delete: {harness.frees}")
                count += 2
    # the list's DeleteNode: the same frees, and the next link when positive
    for offset in (0x4, 0xC):
        for link in (0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x1000):
            node = machine.alloc(0x80)
            machine.w32(node + 0x18, machine.alloc(8))
            machine.w32(node + offset + 4, link)
            lst = machine.alloc(16)
            machine.w32(lst, offset)
            got = harness.run(LIST_DELETE, node, ecx=lst)
            want = link if 0 < link < 0x80000000 else 0
            require(got == want, f"list delete offset {offset:#x} link {link:#x}: {got:#x}")
            require([f[1:] for f in harness.frees] == [(FILE_NAME, 0x11, 0), (NODE_TAG, 0xFFFFFFFE, 0)], f"list delete frees {harness.frees}")
            count += 1
    return count


def list_cases(harness):
    """ChangeLinkOffset and the two terminator destructors on empty lists."""
    machine = harness.machine
    count = 0

    def empty(offset):
        """An empty list: offset, terminator's prevlink -> itself, next = ~terminator."""
        lst = machine.alloc(16)
        machine.w32(lst, offset)
        machine.w32(lst + 4, lst + 4)
        machine.w32(lst + 8, ~(lst + 4))
        return lst

    lst = empty(0x10)
    before = machine.read(lst, 12)
    harness.run(CHANGE_OFFSET, 0x10, ecx=lst)
    require(machine.read(lst, 12) == before, "same offset changed the list")
    harness.run(CHANGE_OFFSET, 0x24, ecx=lst)
    require((machine.r32(lst), machine.r32(lst + 4), machine.r32(lst + 8)) == (0x24, lst + 4, ~(lst + 4) & 0xFFFFFFFF),
            "new offset resets the terminator")
    for address in LIST_DTORS:
        lst = empty(0x10)
        harness.run(address, ecx=lst)
        require((machine.r32(lst + 4), machine.r32(lst + 8)) == (0, 0), f"{address:#x}: terminator not cleared")
        harness.run(address, ecx=lst)       # already cleared: stays cleared
        require((machine.r32(lst + 4), machine.r32(lst + 8)) == (0, 0), f"{address:#x}: second run")
        count += 2
    return count + 2


def main():
    """Run the mapping model and the teardown cases."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    require(hashlib.sha256(exe.read_bytes()).hexdigest() == GAME_SHA256,
            "requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    require(machine.cstr(FILE_NAME) == ".\\Chat\\Ignorelist.cpp", "0x6e6290 is not .\\Chat\\Ignorelist.cpp")
    require(machine.cstr(DELETE_TAG) == "delete", "0x6cc824 is not \"delete\"")
    harness = Harness(machine)

    named = [b"acct(char)", b"acct(char)", b"Zed(Zed2)", b"x)(a)", b"(a)", b"a()", b"a(b", b"a(b)c(d)", b"ab(cd)ef"]
    cases = 0
    for raw in named:
        for length in (len(raw), len(raw) - 1, len(raw) + 1, 0):
            check_add(harness, raw, length, None)
            cases += 1
    checked = 0
    for size in range(1, 8):
        for combo in itertools.product(b"a()", repeat=size):
            raw = bytes(combo)
            for length in (len(raw), len(raw) - 1, len(raw) + 1):
                check_add(harness, raw, length, None)
                checked += 1
    harness.persistent = 0
    check_add(harness, b"acct(char)", 10, None)
    harness.persistent = 1
    require(harness.run(ADD_MAPPING, 0, 5, ecx=harness.this) == 0, "null name")
    require(harness.run(ADD_MAPPING, machine.put(b"\0"), 5, ecx=harness.this) == 0, "empty name")
    require(harness.values().get("acct") == "*char" and harness.news > 3, f"table {harness.values()}")
    cases += checked + 3
    cases += node_cases(harness)
    cases += list_cases(harness)
    print(f"{cases} cases: {len(harness.entries)} accounts mapped; name split, old-value free (line 0x142), "
          "node teardown frees, DeleteNode's next link, list offset and terminator")
    print("ok")


if __name__ == "__main__":
    main()
