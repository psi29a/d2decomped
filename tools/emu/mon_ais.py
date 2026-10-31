# SPDX-License-Identifier: GPL-3.0-or-later
"""Act 2's monster thinks (docs/research/re/monster-ai.md) run natively,
then the curse thinks (special AI 10 / 11) and the MonAI rows' other thinks
(monster-ai.md "Curse thinks and other thinks").

    uv run python mon_ais.py <ai|all> [cases] [seed] [--dump tests/mon_ais_cases.inc]

Each MonAI think runs in game.exe on random scenes: the params block (target,
AI distance, melee), the AI control's +0x14 / +0x18 / +0x1c, MonStats aip1..8,
Skill1..5, Velocity / Run, the unit seed, the frame. Every helper it calls is
hooked at its entry: the acts (FUN_005ddf90 a mode, FUN_005de080 stand,
FUN_005de0f0 wait, FUN_005de130 event, FUN_005dead0 skill, FUN_005de000 its
sequence, FUN_005dec80 walk, FUN_005ded40 / FUN_005defb0 run, FUN_005def80 approach,
FUN_005defe0 back off, FUN_005df7d0 circle, FUN_005de200 wander,
FUN_005df530 about the target, FUN_005de190 pace), the effects (states,
overlays, stats set, the leader's command) and the answers the scene gives
(got hit, life %, stats, the second target FUN_005ddc30, the searches
FUN_005dd0b0 by callback, a skill usable FUN_005fd470, the leader). The
draws FUN_005df7d0 / FUN_005de200 / FUN_005df530 make on the unit seed are
made here as they make them.

Each run comes to one act in d2d's terms (rules::Think: act, n, x, y, the
unit it's at, wait, event), the pace, the effects, the AI control and seed
after. --dump writes them as tests/mon_ais_cases.inc lines for
tests/test_monsters.cpp, which runs rules::mon_think on each. Prints `ok`.
"""
import os
import random
import struct
import sys

import emu
from merc import Seed

MASK = 0xffffffff
S32 = lambda v: v - (1 << 32) if v & 0x80000000 else v
UNITS = ("mon", "foe", "foe2", "kin", "aid")
SKILL_STATE = 0x300                                     # the state every skill record in Act 4's scenes names (+0x80, +0x82)

# MonAI think addresses (the MonAI table 0x73ca18, row + 8).
AIS = {
    "Duriel": 0x5f67b0, "Swarm": 0x5f2460, "PinHead": 0x5f6340, "Mummy": 0x5f2850, "SandLeaper": 0x5f20a0,
    "ClawViper": 0x5f1b60, "PantherWoman": 0x5f22b0, "PantherJavelin": 0x5e1080, "Scarab": 0x5f2540,
    "BatDemon": 0x5f5040, "Baboon": 0x5f0cd0, "SandRaider": 0x5f0700, "GreaterMummy": 0x5f2b10,
    "SandMaggot": 0x5f1800, "Summoner": 0x5f85c0, "Vulture": 0x5f3170, "MaggotEgg": 0x5f6530, "MaggotLarva": 0x5f6220,
    # The curse thinks (special AI table 0x73d358, row + 8: 10 Dim Vision /
    # Cloak, 11 Terror / Howl) and the MonAI rows' other thinks (row + 12).
    "DimVision": 0x5e8020, "Terror": 0x5e8140, "BatDemonOther": 0x5f4fd0, "SandMaggotOther": 0x5f1750,
    # Act 3's (monster-ai.md "Act 3's thinks").
    "Mosquito": 0x5f3730, "ThornHulk": 0x5f4850, "WillOWisp": 0x5f39b0, "ZakarumZealot": 0x5f6e60, "ZakarumPriest": 0x5f72d0,
    "FrogDemon": 0x5f8260, "Tentacle": 0x5f8f80, "TentacleHead": 0x5f9270, "FetishShaman": 0x5f9a80, "HighPriest": 0x5e0490,
    "Hydra": 0x5e9e60, "FetishBlowgun": 0x5e1250, "Mephisto": 0x5f78b0,
    "FrogDemonOther": 0x5f81d0, "FetishShamanOther": 0x5f9950,
    # Coldworm's (MonAI 66), last: the Act 3 scene's fields it ignores.
    "SandMaggotQueen": 0x5f9cf0,
    # Act 4's (monster-ai.md "Act 4's MonAIs").
    "VileMother": 0x5fa010, "VileDog": 0x5fa280, "FingerMage": 0x5fa380, "Regurgitator": 0x5fa710, "DoomKnight": 0x5faa90,
    "AbyssKnight": 0x5fab80, "OblivionKnight": 0x5faf00, "Megademon": 0x5e0c80, "Izual": 0x5f89b0,
    "TrappedSoul": 0x5e9f10, "Diablo": 0x5e9170, "DiabloOther": 0x5e8480,
    # Act 5's (monster-ai.md "Act 5's MonAIs").
    "ReanimatedHorde": 0x5e1540, "SiegeBeast": 0x5e1900, "Minion": 0x5e1b60, "SuicideMinion": 0x5e1d30, "Succubus": 0x5e1e00,
    "SuccubusWitch": 0x5e2120, "Overseer": 0x5e27a0, "MinionSpawner": 0x5e2bd0, "Imp": 0x5e2ff0, "Catapult": 0x5e34c0,
    "FrozenHorror": 0x5e3530, "BloodLord": 0x5e36f0, "CatapultSpotter": 0x5ee040, "NpcBarb": 0x5edc50, "GenericSpawner": 0x5e61b0,
    "DeathMauler": 0x5ee260, "PutridDefiler": 0x5efa90, "BaalTentacle": 0x5ef820, "BaalMinion": 0x5ef910, "Ancient": 0x5ef1a0, "Nihlathak": 0x5ee5d0, "ClawViperEx": 0x5f1de0,
    # Baal's (monster-ai.md "Baal"), last: the cases before keep their numbers.
    "BaalCrab": 0x5fcfe0, "BaalCrabClone": 0x5fd210,
    # The ubers' (monster-ai.md "AncientStatue and the ubers' MonAIs"): three are a bare ret 4.
    "UberIzual": 0x5f8c80, "UberMephisto": 0x5f81c0, "UberDiablo": 0x5e9df0, "UberBaal": 0x5fd200,
    # A riding imp's (special AI 16, 0x73d358 row + 8) and the barricade tower's (MonAI 113).
    "ImpRide": 0x5e2d80, "SiegeTower": 0x5e1860,
    # The rest the levels place (monster-ai.md "Turrets, traps and the rest"): a scene of their own.
    "HellMeteor": 0x5f56d0, "Sarcophagus": 0x5f6a10, "Spirit": 0x5e3840, "ArcaneTower": 0x5e0f60,
    "Trap-RightArrow": 0x5fb6c0, "Trap-LeftArrow": 0x5fb7e0, "DesertTurret": 0x5e0980,
    # Act 1's that take the second target (FUN_005ddc30), last: the late scene, its trap kind unread.
    "Bighead": 0x5eff50, "CorruptArcher": 0x5f5a20, "SkeletonMage": 0x5f96c0, "Vampire": 0x5f4a70,
}
DIABLO_LIKE = ("Diablo", "DiabloOther", "BaalCrab", "BaalCrabClone")   # his pick, kept point, hands
BAAL_SKILLS = {0x13b: 2, 0x13c: 0, 0x13d: 1, 0x13e: 3}                 # FUN_005fcb60's raw ids: Skill3, 1, 2, 4
CURSE_AIS = ("DimVision", "Terror", "BatDemonOther", "SandMaggotOther")
LATE_AIS = tuple(list(AIS)[list(AIS).index("HellMeteor"):])  # their own scene: Act 2's and the trap kind
ACT3_AIS = tuple(a for a in list(AIS)[list(AIS).index("Mosquito"):] if a not in LATE_AIS)   # Act 4's too: their scene is Act 3's and more
ACT4_AIS = tuple(a for a in list(AIS)[list(AIS).index("VileMother"):] if a not in LATE_AIS)
ACT5_AIS = tuple(a for a in list(AIS)[list(AIS).index("ReanimatedHorde"):] if a not in LATE_AIS)
ORDER = list(AIS)


def write_names(path):
    """tests/mon_ais_names.inc: the AIs a case line's first number indexes (ORDER),
    each with whether it's an other think and whether its scene has Act 3's / Act 4's fields."""
    with open(path, "w") as f:
        f.write("// tools/emu/mon_ais.py --dump (write_names): the AIs the cases number, ORDER.\n")
        for ai in ORDER:
            other = ai.endswith("Other")
            f.write(f'{{ "{ai.removesuffix("Other")}", {str(other).lower()}, {str(ai in ACT3_AIS).lower()}, {str(ai in ACT4_AIS).lower()}, {str(ai in ACT5_AIS).lower()}, {str(ai in LATE_AIS).lower()} }},\n')
