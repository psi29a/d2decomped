# SPDX-License-Identifier: GPL-3.0-or-later
"""game.exe 1.14d: does a get-hit (mode 4) interrupt the skill a player is using?  The oracle for
"a hit interrupts an interruptible swing or cast" (d2d Fight::hurt_side, fork branch fid/hitrec-interrupt).

    set -o pipefail; ../../build/tests/test_hit_recovery --interrupt | grep '^I ' > d2d.tsv
    uv run python interrupt_swing.py game > game.tsv      # FUN_0057edd0 and FUN_0057eec0 under unicorn (seconds)
    uv run python interrupt_swing.py compare game.tsv d2d.tsv

The server's mode switch FUN_005809d0 (the get-hit start of FUN_0057cee0, 0x57d383) asks two questions and needs both to be yes:
FUN_0057edd0 (may this unit enter the new mode from its current mode, see mode_switch.py) and, because its fifth argument is 0,
FUN_0057eec0 (the used-skill gate). The gate tests state 0x36 (uninterruptable: a Leap, Leap Attack or Whirlwind under way) first
and refuses every new mode (0x57eec7); then, with a used skill whose Skills.txt `interrupt` column is clear (the row's flags byte +7
against the mask at 0x6ce284), it refuses the hit unless the unit is idle. `game` runs both for every Skills.txt row as the unit's
used skill (FUN_00620250 hooked to a fake pointer, FUN_00643ce0 to the row id, FUN_00639df0, the state test, answering for ONE
unit state at a time) and for the current modes A1 A2 SC TH KK S1 S3 S4 (a mode-end event queued, as a swing has one), new mode 4
and class 3 (Paladin); other classes are run too and a cell whose answer is class dependent (state 0 only) is printed as
`C cls skill mode allowed` (never silently).
Output line: `G skill mode state interrupted` (1 = the hit starts a get-hit and the swing is gone). `state` is 0 none, 1 state 0x36 and,
which d2d does not model (a limit, counted by `compare`, never compared): 2 state 0xf (Concentrate), 3 state 0x2a (the Paladin's
Concentration aura) with stat 0xa4 = 100 (its roll always refuses), 4 state 0x2a with stat 0xa4 = 0 (never refuses).

`compare`: d2d's `I skill mode own state after kicks seq smove` (one big hit while the Paladin's swing_skill is `skill`, pmode is
`mode` and, for state 1, a Leap or Whirlwind is under way; `own` is 1 when `mode` is the mode d2d's swing table plays that skill
in; `after` the mode right after the hit; kicks / seq / smove 0 when the queued repeats, the sequence and the movement of the swing
are gone) and `B skill after`: the barrel kick, the state World::operate leaves (swing_skill 0, pmode KK) with one big hit. game.exe
kicks a barrel with its skill 1, Kick (FUN_005868a0 -> FUN_00580a70), so the answer to compare with is the game's (Kick, KK, state
0) cell, and the `B` line must name Kick's id. The cell (skill 0, KK, state none) is the barrel's own state in d2d (Attack never plays KK), so it
is not compared as Attack; `compare` says so. A d2d cell is "interrupted" when after == 4. The d2d file must carry every
(skill, mode, state 0 and 1) cell of the skills d2d parsed exactly once and exactly one `B` line; a missing, duplicated or
malformed line is an error (exit 1). Skills Skills.txt has but d2d has no row for are counted and listed, not compared.
`selftest` checks the parser and the judge (no game).

Environment: `EMU` the directory with emu.py (default: this directory); `D2_MPQ_DIR` the game data folder with bin/game.exe;
`D2_PATCH_INSTALLER` the 1.14d patch installer (LODPatch_114d.exe; `game` loads the 1.14d tables through it, emu.py's default is
under the home directory); `D2_BUILD` the d2d build directory that holds tools/mpq-cat/mpq-cat (default `build`)."""
import os
import sys

