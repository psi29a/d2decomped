# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify game.exe 1.14d's quest warp gatekeeper (FUN_00545b80).

    uv run python quest_warp.py

FUN_00545b80 (stdcall ret 8, in QUESTS.CPP) is called by FUN_005550b0
(SUnit.cpp) to check whether a transition/warp from from_level to to_level
is blocked by an incomplete quest condition.

Cases handled by jump table at 0x545bd4 / index table at 0x545be8:
- to_level 73 (Duriel's Lair): calls FUN_0059db20 (Seven Tombs, record 0xd).
  Returns 1 (blocked) if record is active (+9 != 0) and orifice wall is
  not opened (data +0xb == 0). Returns 0 if closed or orifice opened.
- to_level 100 (Durance of Hate Level 1): calls FUN_005bbfa0 (Blackened Temple,
  record 0x13). Returns 1 (blocked) if the Compelling Orb is not smashed
  (data +0xc == 0), unless the actor is already in Durance Level 2 (101).
- to_level 118 (Ancient's Way) or 128 (Worldstone Keep Level 1):
  if from_level == 120 (Arreat Summit), calls FUN_0058d090 (Rite of Passage,
  record 0x23). Returns 1 (blocked) if record is active (+9 != 0) and
  Ancients are not defeated (data +0 == 0). If from_level != 120, returns 0.
- to_level 132 (Worldstone Chamber): calls FUN_0058e640 (Eve of Destruction,
  record 0x24). Returns 1 (blocked) if record is missing or Baal has not
  entered the portal (data +0x86 != 1). Returns 0 when data +0x86 == 1.
- All other levels (0..255): returns 0 (not blocked).
"""
import emu


def test_warp_blocking():
    e = emu.Emu()

    rec_buf = e.alloc(0x200)
    data_buf = e.alloc(0x200)

    # State: {record_id: (active_bool, data_bytes)}
    records = {}
    unit_level = 0

    def hook_get_quest(h):
        rec_id = h.mu.reg_read(emu.UC_X86_REG_EDX)
        if rec_id in records:
            active, data = records[rec_id]
            h.mu.mem_write(rec_buf + 9, b"\x01" if active else b"\x00")
            e.w32(rec_buf + 0x18, data_buf)
            h.mu.mem_write(data_buf, data)
            return rec_buf
        return 0

    e.hook(0x543640, hook_get_quest, 0)
    e.hook(0x620BB0, lambda h: 0x1234 if unit_level else 0, 1)
    e.hook(0x61A1B0, lambda h: unit_level, 1)

    tested = 0

    # 1. Sweep all other levels 0..255: must always return 0 (unblocked)
    special_levels = {73, 100, 118, 128, 132}
    for lvl in range(256):
        if lvl not in special_levels:
            res = e.call(0x545B80, 1, lvl)
            assert res == 0, f"Expected level {lvl} to be unblocked, got {res}"
            tested += 1

    # 2. Level 73 (Duriel's Lair): FUN_0059db20
    # No record -> 0
    records.clear()
    assert e.call(0x545B80, 1, 73) == 0
    tested += 1

    # Record active, orifice closed (data[0xb] == 0) -> 1 (blocked)
    records[0xD] = (True, b"\x00" * 20)
    assert e.call(0x545B80, 1, 73) == 1
    tested += 1

    # Record active, orifice opened (data[0xb] == 1) -> 0 (unblocked)
    d = bytearray(20)
    d[0xB] = 1
    records[0xD] = (True, bytes(d))
    assert e.call(0x545B80, 1, 73) == 0
    tested += 1

    # Record closed (+9 == 0) -> 0 (unblocked)
    records[0xD] = (False, b"\x00" * 20)
    assert e.call(0x545B80, 1, 73) == 0
    tested += 1

    # 3. Level 100 (Durance of Hate Level 1): FUN_005bbfa0
    records.clear()
    # From room in Durance Level 2 (101) -> 0 (unblocked)
    unit_level = 101
    assert e.call(0x545B80, 1, 100) == 0
    unit_level = 0
    tested += 1

    # No record -> 0
    assert e.call(0x545B80, 1, 100) == 0
    tested += 1

    # Orb not smashed (data[0xc] == 0) -> 1 (blocked)
    records[0x13] = (True, b"\x00" * 20)
    assert e.call(0x545B80, 1, 100) == 1
    tested += 1

    # Orb smashed (data[0xc] == 1) -> 0 (unblocked)
    d = bytearray(20)
    d[0xC] = 1
    records[0x13] = (True, bytes(d))
    assert e.call(0x545B80, 1, 100) == 0
    tested += 1

    # 4. Level 118 / 128 (Ancient's Way / Worldstone Keep 1): FUN_0058d090
    records.clear()
    # If from_level != 120 (e.g. 117 or 127) -> 0
    assert e.call(0x545B80, 117, 118) == 0
    assert e.call(0x545B80, 127, 128) == 0
    tested += 2

    # If from_level == 120, Ancients not defeated (data[0] == 0) -> 1 (blocked)
    records[0x23] = (True, b"\x00" * 20)
    assert e.call(0x545B80, 120, 118) == 1
    assert e.call(0x545B80, 120, 128) == 1
    tested += 2

    # If from_level == 120, Ancients defeated (data[0] == 1) -> 0 (unblocked)
    d = bytearray(20)
    d[0] = 1
    records[0x23] = (True, bytes(d))
    assert e.call(0x545B80, 120, 118) == 0
    assert e.call(0x545B80, 120, 128) == 0
    tested += 2

    # If from_level == 120, record closed (+9 == 0) -> 0 (unblocked)
    records[0x23] = (False, b"\x00" * 20)
    assert e.call(0x545B80, 120, 118) == 0
    assert e.call(0x545B80, 120, 128) == 0
    tested += 2

    # 5. Level 132 (Worldstone Chamber): FUN_0058e640
    records.clear()
    # Missing record -> 1 (blocked)
    assert e.call(0x545B80, 131, 132) == 1
    tested += 1

    # Record active, Baal not in portal (data[0x86] == 0) -> 1 (blocked)
    records[0x24] = (True, b"\x00" * 0x90)
    assert e.call(0x545B80, 131, 132) == 1
    tested += 1

    # Record active, Baal entered portal (data[0x86] == 1) -> 0 (unblocked)
    d = bytearray(0x90)
    d[0x86] = 1
    records[0x24] = (True, bytes(d))
    assert e.call(0x545B80, 131, 132) == 0
    tested += 1

    print(f"FUN_00545b80 tested across {tested} level transitions and quest states")
    print("ok")


if __name__ == "__main__":
    test_warp_blocking()