# Think acts (rules::MonAct) by name; units (rules::ThinkUnit).
ACTS = ["idle", "a1", "a2", "s2", "skill", "walk", "run", "approach", "keep", "circle", "wander", "none", "die", "around",
        "home", "point", "untraced", "s3", "s4", "seq", "fly", "gone", "s1", "sc", "mode_at", "move_to", "spawn", "direct"]
UNIT = {"foe": 0, None: 1, "foe2": 2, "kin": 3, "aid": 4, "mon": 5}
MODE_ACT = {0: "die", 4: "a1", 5: "a2", 7: "sc", 8: "s1", 9: "s2", 10: "s3", 11: "s4"}


def setup(rng, ai):
    s = {"ai": ai, "diff": rng.randint(0, 2), "frame": rng.randint(100, 5000)}
    s["seed"] = (rng.getrandbits(32), rng.getrandbits(32))
    s["target"] = True                                                     # search types 1 / 4: none found, no think (FUN_005b1650)
    s["spot"] = (rng.randint(4000, 6000), rng.randint(4000, 6000))
    s["dist"] = rng.choice((rng.randint(0, 6), rng.randint(0, 20), rng.randint(0, 60)))
    s["melee"] = rng.random() < 0.4
    s["got_hit"] = rng.random() < 0.3
    s["life"] = rng.choice((rng.randint(0, 100), rng.randint(0, 30), 100))
    word = lambda: rng.choice((0, 0, 1, 2, 3, 4, 5, -1, rng.randint(0, 12), s["frame"] + rng.randint(-60, 60)))
    s["ctrl"] = [word(), word(), word()]
    s["aip"] = [rng.choice((0, 1, 2, rng.randint(0, 100), rng.randint(0, 20), 100)) for _ in range(8)]
    ids = rng.sample(range(400), 5)                                        # distinct: the act names its slot
    s["skills"] = [rng.choice((-1, ids[k])) for k in range(5)]
    if ai == "Vampire": s["skills"][:4] = ids[:4]                         # every vampire row has Skill1..4; its think tests none
    s["modes"] = [rng.randint(0, 15) for _ in range(5)]
    s["velocity"], s["run"] = rng.choice((0, 3, 5, 6, 8)), rng.choice((0, 5, 6, 9, 12))
    s["aidel"] = rng.randint(0, 20)
    s["stat"] = {(u, k): rng.choice((0, rng.randint(0, 100), rng.randint(-50, 300))) for u in ("mon", "foe") for k in (0x4a, 0x27, 0x2b)}
    s["foe2"] = rng.random() < 0.6
    s["foe2_dist"] = rng.choice((rng.randint(0, 10), rng.randint(0, 40)))
    s["kin"] = rng.random() < 0.6
    s["kin_d"] = rng.choice((rng.randint(0, 50), rng.randint(0, 2000)))   # FUN_005b0bd0: squared
    s["corpse"], s["aid"] = rng.random() < 0.5, rng.random() < 0.5
    s["counts"] = (rng.randint(0, 4), rng.randint(0, 4))
    s["usable"] = rng.random() < 0.7
    s["command"] = rng.choice((None, 1, 2))                                # FUN_0058ee80: no command / its kind
    s["aura"] = rng.random() < 0.5                                         # FUN_006201d0: its right skill (an aura) is on
    s["fails"] = rng.random()
    # The Vulture's: the units' spots (subtiles), whether the target's room is
    # its own, whether it can land (FUN_005f2fc0), a victim (FUN_005f30f0), the
    # free spots found by the target's room / its own (FUN_0064e7b0).
    cx, cy = rng.randint(4000, 6000), rng.randint(4000, 6000)
    s["spots"] = {u: (cx + rng.randint(-25, 25), cy + rng.randint(-25, 25)) for u in UNITS}
    s["spot"] = s["spots"]["foe"]
    s["same_room"] = rng.random() < 0.6
    s["can_land"] = rng.random() < 0.6
    s["victim"] = rng.random() < 0.4
    s["group"] = rng.choice((None, "mon", "kin"))                         # FUN_0058f0d0: its group's leader
    s["leader"] = s["group"] == "mon"
    s["free"] = [None if rng.random() < 0.4 else (cx + rng.randint(-25, 25), cy + rng.randint(-25, 25)) for _ in range(2)]
    # The curse / other thinks': an A1 mode (FUN_0046c140 bit 4), an RN mode
    # (bit 15), interact (MonStats flag 9), Terror's state 0x38 on, the unit's
    # mode (11 S4), the SandMaggot's command 14 +0x18.
    if ai in CURSE_AIS:
        s["a1"], s["rn"], s["interact"] = rng.random() < 0.8, rng.random() < 0.6, rng.random() < 0.15
        s["afraid"], s["in_s4"], s["cast_word"] = rng.random() < 0.8, rng.random() < 0.3, rng.choice((0, 1, 1, 2))
    # Act 3's: its next think event's frame (FUN_005415a0 type 1, 0 none),
    # its group leader's mode (+0x10: the tentacles' head, 12 DD / 14 S4),
    # its command's unit found (FUN_00552f60), a circle that can set off.
    if ai in ("FrogDemon", "Tentacle", "TentacleHead", "Hydra"):        # search types 2 / 5: a think with none found
        s["target"] = rng.random() < 0.85
    if ai in ACT3_AIS:
        s["skills"] = [k if k >= 0 else -1 - n for n, k in enumerate(s["skills"])]   # unset ones distinct: a cast of one names its slot
        s["next_event"] = rng.choice((0, s["frame"] + rng.randint(-30, 60)))
        s["leader_mode"] = rng.choice((1, 2, 4, 12, 12, 14, 14, rng.randint(0, 15)))
        s["cmd_unit"] = rng.random() < 0.7
        s["command"] = rng.choice((None, 1, 2, 14))
        s["sk_level"] = rng.choice((0, 1, rng.randint(-2, 40)))           # FUN_006442a0: Skill1's level
        s["state_c"] = rng.random() < 0.4                                  # state 0xc (the shaman's Inferno) on
        s["spot_coll"] = rng.choice((0, 0, 1, 0x400, 0xc01))                # FUN_0064cb30: its own spot's collision (mask 0xc01)
        # The zealots' spare test: the target a player (type 0), or a pet
        # (FUN_0058f090) whose owner's found (FUN_00552f60 type 0); in act 3
        # (the level FUN_0061a1b0 names); its quest 0x15 bit 0 (FUN_0065c310).
        s["foe_kind"] = rng.choice(("player", "pet", "monster"))
        s["owner"] = rng.random() < 0.7
        s["act3"] = rng.random() < 0.7
        s["quest21"] = rng.random() < 0.5
        s["los"] = rng.random() < 0.6                                      # FUN_005dd290: the line to the target clear (mask 4)
        s["miss_range"] = rng.choice((-1, rng.randint(0, 30)))             # MonStats MissS1 (+0x3e)'s Missiles.txt Range (+0x96), -1 none
        # Mephisto's: Skill6..8 (+0x17a), his chooser's search (FUN_005f7700:
        # the weakest unit within 32, a player nearest).
        ids = rng.sample(range(400, 500), 3)
        s["skills68"] = [rng.choice((-6 - k, ids[k])) for k in range(3)]
        s["weak"], s["near5"] = rng.random() < 0.6, rng.random() < 0.5
        # The wisps': how many its search finds (FUN_0065a950), its target a
        # player (unit type 0) not out of reach (FUN_005541b0).
        s["wisps"] = rng.choice((0, 1, 3, 4, 4, 5, 5, 6))
        s["busy"] = rng.random() < 0.3
        if ai == "WillOWisp":
            s["foe_kind"] = rng.choice(("player", "monster"))
            s["frame"] = rng.choice((s["frame"], s["frame"] - s["frame"] % 67, s["frame"] - s["frame"] % 337))   # its 67th / 337th frames
            s["ctrl"][0] = rng.choice((0, 1, 2, 3, 4, 5, 6, rng.randint(0, 12)))   # its states (0..12)
            s["ctrl"][2] = rng.randint(0, 6) if s["ctrl"][0] >= 5 else rng.choice((s["ctrl"][2], rng.randint(0, 6)))
            if s["ctrl"][0] >= 5 and rng.random() < 0.5:               # at its step's point on a 67th frame: the casts, the next step, the blessing
                steps6 = [(0, 0), (-10, -3), (1, -5), (6, 0), (4, 10), (-9, -2), (-6, -11), (-2, -5), (6, 1), (0, 13), (-12, 0), (7, 5), (-3, -11), (-15, -3),
                          (-6, -3), (-4, -5), (-6, -13), (-12, -2), (-6, 2), (-5, 5), (1, 11), (-13, -6), (-8, -11), (-7, 0), (1, 8), (-5, -8), (-8, 1), (1, -6), (6, 9)]
                steps5 = [(-5, 0), (-5, -5), (3, -5), (6, 3), (3, 6), (-5, 3)]
                state, word3 = s["ctrl"][0], s["ctrl"][2]
                step = steps5[word3] if state == 5 and word3 <= 5 else steps6[word3 + state * 4 - 24] if state >= 6 and word3 <= 4 else (0, 0)
                fx, fy = s["spots"]["foe"]
                s["spots"]["mon"] = (fx + step[0] + rng.randint(-1, 1), fy + step[1] + rng.randint(-1, 1))
                s["frame"] -= s["frame"] % 67
                s["ctrl"][1] = rng.choice((0, 1, 2, 3, 3))
    # Act 4's: a state its skills' records name (skill +0x80 / +0x82) on (the
    # Abyss Knight's armor, the Oblivion Knight's curse on its target); the
    # vile mother's young within 25 (FUN_005dd0b0 search type 0xc) and its lay
    # finding a spot (FUN_005f9e30).
    if ai in ACT4_AIS:
        s["skills"] = [400 if k == 0 else k for k in s["skills"]]   # id 0 (Attack) never sits there; FUN_005faf00 reads it as unset (0 < id)
        s["skill_state"] = rng.random() < 0.4
        s["young"] = rng.randint(0, 6)
        s["lay"] = rng.random() < 0.6
    # Diablo's (FUN_005e9170): his target pick (FUN_005e8f20: the player he
    # scores best, its score, the players looked at), his kept point (the
    # leader word, FUN_0058eef0), stuck where he stands (FUN_0064d910 mask
    # 0x40), the target within reach (FUN_00622c40), the target's left and
    # right skills (FUN_00620190 / FUN_006201d0 -> FUN_00643ce0) at sk_level,
    # its state 0xb, the players about him (FUN_00573930), in the Chaos
    # Sanctuary (the level FUN_0061a1b0 names: 108).
    if ai in DIABLO_LIKE:
        cx, cy = s["spots"]["mon"]
        s["d_target"] = rng.random() < 0.85
        s["score"], s["count"] = rng.choice((rng.randint(0, 60), rng.randint(0, 200))), rng.randint(0, 6)
        s["word"] = None if rng.random() < 0.3 else (cx + rng.randint(-40, 40), cy + rng.randint(-40, 40))
        s["stuck"] = rng.random() < 0.1
        s["reach"] = rng.random() < 0.4
        s["lskill"], s["rskill"] = (rng.choice((None, None, 0x3b, 0x38, 0x33, 0x1b, 7)) for _ in range(2))
        s["state_b"] = rng.random() < 0.3
        s["players"] = rng.randint(0, 4)
        s["chaos"] = rng.random() < 0.7
        s["ctrl"][0] = rng.choice((0, 0, 0, 5))
        s["foe_player"] = rng.random() < 0.7                             # a player (unit type 0): his think looks for its leader
        if ai.startswith("Baal"):                                           # Baal's pending act (+0x14) and clones made (+0x18)
            s["ctrl"][0], s["ctrl"][1] = rng.choice((0, 0, 0, 0, rng.randint(1, 16))), rng.choice((0, 1, 2, 3))                           # its pending action: only 5 (Lightning) is ever kept
        s["stat"][("foe", 0x29)] = rng.choice((0, rng.randint(0, 100), rng.randint(-50, 300)))
    # Act 5's: three clear lines to the target (FUN_005dc640), Skill1's reach
    # (FUN_004e6c70), the siege beast's rider (FUN_00552fd0), it lies dead
    # (FUN_0063ea40), its state 0x69 test (FUN_006259b0), an imp to take on
    # (FUN_005dd0b0 with FUN_005e1720); the target in state 0x20 (FUN_00625760),
    # its FUN_00625d10 / FUN_00625d60, the units' own lives (FUN_00621f20);
    # its command's last frame (+0x14).
    # The arrow traps' level kind (FUN_005fb650: 0 / 2 the spike, 1 the poison
    # ball); the desert turret's step (+0x1c) is one of its eight.
    if ai in LATE_AIS:
        s["trap_kind"] = rng.randint(0, 2)
        if ai == "DesertTurret": s["ctrl"][2] = rng.randint(0, 7)
        if ai.startswith("Trap-") and rng.random() < 0.7:                  # in its range and lined up, often
            s["dist"] = rng.randint(min(s["aip"][0], s["aip"][1]), max(s["aip"][0], s["aip"][1]))
            mx, my = s["spots"]["mon"]
            fx, fy = s["spots"]["foe"]
            s["spots"]["foe"] = s["spot"] = (mx + rng.randint(-3, 3), fy) if ai == "Trap-RightArrow" else (fx, my + rng.randint(-3, 3))
            s["ctrl"][0] = rng.choice((0, s["frame"] + rng.randint(-20, 20)))
    if ai in ACT5_AIS:
        s["lines"] = rng.random() < 0.6
        s["sk_range"] = rng.choice((0, rng.randint(0, 30)))
        s["rider"], s["self_dead"], s["state69"], s["imp"] = rng.random() < 0.4, rng.random() < 0.1, rng.random() < 0.2, rng.random() < 0.5
        s["state20"] = rng.random() < 0.3
        s["mana"] = (rng.randint(0, 200), rng.randint(0, 200))
        s["foe_life"] = rng.choice((rng.randint(0, 100), 100))
        s["cmd_until"] = s["frame"] + rng.randint(-30, 30)
        s["foe_kind"] = rng.choice(("player", "monster"))
        s["reach"] = rng.random() < 0.5
        s["attacker"], s["hurt"], s["free_minion"] = rng.random() < 0.6, rng.random() < 0.5, rng.random() < 0.5   # the Overseer's
        s["catapult_gone"], s["strike_spot"] = rng.random() < 0.3, rng.random() < 0.7
        if ai == "CatapultSpotter": s["ctrl"][0] = rng.randint(0, 4)        # its strike pick: only rand(5) is kept
        s["spawn_unset"], s["spawn_found"], s["spawn_room"] = rng.random() < 0.3, rng.random() < 0.5, rng.random() < 0.7
        s["defile"] = rng.random() < 0.5
        s["ancient"], s["asleep"], s["state36"] = rng.randint(0, 2), rng.random() < 0.15, rng.random() < 0.15
        if ai == "Ancient":                                                # no target: a pick (FUN_005e8f20) as Diablo's
            s["target"] = rng.random() < 0.7
            s["d_target"], s["score"], s["count"] = rng.random() < 0.8, rng.randint(0, 60), rng.randint(0, 4)
        s["imp_aip"] = [[rng.choice((0, 1, 2, rng.randint(0, 100), rng.randint(0, 20), 100)) for _ in range(8)] for _ in range(4)]
    if ai == "ImpRide":                                                    # its pick (+0x18: 0 none, 2 / 4) and +0x1c (never set), state 0x8f
        s["ctrl"][1], s["ctrl"][2] = rng.choice((0, 2, 2, 4, 4)), 0
        s["riding"], s["rider"] = rng.random() < 0.9, rng.random() < 0.9
        s["dist"] = rng.choice((s["dist"], rng.randint(20, 30)))
    if ai == "BaalCrabClone":                                              # mostly with a live master: its think
        s["rider"], s["busy"], s["group"] = rng.random() < 0.9, rng.random() < 0.1, rng.choice((None, None, "mon", "kin"))
    if ai == "Vulture":
        for k in range(2):                                                 # its spot words: a point, or unset
            if rng.random() < 0.5: s["ctrl"][1 + k] = s["spots"]["foe"][k] + rng.randint(-12, 12)
    return s


