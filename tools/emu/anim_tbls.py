# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the AnimData loader and the record free in AnimTbls.cpp (.\\DATATBLS\\AnimTbls.cpp) in stock game.exe 1.14d.

    uv run python anim_tbls.py [--mutation-check]     (needs D2_MPQ_DIR; no MPQ is read)

* TXT_AllocTxt_AnimTbls (FUN_0066a770, fastcall pool): a 0x4a4-byte block (line 0x83),
  zeroed; the archive `DATA\\GLOBAL\\AnimData.d2` (wsprintfA, then D2FILE with no
  size pointer, line 0x8a) is stored at +0; then 256 hash slots from +4, slot k
  holding the address of bucket k, where each bucket is a count dword followed
  by count records of 0xa0 bytes (the next bucket starts 4 + 0xa0 * count bytes
  on); the defaults nFrames = 0x800 at +0x40c and nAnimSpeed = 0x100 at +0x410.
* ANIMDATA_FreeAnimDataRecord (FUN_0066a830, fastcall record): when the record is
  non-null, client-free its records (+0, line 0xbe) and pool-free the record
  itself (line 0xbf).

The field oracle's rig (`fieldtbls.Rig`) supplies the pool, the client release,
D2FILE and the fatal path. `--mutation-check` breaks the model on purpose, one
defect at a time, and requires each to fail.
"""
import random
import struct
import sys

import _monster_world as mw
from fieldtbls import Rig

LOAD, FREE = 0x66_A770, 0x66_A830
FILE_PTR, FILE = 0x6E_ED6C, ".\\DATATBLS\\AnimTbls.cpp"
PATH = "DATA\\GLOBAL\\AnimData.d2"
BLOCK, BUCKETS, BUCKET_SIZE, RECORD = 0x4A4, 256, 0xA0, 0xA0
MUTATIONS = ("anim-slot-stride", "anim-defaults", "anim-bucket-count", "anim-free-order", "anim-free-line",
             "anim-free-null-frees", "anim-free-null-records-skipped")
mutation = None


def mut(name):
    """True while the model carries this deliberate defect."""
    return mutation == name


def make_archive(rig, rng):
    """An AnimData archive: 256 buckets, each a count dword and that many 0xa0-byte records; returns (address, counts)."""
    counts = [rng.randrange(0, 3) for _ in range(BUCKETS)]
    body = bytearray()
    for count in counts:
        body += struct.pack("<I", count) + rng.randbytes(count * RECORD)
    address = rig.m.alloc(len(body) + 0x10)
    rig.write(address, bytes(body))
    return address, counts


def check_load(rig, rng):
    """TXT_AllocTxt_AnimTbls: the block is zeroed, then the archive pointer, 256 bucket slots and the two defaults."""
    archive, counts = make_archive(rig, rng)
    rig.archive = archive
    rig.log = []
    pool = rng.randrange(1 << 32)
    got = rig.go(LOAD, ecx=pool)
    mw.require(got[0] == "ret", f"AnimData load {got}")
    block = got[1]
    stride = BUCKET_SIZE if not mut("anim-slot-stride") else BUCKET_SIZE + 1
    count_slots = BUCKETS if not mut("anim-bucket-count") else BUCKETS - 1
    expect = bytearray(BLOCK)
    struct.pack_into("<I", expect, 0, archive)
    pos = archive
    for k in range(count_slots):
        struct.pack_into("<I", expect, 4 + 4 * k, pos)
        pos += 4 + counts[k] * stride
    default_frames, default_speed = (0x800, 0x100) if not mut("anim-defaults") else (0x801, 0x100)
    struct.pack_into("<II", expect, 0x40C, default_frames, default_speed)
    mw.require(bytes(rig.m.read(block, BLOCK)) == bytes(expect), "AnimData block differs from the model")
    file_line = [e for e in rig.log if e[0] == "d2file"]
    mw.require(file_line and file_line[0][1] == pool and file_line[0][2] == PATH and file_line[0][3] == FILE_PTR
               and file_line[0][4] == 0x8A and file_line[0][5] == 0, f"AnimData load: {file_line}")
    alloc = [e for e in rig.log if e[0] == "alloc"]
    mw.require(alloc == [("alloc", 0, BLOCK, FILE, 0x83, 0)], f"AnimData alloc log {alloc}")


def check_free(rig, rng):
    """ANIMDATA_FreeAnimDataRecord: client free of the records (line 0xbe, no null test), then the pool free (line 0xbf)."""
    rig.scramble()
    record = rng.choice((0, rig.m.alloc(BLOCK)))
    records = rng.choice((0, rng.randrange(1, 1 << 32)))
    if record:
        rig.write(record, struct.pack("<I", records))
    rig.log = []
    got = rig.go(FREE, ecx=record)
    mw.require(got[0] == "ret", f"free record {got}")
    steps = []
    if record:
        if mut("anim-free-order"):
            steps.append(("free", 0, record, FILE, 0xBF, 0))
        if not (mut("anim-free-null-records-skipped") and records == 0):
            steps.append(("client-free", records, FILE_PTR, 0xBE if not mut("anim-free-line") else 0xBD, 0))
        if not mut("anim-free-order"):
            steps.append(("free", 0, record, FILE, 0xBF, 0))
    elif mut("anim-free-null-frees"):
        steps.append(("free", 0, 0, FILE, 0xBF, 0))
    mw.require(rig.log == steps, f"free record log {rig.log} != {steps}")


def run_all():
    """Run every check on random inputs; returns the case count."""
    random.seed(0x66A770)
    rng = random.Random(0x66A830)
    rig = Rig()
    plan = [(check_load, 40), (check_free, 40)]
    cases = 0
    for check, rounds in plan:
        for _ in range(rounds):
            check(rig, rng)
            cases += 1
    return cases


def main():
    """Command line: run the oracle, or with --mutation-check require every defect to fail."""
    mw.drive(__doc__, sys.modules[__name__], run_all, "AnimData loader and record free cases")


if __name__ == "__main__":
    main()
