# SPDX-License-Identifier: GPL-3.0-or-later
"""Whirlwind's two-weapon test and its hand pick as the oracle: FUN_005d92d0 and FUN_00535bc0 (weapsel 2).

Run: uv run python whirl_hands.py [--mutation-check]
     uv run python whirl_hands.py inc --out ../../tests/whirl_hands_cases.inc

Both run natively on the loaded 1.14d tables (skill_tips.boot), with everything they call: FUN_0063c050 (the
body cell by component), FUN_0063bef0 (the wielded weapon), FUN_0063c9b0 (the default hand), FUN_00629bb0 (the
ItemTypes test), FUN_00629fe0 (the class key), FUN_00628660 (the component), FUN_0062a4e0 (usable),
FUN_006235a0 (a two-handed class). Hooked: FUN_00620250 (the unit's skill node) answers a fake node whose
record has weapsel 2 at +0x168 and whose flags (+0xc) are the case's 0 or 0x2000.
A player unit (type 0, its class) holds an inventory (magic 0x1020304, one grid at +0x14, the wielded id at
+0x1c) whose body grid's eleven cells hold fake item units (type 4, class row, id, data +0x14 with flags +0x18).
Each case: up to two hand items (cells 4, 5) and maybe a third in another body cell, their flags (0x100,
0x4000), which id the inventory calls wielded, the class and the flag. game.exe's answers, against
rules::two_melee and rules::weapsel2_pick re-stated here (the C++ is checked by tests/game/test_whirl_hands.cpp
on the frozen cases): FUN_005d92d0's 0 / 1, FUN_00535bc0's item as its cell (-1 none).
"""
import argparse
import hashlib
import random
import sys
import tempfile
from pathlib import Path

import emu
import skill_tips

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
TWO_MELEE, PICK = 0x5D92D0, 0x535BC0
CALL_LIMIT = 200_000                            # instructions per call; none of these comes near


def capped(e, addr, *args, **regs):
    """e.call with an instruction cap; a call that stops short of its return is a failure, not a result."""
    got = e.call(addr, *args, limit=CALL_LIMIT, **regs)
    if e.mu.reg_read(emu.UC_X86_REG_EIP) != emu.RET_MAGIC:
        raise RuntimeError(f"FUN_{addr:08x} stopped at {e.mu.reg_read(emu.UC_X86_REG_EIP):08x} before its return")
    return got
MUT = set()
# Item codes that go in a hand (and two that don't): every wclass, the throwing kinds, claws, an orb, shields.
CODES = ("hax", "ssd", "2hs", "clb", "mac", "scp", "wnd", "sst", "lax", "dgr", "spr", "jav", "tkf", "tax", "gps",
         "ktr", "sbw", "am1", "lxb", "ob1", "buc", "ned", "aqv", "cap")
SLOTS = (1, 2, 3, 6, 7, 8, 9, 10)