CUR = {}


def install(e):
    cur = CUR
    def out(*item): cur["out"].append(item); return 0
    def name(addr): return cur["names"].get(addr)
    def seed():
        u = cur["units"]["mon"]
        return Seed(e.r32(u + 0x20), e.r32(u + 0x24))
    def put(sd):
        u = cur["units"]["mon"]
        e.w32(u + 0x20, sd.lo); e.w32(u + 0x24, sd.hi)
    def moved():
        return int(cur["moves"].random() >= cur["s"]["fails"])
    # The acts.
    e.hook(0x5ddf90, lambda e: out("mode", e.arg(0), name(e.arg(1))) or int(cur["s"]["ai"] in ACT5_AIS), 2)   # Act 5's read it (set)
    e.hook(0x5de080, lambda e: out("stand", S32(e.arg(0))), 1)
    e.hook(0x5de0f0, lambda e: out("wait", S32(e.arg(0))), 1)
    e.hook(0x5de130, lambda e: out("event", S32(e.arg(0))), 1)
    e.hook(0x5dead0, lambda e: out("skill", e.arg(0) & 0xff, S32(e.arg(1)), name(e.arg(2)), S32(e.arg(3)), S32(e.arg(4))) or 1, 5)
    e.hook(0x5de000, lambda e: out("seq", S32(e.arg(0)), name(e.arg(1))) or 1, 4)
    def walk(kind):                                                    # set off (ai.cpp handles one that can't)
        def go(e):
            out(kind, name(e.arg(0)), S32(e.arg(1)), 1)
            return 1
        return go
    e.hook(0x5dec80, walk("walk"), 2)
    e.hook(0x5ded40, walk("run"), 2)
    e.hook(0x5def80, walk("approach"), 2)
    e.hook(0x5defb0, walk("run"), 2)                                   # a run of n subtiles (FUN_005deb60 mode 0xf)
    def away(e):
        ok = moved()
        out("away", name(e.arg(0)), S32(e.arg(1)), e.arg(2), ok)
        return ok
    e.hook(0x5defe0, away, 3)
    def circle(e):                                                     # FUN_005df7d0: a step, then at the target
        sd = seed(); v = sd.step(); put(sd)
        ok = moved() if cur["s"]["ai"] in ACT3_AIS else 1                   # Act 3's read whether it set off
        out("circle", name(e.arg(0)), S32(e.arg(1)), e.arg(2), int((v & 0xff) >= 0x80), ok)
        return ok
    e.hook(0x5df7d0, circle, 3)
    def wander_draws(n):                                               # FUN_005de200 / FUN_005df530's three draws
        sd = seed()
        if sd.step() & 1 == 0: x, y = sd.rand(n), n
        else: x, y = n, sd.rand(n)
        if sd.step() & 1: x = -x
        if sd.step() & 1: y = -y
        put(sd)
        return x, y
    def wander(e):
        n = e.arg(0) & 0xff
        out("wander", n, *wander_draws(n))
        return 1
    e.hook(0x5de200, wander, 1)
    def around(e):
        unit, n = e.arg(0), e.arg(1) & 0xff
        if not unit: return wander_n(2)
        out("around", name(unit), n, *wander_draws(n))
        return 1
    def wander_n(n):
        out("wander", n, *wander_draws(n)); return 1
    e.hook(0x5df530, around, 2)
    def pace(e):                                                       # FUN_005a6260 keeps the steps as a byte (Act 4's: the Abyss Knight's high bits)
        out("pace", S32(e.arg(0)), e.arg(1) & 0xff if cur["s"]["ai"] in ACT4_AIS else S32(e.arg(1)))
        return 0
    e.hook(0x5de190, pace, 2)
    # The effects.
    e.hook(0x639db0, lambda e: out("state", e.arg(1), e.arg(2)), 3)
    e.hook(0x621e40, lambda e: out("overlay", e.arg(1)), 3)
    def setstat(e):
        out("stat", e.arg(1), S32(e.arg(2)))
        return 0
    e.hook(0x627260, setstat, 4)
    e.hook(0x58f730, lambda e: out("rally", e.r32(e.arg(0) + 8)) if cur["s"]["ai"] in ACT3_AIS else out("rally"), 1)   # Act 3's: the command's kind
    e.hook(0x58ef40, lambda e: 0, 1)
    e.hook(0x58ed10, lambda e: out("command_done"), 0)
    e.hook(0x59c330, lambda e: out("summoner_open"), 0)
    e.hook(0x56deb0, lambda e: 0, 2)
    e.hook(0x5701b0, lambda e: out("aura", S32(e.arg(0))), 2)
    e.hook(0x620c10, lambda e: 0, 2)                                   # face the unit
    # The answers.
    e.hook(0x5dd2b0, lambda e: int(cur["s"]["got_hit"]), 0)
    e.hook(0x621f20, lambda e: cur["s"]["life"] if cur["s"]["ai"] not in ACT5_AIS or name(e.arg(0)) == "mon" else cur["s"]["foe_life"], 1)
    e.hook(0x5dd280, lambda e: cur["s"]["life"], 0)
    e.hook(0x625480, lambda e: cur["s"]["stat"].get((name(e.arg(0)), e.arg(1)), 0) & MASK, 3)
    def second(e):
        s = cur["s"]
        e.w32(e.arg(0), s["foe2_dist"] if s["foe2"] else 0x7fffffff)
        if not s["foe2"]: return 0
        return cur["units"]["foe2"]
    e.hook(0x5ddc30, second, 2)
    def search(e):
        s, ctx, cb = cur["s"], e.arg(0), e.arg(1)
        if cb in (0x5b0d00, 0x5f06b0):                                     # the nearest kin / other monster
            if s["kin"] and S32(s["kin_d"]) < S32(e.r32(ctx + 4)):
                e.w32(ctx, cur["units"]["kin"]); e.w32(ctx + 4, s["kin_d"])
        elif cb == 0x5f7700:                                               # Mephisto's chooser: [3] the weakest, [5] a player nearest
            if s["weak"]: e.w32(ctx + 12, cur["units"]["foe2"])
            if s["near5"]: e.w32(ctx + 20, cur["units"]["kin"])
        elif cb == 0x5e0430:                                               # the weakest ally under 76 % within 50 (squared): the council's
            if s["aid"] and S32(s["kin_d"]) <= S32(e.r32(ctx + 8)): e.w32(ctx, cur["units"]["aid"])
        elif cb == 0x5f7240:                                               # a hurt zakarum within aip6 (squared): the priest's
            if s["aid"] and S32(s["kin_d"]) <= S32(e.r32(ctx + 12)): e.w32(ctx, cur["units"]["aid"])
        elif cb == 0x5f99a0:                                               # a dead fetish within aip5 (squared): the shaman's
            if s["corpse"] and S32(s["kin_d"]) <= S32(e.r32(ctx + 8)):
                e.w32(ctx, cur["units"]["kin"]); e.w32(ctx + 4, s["kin_d"])
        elif cb == 0x5f30f0:                                               # a weak one within 11 (the Vulture's)
            if s["victim"]: e.w32(ctx, cur["units"]["foe2"])
        elif cb == 0 and e.arg(2) == 0xc:                                  # the vile mother's young within 25 (type 0xc: a count, +8)
            e.w32(ctx + 8, s["young"])
        elif cb == 0x5fa690:                                               # the Regurgitator's corpse within aip6 (squared, +4)
            if s["corpse"] and S32(s["kin_d"]) <= S32(e.r32(ctx + 4)): e.w32(ctx, cur["units"]["kin"]); e.w32(ctx + 4, s["kin_d"])
        elif cb == 0x5fae40:                                               # the Oblivion Knight's knight (class 0x136) within 2500 (squared, +8)
            if s["kin"] and S32(s["kin_d"]) <= S32(e.r32(ctx + 8)): e.w32(ctx, cur["units"]["kin"]); e.w32(ctx + 12, s["kin_d"])
        elif cb == 0x5e1720:                                               # the siege beast's imp to take on (returned)
            return cur["units"]["kin"] if s["imp"] else 0
        elif cb == 0x5e26d0:                                               # the Overseer's: a hurt minion, a free one, the count
            if s["hurt"]: e.w32(ctx + 12, cur["units"]["kin"])
            if s["free_minion"]: e.w32(ctx + 16, cur["units"]["aid"])
            e.w32(ctx + 20, s["young"])
        elif cb == 0x5efa50:                                               # the putrid defiler's find (returned)
            return cur["units"]["kin"] if s["defile"] else 0
        elif cb == 0x5e2b70:                                               # the spawner's live minions
            e.w32(ctx, s["young"])
        elif cb == 0x5f2a00:                                               # a corpse, a hurt one, their counts
            if s["corpse"]: e.w32(ctx, cur["units"]["kin"])
            if s["aid"]: e.w32(ctx + 4, cur["units"]["aid"])
            e.w32(ctx + 8, s["counts"][0]); e.w32(ctx + 12, s["counts"][1])
        else:
            raise RuntimeError(f"search callback {cb:#x}")
        return 0
    e.hook(0x5dd0b0, search, 3)
    e.hook(0x5fd470, lambda e: int(cur["s"]["usable"]), 4)
    def command(e):
        c = cur["s"]["command"]
        if c is None: return 0
        e.w32(cur["cmd"] + 8, c)
        if "cmd_until" in cur["s"]: e.w32(cur["cmd"] + 0x14, cur["s"]["cmd_until"] & MASK)
        return cur["cmd"]
    e.hook(0x58ee80, command, 0)
    e.hook(0x58f0d0, lambda e: cur["units"][cur["s"]["group"]] if cur["s"]["group"] else 0, 0)
    e.hook(0x6201d0, lambda e: (cur["rrec"] if cur["s"]["rskill"] is not None else 0) if cur["s"]["ai"] in DIABLO_LIKE else int(cur["s"]["aura"]), 1)
    e.hook(0x6488c0, lambda e: cur["s"]["spots"][cur["paths"][e.arg(0)]][0], 1)   # a unit's path spot
    e.hook(0x648900, lambda e: cur["s"]["spots"][cur["paths"][e.arg(0)]][1], 1)
    def squared(e):                                                    # FUN_005b0bd0 (ECX, EDX: the two units)
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX
        (ax, ay), (bx, by) = (cur["s"]["spots"][name(e.mu.reg_read(r))] for r in (UC_X86_REG_ECX, UC_X86_REG_EDX))
        return (ax - bx) ** 2 + (ay - by) ** 2
    e.hook(0x5b0bd0, squared, 0)
    e.hook(0x620bb0, lambda e: 1 if name(e.arg(0)) == "mon" or cur["s"]["same_room"] else 2, 1)
    e.hook(0x620510, lambda e: 2, 1)
    def land(e):                                                       # FUN_005f2fc0
        if not cur["s"]["can_land"]: return 0
        out("land"); out("wait", 12)
        return 1
    e.hook(0x5f2fc0, land, 0)
    e.hook(0x5f2eb0, lambda e: out("takeoff") or out("wait", 12), 0)
    def free(e):                                                       # FUN_0064e7b0(room, &spot, size, mask, 1): by the target, then itself
        spot = cur["s"]["free"][cur["frees"]]
        cur["frees"] += 1
        if spot is None: return 0
        e.w32(e.arg(1), spot[0]); e.w32(e.arg(1) + 4, spot[1])
        return 1
    e.hook(0x64e7b0, free, 5)
    e.hook(0x5de4e0, lambda e: out("keep", name(e.arg(0)), e.arg(1), S32(e.arg(2)), S32(e.arg(3))) or 1, 4)
    e.hook(0x5de440, lambda e: out("fly", S32(e.arg(0)), S32(e.arg(1)), e.arg(2)) or 1, 3)
    e.hook(0x599420, lambda e: 0, 0)
    e.hook(0x553540, lambda e: cur["units"]["foe"], 2)                 # the unit its mode is at (the target)
    e.hook(0x57ccb0, lambda e: out("die"), 2)
    # The curse / other thinks'.
    from unicorn.x86_const import UC_X86_REG_EDX as EDX
    e.hook(0x46c140, lambda e: int(cur["s"]["a1"] if e.mu.reg_read(EDX) == 4 else cur["s"]["rn"] if e.mu.reg_read(EDX) == 15 else 0), 0)
    e.hook(0x457490, lambda e: int(cur["s"]["interact"] and e.mu.reg_read(EDX) == 9), 0)
    e.hook(0x639df0, lambda e: int(cur["s"].get("afraid", False) and e.arg(1) == 0x38 or cur["s"].get("state_c", False) and e.arg(1) == 0xc
                                    or cur["s"].get("state_b", False) and e.arg(1) == 0xb
                                    or cur["s"].get("state36", False) and e.arg(1) == 0x36
                                    or cur["s"].get("riding", False) and e.arg(1) == 0x8f
                                    or cur["s"].get("skill_state", False) and e.arg(1) in (SKILL_STATE, 0x54)), 2)   # 0x54: the finger mage's target's
    def toggle(e):                                                     # FUN_005b0e00 back to the MonAI row's think: the reset branch
        out("toggle", e.arg(1))
        for k in range(3): e.w32(e.arg(0) + 0x14 + 4 * k, 0)
        return 0
    e.hook(0x5b0e00, toggle, 2)
    e.hook(0x540e60, lambda e: 0, 2)                                   # its pending think events dropped
    e.hook(0x5417d0, lambda e: out("event", S32(e.arg(1)) - cur["s"]["frame"]) if e.arg(0) != 0xc else 0, 4)   # a think event at frame + n (0xc: the wisps' blessing's end)
    e.hook(0x573120, lambda e: out("unset"), 0)
    e.hook(0x5df140, away, 3)
    def command14(e):
        e.w32(cur["cmd"] + 0x18, cur["s"]["cast_word"])
        return cur["cmd"]
    e.hook(0x58efa0, command14, 2)
    # Act 3's.
    e.hook(0x5415a0, lambda e: cur["s"].get("next_event", 0) & MASK, 1)
    e.hook(0x5ddfc0, lambda e: out("gone", S32(e.arg(0)), S32(e.arg(1)), S32(e.arg(2))), 3)   # a mode at a point (0: it leaves the game)
    def wisps(e):                                                      # FUN_0065a950(.., &ctx, ..): the finds' list (+4) and count (+0x18)
        found = [cur["units"][n] for n in ("kin", "aid", "foe2", "foe", "mon", "kin")][:cur["s"]["wisps"]]
        lst = e.alloc(4 * 8)
        for k, u in enumerate(found): e.w32(lst + 4 * k, u)
        e.w32(e.arg(1) + 4, lst); e.w32(e.arg(1) + 0x18, len(found))
        return 0
    e.hook(0x65a950, wisps, 8)
    e.hook(0x65ac70, lambda e: 0, 1)
    e.hook(0x65aa00, lambda e: 0, 1)
    from unicorn.x86_const import UC_X86_REG_ECX as ECX
    e.hook(0x58ec00, lambda e: out("word", cur["word_index"].setdefault(e.mu.reg_read(ECX), len(cur["word_index"])), e.mu.reg_read(EDX), S32(e.arg(0))), 1)
    e.hook(0x5de490, lambda e: out("move_to", S32(e.arg(0)), S32(e.arg(1)), S32(e.arg(2))) or 1, 3)
    e.hook(0x5541b0, lambda e: int(cur["s"].get("busy", False)), 0)
    e.hook(0x6251f0, lambda e: cur["pcdata"], 5)
    e.hook(0x627150, lambda e: out("bless", e.arg(1), S32(e.arg(2))), 4)
    e.hook(0x6260b0, lambda e: 0, 2)
    e.hook(0x626e10, lambda e: 0, 3)
    e.hook(0x6439b0, lambda e: 1, 1)                                   # its Skill1 record (FUN_006442a0 reads none of it here)
    e.hook(0x6442a0, lambda e: cur["s"].get("sk_level", 0) & MASK, 3)
    e.hook(0x64cb30, lambda e: cur["s"].get("spot_coll", 0), 4)
    e.hook(0x5ded20, lambda e: out("run", name(e.arg(0)), 0) or 1, 1)   # a move in mode 15 (RN)
    e.hook(0x5dd290, lambda e: int(cur["s"].get("los", True)), 0)
    e.hook(0x622aa0, lambda e: int(not cur["s"].get("los", True)), 3)     # the line blocked (mask 4)
    def missile_row(e):                                                # FUN_0046ace0(ECX id): its Missiles.txt row
        if cur["s"].get("miss_range", -1) < 0: return 0
        e.mu.mem_write(cur["missile"] + 0x96, struct.pack("<h", cur["s"]["miss_range"]))
        return cur["missile"]
    e.hook(0x46ace0, missile_row, 0)
    def owner(e):                                                      # FUN_0058f090(ECX unit, EDX &id, &type)
        from unicorn.x86_const import UC_X86_REG_EDX
        pet = cur["s"].get("foe_kind") == "pet"
        e.w32(e.mu.reg_read(UC_X86_REG_EDX), 7 if pet else MASK)
        if pet: e.w32(e.arg(0), 0)
        return 0
    e.hook(0x58f090, owner, 1)
    e.hook(0x61a1b0, lambda e: (0x6c if cur["s"]["chaos"] else 0x67) if "chaos" in cur["s"] else 80 if cur["s"].get("act3") else 40, 1)
    e.hook(0x6221a0, lambda e: cur["pcdata"], 1)
    e.hook(0x65c310, lambda e: int(cur["s"].get("quest21", False) and e.arg(1) == 0x15 and e.arg(2) == 0), 3)                         # it leaves the game (mode 0)
    def by_id(e):                                                      # FUN_00552f60(ECX game, EDX type, id)
        from unicorn.x86_const import UC_X86_REG_EDX
        if cur["s"]["ai"] == "ZakarumZealot" and e.mu.reg_read(UC_X86_REG_EDX) == 0:
            return cur["units"]["foe2"] if cur["s"]["foe_kind"] == "pet" and cur["s"]["owner"] else 0   # id -1 (no owner) finds none
        return cur["units"]["kin"] if cur["s"].get("cmd_unit") else 0
    e.hook(0x552f60, by_id, 1)
    # Coldworm's young (FUN_005f9cf0): FUN_0063efa0(unit, &class, &x, &y, &mode, 0, fn) its spawn's
    # class, spot and mode; FUN_00463740 a room there; FUN_005b2f20 makes it (here: not made,
    # so the count the made one adds, ai.cpp's, stays out of the comparison).
    def spawn_spot(e):
        for k, v in enumerate((68, 108, 100, 8)): e.w32(e.arg(1 + k), v)
        return 0
    e.hook(0x63efa0, spawn_spot, 7)
    e.hook(0x463740, lambda e: 1, 0)
    e.hook(0x5b2f20, lambda e: out("spawn"), 6)
    # Act 4's: the vile mother lays (FUN_005f9e30: Skill1 at the first of
    # eight spots toward the target it fits), Izual's first think greets
    # (FUN_005b4390: a4q1's log to every player).
    e.hook(0x5f9e30, lambda e: out("lay") or int(cur["s"]["lay"]), 2)
    # Diablo's.
    diablo = lambda: cur["s"]["ai"] in DIABLO_LIKE
    def pick(e):                                                       # FUN_005e8f20(ECX game, EDX him; &score, &count, word, fn)
        s = cur["s"]
        e.w32(e.arg(0), s["score"]); e.w32(e.arg(1), s["count"])
        return cur["units"]["foe"] if s["d_target"] else 0
    e.hook(0x5e8f20, pick, 4)
    def baal_pick(e):                                                  # FUN_005fbdd0(game, &score, &count, 0, fn; EBX him)
        s = cur["s"]
        e.w32(e.arg(1), s["score"]); e.w32(e.arg(2), s["count"])
        return cur["units"]["foe"] if s["d_target"] else 0
    e.hook(0x5fbdd0, baal_pick, 5)
    e.hook(0x5fc860, lambda e: out("clone") or 1, 2)                   # FUN_005fc860: a Vile Effigy (World's)
    e.hook(0x6416d0, lambda e: cur["s"]["dist"], 2)                    # FUN_006416d0: him to the target
    e.hook(0x54dc40, lambda e: 1, 4)                                   # a free spot there (ponytail: d2d's nearest_free)
    e.hook(0x647280, lambda e: 0, 6)
    def word(e):                                                       # FUN_0058eef0(ECX unit, EDX kind; 0): his kept point
        spot = cur.get("word") if diablo() else None
        if not spot: return 0
        e.w32(cur["word_rec"] + 0xc, spot[0]); e.w32(cur["word_rec"] + 0x10, spot[1])
        return cur["word_rec"]
    e.hook(0x58eef0, word, 1)
    def keep_word(e):                                                  # FUN_0058ef40(.., &{.., kind +8, x +0xc, y +0x10})
        if diablo():
            spot = (S32(e.r32(e.arg(0) + 0xc)), S32(e.r32(e.arg(0) + 0x10)))
            cur["word"] = spot; out("mark", *spot)
        return 0
    e.hook(0x58ef40, keep_word, 1)
    e.hook(0x64d910, lambda e: int(cur["s"].get("stuck", False)), 5)
    e.hook(0x5353f0, lambda e: 7, 0)                                   # the player's id his think looks up (FUN_00552f60 answers)
    e.hook(0x622c40, lambda e: int(cur["s"].get("reach", False)), 3)
    e.hook(0x620190, lambda e: cur["lrec"] if diablo() and cur["s"]["lskill"] is not None else 0, 1)
    e.hook(0x643ce0, lambda e: (cur["s"]["lskill"] if e.arg(0) == cur["lrec"] else cur["s"]["rskill"]) if diablo() else cur["s"]["skills"][0] & MASK, 3)
    e.hook(0x644360, lambda e: cur["s"]["modes"][0], 1)                 # a skill record's mode (Nihlathak's teleport)
    e.hook(0x63ec70, lambda e: 0x1c5, 2)
    e.hook(0x58bc40, lambda e: 0, 0)
    e.hook(0x56e390, lambda e: cur["units"]["kin"] if cur["s"].get("corpse") else 0, 2)   # a corpse to explode
    def players(e):                                                    # FUN_00573930(ECX game, EDX &out; him, room): +8 MonsterSkillBonus, +0xc the difficulty
        from unicorn.x86_const import UC_X86_REG_EDX
        e.w32(e.mu.reg_read(UC_X86_REG_EDX) + 8, cur["s"].get("players", 0))
        e.w32(e.mu.reg_read(UC_X86_REG_EDX) + 0xc, cur["s"]["diff"])
        return 0
    e.hook(0x573930, players, 2)
    def go(e):                                                         # FUN_005dee50(ECX game, EDX him; x, y): a walk to the point, set off?
        ok = moved()
        out("goto", S32(e.arg(0)), S32(e.arg(1)), ok)
        return ok
    e.hook(0x5dee50, go, 2)
    e.hook(0x5ded90, lambda e: out("walkto", S32(e.arg(0)), S32(e.arg(1))), 2)
    e.hook(0x5b4390, lambda e: out("greet"), 0)
    # Act 5's.
    act5 = lambda: cur["s"]["ai"] in ACT5_AIS
    e.hook(0x5dc640, lambda e: int(cur["s"].get("lines", True)), 0)
    e.hook(0x4e6c70, lambda e: cur["s"].get("sk_range", 0), 0)
    e.hook(0x552fd0, lambda e: cur["units"]["foe2"] if cur["s"].get("rider") else 0, 0)
    e.hook(0x63ea40, lambda e: int(cur["s"].get("self_dead", False)), 1)
    e.hook(0x6259b0, lambda e: int(cur["s"].get("state69", False)), 1)
    e.hook(0x5e17e0, lambda e: out("mount"), 0)
    e.hook(0x625760, lambda e: int(cur["s"].get("state20", False) and e.arg(1) == 0x20), 2)
    e.hook(0x625d10, lambda e: cur["s"]["mana"][0], 1)
    e.hook(0x625d60, lambda e: cur["s"]["mana"][1], 1)
    e.hook(0x5deca0, lambda e: out("direct", name(e.arg(0))), 1)
    e.hook(0x5de830, lambda e: cur["units"]["foe2"] if cur["s"].get("attacker") else 0, 0)
    e.hook(0x5dd2d0, lambda e: 1, 0)                                   # it has the skill (its own row's)
    e.hook(0x6439f0, lambda e: 1, 0)                                   # its Skill1 record
    e.hook(0x554010, lambda e: 0, 0)
    e.hook(0x587900, lambda e: 0, 0)                                   # Shenk's quest step
    e.hook(0x5edf70, lambda e: int(cur["s"].get("catapult_gone", False)), 0)
    e.hook(0x58cf90, lambda e: int(cur["s"].get("asleep", False)), 0)
    def mode_set(e):                                                   # FUN_005a7e60(unit, mode, &ctx): the mode it's given
        e.w32(e.arg(2), e.arg(1)); return 0
    e.hook(0x5a7e60, mode_set, 3)
    e.hook(0x620210, lambda e: 0, 2)
    e.hook(0x649070, lambda e: 0, 2)
    e.hook(0x5a7c20, lambda e: out("leap", e.r32(e.arg(1)), S32(e.r32(e.arg(1) + 0xc)), S32(e.r32(e.arg(1) + 0x10))), 3)
    def strike_spot(e):                                                # FUN_0064e7e0(room, &spot, 2, 0x805, 3): the spot kept
        return int(cur["s"].get("strike_spot", True))
    e.hook(0x64e7e0, strike_spot, 5)
    from unicorn.x86_const import UC_X86_REG_ECX as RECX
    e.hook(0x45adf0, lambda e: cur["s"]["spots"][name(e.mu.reg_read(RECX)) or "mon"][0], 0)
    e.hook(0x45ae20, lambda e: cur["s"]["spots"][name(e.mu.reg_read(RECX)) or "mon"][1], 0)
    e.hook(0x5e6020, lambda e: int(cur["s"].get("spawn_found", True)), 1)
    e.hook(0x5fd350, lambda e: int(cur["s"].get("spawn_room", True)), 3)                          # it dies (the egg hatched)                                   # Radament's: FUN_00599420 (the quest's)
    e.hook(0x5fb650, lambda e: cur["s"]["trap_kind"], 0)                                            # the arrow traps' level kind


