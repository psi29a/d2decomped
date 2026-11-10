# SPDX-License-Identifier: GPL-3.0-or-later
"""Check game.exe 1.14d's small client-packet handlers of PlrMsg.cpp (server side).

Each handler is called as `status = handler(game in ECX, unit in EDX, packet, size)`
(`ret 8`; 0 ok, 1 bad target, 2 bad data, 3 bad length). 0x33 (FUN_0054bb20) and 0x4f
(FUN_0054c7c0) return the callee's status (the 0x57_9510 / 0x56_8060 callees), every other
handler returns 0 after its callee. The oracle runs the stock
code on a fake game (+0x6d difficulty byte, +0x70 expansion dword), a fake unit
(+0 type, +0x10 mode, +0xc4 flags) and a random packet, with every callee hooked
and logged (registers ECX / EDX and the stack arguments), and compares the status
and the callee log with a model written from the disassembly.

Covers: FUN_0054ba90 (0x31 NPC gossip), 0054bb20 (0x33 sell), 0054bb60 (0x35
repair), 0054bba0 (0x34 identify), 0054bbd0 (0x36 hire), 0054bc00 (0x62 resurrect
merc, expansion only), 0054c070 (0x3f emote), 0054c0c0 (0x40 refresh quests), 0054c590
(0x48 cutscene over), 0054c7c0 (0x4f click button), 0054c940 (0x53 gamepad run),
0054c990 (0x54 stop running), 0054c9c0 (0x58 mark quest done).

Hooked (their work is not claimed): 0x54_43B0, 0x57_9510, 0x57_8050, 0x57_8460, 0x57_7FE0,
0x57_9C00, 0x55_3380, 0x54_6040, 0x62_21A0, 0x53_50B0, 0x56_8060, 0x62_5480, 0x62_4690, 0x65_C360.

Usage: D2_MPQ_DIR=<MPQ folder> uv run python plrmsg_small.py [--mutation-check]
"""
import random
import struct

import _monster_world as mw
from _monster_world import require

OK, BAD_TARGET, BAD_DATA, BAD_PARAMS = 0, 1, 2, 3
MUTATIONS = ("gossip-size-8", "sell-forwards-mode-dword", "repair-swaps-params", "identify-size-any",
             "hire-size-any", "resurrect-ignores-expansion", "resurrect-size-any", "emote-range-off-by-one",
             "emote-flag-ignored", "emote-null-unit-blocked", "emote-size-any", "quest-bound-0x29",
             "quest-wrong-difficulty-slot", "quest-size-any", "cutscene-flag-inverted", "button-param-swapped",
             "gamepad-ignores-stamina", "gamepad-mode-run", "gamepad-null-unit-calls", "gamepad-stamina-arg-0xb",
             "gamepad-flag-2", "stop-running-mode-walk", "stop-running-null-unit-calls", "stop-running-flag-3",
             "stop-running-args-game", "refresh-size-any", "quest-state-not-0xc", "sell-status-ok",
             "button-status-ok")
mutation = None


def mut(name):
    """True while the model carries the deliberate defect `name` (--mutation-check)."""
    return mutation == name


CALLEES = {  # address: (name, stack args popped); the return value is Rig.returns[name]
    0x54_43B0: ("gossip", 2), 0x57_9510: ("sell", 4), 0x57_8050: ("repair", 4), 0x57_8460: ("identify", 1),
    0x57_7FE0: ("hire", 2), 0x57_9C00: ("resurrect", 1), 0x55_3380: ("emote", 1), 0x54_6040: ("refresh", 0),
    0x53_50B0: ("cutscene_clear", 0), 0x56_8060: ("button", 2), 0x62_4690: ("update_flag", 2),
    0x65_C360: ("set_quest", 3),
}


