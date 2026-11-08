"""game.exe's scroll / tome count searches (FUN_004a9a90, FUN_004a9ae0, FUN_004a9b30)
and the list accessors under them (FUN_006450f0, FUN_00620150).

Usage: uv run --project tools/emu python tools/emu/skill_count_search.py

Builds a unit whose +0xa8 list holds a node with a record (first short = skill
id) and a count at node +0x30 above 0, then calls each search with that skill
id. docs/research/re/bugs.md 24 says every path returns 0; this prints the
results (expected 0 0 0 and the accessors' values).
"""
import emu

e = emu.Emu()
SKILL = 77
record = e.alloc(0x40)
e.w32(record, SKILL)                       # first short: the skill id
node = e.alloc(0x40)
e.w32(node + 0, record)
e.w32(node + 4, 0)                         # next
e.w32(node + 0x30, 5)                      # count
header = e.alloc(0x40)
e.w32(header + 4, node)                    # first node
unit = e.alloc(0x100)
e.w32(unit + 0xa8, header)

count = e.call(0x6450f0, node)
first = e.call(0x620150, unit) == node
print("count", count)
print("first", first)
ok = count == 5 and first
for addr in (0x4a9a90, 0x4a9ae0, 0x4a9b30, 0x4a9b80):
    for skill in (SKILL, 1):
        got = e.call(addr, ecx=unit, edx=skill)
        print(f"{addr:06x} skill {skill} -> {got}")
        ok &= got == 0
print("match" if ok else "MISMATCH")
