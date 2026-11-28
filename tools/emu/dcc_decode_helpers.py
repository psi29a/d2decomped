# SPDX-License-Identifier: GPL-3.0-or-later
"""Run two game.exe 1.14d DCC decoder routines (SRC/Codec.cpp) on synthetic state.

    uv run python dcc_decode_helpers.py --exe /path/to/Game.exe [--mutation-check]

Runs game.exe's own code for:
- FUN_0060b7b0 (DCC cell pixel fill and equal-cell copy): ESI = dst cell,
  ECX = &pb_ptr, EDX = equal-cells bit reader (or 0), stack = (pixel-codes
  reader, prev cell), ret 8. Runs FUN_0060b200 (bits_read_lsb), memcpy and
  memset for real.
- FUN_0060bdb0 (DCC frame -> DC6 RLE packer): ECX = frame record, EDX = cel
  header, stack = out buffer (cel_hdr + 0x20), ret 4. Packs transparent runs
  (0x80 | min(len, 0x7f)), opaque runs (min(len, 0x7f) + pixels), row end
  0x80, bottom-to-top when bottom_up == 0, and checks the encoded length
  against *(cel_hdr + 0x1c).

Hooked, not claimed: FUN_00410610 (d2d_log_printf, recorded on size mismatch).
d2d is not compared.
"""
import argparse
import sys

import emu

if not __debug__:
    sys.exit("dcc_decode_helpers.py requires assertions; do not use python -O")

CALL_LIMIT = 200000
LOG_ERR_STR = 0x6E6138


def make_reader(engine, raw_bytes):
    """Allocate a game.exe bit-reader struct (+4 dword ptr, +0xc bit pos) over raw_bytes."""
    padded = raw_bytes + bytes((-len(raw_bytes)) % 4 + 8)
    buf = engine.alloc(len(padded))
    engine.mu.mem_write(buf, padded)
    reader = engine.alloc(0x18)
    engine.w32(reader + 0x0, buf)
    engine.w32(reader + 0x4, buf)
    engine.w32(reader + 0x8, len(raw_bytes) * 8)
    engine.w32(reader + 0xC, 0)
    return reader


def pack_bits_lsb(pairs):
    """Pack a sequence of (value, bit_count) into LSB-first bytes."""
    total = 0
    pos = 0
    for value, count in pairs:
        total |= (value & ((1 << count) - 1)) << pos
        pos += count
    nbytes = (pos + 7) // 8
    return total.to_bytes(max(nbytes, 1), "little")


def write_cell(engine, addr, pb_entry, pixels, pitch, width, height):
    """Populate a 5-dword DCC cell descriptor."""
    engine.w32(addr + 0x00, pb_entry)
    engine.w32(addr + 0x04, pixels)
    engine.w32(addr + 0x08, pitch)
    engine.w32(addr + 0x0C, width)
    engine.w32(addr + 0x10, height)


def read_rect(engine, ptr, pitch, width, height):
    """Read a width x height byte rectangle with row stride `pitch`."""
    return [engine.read(ptr + row * pitch, width) for row in range(height)]