class Game:
    """game.exe's tables, a fake player, its inventory and the item units."""

    def __init__(self):
        """Boot the tables, find the codes' records, lay out the fake objects."""
        self.e = e = skill_tips.boot()
        rows = {}
        for row in range(e.r32(0x96CA58)):
            rec = capped(e, 0x6335F0, row)
            rows.setdefault(e.read(rec + 0x80, 4).decode("latin1").strip("\0 "), row)
        self.rows = {code: rows[code] for code in CODES}
        self.unit, self.inv, self.grid, self.cells = e.alloc(0x100), e.alloc(0x40), e.alloc(0x10), e.alloc(0x40)
        self.node, self.record = e.alloc(0x20), e.alloc(0x240)
        self.items = [e.alloc(0x40) for _ in range(3)]
        self.datas = [e.alloc(0x80) for _ in range(3)]
        e.mu.mem_write(self.record + 0x168, b"\x02")
        e.w32(self.node, self.record)
        e.hook(0x620250, lambda _: self.node, 1)
        e.w32(self.unit + 0x60, self.inv)
        e.w32(self.inv, 0x1020304)
        e.w32(self.inv + 0x14, self.grid)
        e.w32(self.inv + 0x18, 1)
        e.mu.mem_write(self.grid + 8, e.read(0x74479C, 2))   # the body grid's size, as FUN_0063add0 checks it
        e.w32(self.grid + 0xC, self.cells)
        self.attrs = {code: self.attributes(code) for code in CODES}

    def attributes(self, code):
        """game.exe's reading of one base: ItemTypes 0x2e and 0x2d (FUN_00629bb0), class key, component."""
        e, item = self.e, self.items[0]
        e.w32(item, 4)
        e.w32(item + 4, self.rows[code])
        e.w32(item + 0x14, self.datas[0])
        return (capped(e, 0x629BB0, item, 0x2E), capped(e, 0x629BB0, item, 0x2D), capped(e, 0x629FE0, item) & 0xFFFF, capped(e, 0x628660, item) & 0xFF)

    def run(self, case):
        """One case: (FUN_005d92d0's answer, the cell of FUN_00535bc0's item or -1)."""
        e = self.e
        held, wielded, cls, flag = case
        e.mu.mem_write(self.cells, bytes(0x40))
        e.w32(self.unit, 0)
        e.w32(self.unit + 4, cls)
        e.w32(self.node + 0xC, flag)
        cell_of = {}
        for k, (cell, code, flags) in enumerate(held):
            item, data = self.items[k], self.datas[k]
            e.mu.mem_write(item, bytes(0x40))
            e.mu.mem_write(data, bytes(0x80))
            e.w32(item, 4)
            e.w32(item + 4, self.rows[code])
            e.w32(item + 0xC, 0x100 + k)
            e.w32(item + 0x14, data)
            e.w32(data + 0x18, flags)
            e.w32(self.cells + 4 * cell, item)
            cell_of[item] = cell
        e.w32(self.inv + 0x1C, 0xFFFFFFFF if wielded < 0 else 0x100 + wielded)
        dual = capped(e, TWO_MELEE, regs={"eax": self.unit})
        picked = capped(e, PICK, ecx=self.unit)
        return dual, cell_of.get(picked, -1) if picked else -1


def worn(game, case):
    """The case as rules::WornItem rows in cell order: cell, key, component, melee, weapon, usable, wielded."""
    held, wielded, _, _ = case
    rows = []
    for k, (cell, code, flags) in sorted(enumerate(held), key=lambda entry: entry[1][0]):
        melee, weapon, key, component = game.attrs[code]
        rows.append((cell, key, component, int(bool(melee)), int(bool(weapon)), int(flags & 0x4100 == 0), k))
    # FUN_0063bef0: the left hand's (cell 5) then the right's (cell 4), a weapon with the inventory's id.
    marked = -1
    for cell in (5, 4):
        for row in rows:
            if row[0] == cell and row[4] and row[6] == wielded and marked < 0: marked = cell
    return [row[:6] + (int(row[0] == marked),) for row in rows]


def by_component(rows, component):
    """FUN_0063c050 re-stated: the first body cell whose item is that component (1hs, 1ht, ht1 by the wielded mark)."""
    for row in rows:
        cell, key, comp, _, _, _, wielded = row
        if key in (2, 3, 12) and "by_cell" not in MUT:
            if (component == 6 and not wielded) or (component == 5 and wielded): return row
        elif key in (2, 3, 12):
            if (component == 5 and cell == 4) or (component == 6 and cell == 5): return row
        elif comp == component:
            return row
    return None


def model(rows, cls, flag):
    """rules::two_melee and rules::weapsel2_pick re-stated: (dual, picked cell or -1)."""
    right, left = by_component(rows, 5), by_component(rows, 6)
    if "old_dual" in MUT:
        hand = lambda cell: next((r for r in rows if r[0] == cell and r[4]), None)
        dual = int(bool(hand(4) and hand(5)))
    else:
        dual = int(bool(right and left and right[3] and left[3]))
    default = next((r for cell in (4, 5) for r in rows if r[0] == cell and r[4] and r[5]), None)
    pick = default
    if cls in (4, 6):
        pick = left if (not right or not right[4] or (left and left[4] and flag & 0x2000)) else right
        if not (pick and pick[4] and (pick[5] or "no_usable" in MUT)): pick = default
    return dual, pick[0] if pick else -1