def run_game(e, s, addr, rng_moves):
    def alloc(n): a = e.alloc(n); e.mu.mem_write(a, bytes(n)); return a
    game, params, ctrl, row, cmd = (alloc(n) for n in (0x2000, 0x40, 0x40, 0x200, 0x40))
    units, names, paths = {}, {}, {}
    for k, n in enumerate(UNITS):
        u = alloc(0x200)
        path = alloc(0x80)
        e.w32(u, 1); e.w32(u + 4, 0x100 + k); e.w32(u + 0xc, k + 1); e.w32(u + 0x2c, path)
        units[n], names[u], paths[path] = u, n, n
    mon = units["mon"]
    e.w32(mon + 0x20, s["seed"][0]); e.w32(mon + 0x24, s["seed"][1])
    e.mu.mem_write(game + 0x6d, bytes([s["diff"]])); e.w32(game + 0xa8, s["frame"])
    for k, v in enumerate(s["aip"]): e.mu.mem_write(row + 0x56 + 6 * k + 2 * s["diff"], struct.pack("<h", v))
    e.mu.mem_write(row + 0x32, struct.pack("<hh", s["velocity"], s["run"]))
    e.mu.mem_write(row + 0x170, struct.pack("<5h", *s["skills"]))
    e.mu.mem_write(row + 0x180, bytes(s["modes"]))
    e.mu.mem_write(row + 0x4f + s["diff"], bytes([s["aidel"]]))
    if s["ai"] in ACT3_AIS:
        e.mu.mem_write(row + 0x3e, struct.pack("<h", 1))
        e.mu.mem_write(row + 0x17a, struct.pack("<3h", *s["skills68"]))   # MissS1: a missile (FUN_0046ace0 answers by miss_range)
    for k, v in enumerate(s["ctrl"]): e.w32(ctrl + 0x14 + 4 * k, v & MASK)
    e.w32(params, ctrl); e.w32(params + 7 * 4, row)
    if s["target"]: e.w32(params + 8, units["foe"])
    e.w32(params + 20, s["dist"]); e.w32(params + 24, int(s["melee"]))
    if s["ai"] in DIABLO_LIKE: e.w32(units["foe"], 0 if s["foe_player"] else 1)
    if s["ai"] in ("ZakarumZealot", "WillOWisp") or s["ai"] in ACT5_AIS:
        e.w32(units["foe"], 0 if s["foe_kind"] == "player" else 1)
        e.w32(units["foe2"], 0)                                          # the owner, a player
    if s["ai"] in ACT3_AIS:
        e.w32(units["kin"] + 0x10, s["leader_mode"]); e.w32(mon + 0x10, s["leader_mode"])
    if s["ai"] in CURSE_AIS:
        e.w32(mon + 0x10, 11 if s["in_s4"] else 1)
        data, rows = alloc(0x1000), alloc(0x1a8 * 0x106)                 # MonStats: the unit's row's flag byte +0xd (bit 1: interact)
        e.w32(0x744304, data); e.w32(data + 0xa80, 0x106); e.w32(data + 0xa78, rows)
        e.mu.mem_write(rows + 0x1a8 * 0x100 + 0xd, bytes([2 if s["interact"] else 0]))
    if s["ai"] in ACT4_AIS:                                              # skill records: each names SKILL_STATE
        data, rows = alloc(0x1000), alloc(0x23c * 0x200)
        e.w32(0x744304, data); e.w32(data + 0xba0, 0x200); e.w32(data + 0xb98, rows)
        for k in s["skills"] + s.get("skills68", []):
            if 0 <= k < 0x200: e.mu.mem_write(rows + 0x23c * k + 0x80, struct.pack("<hh", SKILL_STATE, SKILL_STATE))
    if s["ai"] in ACT5_AIS:
        if s["ai"] == "GenericSpawner": e.w32(ctrl + 0x3c, MASK if s["spawn_unset"] else 0)
        if s["ai"] == "Ancient": e.w32(mon + 4, 0x21c + s["ancient"])     # its class picks the think
        e.w32(e.r32(0x744304) + 0xc4, 0x400)                             # the states' count: skill +0x80 / +0x82's tested
        block = alloc(0x40); e.w32(mon + 0x14, block); e.w32(block + 0x28, ctrl)   # unit +0x14 -> +0x28: its AI control
        data = e.r32(0x744304)
        if s["ai"] in ("Imp", "ImpRide"):                                # MonStats imp1..imp4 (0x1ec..0x1ef): their aips; imp1's skills (the ride's)
            rows = alloc(0x1a8 * 0x1f0)
            e.w32(data + 0xa80, 0x1f0); e.w32(data + 0xa78, rows)
            e.mu.mem_write(rows + 0x1a8 * 0x1ec + 0x170, struct.pack("<5h", *s["skills"]))
            e.mu.mem_write(rows + 0x1a8 * 0x1ec + 0x180, bytes(s["modes"]))
            for k, row_aip in enumerate(s["imp_aip"]):
                for j, v in enumerate(row_aip): e.mu.mem_write(rows + 0x1a8 * (0x1ec + k) + 0x56 + 6 * j + 2 * s["diff"], struct.pack("<h", v))
    if s["ai"].startswith("Baal"):                                       # MonStats by class (FUN_005fcb60): its row
        data = e.r32(0x744304)
        e.w32(data + 0xa80, 0x200); e.w32(data + 0xa78, (row - 0x1a8 * 0x100) & MASK)
    CUR.update(word=s.get("word"), word_rec=alloc(0x20), lrec=alloc(0x40), rrec=alloc(0x40))
    CUR.update(s=s, out=[], units=units, names=names, paths=paths, moves=rng_moves, cmd=cmd, frees=0, pcdata=alloc(0x40), missile=alloc(0x1a4), word_index={})
    e.call(addr, params, ecx=game, edx=mon)
    word = [e.r32(cmd + 0x18)] if s["ai"] == "SandMaggotOther" else []
    return CUR["out"], [S32(e.r32(ctrl + 0x14 + 4 * k)) for k in range(3)] + word, (e.r32(mon + 0x20), e.r32(mon + 0x24))


