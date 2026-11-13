"""The Act 1 thinks that set a pace, run natively (Fallen flee, Blood Raven, Zombie, Brute...): imports mon_ais_act1.py (and,
through it, upstream's mon_ais.py); edits neither. Adds scenes and hooks for what the first pass could not run:

  Fallen          a unit dying near (FUN_00619790 room scan, FUN_00552f60 by id, FUN_005dc530 distance: all game.exe's own code but the
                  scan and the lookup), so the flee branch (pace 50, back off 12) runs; the case line carries `dying` in an unused stat word
  FallenShaman    unset skill ids made distinct (the first pass's slot labels), Skill1's usable test (FUN_005fd470) true
  BloodRaven      her home point (kind 10 word, FUN_0058eef0), the walks home / about the target (set off or not), her flag word
  Wraith, SkeletonBow, GargoyleTrap, FoulCrowNest   re-run with the labels the first pass's mismatches turned out to be

Labels normalised before the case is written (each a representation difference, stated in the trace): `keep`'s y is the move mode (always 2:
FUN_005de6d0 pushes 2), which d2d's Think does not carry; the second target is the primary one (d2d feeds it so), so unit foe2 reads foe for
SkeletonBow. Blood Raven's walks home, past 20 off and her circle answer as the scene's moves do: each an ask.

    PYTHONPATH=<upstream tools/emu>:tools/emu D2_MPQ_DIR=<dir> python mon_ais_pace.py <ai,ai|all> <cases> <seed> --dump <dir>
    env MAI2_CMD=none1 (leader command none or 1) | two (command 2: the D8 regime) | (unset: as drawn)
Writes <dir>/mon_ais_pace_cases.inc and mon_ais_pace_names.inc for tests/monsters/test_monster_ai.cpp's replay. Never runs a game."""
import os
import random
import sys

from unicorn.x86_const import UC_X86_REG_ESP

import mon_ais as M
import mon_ais_act1 as A

SET = ("Fallen", "FallenShaman", "GargoyleTrap", "FoulCrowNest", "BloodRaven", "Wraith", "SkeletonBow")


def setup(rng, ai):
    s = A.setup(rng, ai)
    mx, my = s["spots"]["mon"]
    cmd = os.environ.get("MAI2_CMD", "")
    if ai == "Fallen":
        s["dying_found"] = rng.random() < 0.55
        if rng.random() < 0.6: s["spots"]["aid"] = (mx + rng.randint(-14, 14), my + rng.randint(-14, 14))
        if cmd == "none1": s["command"] = rng.choice((None, 1))
        if cmd == "two": s["command"] = 2
    if ai == "FallenShaman":
        s["skills"] = [k if k >= 0 else -1 - n for n, k in enumerate(s["skills"])]   # unset ones distinct: a cast of one names its slot
        s["usable"] = True
        if s["skills"][0] < 0: s["skills"][0] = 400                                # Skill1 (the raise) is set on every shaman row
    if ai == "BloodRaven":
        reach = rng.choice((20, 35, 60))
        s["home"] = None if rng.random() < 0.15 else (mx + rng.randint(-reach, reach), my + rng.randint(-reach, reach))
        s["free"][0] = s["home"]                                                  # the case line carries it in free spot 0
        s["ctrl"][2] = rng.choice((0, 0, 1)) if s["home"] else 0                  # her flag (+0x1c); clear at her first think, which sets the home
        s["command"] = None
    return s


def patch2(e):
    cur = M.CUR
    prev = e.hook
    def hook(addr, fn, nstack):
        if addr == 0x552f60:                                          # FUN_00552f60(game, type, id): the dying Fallen / the base answer
            base = fn
            def by_id(e):
                from unicorn.x86_const import UC_X86_REG_EDX
                s = cur["s"]
                if s["ai"] == "Fallen" and s.get("dying_found") and e.mu.reg_read(UC_X86_REG_EDX) == 1:
                    return cur["units"]["aid"]
                return base(e)
            return prev(addr, by_id, nstack)
        if addr == 0x58eef0:                                          # FUN_0058eef0: the Raven's kind 10 word (home point)
            base = fn
            def word(e):
                s = cur["s"]
                if s["ai"] == "BloodRaven":
                    home = s["home"] or s.get("home_set")                 # none at her first think: the init (FUN_0058ef40) sets it to where she stands
                    if not home: return 0
                    e.w32(cur["word_rec"] + 0xc, home[0] & 0xffffffff); e.w32(cur["word_rec"] + 0x10, home[1] & 0xffffffff)
                    return cur["word_rec"]
                return base(e)
            return prev(addr, word, nstack)
        if addr == 0x58ef40:                                          # FUN_0058ef40(game, unit; &{.., kind +8, x +0xc, y +0x10}): keeps the word
            base = fn
            def keep(e):
                s = cur["s"]
                if s["ai"] == "BloodRaven":
                    rec = e.arg(0)
                    s["home_set"] = (M.S32(e.r32(rec + 0xc)), M.S32(e.r32(rec + 0x10)))
                    return 0
                return base(e)
            return prev(addr, keep, nstack)
        return prev(addr, fn, nstack)
    e.hook = hook


