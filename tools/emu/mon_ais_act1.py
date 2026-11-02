"""monsterai: game.exe's Act 1 core monster thinks run natively (the AIs upstream's mon_ais.py leaves out), dumped for d2d's replay.

    PYTHONPATH=<upstream tools/emu> D2_MPQ_DIR=<dir with bin/game.exe + MPQs> python mon_ais_act1.py <ai,ai|all> <cases> <seed> --dump <tests/mon_ais_pace_cases.inc>

Same scene, hooks and case format as upstream tools/emu/mon_ais.py (imported, not copied): only the think addresses are added
(MonAI table 0x73ca18, row + 8; mon_ais.py prints it). The new AIs are appended to ORDER, so the existing indices keep their
numbers; the names file written next to the dump holds the whole ORDER. Never runs a game."""
import os
import random
import sys

import mon_ais as M

# MonAI row + 8 (mon_ais.py, 2026-11-01), the AIs placed in Acts 1 and 2 first.
NEW = {
    "Skeleton": 0x5efcf0, "Zombie": 0x5efe20, "BloodHawk": 0x5f00e0, "Fallen": 0x5f02c0, "Brute": 0x5efb80, "Wraith": 0x5f0a20,
    "CorruptRogue": 0x5f0b00, "Goatman": 0x5f12a0, "FallenShaman": 0x5f1440, "QuillRat": 0x5f1140, "Arach": 0x5f4510, "Fetish": 0x5f53e0,
    "Andariel": 0x5f5830, "CorruptLancer": 0x5f5d50, "SkeletonBow": 0x5f6070, "GargoyleTrap": 0x5f9490, "Griswold": 0x5e5ac0,
    "Smith": 0x5e3890, "FoulCrowNest": 0x5f6650, "BloodRaven": 0x5e6320, "ElementalBeast": 0x5f6b70, "FlyingScimitar": 0x5f6ca0,
    "QuillMother": 0x5fb2a0, "DarkWanderer": 0x5ea130, "TownRogue": 0x5e7d60,
}


SHAMAN_CB = 0x5f1380                      # FallenShaman's corpse test (search callback), limit aip4 squared at ctx + 0xc


def patch(e):
    """FallenShaman: FUN_005a0180 (unit, mask) reads the AI flags at monster data +0x16: bit 8 (aware) and bit 4 pick the corpse
    callback or the list-9 search. The scene gives them as s['flags']; every other AI keeps the real code's answer (flags 0)."""
    from unicorn.x86_const import UC_X86_REG_EDX
    cur = M.CUR
    orig_hook = e.hook
    def flags(e):
        s = cur["s"]
        return int(bool(s.get("flags", 0) & e.mu.reg_read(UC_X86_REG_EDX) & 0xffff))
    orig_hook(0x5a0180, flags, 0)
    held = {}
    def hook(addr, fn, nstack):
        if addr == 0x5dd0b0:
            held["search"] = fn
            def search(e):
                s, ctx, cb = cur["s"], e.arg(0), e.arg(1)
                if cb == SHAMAN_CB:
                    if s["corpse"]:
                        e.w32(ctx, cur["units"]["kin"]); e.w32(ctx + 4, e.r32(ctx + 4) + 1); e.w32(ctx + 8, s["kin_d"])
                    return 0
                if cb == 0 and e.arg(2) == 9:
                    return 0
                return held["search"](e)
            return orig_hook(addr, search, nstack)
        return orig_hook(addr, fn, nstack)
    e.hook = hook
    orig_call = e.call
    def call(addr, *args, **kw):                      # CorruptLancer reads a byte at params[8] + 0xe: a block of zeros (what the
        if cur["s"]["ai"] == "Fallen":                # the think returns at once in mode 0 / 12 (DT / DD); NU, as a living one
            e.w32(kw["edx"] + 0x10, 1)
        if cur["s"]["ai"] == "CorruptLancer":         # byte means is not modelled: d2d's port has no input for it)
            e.w32(args[0] + 0x20, e.alloc(0x40))
        return orig_call(addr, *args, **kw)
    e.call = call
    def room_units(e):                                # FUN_00619790 (room, &list, &count): the Fallen's scan for a Fallen in DT within 15
        e.w32(e.arg(2), 0)                            # (rules::ThinkIn::dying, which the case line does not carry: false)
        return 0
    orig_hook(0x619790, room_units, 3)


def setup(rng, ai):
    s = M._setup(rng, ai)
    # Leader commands: d2d's QuillRat / Fetish ports never receive one (ponytail notes in monsters.hpp), so their scenes carry none;
    # MAI_COMMAND=none drops them for every AI (a run to tell the command paths from the rest).
    if ai in ("QuillRat", "Fetish") or os.environ.get("MAI_COMMAND") == "none":
        s["command"] = None
    # d2d feeds the second target (FUN_005ddc30) as the primary: a monster's finds its target, at the same distance (monster-ai.md, ai.cpp).
    s["foe2"], s["foe2_dist"] = bool(s["target"]), s["dist"]
    if ai == "FallenShaman":
        s["flags"] = rng.choice((0, 8, 8, 8, 4, 12))
        found = s["flags"] & 8 and not s["flags"] & 4 and s["kin_d"] <= s["aip"][3] * s["aip"][3]
        s["corpse"] = bool(s["corpse"] and found)         # the case line tells d2d what the search found
    return s


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--") and not a.endswith(".inc")]
    which = args[0] if args else "all"
    cases = int(args[1]) if len(args) > 1 else 2000
    rng = random.Random(int(args[2]) if len(args) > 2 else 1)
    for name, addr in NEW.items():
        M.AIS.setdefault(name, addr)
    M.ORDER[:] = list(M.AIS)
    M._setup, M.setup = M.setup, setup
    e = M.emu.Emu()
    patch(e)
    M.install(e)
    dumped = {} if "--dump" in sys.argv else None
    for ai in (NEW if which == "all" else which.split(",")):
        M.check(e, ai, cases, rng, dumped)
    if dumped is not None:
        path = sys.argv[sys.argv.index("--dump") + 1]
        with open(path, "w") as f:
            f.write("// tools/emu/mon_ais_act1.py --dump: game.exe's own Act 1 monster thinks, a line each.\n")
            for rows in dumped.values():
                for lines in rows.values():                     # all of them (upstream keeps 6 a key)
                    f.write("".join(f'"{line}",\n' for line in lines))
        M.write_names(os.path.join(os.path.dirname(path), "mon_ais_pace_names.inc"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