def outcome(s, out):
    """The native calls in rules::Think's terms: (act, n, x, y, unit, wait, event), the
    pace (pct, steps), the effects, the back-offs and moves asked (n, ok) in order."""
    act, pace, effects, asks = ["none", 0, 0, 0, 0, 0, 0], [0, 0], [], []
    def to(name, n=0, x=0, y=0, unit="foe"):
        act[:] = [name, n, x, y, UNIT[unit], act[5], act[6]]
    for item in out:
        k = item[0]
        if k == "mode": to(MODE_ACT[item[1]], unit=item[2])
        elif k == "stand" and act[0] == "direct": act[5] = item[1]          # Nihlathak's walk, then a stand: kept as its wait
        elif k == "stand": to("idle", item[1])
        elif k == "wait": act[5] = item[1]
        elif k == "event": act[6] = item[1]
        elif k == "skill" and s["ai"].startswith("Baal") and item[2] in BAAL_SKILLS:
            to("skill", BAAL_SKILLS[item[2]], item[4], item[5], item[3])
        elif k == "skill" and item[2] not in s["skills"] + s.get("skills68", []):   # a raw skill id (the catapult spotter's strike)
            to("skill", item[2], item[4], item[5], item[3])
        elif k == "skill":
            slot = (s["skills"] + s.get("skills68", [])).index(item[2])
            spot = (item[4], item[5]) == s["spot"]                         # at the target's spot (the Summoner's)
            to("skill", slot, 0 if spot else item[4], 1 if spot else item[5], item[3])
        elif k == "seq": to("seq", s["skills"].index(item[1]) if item[1] in s["skills"] else item[1], unit=item[2])
        elif k in ("walk", "run", "approach"): to(k, item[2], unit=item[1])
        elif k == "away":
            asks.append((item[2], item[4]))
            if item[4]: to("none")
            elif not item[3] and s["ai"] in ("Terror", "Vampire"): to("idle", s["aidel"])   # flags 0: FUN_005a73e0's think aidel on
        elif k == "circle":
            if s["ai"] in ACT3_AIS: asks.append((item[2], item[5]))
            to("circle", item[2], item[4], item[3], item[1])
        elif k == "wander": to("wander", item[1], item[2], item[3])
        elif k == "around": to("around", item[2], item[3], item[4], item[1])
        elif k == "pace":
            if item[1]: pace[0] = item[1]
            if item[2]: pace[1] = item[2]
        elif k == "die": to("die")
        elif k == "gone": to("gone") if item[1] == 0 else to("mode_at", item[1], item[2], item[3], None)
        elif k == "move_to": to("move_to", item[3], item[1], item[2], None)
        elif k == "word": effects.append(("set_word", item[1] * 4 + item[2], item[3]))
        elif k == "keep": to("keep", item[3], item[4], item[2], item[1])
        elif k == "fly" and s["ai"] == "SandMaggotQueen": to("spawn", 0)   # mode 8 where she stands
        elif k == "fly": to("fly", item[3], item[1], item[2], None)
        elif k == "spawn": to("spawn", 1)
        elif k == "direct": to("direct", unit=item[1])
        elif k == "leap": to("skill", 0, item[2], item[3], None)
        elif k == "goto":                                                  # FUN_005dee50: a walk to a point, asked whether it set off
            asks.append((-1, item[3]))
            if item[3]: to("move_to", 0, item[1], item[2], None)
        elif k == "walkto": to("move_to", 0, item[1], item[2], None)
        elif k == "mark": effects.append(("mark", item[1], item[2]))
        elif k == "lay":
            if s["lay"]: to("spawn", 2)                                    # the vile mother's lay toward the target
        elif k == "aura": effects.append(("aura", s["skills"].index(item[1])))
        else: effects.append(item)
    if any(item[0] == "die" for item in out): act[:] = ["die", 0, 0, 0, 0, 0, 0]   # dead: the stand after it is moot
    return act, pace, effects, asks


