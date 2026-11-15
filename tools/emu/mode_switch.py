# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d: may a player unit enter get-hit (4) or block (9) from its current mode?  The oracle for
"a get-hit is not restarted by a hit" (d2d Fight::hurt_side).

    set -o pipefail; ../../build/tests/test_hit_recovery --hit-modes | grep '^M ' > d2d.tsv
    uv run python mode_switch.py game > game.tsv         # game.exe's FUN_0057edd0 under unicorn (seconds)
    # game.exe: $GAMEEXE, else the data path emu.py finds (D2_MPQ_DIR/bin/game.exe); emu.py: $EMU, else this directory
    uv run python mode_switch.py compare game.tsv d2d.tsv
    set -o pipefail; ../../build/tests/test_hit_recovery --knock-modes | grep '^K ' > knock.tsv
    uv run python mode_switch.py compare-knock game.tsv knock.tsv     # a knocking hit: FUN_00580a70 asks FUN_0057edd0 for new mode 0x13

`game` runs FUN_0057edd0 (may unit EAX enter mode EBX; ECX = the game; called by FUN_005809d0, the server's player mode switch)
over class 3 (Paladin; classes 0..6 are run too and a cell whose answer is class-dependent is printed as `C cls cur new queued allowed`),
current mode 0..19 (PlrMode order: DT NU WL RN GH TN TW A1 A2 BL SC TH KK S1 S2 S3 S4 DD SQ KB; 19 = 0x13, the knockback),
new mode 4, 9 and 19 (0x13, the knockback FUN_00580a70 asks for), with (queued = 1) and without (queued = 0) a pending type-1 mode-end event.
Hooks as hr_modeswitch.py (decomp20): FUN_005415a0 (the unit's queued type-1 event frame) returns the case's value, unit +0x60 = 0
(the branch FUN_0057edd0 takes for a unit whose +0x60 is not null refuses more, never less: a player holding a cursor item is
refused mode 4 from every mode; run, not modelled in d2d).
Output line: `M cur new queued allowed`.

`compare`: d2d's `M mode after restarted` (one big hit, standing in `mode` with a hold running, i.e. an event pending = queued 1;
mode 19 is a knockback slide, which d2d draws as GH with a knock set). d2d entered GH when restarted == 1 or (after == 4 and mode
not in (4, 19)); game.exe allowed is its queued=1 answer for new mode 4. Both files must carry every cell exactly once and no
other line: a missing, duplicated or malformed line is an error (exit 1), never a cell skipped. `selftest` checks that.
Cells the d2d rule leaves to other claims are listed and excluded by name, never silently: the swing modes A1 KK S1 S3 S4 (d2d never
interrupts a swing: card P04) and the dead modes DT DD (a dead unit)."""
import os
import sys

NAMES = "DT NU WL RN GH TN TW A1 A2 BL SC TH KK S1 S2 S3 S4 DD SQ KB".split()
NMODES = len(NAMES)    # 0..19
EXCLUDED = {"DT": "dead unit", "DD": "dead unit", "A1": "swing, P04", "KK": "swing, P04", "S1": "swing, P04", "S3": "swing, P04", "S4": "swing, P04"}
PALADIN = 3
NEWS = (4, 9, 19)       # the new modes asked: get-hit, block, and 0x13, the knockback (FUN_00580a70 asks FUN_0057edd0 with it at 0x580adc)
FRAME = 5000


def game():
    sys.path.insert(0, os.environ.get("EMU", os.path.dirname(os.path.abspath(__file__))))
    import emu
    exe = os.environ.get("GAMEEXE") or os.path.join(emu.data_path(), "bin", "game.exe")
    if not os.path.isfile(exe):
        sys.exit("mode_switch.py game: no game.exe at %s (set D2_MPQ_DIR to the game data directory, or GAMEEXE to the file)" % exe)
    e = emu.Emu(exe)
    state = {"end": 0}
    e.hook(0x5415a0, lambda e_: state["end"], 1)
    unit = e.alloc(0x200)
    gm = e.alloc(0x2000)
    e.w32(gm + 0xa8, FRAME)
    out = {}
    for cls in range(7):
        for queued in (0, 1):
            state["end"] = FRAME + 40 if queued else 0
            for cur in range(NMODES):
                for new in NEWS:
                    e.w32(unit, 0)
                    e.w32(unit + 4, cls)
                    e.w32(unit + 0x10, cur)
                    e.w32(unit + 0x60, 0)
                    out[(cls, queued, cur, new)] = e.call(0x57edd0, ecx=gm, regs={"eax": unit, "ebx": new}) & 0xff != 0
    n = 0
    for (cls, queued, cur, new), v in sorted(out.items()):
        if cls == PALADIN:
            print("M %d %d %d %d" % (cur, new, queued, int(v)))
            n += 1
        elif v != out[(PALADIN, queued, cur, new)]:
            print("C %d %d %d %d %d" % (cls, cur, new, queued, int(v)))
    if n == 0:
        sys.exit(1)


def die(msg):
    print("error: " + msg)
    sys.exit(1)


def parse_game(lines):
    """{(mode, new, queued): allowed}; every (mode 0..19) x (new 4, 9, 19) x (queued 0, 1) exactly once, `M` lines only (a `C` line is ignored)."""
    allowed = {}
    for k, line in enumerate(lines, 1):
        f = line.split()
        if not f or f[0] == "C":
            continue
        try:
            if f[0] != "M" or len(f) != 5:
                raise ValueError
            mode, new, queued, ok = (int(x) for x in f[1:])
            if mode not in range(NMODES) or new not in NEWS or queued not in (0, 1) or ok not in (0, 1):
                raise ValueError
        except ValueError:
            die("game line %d malformed: %r" % (k, line.strip()))
        if (mode, new, queued) in allowed:
            die("game line %d repeats cell %d %d %d" % (k, mode, new, queued))
        allowed[(mode, new, queued)] = ok
    missing = ["%d %d %d" % (m, n, q) for m in range(NMODES) for n in NEWS for q in (0, 1) if (m, n, q) not in allowed]
    if missing:
        die("game file lacks %d cells, first: %s" % (len(missing), missing[0]))
    return allowed


def parse_d2d(lines):
    """{mode: (after, restarted)}; every mode 0..19 exactly once, `M` lines only."""
    cells = {}
    for k, line in enumerate(lines, 1):
        f = line.split()
        if not f:
            continue
        try:
            if f[0] != "M" or len(f) != 4:
                raise ValueError
            mode, after, restarted = (int(x) for x in f[1:])
            if mode not in range(NMODES) or after not in range(NMODES) or restarted not in (0, 1):
                raise ValueError
        except ValueError:
            die("d2d line %d malformed: %r" % (k, line.strip()))
        if mode in cells:
            die("d2d line %d repeats mode %d" % (k, mode))
        cells[mode] = (after, restarted)
    missing = [str(m) + " " + NAMES[m] for m in range(NMODES) if m not in cells]
    if missing:
        die("d2d file lacks %d modes: %s" % (len(missing), ", ".join(missing)))
    return cells


def judge(allowed, cells, out):
    n = bad = excluded = 0
    for mode in range(NMODES):
        after, restarted = cells[mode]
        name = NAMES[mode]
        entered = restarted == 1 or (after == 4 and mode not in (4, 19))
        g = allowed[(mode, 4, 1)]
        if name in EXCLUDED:
            excluded += 1
            out("excluded %s (%s): d2d entered %d, game allowed %d" % (name, EXCLUDED[name], int(entered), g))
            continue
        n += 1
        if int(entered) != g:
            bad += 1
            out("DIFF mode %s: d2d entered GH %d (after %s, restarted %d), game.exe allowed %d" % (name, int(entered), NAMES[after], restarted, g))
    out("%d cells, %d differ, %d excluded" % (n, bad, excluded))
    return n, bad


def compare(gpath, dpath):
    allowed = parse_game(open(gpath).read().splitlines())
    cells = parse_d2d(open(dpath).read().splitlines())
    n, bad = judge(allowed, cells, print)
    sys.exit(1 if bad or n == 0 else 0)


def parse_knock(lines):
    """{mode: (after, knock, restarted)} from d2d's `K mode after knock restarted`; every mode 0..19 exactly once, `K` lines only."""
    cells = {}
    for k, line in enumerate(lines, 1):
        f = line.split()
        if not f:
            continue
        try:
            if f[0] != "K" or len(f) != 5:
                raise ValueError
            mode, after, knock, restarted = (int(x) for x in f[1:])
            if mode not in range(NMODES) or after not in range(NMODES) or knock not in (0, 1) or restarted not in (0, 1):
                raise ValueError
        except ValueError:
            die("d2d knock line %d malformed: %r" % (k, line.strip()))
        if mode in cells:
            die("d2d knock line %d repeats mode %d" % (k, mode))
        cells[mode] = (after, knock, restarted)
    missing = [str(m) + " " + NAMES[m] for m in range(NMODES) if m not in cells]
    if missing:
        die("d2d knock file lacks %d modes: %s" % (len(missing), ", ".join(missing)))
    return cells


def judge_knock(allowed, cells, out):
    """A knocking hit (new mode 0x13, queued 1 = a hold running): d2d entered the knockback when the hold's end moved. Excluded by name:
    the swing modes (P04: d2d never interrupts a swing) and the dead modes (a dead unit)."""
    n = bad = excluded = 0
    for mode in range(NMODES):
        after, knock, restarted = cells[mode]
        name = NAMES[mode]
        entered = restarted == 1
        g = allowed[(mode, 19, 1)]
        if name in EXCLUDED:
            excluded += 1
            out("excluded %s (%s): d2d entered %d, game allowed %d" % (name, EXCLUDED[name], int(entered), g))
            continue
        n += 1
        if int(entered) != g:
            bad += 1
            out("DIFF mode %s: d2d started the knockback %d (after %s, knock %d, restarted %d), game.exe allowed 0x13 %d" % (name, int(entered), NAMES[after], knock, restarted, g))
    out("%d cells, %d differ, %d excluded" % (n, bad, excluded))
    return n, bad


def compare_knock(gpath, dpath):
    allowed = parse_game(open(gpath).read().splitlines())
    cells = parse_knock(open(dpath).read().splitlines())
    n, bad = judge_knock(allowed, cells, print)
    sys.exit(1 if bad or n == 0 else 0)


def selftest():
    """compare must FAIL on a missing, duplicated or malformed line and pass on a complete matching pair (no game, no emulator)."""
    full_game = ["M %d %d %d %d" % (m, n, q, 0 if NAMES[m] in ("DT", "GH", "BL", "DD") else 1) for m in range(NMODES) for n in NEWS for q in (0, 1)]
    refused = ("DT", "GH", "BL", "DD")
    full_d2d = ["M %d %d %d" % (m, m if NAMES[m] in refused else 4, 0 if NAMES[m] in refused else 1) for m in range(NMODES)]
    assert full_d2d[19] == "M 19 4 1"      # a knocked-back player: the hit began a get-hit; game.exe allows it

    def fails(g, d):
        sys.stdout = open(os.devnull, "w")
        try:
            judge(parse_game(g), parse_d2d(d), lambda s: None)
        except SystemExit as e:
            return e.code == 1
        finally:
            sys.stdout = sys.__stdout__
        return False

    def fails_knock(g, d):
        try:
            judge_knock(parse_game(g), parse_knock(d), lambda s: None)
        except SystemExit as e:
            return e.code == 1
        return False

    ok = [not fails(full_game, full_d2d)]
    ok.append(fails(full_game, full_d2d[:1]))                                       # one line only
    ok.append(fails(full_game, full_d2d[:-1]))                                      # mode 19 missing
    ok.append(fails(full_game, full_d2d + [full_d2d[0]]))                           # duplicate
    ok.append(fails(full_game, full_d2d[:5] + ["M 5 x 0"] + full_d2d[6:]))          # malformed
    ok.append(fails(full_game, full_d2d[:5] + ["M 5 4"] + full_d2d[6:]))            # short line
    ok.append(fails(full_game[:-1], full_d2d))                                      # a game cell missing
    ok.append(fails([], full_d2d))                                                  # empty game file
    bad_d2d = list(full_d2d)
    bad_d2d[19] = "M 19 4 0"               # d2d refuses the hit in a knock: judge must count the difference
    ok.append(judge(parse_game(full_game), parse_d2d(bad_d2d), lambda s: None)[1] == 1)
    # the knock cells: game.exe refuses 0x13 from GH, BL and the dead modes; a d2d that moves the hold in GH differs there
    full_knock = ["K %d %d %d %d" % (m, m if NAMES[m] in refused else 4, 0, 0 if NAMES[m] in refused else 1) for m in range(NMODES)]
    ok.append(judge_knock(parse_game(full_game), parse_knock(full_knock), lambda s: None)[1] == 0)
    bad_knock = list(full_knock)
    bad_knock[4] = "K 4 4 1 1"             # d2d knocks a player in a get-hit again
    ok.append(judge_knock(parse_game(full_game), parse_knock(bad_knock), lambda s: None)[1] == 1)
    ok.append(fails_knock(full_game, full_knock[:-1]))
    print("selftest: %d of %d checks" % (sum(ok), len(ok)))
    sys.exit(0 if all(ok) else 1)


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "game":
        game()
    elif len(sys.argv) == 4 and sys.argv[1] == "compare":
        compare(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "compare-knock":
        compare_knock(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 2 and sys.argv[1] == "selftest":
        selftest()
    else:
        sys.exit(__doc__)
