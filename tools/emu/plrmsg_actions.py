# SPDX-License-Identifier: GPL-3.0-or-later
"""Run four C->S packet handlers of PlrMsg.cpp in the emulator on synthetic state.

    uv run python plrmsg_actions.py --exe /path/to/Game.exe [--mutation-check]

Runs game.exe's own code for:
- FUN_0054bc30 (0x37, size 5): looks up item id via FUN_00552f60(game, 4, id),
  returns 2 if missing; checks player_data(unit)+0x6c == id (returns 3 if not);
  calls FUN_00578640(game, unit, id) and returns 0.
- FUN_0054bca0 (0x38, size 13): checks NPC via FUN_00548f80(game, 1, eax=npc_id,
  0x32); if nonzero returns that status, else returns FUN_00579d60(game, unit,
  action, npc_id, arg3).
- FUN_0054bf10 (0x3d, size 5): checks object via FUN_00548f80(game, 2, eax=obj_id,
  10); if nonzero returns that status, else calls FUN_005845d0(game, unit, obj_id)
  and returns 0.
- FUN_0054c760 (0x4c, size 5): looks up item id via FUN_00552f60(game, 4, id);
  returns 1 if missing and id != -1; otherwise calls FUN_0056c6a0(game, unit,
  item_ptr) and returns 0.

Hooked, not claimed: FUN_00552f60, FUN_006221a0, FUN_00578640, FUN_00548f80,
FUN_00579d60, FUN_005845d0, FUN_0056c6a0. Their arguments are recorded and
asserted. d2d is not compared.
"""
import argparse
import struct
import sys

import emu

if not __debug__:
    sys.exit("plrmsg_actions.py requires assertions; do not use python -O")

CALL_LIMIT = 100000


def write_packet(engine, buf, payload):
    """Write raw packet bytes into the emulator buffer."""
    engine.mu.mem_write(buf, payload + bytes(0x40 - len(payload)))


def check_0x37(engine):
    """FUN_0054bc30 (C->S 0x37): size 5, item lookup, player_data+0x6c match, FUN_00578640."""
    game = engine.alloc(0x100)
    unit = engine.alloc(0x100)
    pdata = engine.alloc(0x100)
    pkt = engine.alloc(0x40)
    item_ptr = [0x11223344]
    calls = []

    def lookup(machine):
        calls.append(("lookup", machine.mu.reg_read(emu.UC_X86_REG_ECX), machine.mu.reg_read(emu.UC_X86_REG_EDX), machine.arg(0)))
        return item_ptr[0]

    def get_pdata(machine):
        calls.append(("pdata", machine.arg(0)))
        return pdata

    def identify(machine):
        calls.append(("identify", machine.mu.reg_read(emu.UC_X86_REG_ECX), machine.mu.reg_read(emu.UC_X86_REG_EDX), machine.arg(0)))
        return 0

    engine.hook(0x552F60, lookup, 1)
    engine.hook(0x6221A0, get_pdata, 1)
    engine.hook(0x578640, identify, 1)

    cases = 0
    for bad_size in (0, 1, 4, 6, 13):
        calls.clear()
        write_packet(engine, pkt, struct.pack("<BI", 0x37, 0x100))
        got = engine.call(0x54BC30, pkt, bad_size, ecx=game, edx=unit, limit=CALL_LIMIT)
        assert (got, calls) == (3, []), (bad_size, got, calls)
        cases += 1

    # Missing item -> returns 2, no player_data or identify call.
    calls.clear()
    item_ptr[0] = 0
    write_packet(engine, pkt, struct.pack("<BI", 0x37, 0x555))
    got = engine.call(0x54BC30, pkt, 5, ecx=game, edx=unit, limit=CALL_LIMIT)
    assert (got, calls) == (2, [("lookup", game, 4, 0x555)]), (got, calls)
    cases += 1

    # Item found, remembered id at player_data+0x6c mismatches -> returns 3.
    calls.clear()
    item_ptr[0] = 0x22334455
    engine.w32(pdata + 0x6C, 0x554)
    write_packet(engine, pkt, struct.pack("<BI", 0x37, 0x555))
    got = engine.call(0x54BC30, pkt, 5, ecx=game, edx=unit, limit=CALL_LIMIT)
    assert (got, calls) == (3, [("lookup", game, 4, 0x555), ("pdata", unit)]), (got, calls)
    cases += 1

    # Item found and remembered id matches -> calls FUN_00578640 and returns 0.
    for item_id in (0, 1, 0x555, 0x12345678):
        calls.clear()
        engine.w32(pdata + 0x6C, item_id)
        write_packet(engine, pkt, struct.pack("<BI", 0x37, item_id))
        got = engine.call(0x54BC30, pkt, 5, ecx=game, edx=unit, limit=CALL_LIMIT)
        assert (got, calls) == (
            0,
            [("lookup", game, 4, item_id), ("pdata", unit), ("identify", game, unit, item_id)],
        ), (item_id, got, calls)
        cases += 1
    return cases