MODES = {7: "A1", 8: "A2", 10: "SC", 11: "TH", 12: "KK", 13: "S1", 15: "S3", 16: "S4"}
KK = 12
NO_SKILL = 0        # d2d's swing_skill 0: Attack's row, and "no skill id" (the barrel kick plays KK with it)
# Unit states the gate reads: (name, the state FUN_00639df0 answers 1 for, stat 0xa4 of the aura).  d2d models 0 and 1.
STATES = {0: ("none", None, 0), 1: ("0x36", 0x36, 0), 2: ("0xf", 0xF, 0), 3: ("0x2a roll 100", 0x2A, 100), 4: ("0x2a roll 0", 0x2A, 0)}
MODELLED = (0, 1)
PALADIN = 3
FRAME = 5000


def game():
    sys.path.insert(0, os.environ.get("EMU", os.path.dirname(os.path.abspath(__file__))))   # emu.py: $EMU, else this directory
    import skill_tips
    e = skill_tips.boot()
    tables = e.r32(0x744304)
    count = e.r32(tables + 0xba0)
    cur = {"id": 0, "end": 0, "state": None, "roll": 0}
    e.hook(0x620250, lambda e_: 0xA0000000, 1)       # the unit's used skill: a fake pointer
    e.hook(0x643ce0, lambda e_: cur["id"], 3)         # its id
    e.hook(0x639df0, lambda e_: int(e_.arg(1) == cur["state"]), 2)   # the unit has the one state under test (0x36, 0x2a, 0xf) or none
    e.hook(0x6256b0, lambda e_: 0xB0000000, 2)        # the state's stat list (the aura's roll below)
    e.hook(0x625d00, lambda e_: cur["roll"] if e_.arg(1) == 0xA4 else 0, 3)   # stat 0xa4 skill_concentration
    e.hook(0x5415a0, lambda e_: cur["end"], 1)        # the unit's queued type-1 event frame
    unit = e.alloc(0x200)
    gm = e.alloc(0x2000)
    e.w32(gm + 0xa8, FRAME)
    out = {}
    for cls in range(7):
        cur["end"] = FRAME + 40
        for state, (_, number, roll) in STATES.items():
            if cls != PALADIN and state:
                continue                                   # the class gate is FUN_0057edd0's: one state shows it
            cur["state"], cur["roll"] = number, roll
            for sid in range(count):
                cur["id"] = sid
                for mode in MODES:
                    e.mu.mem_write(unit, b"\0" * 0x200)
                    e.w32(unit + 4, cls)
                    e.w32(unit + 0x10, mode)
                    e.w32(unit + 0x20, 12345)              # the unit's seed, read by the aura's roll
                    e.w32(unit + 0x60, 0)
                    switch = e.call(0x57edd0, ecx=gm, regs={"eax": unit, "ebx": 4}) & 0xff != 0
                    gate = e.call(0x57eec0, 4, 1, 0, ecx=gm, edx=unit) & 0xff != 0
                    out[(cls, sid, mode, state)] = switch and gate
    n = 0
    for (cls, sid, mode, state), v in sorted(out.items()):
        if cls == PALADIN:
            print("G %d %d %d %d" % (sid, mode, state, int(v)))
            n += 1
        elif v != out[(PALADIN, sid, mode, 0)]:
            print("C %d %d %d %d" % (cls, sid, mode, int(v)))
    if n == 0:
        sys.exit(1)


def die(msg):
    print("error: " + msg)
    sys.exit(1)


def parse_game(lines):
    """{(skill, mode, state): interrupted}, `G` lines only (a `C` line is ignored); a malformed or repeated line is an error."""
    cells = {}
    for k, line in enumerate(lines, 1):
        f = line.split()
        if not f or f[0] == "C":
            continue
        try:
            if f[0] != "G" or len(f) != 5:
                raise ValueError
            sid, mode, state, v = (int(x) for x in f[1:])
            if sid < 0 or mode not in MODES or state not in STATES or v not in (0, 1):
                raise ValueError
        except ValueError:
            die("game line %d malformed: %r" % (k, line.strip()))
        if (sid, mode, state) in cells:
            die("game line %d repeats cell %d %d %d" % (k, sid, mode, state))
        cells[(sid, mode, state)] = v
    if not cells:
        die("game file has no cells")
    return cells