class Rig:
    """One emulated game.exe with the callees hooked and the fake game / unit / packet blocks."""

    def __init__(self):
        self.w = mw.World()
        self.m = self.w.m
        self.returns = {name: 1 for name, _ in CALLEES.values()}
        self.stamina = 1
        for address, (name, nstack) in CALLEES.items():
            self.w.logger(address, nstack, name, ret=lambda _c, name=name: self.returns[name])
        self.w.logger(0x62_5480, 3, "stamina", ret=lambda _c: self.stamina)
        self.player_data = self.m.alloc(0x400)
        self.w.logger(0x62_21A0, 1, "player_data", ret=lambda _c: self.player_data)
        self.game = self.m.alloc(0x100)
        self.unit = self.m.alloc(0x100)
        self.packet = self.m.alloc(0x40)

    def reroll(self, rng):
        """Give every hooked callee a new random non-zero return value (a handler returning it shows)."""
        for name in self.returns:
            self.returns[name] = rng.randrange(1, 1 << 32)

    def call(self, address, packet, size, unit_ok=True):
        """Run one handler; return (status, callee log)."""
        self.m.mu.mem_write(self.packet, bytes(packet) + bytes(0x40 - len(packet)))
        status = self.w.run(address, self.packet, size, ecx=self.game, edx=self.unit if unit_ok else 0, nstack=2)
        return status, list(self.w.log)


def u16(value):
    """Two little-endian bytes."""
    return struct.pack("<H", value & 0xFFFF)


def rd16(packet, offset):
    """Little-endian u16 of the packet at `offset`."""
    return struct.unpack_from("<H", packet, offset)[0]


def rd32(packet, offset):
    """Little-endian u32 of the packet at `offset`."""
    return struct.unpack_from("<I", packet, offset)[0]


def check(got, want, label):
    """Compare the emulated answer with the model; mismatch stops the oracle."""
    require(got == want, f"{label}: game.exe {got!r}, model {want!r}")


def stack_args(log):
    """The callee log as (name, stack arguments...), without the ECX / EDX columns."""
    return [(entry[0],) + entry[3:] for entry in log]


def rand_packet(rng):
    """A packet of random bytes."""
    return bytes(rng.randrange(256) for _ in range(0x20))


def check_size_gate(rig, rng, address, size, label):
    """Every wrong size returns 3 with no callee run."""
    for wrong in sorted({0, size - 1, size + 1, size + 2, 0xFFFFFFFF, rng.randrange(0, 0x30)} - {size}):
        status, log = rig.call(address, rand_packet(rng), wrong)
        if mut(f"{label}-size-any"):        # a model that accepts any size answers OK with the callee logged
            status, log = OK, [("model-accepts",)]
        check((status, log), (BAD_PARAMS, []), f"{label} size {wrong:#x}")