def check_cell_decode(engine):
    """FUN_0060b7b0: equal-cell copy/overlap/mismatch-zero and 0/1/2-bit pixel decoding."""
    pb_buf = engine.alloc(0x40)
    pb_ptr_slot = engine.alloc(4)
    dst_cell = engine.alloc(0x14)
    prev_cell = engine.alloc(0x14)
    cases = 0

    # 1. Equal-cell copy (bit = 1, same dimensions, distinct buffers).
    src_canvas = engine.alloc(0x40)
    dst_canvas = engine.alloc(0x40)
    for row in range(4):
        engine.mu.mem_write(src_canvas + row * 8, bytes([10 + row * 4 + col for col in range(4)]))
    engine.mu.mem_write(dst_canvas, b"\xee" * 0x40)
    eq_reader = make_reader(engine, pack_bits_lsb([(1, 1)]))
    px_reader = make_reader(engine, b"\x00\x00\x00\x00")
    engine.w32(pb_ptr_slot, pb_buf)
    write_cell(engine, prev_cell, 1, src_canvas, 8, 4, 4)
    write_cell(engine, dst_cell, pb_buf, dst_canvas, 8, 4, 4)
    engine.call(0x60B7B0, px_reader, prev_cell, ecx=pb_ptr_slot, edx=eq_reader, regs={"esi": dst_cell}, limit=CALL_LIMIT)
    assert read_rect(engine, dst_canvas, 8, 4, 4) == read_rect(engine, src_canvas, 8, 4, 4)
    assert engine.r32(pb_ptr_slot) == pb_buf
    cases += 1

    # 2. Equal-cell overlapping copy (dst = src + 1 byte, same pitch): stack buffer prevents clobber.
    ov_canvas = engine.alloc(0x40)
    for row in range(3):
        engine.mu.mem_write(ov_canvas + row * 8, bytes([1 + row * 4, 2 + row * 4, 3 + row * 4, 4 + row * 4]))
    expected_ov = read_rect(engine, ov_canvas, 8, 4, 3)
    eq_reader = make_reader(engine, pack_bits_lsb([(1, 1)]))
    write_cell(engine, prev_cell, 1, ov_canvas, 8, 4, 3)
    write_cell(engine, dst_cell, pb_buf, ov_canvas + 1, 8, 4, 3)
    engine.call(0x60B7B0, px_reader, prev_cell, ecx=pb_ptr_slot, edx=eq_reader, regs={"esi": dst_cell}, limit=CALL_LIMIT)
    assert read_rect(engine, ov_canvas + 1, 8, 4, 3) == expected_ov
    cases += 1

    # 3. Equal-cell size mismatch (bit = 1, width differs) -> zeroes dst cell.
    engine.mu.mem_write(dst_canvas, b"\xaa" * 0x40)
    eq_reader = make_reader(engine, pack_bits_lsb([(1, 1)]))
    write_cell(engine, prev_cell, 1, src_canvas, 8, 3, 4)
    write_cell(engine, dst_cell, pb_buf, dst_canvas, 8, 4, 4)
    engine.call(0x60B7B0, px_reader, prev_cell, ecx=pb_ptr_slot, edx=eq_reader, regs={"esi": dst_cell}, limit=CALL_LIMIT)
    assert read_rect(engine, dst_canvas, 8, 4, 4) == [b"\x00" * 4] * 4
    assert engine.r32(pb_ptr_slot) == pb_buf
    cases += 1

    # 4. 0-bit solid fill (pb[0] == pb[1]) with equal_reader = 0; advances *pb_ptr by 4.
    engine.mu.mem_write(pb_buf, bytes([0x5A, 0x5A, 0x11, 0x22]))
    engine.mu.mem_write(dst_canvas, b"\x00" * 0x40)
    engine.w32(pb_ptr_slot, pb_buf)
    write_cell(engine, prev_cell, 0, 0, 0, 0, 0)
    write_cell(engine, dst_cell, pb_buf, dst_canvas, 8, 4, 3)
    engine.call(0x60B7B0, px_reader, prev_cell, ecx=pb_ptr_slot, edx=0, regs={"esi": dst_cell}, limit=CALL_LIMIT)
    assert read_rect(engine, dst_canvas, 8, 4, 3) == [b"\x5a" * 4] * 3
    assert engine.r32(pb_ptr_slot) == pb_buf + 4
    cases += 1

    # 5. 1-bit pixel decode (pb[0] != pb[1] and pb[1] == pb[2]) with eq bit = 0.
    palette_1b = bytes([0x10, 0x20, 0x20, 0x30])
    engine.mu.mem_write(pb_buf, palette_1b)
    engine.w32(pb_ptr_slot, pb_buf)
    indices_1b = [0, 1, 1, 0, 1, 0, 0, 1]
    eq_reader = make_reader(engine, pack_bits_lsb([(0, 1)]))
    px_reader = make_reader(engine, pack_bits_lsb([(v, 1) for v in indices_1b]))
    write_cell(engine, prev_cell, 1, src_canvas, 8, 4, 2)
    write_cell(engine, dst_cell, pb_buf, dst_canvas, 8, 4, 2)
    engine.call(0x60B7B0, px_reader, prev_cell, ecx=pb_ptr_slot, edx=eq_reader, regs={"esi": dst_cell}, limit=CALL_LIMIT)
    want_1b = [bytes(palette_1b[i] for i in indices_1b[:4]), bytes(palette_1b[i] for i in indices_1b[4:])]
    assert read_rect(engine, dst_canvas, 8, 4, 2) == want_1b
    assert engine.r32(pb_ptr_slot) == pb_buf + 4
    cases += 1

    # 6. 2-bit pixel decode (all 4 palette entries distinct), 4x4 cell.
    palette_2b = bytes([0x00, 0x41, 0x82, 0xC3])
    engine.mu.mem_write(pb_buf, palette_2b)
    engine.w32(pb_ptr_slot, pb_buf)
    indices_2b = [0, 1, 2, 3, 3, 2, 1, 0, 1, 3, 0, 2, 2, 0, 3, 1]
    px_reader = make_reader(engine, pack_bits_lsb([(v, 2) for v in indices_2b]))
    write_cell(engine, dst_cell, pb_buf, dst_canvas, 8, 4, 4)
    engine.call(0x60B7B0, px_reader, prev_cell, ecx=pb_ptr_slot, edx=0, regs={"esi": dst_cell}, limit=CALL_LIMIT)
    want_2b = [bytes(palette_2b[i] for i in indices_2b[r * 4:(r + 1) * 4]) for r in range(4)]
    assert read_rect(engine, dst_canvas, 8, 4, 4) == want_2b
    assert engine.r32(pb_ptr_slot) == pb_buf + 4
    cases += 1
    return cases