def cases():
    """Hands of every kind, broken or flagged, the wielded mark on either, none or a stranger, both flags."""
    rng = random.Random(0x5D92D0)
    out = []
    flags = lambda: rng.choice((0, 0, 0, 0x100, 0x4000, 0x4100))
    for _ in range(3000):
        held = []
        for cell in (4, 5):
            if rng.random() < 0.9: held.append((cell, rng.choice(CODES), flags()))
        if rng.random() < 0.3: held.append((rng.choice(SLOTS), rng.choice(CODES), flags()))
        wielded = rng.choice([-1, 2] + list(range(len(held))) * 2)
        out.append((tuple(held), wielded, rng.choice((4, 4, 6, 0, 1)), rng.choice((0, 0x2000))))
    return out


def run(game, mutation):
    """Compare game.exe to the model; return (bad, results)."""
    MUT.clear()
    MUT.update(mutation)
    bad, results = 0, []
    for case in cases():
        got = game.run(case) if not mutation else game.answers[len(results)]
        rows = worn(game, case)
        want = model(rows, case[2], case[3])
        results.append((case, rows, got))
        if got != want:
            bad += 1
            if bad <= 10 and not mutation:
                print(f"{case}: game {got} model {want} rows {rows}", file=sys.stderr)
    if not mutation: game.answers = [got for _, _, got in results]
    return bad, results


def write_fixture(out, text):
    """Write `text` to `out` through a staging file in the same directory, removed on failure."""
    handle = tempfile.NamedTemporaryFile("w", dir=Path(out).parent, prefix=Path(out).name + ".", suffix=".tmp", delete=False)
    try:
        with handle:
            handle.write(text)
        Path(handle.name).replace(out)
    except BaseException:
        Path(handle.name).unlink(missing_ok=True)
        raise


def fixture(game, results):
    """The C++ fixture: the bases' readings, then each case's rows and game.exe's answers."""
    lines = ["// Stock game.exe 1.14d, FUN_005d92d0 and FUN_00535bc0 (weapsel 2); generated by tools/emu/whirl_hands.py inc.",
             "// code, ItemTypes 0x2e (mele), 0x2d (weap), class key (FUN_00629fe0), component (FUN_00628660)",
             "inline constexpr WhirlBase kWhirlBases[] = {"]
    lines += [f"    {{ \"{code}\", {int(bool(m))}, {int(bool(w))}, {k}, {c} }}," for code, (m, w, k, c) in game.attrs.items()]
    lines += ["};",
              "// class, flag 0x2000, rows used; rows (cell, class key, component, melee, weapon, usable, wielded); dual, picked cell",
              "inline constexpr WhirlCase kWhirlCases[] = {"]
    for (_, _, cls, flag), rows, (dual, picked) in results[:2000]:
        body = ", ".join("{ " + ", ".join(str(v) for v in row) + " }" for row in rows)
        lines.append(f"    {{ {cls}, {int(bool(flag))}, {len(rows)}, {{ {{ {body} }} }}, {dual}, {picked} }},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    """Run the oracle; `inc` prints the C++ fixture; --mutation-check proves the mutants are caught."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", default="check", choices=("check", "inc"))
    parser.add_argument("--mutation-check", action="store_true")
    parser.add_argument("--out", help="with inc: write the fixture here, after the run has passed")
    args = parser.parse_args()
    if args.mode == "inc" and args.mutation_check:
        parser.error("inc and --mutation-check are separate runs")
    if args.out and args.mode != "inc":
        parser.error("--out goes with inc")
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if not exe.is_file():
        raise SystemExit(f"no game.exe at {exe}: set D2_MPQ_DIR to the folder whose bin/ holds stock 1.14d game.exe")
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    game = Game()
    bad, results = run(game, ())
    if bad:
        print(f"FAIL: {bad} of {len(results)} differ", file=sys.stderr)
        sys.exit(1)
    if args.mode == "inc":
        text = fixture(game, results)
        if args.out:
            write_fixture(args.out, text)
        else:
            sys.stdout.write(text)
        return
    duals = sum(got[0] for _, _, got in results)
    print(f"ok: {len(results)} FUN_005d92d0 / FUN_00535bc0 weapsel 2 cases ({duals} two-weapon), 0 differ")
    if args.mutation_check:
        for name in ("old_dual", "no_usable", "by_cell"):
            missed, _ = run(game, (name,))
            if not missed:
                print(f"mutation {name}: NOT caught", file=sys.stderr)
                sys.exit(1)
            print(f"mutation {name}: caught ({missed} differ)")
        print("mutation check ok")


if __name__ == "__main__":
    main()