def extra_hooks(e):
    cur = M.CUR
    def room_units(e):                                                # FUN_00619790(room, &list, &count): one room entry whose four ids are the dying unit's
        s = cur["s"]
        if s["ai"] == "Fallen" and s.get("dying_found"):
            rec, arr = e.alloc(0x80), e.alloc(8)
            e.w32(rec + 0x38, 7); e.w32(arr, rec); e.w32(e.arg(1), arr); e.w32(e.arg(2), 1)
        else:
            e.w32(e.arg(2), 0)
        return 0
    e.hook(0x619790, room_units, 3)
    e.hook(0x553380, lambda e: cur["out"].append(("scream",)) or 0, 1)   # the Fallen's scream (1 in 20 on a flee), ai.cpp's flee_sound
    def around(e):                                                    # FUN_005df680(game, unit; target, radius): a walk to a point off the target, set off
        u = cur["units"]["mon"]
        sd, n = M.Seed(e.r32(u + 0x20), e.r32(u + 0x24)), e.arg(1) & 0xff
        if sd.step() & 1 == 0: x, y = sd.rand(n), n                  # the same four seed steps as FUN_005de200 / FUN_005df530
        else: x, y = n, sd.rand(n)
        if sd.step() & 1: x = -x
        if sd.step() & 1: y = -y
        e.w32(u + 0x20, sd.lo); e.w32(u + 0x24, sd.hi)
        asked = cur["s"]["ai"] == "BloodRaven" and e.r32(e.mu.reg_read(UC_X86_REG_ESP)) == 0x5e6511   # her walk past 20 off reads the answer; her 5 % one doesn't
        ok = int(cur["moves"].random() >= cur["s"]["fails"]) if asked else 1
        cur["out"].append(("around", cur["names"].get(e.arg(0)), n, x, y, ok if asked else None))
        return ok
    e.hook(0x5df680, around, 2)
    def command_done(e):                                              # FUN_0058ed10: the leader's command is done: later reads find none
        cur["out"].append(("command_done",)); cur["s"]["command"] = None
        return 0
    e.hook(0x58ed10, command_done, 0)
    def home(e):                                                      # FUN_005dede0(game, unit; x, y): a walk to the home point, setting off as the scene's moves do
        ok = int(cur["moves"].random() >= cur["s"]["fails"])
        cur["out"].append(("home", e.arg(0), e.arg(1), ok))
        return ok
    e.hook(0x5dede0, home, 2)


def check(e, ai, cases, rng, lines, tally):
    mark = e.brk
    for _ in range(cases):
        e.brk = mark
        s = M.setup(rng, ai)
        command = s["command"]                                                # the flee clears it in the scene (FUN_0058ed10); the case line keeps what it was
        out, ctrl, seed = M.run_game(e, s, M.AIS[ai], random.Random(rng.getrandbits(32)))
        s["command"] = command
        out = [x for x in out if x[0] != "scream"]
        s["dying"] = ai == "Fallen" and ("pace", 50, 0) in out                    # the flee branch's pace (0x5f03a8); command_done also comes from a command word that is not 1 (0x5f068b)
        act, pace, effects, asks = M.outcome(s, out)
        if ai == "BloodRaven" and s.get("home_set"): s["free"][0] = s["home_set"]   # the case line carries the home she kept
        if ai == "FallenShaman" and act[0] == "seq": act[:] = ["skill", act[1], 0, 0, 0, act[5], act[6]]   # ai.cpp:1559-1560 runs seq and skill alike; its corpse is skill_unit
        if ai == "BloodRaven" and act[0] == "skill" and act[4] == 1:                  # her raise: a point (unit none); d2d keeps its offset from the target
            fx, fy = s["spots"]["foe"]; act[:] = ["skill", act[1], act[2] - fx, act[3] - fy, 0, act[5], act[6]]
        if ai == "BloodRaven" and act[0] == "circle": act[3] = 0                    # the circle's flag 1 (walk flagged 4): d2d's Think has no field for it
        if act[0] == "keep": act[3] = 0                                       # y is the move mode, always 2 (FUN_005de6d0), d2d's Think has none
        if ai == "SkeletonBow" and act[4] == 2: act[4] = 0                    # the second target is the primary one, as ai.cpp feeds it
        s["stat"][("mon", 0x2b)] = int(s["dying"])
        lines.append(M.dump_case(s, act, pace, effects, asks, ctrl, seed))
        key = (ai, "dying" if s["dying"] else "-", act[0])
        tally[key] = tally.get(key, 0) + 1


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--") and not os.path.isdir(a)]
    which = args[0] if args else "all"
    cases = int(args[1]) if len(args) > 1 else 1500
    rng = random.Random(int(args[2]) if len(args) > 2 else 1)
    out_dir = sys.argv[sys.argv.index("--dump") + 1]
    for name, addr in A.NEW.items(): M.AIS.setdefault(name, addr)
    M.ORDER[:] = list(M.AIS)
    M._setup = M.setup                                                    # mon_ais_act1.setup calls it
    M.setup = setup
    e = M.emu.Emu()
    A.patch(e)
    patch2(e)
    M.install(e)
    extra_hooks(e)
    lines, tally = [], {}
    for ai in (SET if which == "all" else which.split(",")):
        check(e, ai, cases, rng, lines, tally)
    with open(os.path.join(out_dir, "mon_ais_pace_cases.inc"), "w") as f:
        f.write("// tools/emu/mon_ais_pace.py --dump\n" + "".join(f'"{line}",\n' for line in lines))
    M.write_names(os.path.join(out_dir, "mon_ais_pace_names.inc"))
    for key, n in sorted(tally.items()): print("acts", *key, n)
    print("cases", len(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
