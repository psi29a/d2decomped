# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the tint colormap lookup FUN_005038d0 in stock game.exe 1.14d.

Run: export D2_MPQ_DIR=/home/gui/Projects/d2decomped/dev/data
     uv run python tint_colormap.py [--mutation-check]
     uv run python tint_colormap.py inc > ../../tests/tint_of_cases.inc

FUN_005038d0 (ECX = a layer's stored tint byte) returns the address of one
256-byte colormap in the table at 0x842710, or 0. Its only caller,
FUN_00503ba0 (at 0x503dad), calls it for a stored byte that is not 0xff, and
FUN_005066c0 stores the d2s tint byte minus one (0xff stays 0xff). The claim,
run on game.exe's code for every stored byte and wider values:
  - colour = ECX & 0x1f, transform = ECX >> 5 (arithmetic);
  - colour above 0x14 gives 0;
  - transform 0 gives (colour + 0x348) * 0x100, that is Transform 8's set
    (8 * 0x69 + colour); transforms 1, 2 and 5..8 give
    (transform * 0x69 + colour) * 0x100; 3, 4 and anything else give 0;
and so, through the stored = raw - 1 step, d2d's `compcode::tint_of(raw)`
(components/compcode/compcode.hpp) says the same for every raw byte 0..255.
`inc` prints the raw -> (transform, colour) rows tests/test_compcode_tint.cpp
reads. FUN_005066c0's minus-one store is read, not run here. No MPQs are read.
"""
import argparse
import hashlib
import sys
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TARGET, TABLE = 0x5038D0, 0x842710
M32 = 0xFFFFFFFF
MUT = set()


def tint_of(packed):
    """d2d's compcode::tint_of(raw) re-stated: (transform, colour) or None."""
    if packed == 0xFF or packed == 0:
        return None
    transform, colour = (packed - 1) >> 5, (packed - 1) & 31
    if transform == 0 and "nowrap" not in MUT:
        transform = 8
    limit = 22 if "colour" in MUT else 21
    return (transform, colour) if colour < limit and (tints_with(transform) or "with" in MUT) else None


def tints_with(transform):
    """compcode::tints_with: the transforms that have a colormap set (FUN_00600c20)."""
    return 0 < transform <= 8 and transform not in (3, 4)


def game_pair(machine, stored):
    """Run FUN_005038d0 on a stored byte (or wide value); decode the address to (transform, colour) or None."""
    got = machine.call(TARGET, ecx=stored & M32)
    if got == 0:
        return None
    index = (got - TABLE) // 0x100
    assert (got - TABLE) % 0x100 == 0, hex(got)
    for transform in range(9):
        if 0 <= index - transform * 0x69 < 21:
            return transform, index - transform * 0x69
    raise AssertionError(hex(got))


def stored_of(raw):
    """What FUN_005066c0 stores for a d2s tint byte (read in its loop: the byte minus one, 0xff stays)."""
    return 0xFF if raw == 0xFF else (raw - 1) & 0xFF


def rows(machine):
    """Every case: (label, value, game pair, d2d pair); the d2d pair is None where a case is raw-less."""
    out = []
    for value in list(range(256)) + [0x100, 0x120, 0x7FFFFFE0, 0x80000000, 0x80000001, 0xFFFFFFE0, 0xFFFFFFFF, 0xFFFFFF00]:
        out.append(("stored", value, game_pair(machine, value)))
    return out


def run(mutation):
    """Compare game.exe to the model; return (failures, cases)."""
    MUT.clear()
    MUT.update(mutation)
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    machine = emu.Emu(str(exe))
    bad = cases = 0
    # the function itself against the formula in the docstring
    for _, value, pair in rows(machine):
        signed = value - (1 << 32) if value >> 31 else value
        colour, transform = value & 31, signed >> 5
        if colour > 0x14:
            want = None
        elif transform == 0:
            want = (8, colour)
        elif 0 < transform < 9 and transform not in (3, 4):
            want = (transform, colour)
        else:
            want = None
        cases += 1
        if pair != want:
            bad += 1
            print(f"stored {value:#x}: game {pair} model {want}")
    # d2d's tint_of(raw) against the game through the stored = raw - 1 step
    for raw in range(256):
        cases += 1
        stored = stored_of(raw)
        pair = None if stored == 0xFF else game_pair(machine, stored)   # FUN_00503ba0 skips 0xff
        if raw == 0:
            pair = None             # stored 0xff: no tint
        if pair != tint_of(raw):
            bad += 1
            print(f"raw {raw:#x}: game {pair} d2d {tint_of(raw)}")
    return bad, cases, machine


def main():
    """Run the oracle; `inc` prints the C++ fixture; --mutation-check proves the mutants are caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", default="check", choices=("check", "inc"))
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    bad, cases, machine = run(())
    if bad:
        print(f"FAIL: {bad} of {cases} differ", file=sys.stderr)
        sys.exit(1)
    if args.mode == "inc":
        print("// Stock game.exe 1.14d, FUN_005038d0 through FUN_005066c0's byte - 1; generated by tools/emu/tint_colormap.py inc.")
        print("// raw d2s tint byte, transform (0: none), colour")
        for raw in range(256):
            stored = stored_of(raw)
            pair = None if raw == 0 or stored == 0xFF else game_pair(machine, stored)
            print(f"    {{ {raw}, {pair[0] if pair else 0}, {pair[1] if pair else 0} }},")
        return
    print(f"ok: {cases} FUN_005038d0 cases (every stored byte, 8 wide values, every raw byte through byte - 1)")
    if args.mutation_check:
        for name in ("nowrap", "colour", "with"):
            bad, _, _ = run((name,))
            print(f"mutant {name}: {'caught' if bad else 'NOT CAUGHT'} ({bad})")
            if not bad:
                sys.exit(1)
        print("mutation check ok")


if __name__ == "__main__":
    main()