def parse_d2d(lines):
    """({(skill, mode, state): (own, after, kicks, seq, smove)}, (kick id, after)); `I` lines and the one `B` line."""
    cells = {}
    barrel = None
    for k, line in enumerate(lines, 1):
        f = line.split()
        if not f:
            continue
        try:
            if f[0] == "B":
                if len(f) != 3 or barrel is not None:
                    raise ValueError
                sid, after = (int(x) for x in f[1:])
                if sid < 0 or after < -1 or after > 20:
                    raise ValueError
                barrel = (sid, after)
                continue
            if f[0] != "I" or len(f) != 9:
                raise ValueError
            sid, mode, own, state, after, kicks, seq, smove = (int(x) for x in f[1:])
            if sid < 0 or mode not in MODES or own not in (0, 1) or state not in MODELLED or after < -1 or after > 20 or min(kicks, seq, smove) < 0:
                raise ValueError
        except ValueError:
            die("d2d line %d malformed or repeated: %r" % (k, line.strip()))
        if (sid, mode, state) in cells:
            die("d2d line %d repeats cell %d %d %d" % (k, sid, mode, state))
        cells[(sid, mode, state)] = (own, after, kicks, seq, smove)
    if not cells:
        die("d2d file has no cells")
    if barrel is None:
        die("d2d file has no B (barrel kick) line")
    return cells, barrel


def judge(game_cells, d2d_parsed, out):
    d2d_cells, (kick, barrel_after) = d2d_parsed
    skills = sorted({s for s, _, _ in d2d_cells})
    missing = [(s, m, t) for s in skills for m in MODES for t in MODELLED if (s, m, t) not in d2d_cells]
    if missing:
        die("d2d file lacks %d cells, first: %s" % (len(missing), missing[0]))
    absent = sorted({s for s, _, _ in game_cells} - set(skills))
    n = bad = n_own = bad_own = left = skipped = 0
    for s in skills:
        for m in MODES:
            for t in MODELLED:
                if (s, m, t) not in game_cells:
                    die("game file lacks cell %d %d %d" % (s, m, t))
                if (s, m, t) == (NO_SKILL, KK, 0):
                    # d2d cannot tell Attack in KK from the barrel kick (both are KK with swing_skill 0, and Attack never plays KK): the cell is judged
                    # as the barrel kick below, against game.exe's Kick.
                    skipped += 1
                    continue
                own, after, kicks, seq, smove = d2d_cells[(s, m, t)]
                d = int(after == 4)
                g = game_cells[(s, m, t)]
                n += 1
                n_own += own if t == 0 else 0
                if d != g:
                    bad += 1
                    bad_own += own if t == 0 else 0
                    out("DIFF skill %d mode %s state %s%s: d2d after %d, game.exe interrupted %d" % (s, MODES[m], STATES[t][0], " (own)" if own and t == 0 else "", after, g))
                elif d and (kicks or seq or smove):
                    left += 1
                    out("LEFT skill %d mode %s state %s: interrupted but kicks %d seq %d smove %d remain" % (s, MODES[m], STATES[t][0], kicks, seq, smove))
    # The barrel kick: game.exe's used skill is Kick (skill 1 of the unit), its mode KK.
    kick_cell = (kick, KK, 0)
    if kick_cell not in game_cells:
        die("game file lacks the Kick cell %s the B line names" % (kick_cell,))
    barrel_bad = int(int(barrel_after == 4) != game_cells[kick_cell])
    if barrel_bad:
        bad += 1
        out("DIFF barrel kick (skill %d mode KK): d2d after %d, game.exe interrupted %d" % (kick, barrel_after, game_cells[kick_cell]))
    # What d2d does not model: the other states, counted against state 0 of the same cell.
    other = {t: sum(1 for s in skills for m in MODES if game_cells.get((s, m, t)) is not None and game_cells[(s, m, t)] != game_cells.get((s, m, 0))) for t in STATES if t not in MODELLED}
    out("%d cells (%d skills x %d modes x states none, 0x36; %d cell judged as the barrel kick) and the barrel kick, %d differ; own-mode cells %d, %d differ; interrupted with swing state left over: %d; game rows d2d lacks: %d %s"
        % (n, len(skills), len(MODES), skipped, bad, n_own, bad_own, left, len(absent), absent[:20]))
    out("not modelled, not compared (cells where game.exe's answer differs from state none): %s" % ", ".join("state %s %d" % (STATES[t][0], v) for t, v in other.items()))
    return n, bad, left