def check_0x38(engine):
    """FUN_0054bca0 (C->S 0x38): size 13, FUN_00548f80(1, npc, 0x32), then FUN_00579d60."""
    game = engine.alloc(0x100)
    unit = engine.alloc(0x100)
    pkt = engine.alloc(0x40)
    gate_ret = [0]
    action_ret = [0]
    calls = []

    def gate(machine):
        calls.append((
            "gate",
            machine.mu.reg_read(emu.UC_X86_REG_ECX),
            machine.mu.reg_read(emu.UC_X86_REG_EDX),
            machine.mu.reg_read(emu.UC_X86_REG_EAX),
            machine.arg(0),
        ))
        return gate_ret[0]

    def npc_action(machine):
        calls.append((
            "action",
            machine.mu.reg_read(emu.UC_X86_REG_ECX),
            machine.mu.reg_read(emu.UC_X86_REG_EDX),
            machine.arg(0),
            machine.arg(1),
            machine.arg(2),
        ))
        return action_ret[0]

    engine.hook(0x548F80, gate, 1)
    engine.hook(0x579D60, npc_action, 3)

    cases = 0
    for bad_size in (0, 5, 12, 14):
        calls.clear()
        write_packet(engine, pkt, struct.pack("<BIII", 0x38, 1, 2, 3))
        got = engine.call(0x54BCA0, pkt, bad_size, ecx=game, edx=unit, limit=CALL_LIMIT)
        assert (got, calls) == (3, []), (bad_size, got, calls)
        cases += 1

    # Gate refuses -> returns gate status directly, no FUN_00579d60 call.
    for status in (1, 2, 3, 7):
        calls.clear()
        gate_ret[0] = status
        write_packet(engine, pkt, struct.pack("<BIII", 0x38, 0x11, 0x22, 0x33))
        got = engine.call(0x54BCA0, pkt, 13, ecx=game, edx=unit, limit=CALL_LIMIT)
        assert (got, calls) == (status, [("gate", game, 1, 0x22, 0x32)]), (status, got, calls)
        cases += 1

    # Gate passes -> forwards to FUN_00579d60 and returns its status.
    gate_ret[0] = 0
    for status, args in ((0, (0, 10, 20)), (1, (1, 0x99, 0x88)), (2, (2, 0xABCDEF, 0))):
        calls.clear()
        action_ret[0] = status
        write_packet(engine, pkt, struct.pack("<BIII", 0x38, *args))
        got = engine.call(0x54BCA0, pkt, 13, ecx=game, edx=unit, limit=CALL_LIMIT)
        assert (got, calls) == (
            status,
            [("gate", game, 1, args[1], 0x32), ("action", game, unit, args[0], args[1], args[2])],
        ), (status, args, got, calls)
        cases += 1
    return cases