def spared(s):
    """The zealot's spare test, as its code reaches it."""
    if s["ai"] != "ZakarumZealot": return False
    player = s["foe_kind"] == "player" or (s["foe_kind"] == "pet" and s["owner"])
    return player and s["act3"] and s["quest21"]


def dump_case(s, act, pace, effects, asks, ctrl, seed):
    """A tests/mon_ais_cases.inc line: the scene | the moves asked | what it comes to."""
    nums = [ORDER.index(s["ai"]), s["diff"], s["frame"], int(s["target"]), s["dist"], int(s["melee"]), int(s["got_hit"]), s["life"]]
    nums += s["ctrl"] + s["aip"] + [int(k >= 0) for k in s["skills"]]
    nums += [s["velocity"], s["run"], s["aidel"]]
    nums += [s["stat"][(u, k)] for u in ("mon", "foe") for k in (0x4a, 0x27, 0x2b)]
    nums += [int(s["foe2"]), s["foe2_dist"], int(s["kin"]), s["kin_d"], int(s["corpse"]), int(s["aid"]), *s["counts"]]
    nums += [int(s["usable"]), s["command"] or 0, int(s["leader"]), int(s["aura"])]
    nums += [*s["spots"]["mon"], *s["spots"]["foe"], int(s["same_room"]), int(s["can_land"]), int(s["victim"]), int(s["group"] is not None)]
    nums += [c for f in s["free"] for c in ((1, *f) if f else (0, 0, 0))]
    if s["ai"] in CURSE_AIS:
        nums += [int(s["a1"] and not s["interact"]), int(s["rn"]), int(s["afraid"]), int(s["in_s4"]), s["cast_word"]]
    else:
        nums += [1, 0, 1, 0, 0]
    if s["ai"] in ACT3_AIS:
        nums += [s["next_event"], s["leader_mode"], int(s["cmd_unit"]), s["sk_level"], int(s["state_c"]), int(s["spot_coll"] == 0), int(spared(s)), int(s["los"]), s["miss_range"]] + [int(k >= 0) for k in s["skills68"]] + [int(s["weak"]), int(s["near5"]), s["wisps"], int(s["foe_kind"] == "player" and not s["busy"])]
    if s["ai"] in ACT4_AIS:
        nums += [int(s["skill_state"]), s["young"], int(s["lay"]), *s["spots"]["kin"]]
        if s["ai"] in DIABLO_LIKE:
            w = s["word"] or (0, 0)
            nums += [int(s["d_target"]), s["score"], s["count"], int(s["word"] is not None), *w, int(s["stuck"]), int(s["reach"]),
                     -1 if s["lskill"] is None else s["lskill"], -1 if s["rskill"] is None else s["rskill"], int(s["state_b"]), s["players"],
                     int(s["chaos"]), s["stat"][("foe", 0x29)], int(s["foe_player"])]
    if s["ai"] in ACT5_AIS:
        nums += [int(s["lines"]), s["sk_range"], int(s["rider"]), int(s["self_dead"]), int(s["state69"]), int(s["imp"]), int(s["state20"]),
                 *s["mana"], s["foe_life"], s["cmd_until"], int(s["foe_kind"] == "player"), int(s["reach"]), int(s["busy"]),
                 int(s["attacker"]), int(s["hurt"]), int(s["free_minion"])] + [v for row in s["imp_aip"] for v in row]
        nums += [int(s["catapult_gone"]), int(s["strike_spot"]), int(s["spawn_unset"]), int(s["spawn_found"]), int(s["spawn_room"]), int(s["defile"]),
                 s["ancient"], int(s["asleep"]), int(s["state36"]), int(s.get("d_target", False))]
        if s["ai"] == "ImpRide": nums += [int(s["riding"])]
    if s["ai"] in LATE_AIS: nums += [s["trap_kind"]]
    nums += list(s["seed"])
    kinds = {"state": 0, "overlay": 1, "stat": 2, "rally": 3, "command_done": 4, "summoner_open": 5, "aura": 6, "land": 7, "takeoff": 8,
             "toggle": 9, "unset": 10, "set_word": 11, "bless": 12, "greet": 13, "mark": 14, "mount": 15, "retarget": 16, "clone": 17}
    keep = lambda x: int(x[0] == "toggle" and s["ai"] == "SandMaggotOther")          # it puts the third word back after
    eff = [f"{kinds[x[0]]} {x[1] if len(x) > 1 else 0} {x[2] if len(x) > 2 else keep(x)}" for x in effects]
    tail = [ACTS.index(act[0])] + act[1:] + pace + ctrl[:3] + list(seed) + ctrl[3:4]   # the SandMaggot's command word after, last
    return " ".join(map(str, nums)) + " | " + " ; ".join(f"{n} {ok}" for n, ok in asks) + " | " + " ; ".join(eff) + " | " + " ".join(map(str, tail))