def compare(gpath, dpath):
    n, bad, left = judge(parse_game(open(gpath).read().splitlines()), parse_d2d(open(dpath).read().splitlines()), print)
    sys.exit(1 if bad or left or n == 0 else 0)


def selftest():
    skills = (0, 1)                                        # 1 is Kick, as in Skills.txt
    g = ["G %d %d %d %d" % (s, m, t, 1 if s == 0 and t == 0 else 0) for s in skills for m in MODES for t in STATES]
    d = ["I %d %d 1 %d %d 0 0 0" % (s, m, t, 4 if s == 0 and t == 0 else m) for s in skills for m in MODES for t in MODELLED]
    barrel = "B 1 12"

    def fails(gl, dl):
        sys.stdout = open(os.devnull, "w")
        try:
            judge(parse_game(gl), parse_d2d(dl), lambda s: None)
        except SystemExit as ex:
            return ex.code == 1
        finally:
            sys.stdout = sys.__stdout__
        return False

    def res(gl, dl):
        return judge(parse_game(gl), parse_d2d(dl), lambda s: None)

    ok = [not fails(g, d + [barrel]), fails(g, d[:-1] + [barrel]), fails(g[1:], d + [barrel]), fails(g, d + [d[0], barrel]),
          fails(g, d[:3] + ["I 0 x"] + d[4:] + [barrel]), fails([], d + [barrel]), fails(g, []),
          fails(g, d), fails(g, d + [barrel, barrel]), fails(g, d + ["B 9 12"]),              # no B line, two, one naming a skill the game lacks
          fails(g, d + [barrel, "I 0 7 1 2 4 0 0 0"])]                                        # a state d2d does not model on its side
    bad = list(d)
    bad[0] = "I 0 7 1 0 7 0 0 0"
    ok.append(res(g, bad + [barrel])[1] == 1)
    ok.append(res(g, d + ["B 1 4"])[1] == 1)                                                  # a barrel kick that interrupts: Kick refuses
    uninterruptable = list(d)                                                                 # a Leap in flight that still takes the hit
    uninterruptable[uninterruptable.index("I 1 7 1 1 7 0 0 0")] = "I 1 7 1 1 4 0 0 0"
    ok.append(res(g, uninterruptable + [barrel])[1] == 1)
    barrel_cell = list(d)                                                                     # Attack in KK is the barrel's own state: not compared as Attack
    barrel_cell[barrel_cell.index("I 0 12 1 0 4 0 0 0")] = "I 0 12 1 0 12 0 0 0"
    ok.append(res(g, barrel_cell + [barrel])[1] == 0)
    left = list(d)
    left[0] = "I 0 7 1 0 4 1 0 0"
    ok.append(res(g, left + [barrel])[2] == 1)
    print("selftest: %d of %d checks" % (sum(ok), len(ok)))
    sys.exit(0 if all(ok) else 1)


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "game":
        game()
    elif len(sys.argv) == 4 and sys.argv[1] == "compare":
        compare(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 2 and sys.argv[1] == "selftest":
        selftest()
    else:
        sys.exit(__doc__)