def check_0x3d(engine):
    """FUN_0054bf10 (C->S 0x3d): size 5, FUN_00548f80(2, obj, 10), then FUN_005845d0."""
    game = engine.alloc(0x100)
    unit = engine.alloc(0x100)
    pkt = engine.alloc(0x40)
    gate_ret = [0]
    calls = []

    def gate(machine):
        calls.append((
            "gate",
            machine.mu.reg_read(emu.UC_X86_REG_ECX),
            machine.mu.reg_read(emu.UC_X86_REG_EDX),
            machine.mu.reg_read(emu.UC_X86_REG_EAX),
            machine.arg(0),
        ))
        return gate_ret[0]

    def obj_action(machine):
        calls.append(("obj", machine.mu.reg_read(emu.UC_X86_REG_ECX), machine.mu.reg_read(emu.UC_X86_REG_EDX), machine.arg(0)))
        return 0x99

    engine.hook(0x548F80, gate, 1)
    engine.hook(0x5845D0, obj_action, 1)

    cases = 0
    for bad_size in (0, 1, 4, 6):
        calls.clear()
        write_packet(engine, pkt, struct.pack("<BI", 0x3D, 0x42))
        got = engine.call(0x54BF10, pkt, bad_size, ecx=game, edx=unit, limit=CALL_LIMIT)
        assert (got, calls) == (3, []), (bad_size, got, calls)
        cases += 1

    # Gate refuses -> returns gate status directly.
    for status in (1, 2, 3):
        calls.clear()
        gate_ret[0] = status
        write_packet(engine, pkt, struct.pack("<BI", 0x3D, 0x777))
        got = engine.call(0x54BF10, pkt, 5, ecx=game, edx=unit, limit=CALL_LIMIT)
        assert (got, calls) == (status, [("gate", game, 2, 0x777, 10)]), (status, got, calls)
        cases += 1

    # Gate passes -> calls FUN_005845d0 and always returns 0 (ignores callee return).
    gate_ret[0] = 0
    for obj_id in (0, 1, 0x777, 0xCAFE):
        calls.clear()
        write_packet(engine, pkt, struct.pack("<BI", 0x3D, obj_id))
        got = engine.call(0x54BF10, pkt, 5, ecx=game, edx=unit, limit=CALL_LIMIT)
        assert (got, calls) == (0, [("gate", game, 2, obj_id, 10), ("obj", game, unit, obj_id)]), (obj_id, got, calls)
        cases += 1
    return cases


def check_0x4c(engine):
    """FUN_0054c760 (C->S 0x4c): size 5, item lookup (allowing -1 cancel), then FUN_0056c6a0."""
    game = engine.alloc(0x100)
    unit = engine.alloc(0x100)
    pkt = engine.alloc(0x40)
    item_ptr = [0]
    calls = []

    def lookup(machine):
        calls.append(("lookup", machine.mu.reg_read(emu.UC_X86_REG_ECX), machine.mu.reg_read(emu.UC_X86_REG_EDX), machine.arg(0)))
        return item_ptr[0]

    def transmogrify(machine):
        calls.append(("transmogrify", machine.mu.reg_read(emu.UC_X86_REG_ECX), machine.mu.reg_read(emu.UC_X86_REG_EDX), machine.arg(0)))
        return 0x77

    engine.hook(0x552F60, lookup, 1)
    engine.hook(0x56C6A0, transmogrify, 1)

    cases = 0
    for bad_size in (0, 1, 4, 6):
        calls.clear()
        write_packet(engine, pkt, struct.pack("<BI", 0x4C, 1))
        got = engine.call(0x54C760, pkt, bad_size, ecx=game, edx=unit, limit=CALL_LIMIT)
        assert (got, calls) == (3, []), (bad_size, got, calls)
        cases += 1

    # Missing item with id != -1 -> returns 1, no FUN_0056c6a0 call.
    item_ptr[0] = 0
    for item_id in (0, 1, 0x1234, 0xFFFFFFFE):
        calls.clear()
        write_packet(engine, pkt, struct.pack("<BI", 0x4C, item_id))
        got = engine.call(0x54C760, pkt, 5, ecx=game, edx=unit, limit=CALL_LIMIT)
        assert (got, calls) == (1, [("lookup", game, 4, item_id)]), (item_id, got, calls)
        cases += 1

    # Missing item with id == -1 (0xffffffff) -> still calls FUN_0056c6a0(game, unit, 0) and returns 0.
    calls.clear()
    item_ptr[0] = 0
    write_packet(engine, pkt, struct.pack("<BI", 0x4C, 0xFFFFFFFF))
    got = engine.call(0x54C760, pkt, 5, ecx=game, edx=unit, limit=CALL_LIMIT)
    assert (got, calls) == (0, [("lookup", game, 4, 0xFFFFFFFF), ("transmogrify", game, unit, 0)]), (got, calls)
    cases += 1

    # Found item -> calls FUN_0056c6a0(game, unit, item_ptr) and returns 0.
    for item_id, ptr in ((1, 0x30001000), (0x42, 0x30002000)):
        calls.clear()
        item_ptr[0] = ptr
        write_packet(engine, pkt, struct.pack("<BI", 0x4C, item_id))
        got = engine.call(0x54C760, pkt, 5, ecx=game, edx=unit, limit=CALL_LIMIT)
        assert (got, calls) == (0, [("lookup", game, 4, item_id), ("transmogrify", game, unit, ptr)]), (item_id, got, calls)
        cases += 1
    return cases