def encode_dc6_rle(rows, bottom_up):
    """Reference encoder matching FUN_0060bdb0's DC6-style RLE stream."""
    ordered = rows if bottom_up else list(reversed(rows))
    out = bytearray()
    for row in ordered:
        col = 0
        width = len(row)
        while col < width:
            start = col
            while col < width and row[col] == 0:
                col += 1
            if col >= width:
                break
            while start < col:
                chunk = min(col - start, 0x7F)
                out.append(0x80 | chunk)
                start += chunk
            opaque_start = col
            while col < width and row[col] != 0:
                col += 1
            while opaque_start < col:
                chunk = min(col - opaque_start, 0x7F)
                out.append(chunk)
                out.extend(row[opaque_start:opaque_start + chunk])
                opaque_start += chunk
        out.append(0x80)
    return bytes(out)


def check_rle_pack(engine):
    """FUN_0060bdb0: DC6 RLE packing, row flip on bottom_up == 0, 0x7f chunking and size check."""
    logs = []
    engine.hook(0x410610, lambda m: (logs.append(m.arg(0)), 0)[1], 0)

    frame_rec = engine.alloc(0x30)
    pixels = engine.alloc(0x400)
    cel_hdr = engine.alloc(0x400)
    out_buf = cel_hdr + 0x20

    test_images = (
        ("empty-2x2", 2, 2, 4, 0, [b"\x00\x00", b"\x00\x00"]),
        ("mixed-topdown-flip", 5, 3, 8, 0, [b"\x00\x11\x22\x00\x00", b"\x00\x00\x00\x00\x00", b"\x33\x00\x44\x55\x00"]),
        ("mixed-bottomup", 5, 3, 8, 1, [b"\x00\x11\x22\x00\x00", b"\x00\x00\x00\x00\x00", b"\x33\x00\x44\x55\x00"]),
        ("long-transparent-140", 141, 1, 144, 0, [b"\x00" * 140 + b"\x77"]),
        ("boundary-opaque-127", 127, 1, 128, 1, [bytes((i % 200) + 1 for i in range(127))]),
        ("long-opaque-140", 140, 1, 144, 1, [bytes((i % 200) + 1 for i in range(140))]),
    )
    cases = 0
    for label, width, height, pitch, bottom_up, rows in test_images:
        logs.clear()
        for idx, row in enumerate(rows):
            engine.mu.mem_write(pixels + idx * pitch, row)
        want = encode_dc6_rle(rows, bottom_up)
        engine.w32(frame_rec + 0x04, width)
        engine.w32(frame_rec + 0x08, height)
        engine.w32(frame_rec + 0x20, pixels)
        engine.w32(frame_rec + 0x24, pitch)
        engine.w32(frame_rec + 0x28, bottom_up)
        engine.w32(cel_hdr + 0x1C, len(want))
        engine.mu.mem_write(out_buf, b"\xcc" * (len(want) + 8))
        got = engine.call(0x60BDB0, out_buf, ecx=frame_rec, edx=cel_hdr, limit=CALL_LIMIT)
        assert got == 1, (label, got)
        assert engine.read(out_buf, len(want)) == want, label
        assert logs == [], (label, logs)
        cases += 1

    # Coded-size mismatch returns 0 and logs "Sprite Decompression Error".
    logs.clear()
    engine.w32(cel_hdr + 0x1C, len(want) + 1)
    got = engine.call(0x60BDB0, out_buf, ecx=frame_rec, edx=cel_hdr, limit=CALL_LIMIT)
    assert got == 0, got
    assert logs == [LOG_ERR_STR], logs
    cases += 1
    return cases


CHECKS = (
    ("FUN_0060b7b0", 0x60B7B0, check_cell_decode),
    ("FUN_0060bdb0", 0x60BDB0, check_rle_pack),
)

# One real defect per target: (address, original bytes, broken bytes).
MUTATIONS = {
    "FUN_0060b7b0": (
        (0x60B980, b"\x04", b"\x08"),
        (0x60B8FA, b"\x04", b"\x02"),
        (0x60B7B0, None, b"\xc2\x08\x00"),
    ),
    "FUN_0060bdb0": (
        (0x60BE90, b"\x7f", b"\x7e"),
        (0x60BECD, b"\x80", b"\x81"),
        (0x60BDB0, None, b"\xc2\x04\x00"),
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