def run_all():
    """Run every check; return the case count."""
    rng = random.Random(2026_11_10)
    rig = Rig()
    game, unit = rig.game, rig.unit
    cases = 0
    for _ in range(40):
        rig.m.mu.mem_write(game, bytes(rng.randrange(256) for _ in range(0x100)))
        rig.m.mu.mem_write(unit, bytes(rng.randrange(256) for _ in range(0x100)))
        rig.reroll(rng)
        b = rand_packet(rng)
        # --- forwarders with one exact size: the callee gets (game, unit, fields); they answer 0 ---------
        status, log = rig.call(0x54BA90, b, 9)
        want = (BAD_PARAMS, []) if mut("gossip-size-8") else (OK, [("gossip", game, unit, rd32(b, 1), rd16(b, 5))])
        check((status, log), want, "gossip")
        # 0x33 returns the callee's status (the 0x57_9510 callee), not 0
        mode = rd32(b, 9) if mut("sell-forwards-mode-dword") else rd16(b, 9)
        status, log = rig.call(0x54BB20, b, 0x11)
        want_status = OK if mut("sell-status-ok") else rig.returns["sell"]
        check((status, log), (want_status, [("sell", game, unit, rd32(b, 1), rd32(b, 5), mode, rd32(b, 0xD))]), "sell")
        word, dword = rd16(b, 9), rd32(b, 0xD)
        if mut("repair-swaps-params"):
            word, dword = dword & 0xFFFF, word
        status, log = rig.call(0x54BB60, b, 0x11)
        check((status, log), (OK, [("repair", game, unit, rd32(b, 1), rd32(b, 5), word, dword)]), "repair")
        for address, label, size, args in (
                (0x54BBA0, "identify", 5, (rd32(b, 1),)),
                (0x54BBD0, "hire", 9, (rd32(b, 1), rd16(b, 5))),
                (0x54C0C0, "refresh", 1, ())):
            status, log = rig.call(address, b, size)
            check((status, log), (OK, [(label, game, unit) + args]), label)
            check_size_gate(rig, rng, address, size, label)
        for address, size, label in ((0x54BA90, 9, "gossip"), (0x54BB20, 0x11, "sell"), (0x54BB60, 0x11, "repair")):
            check_size_gate(rig, rng, address, size, label)
        # --- 0x62 resurrect merc: expansion games only; the wrong sizes are tested with expansion on ----
        for expansion in (0, 1, rng.randrange(2, 1 << 32)):
            rig.m.w32(game + 0x70, expansion)
            status, log = rig.call(0x54BC00, b, 5)
            allowed = expansion != 0 or mut("resurrect-ignores-expansion")
            want = (OK, [("resurrect", game, unit, rd32(b, 1))]) if allowed else (BAD_PARAMS, [])
            check((status, log), want, f"resurrect expansion {expansion:#x}")
            if expansion:
                check_size_gate(rig, rng, 0x54BC00, 5, "resurrect")
        # --- 0x3f emote ---------------------------------------------------------------------------
        flags = rng.choice((0, 1 << 10, rng.randrange(1 << 32)))
        rig.m.w32(unit + 0xC4, flags)
        for emote in (0x18, 0x19, 0x1F, 0x20, 0x21, 0, 0xFFFF, rng.randrange(0x10000)):
            emote_b = bytes([0x3F]) + u16(emote) + bytes(0x1D)
            in_range = (emote - 0x19) & 0xFFFFFFFF <= (6 if mut("emote-range-off-by-one") else 7)
            for unit_ok in (True, False):
                status, log = rig.call(0x54C070, emote_b, 3, unit_ok)
                flag_set = (flags >> 10) & 1
                if unit_ok:
                    blocked = flag_set and not mut("emote-flag-ignored")
                else:
                    blocked = flag_set and mut("emote-null-unit-blocked")
                if not in_range:
                    want = (OK, [])
                elif blocked:
                    want = (BAD_TARGET, [])
                else:
                    want = (OK, [("emote", unit if unit_ok else 0, emote, 0)])
                check((status, log), want, f"emote {emote:#x} flags {flags:#x} unit {unit_ok}")
        check_size_gate(rig, rng, 0x54C070, 3, "emote")
        # --- 0x48 cutscene over -----------------------------------------------------------------------
        for pending in (0, 1, rng.randrange(1, 1 << 32)):
            rig.m.w32(rig.player_data + 0x4C, pending)
            status, log = rig.call(0x54C590, b, 1)
            cleared = (pending == 0) if mut("cutscene-flag-inverted") else (pending != 0)
            check((status, [entry[0] for entry in log]),
                  (OK, ["player_data", "cutscene_clear"]) if cleared else (BAD_DATA, ["player_data"]),
                  f"cutscene pending {pending:#x}")
            check(log[0], ("player_data", game, unit, unit), "cutscene: the getter's argument is the unit (stack)")
            if cleared:
                check(log[1][1], unit, "cutscene clear: ECX is the unit")
        check_size_gate(rig, rng, 0x54C590, 1, "cutscene")
        # --- 0x4f click button: returns the callee's status (the 0x56_8060 callee) ---
        w3, w5 = rd16(b, 3), rd16(b, 5)
        param = ((w5 << 16) | w3) if mut("button-param-swapped") else ((w3 << 16) | w5)
        status, log = rig.call(0x54C7C0, b, 7)
        check(status, OK if mut("button-status-ok") else rig.returns["button"], "button status")
        check(log, [("button", game, unit, rd16(b, 1), param)], "button")
        check_size_gate(rig, rng, 0x54C7C0, 7, "button")
        # --- 0x54 stop running / 0x53 gamepad (a null unit does nothing) ---------------------------------
        for unit_type in (0, 1, rng.randrange(1 << 32)):
            for mode in (0, 1, 2, 3, 5, 0xE, 17):
                rig.m.w32(unit, unit_type)
                rig.m.w32(unit + 0x10, mode)
                for unit_ok in (True, False):
                    seen = unit_ok or mut("stop-running-null-unit-calls")
                    run_mode = 2 if mut("stop-running-mode-walk") else 3
                    status, log = rig.call(0x54C990, b, 1, unit_ok)
                    target = unit if unit_ok else 0
                    flag_unit = game if mut("stop-running-args-game") else target
                    want = [("update_flag", flag_unit, 3 if mut("stop-running-flag-3") else 2)] \
                        if seen and unit_type == 0 and mode == run_mode else []
                    check((status, stack_args(log)), (OK, want), f"stop-running t{unit_type} m{mode} u{unit_ok}")
                    for stamina in (0, 1, 77):
                        rig.stamina = stamina
                        status, log = rig.call(0x54C940, b, 1, unit_ok)
                        seen = unit_ok or mut("gamepad-null-unit-calls")
                        walk = 3 if mut("gamepad-mode-run") else 2
                        ask = seen and unit_type == 0 and mode == walk
                        use = ask and (stamina != 0 or mut("gamepad-ignores-stamina"))
                        want = ([("stamina", target, 0xB if mut("gamepad-stamina-arg-0xb") else 0xA, 0)] if ask else []) \
                            + ([("update_flag", target, 2 if mut("gamepad-flag-2") else 3)] if use else [])
                        check((status, stack_args(log)), (OK, want), f"gamepad t{unit_type} m{mode} s{stamina} u{unit_ok}")
        rig.stamina = 1
        check_size_gate(rig, rng, 0x54C990, 1, "stop-running")
        check_size_gate(rig, rng, 0x54C940, 1, "gamepad")
        # --- 0x58 mark quest done --------------------------------------------------------------------------
        rig.m.mu.mem_write(rig.player_data + 0x10, bytes(rng.randrange(256) for _ in range(12)))
        difficulty = rng.randrange(0, 3)
        rig.m.mu.mem_write(game + 0x6D, bytes([difficulty]))
        for quest in (0, 1, 0x28, 0x29, 0x2A, 0x2B, 0xFFFF, rng.randrange(0x10000)):
            qb = bytes([0x58]) + u16(quest) + bytes(0x1D)
            status, log = rig.call(0x54C9C0, qb, 3)
            limit = 0x29 if mut("quest-bound-0x29") else 0x2A
            slot = (difficulty + 1) % 3 if mut("quest-wrong-difficulty-slot") else difficulty
            record = rig.m.r32(rig.player_data + 0x10 + 4 * slot)   # the pointer stored at player data +0x10 + 4 * slot
            state = 0xB if mut("quest-state-not-0xc") else 0xC
            if quest < limit:
                want = (OK, [("player_data", game, unit, unit), ("set_quest", record, quest, state)])
                check((status, [log[0]] + stack_args(log[1:])), want, f"quest {quest:#x}")
            else:
                check((status, log), (BAD_DATA, []), f"quest {quest:#x} out of range")
        check_size_gate(rig, rng, 0x54C9C0, 3, "quest")
        cases += 1
    return cases


if __name__ == "__main__":
    mw.drive(__doc__, __import__("sys").modules[__name__], run_all, "PlrMsg small handler rounds")