CHECKS = (
    ("FUN_0054bc30", 0x54BC30, check_0x37),
    ("FUN_0054bca0", 0x54BCA0, check_0x38),
    ("FUN_0054bf10", 0x54BF10, check_0x3d),
    ("FUN_0054c760", 0x54C760, check_0x4c),
)

# One real defect per target: (address, original bytes, broken bytes).
MUTATIONS = {
    "FUN_0054bc30": (
        (0x54BC36, b"\x05", b"\x06"),
        (0x54BC53, b"\x04", b"\x03"),
        (0x54BC74, b"\x6c", b"\x68"),
        (0x54BC30, None, b"\xc2\x08\x00"),
    ),
    "FUN_0054bca0": (
        (0x54BCA7, b"\x0d", b"\x0c"),
        (0x54BCD0, b"\x32", b"\x33"),
        (0x54BCD4, b"\x01", b"\x02"),
        (0x54BCA0, None, b"\xc2\x08\x00"),
    ),
    "FUN_0054bf10": (
        (0x54BF17, b"\x05", b"\x06"),
        (0x54BF34, b"\x0a", b"\x0b"),
        (0x54BF38, b"\x02", b"\x01"),
        (0x54BF10, None, b"\xc2\x08\x00"),
    ),
    "FUN_0054c760": (
        (0x54C766, b"\x05", b"\x06"),
        (0x54C783, b"\x04", b"\x03"),
        (0x54C792, b"\xff", b"\xfe"),
        (0x54C760, None, b"\xc2\x08\x00"),
    ),
}


def run_check(executable, check, patches=()):
    """Run one check in a fresh emulator after applying byte patches; return its case count."""
    engine = emu.Emu(exe=executable)
    for address, _, broken in patches:
        engine.mu.mem_write(address, broken)
        engine.mu.ctl_remove_cache(address, address + len(broken))
    return check(engine)


def main():
    """Run every check, or with --mutation-check require each patched target to fail."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="local game.exe 1.14d")
    parser.add_argument("--mutation-check", action="store_true", help="require every patched target to fail")
    options = parser.parse_args()
    if not options.mutation_check:
        total = 0
        for name, _, check in CHECKS:
            cases = run_check(options.exe, check)
            total += cases
            print(f"ok: {name}: {cases} cases")
        print(f"ok: {total} cases over {len(CHECKS)} functions")
        return
    caught = 0
    pristine = emu.Emu(exe=options.exe)
    for name, _, check in CHECKS:
        run_check(options.exe, check)
        for address, original, _ in MUTATIONS[name]:
            assert original is None or pristine.read(address, len(original)) == original, hex(address)
        for patch in MUTATIONS[name]:
            try:
                run_check(options.exe, check, (patch,))
            except AssertionError:
                caught += 1
                print(f"mutation {name} @{patch[0]:x}: caught")
            except RuntimeError as fault:
                raise SystemExit(f"mutation {name} @{patch[0]:x}: emulator fault, not a check failure: {fault}") from None
            except SystemExit as stop:
                if str(stop).startswith("emu:"):
                    raise SystemExit(f"mutation {name} @{patch[0]:x}: emulator fault, not a check failure: {stop}") from None
                caught += 1
                print(f"mutation {name} @{patch[0]:x}: caught")
            else:
                raise SystemExit(f"mutation {name} @{patch[0]:x}: NOT caught")
    print(f"ok: {caught} target mutations caught")


if __name__ == "__main__":
    main()
