# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe's skill tooltip lines (FUN_004edea0) as the oracle.

    uv run python skill_tips.py [levels] [difficulty] > game.txt
    ../../build/tests/test_game --skill-tips [levels] [difficulty] | grep '^[0-9]' | iconv -f latin1 -t utf-8 > d2d.txt
    diff game.txt d2d.txt

Loads the string tables (FUN_005259c0) and every excel table, then for
every SkillDesc'd skill, each of its 17 lines at levels 1..N (default 3),
on the given difficulty (0..2), runs FUN_004edea0 with no unit (calcs see no other skill points, no
masteries) and prints what it appended, one line per call:
`skill line level: text` with game.exe's '\n' shown as '|'.
"""
import sys

import emu


def boot():
    e = emu.Emu()
    original = e.read(0x524d30, 5)           # crt_init stubs the key lookup; the tables load for real here
    emu.serve_files(e)
    e.crt_init()
    e.mu.mem_write(0x524d30, original)
    e.mu.ctl_remove_cache(0x524d30, 0x524d35)
    lang = e.put(b"ENG\0")
    e.call(0x5259c0, 1, ecx=0, edx=lang)   # 1: expansionstring.tbl too
    e.w32(0x7a04f4, 1)                       # an expansion game (FUN_0044dcc0)
    if e.call(0x619300, 0, 1, 1) != 0: raise SystemExit("skill_tips: data tables failed to load")
    return e


def wide(e, a):
    out = []
    while (c := e.r16(a + 2 * len(out))): out.append(c)
    return "".join(chr(c) for c in out)


def main():
    levels = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    e = boot()
    e.w32(0x7a060c, int(sys.argv[2]) if len(sys.argv) > 2 else 0)   # FUN_0044dcd0's difficulty
    tables = e.r32(0x744304)
    skills, count = e.r32(tables + 0xb98), e.r32(tables + 0xba0)
    out = e.alloc(0x2000)
    for skill in range(count):
        record = skills + skill * 0x23c
        if e.call(0x4a89d0, ecx=skill) == 0: continue      # no SkillDesc row
        for line in range(17):
            for level in range(1, levels + 1):
                e.mu.mem_write(out, bytes(0x2000))
                wrote = e.call(0x4edea0, record, skill, level, 0, line, 0, ecx=0, edx=out)
                text = wide(e, out).replace("\n", "|")
                if wrote or text: print(f"{skill} {line} {level}: {wrote} {text}")


if __name__ == "__main__":
    main()