def check(e, ai, cases, rng, dumped):
    mark, kinds = e.brk, {}
    for _ in range(cases):
        e.brk = mark
        s = setup(rng, ai)
        out, ctrl, seed = run_game(e, s, AIS[ai], random.Random(rng.getrandbits(32)))
        act, pace, effects, asks = outcome(s, out)
        kinds[act[0]] = kinds.get(act[0], 0) + 1
        if dumped is not None:
            key = (act[0], act[1] if act[0] == "skill" else 0, tuple(x[0] for x in out), len(asks))   # each skill slot its own
            rows = dumped.setdefault(ai, {})
            if len(rows) < 40 or key not in rows: rows.setdefault(key, []).append(dump_case(s, act, pace, effects, asks, ctrl, seed))
    print(f"{ai} ok: {cases} cases, {kinds}")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--") and not a.endswith(".inc")]
    which = args[0] if args else "all"
    cases = int(args[1]) if len(args) > 1 else 2000
    rng = random.Random(int(args[2]) if len(args) > 2 else 1)
    e = emu.Emu()
    install(e)
    dumped = {} if "--dump" in sys.argv else None
    for ai in (AIS if which == "all" else which.split(",")): check(e, ai, cases, rng, dumped)
    if dumped is not None:
        with open(sys.argv[sys.argv.index("--dump") + 1], "w") as f:
            f.write("// tools/emu/mon_ais.py --dump: game.exe's own monster thinks (see the script), a line each.\n")
            for rows in dumped.values():
                for lines in rows.values(): f.write("".join(f'"{line}",\n' for line in lines[:6]))
        write_names(os.path.join(os.path.dirname(sys.argv[sys.argv.index("--dump") + 1]), "mon_ais_names.inc"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
