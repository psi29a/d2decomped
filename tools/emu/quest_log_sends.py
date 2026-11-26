# SPDX-License-Identifier: GPL-3.0-or-later
"""Check which players a quest record's log send reaches against stock game.exe 1.14d.

Run: export D2_MPQ_DIR=/path/to/the/local/game/data
     .venv/bin/python quest_log_sends.py [--mutation-check]
     .venv/bin/python quest_log_sends.py inc --out ../../tests/quest_log_sends_cases.inc

FUN_00544300 with flag 1 runs its record's per-player callback (act 1's six,
act 2's five: a2q2 has none) over the
game's players (FUN_005537d0); the callback reads the player's quest bits and
calls FUN_00544190 (the 0x5d send) or not. This runs each callback on a
synthetic game and player over every combination of the bits it reads (0, 13,
14, 15), the bit test itself (FUN_0065c310 -> FUN_00410b30) game.exe's own on
d2d's QuestBits layout; FUN_00544190 is hooked to record the send.
"""
import argparse
import hashlib
import sys
import tempfile
from itertools import product
from pathlib import Path

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
QUEST_LOG_SEND = 0x544190
# quest word -> (its record's per-player send callback, the fn FUN_00544300 is passed; the record id the callback
# hands FUN_00544190: act 1's are their words, act 2's one less, 0x731510's table)
CALLBACKS = {
    1: (0x58FBE0, 1),    # a1q1 Den of Evil
    2: (0x590830, 2),    # a1q2 Sisters' Burial Grounds
    3: (0x5912E0, 3),    # a1q3 Tools of the Trade
    4: (0x5920D0, 4),    # a1q4 The Search for Cain
    5: (0x594830, 5),    # a1q5 The Forgotten Tower
    6: (0x595B20, 6),    # a1q6 Sisters to the Slaughter
    9: (0x5988B0, 8),    # a2q1 Radament's Lair
    11: (0x599F50, 10),  # a2q3 The Tainted Sun
    12: (0x59AD50, 11),  # a2q4 The Arcane Sanctuary
    13: (0x59BCB0, 12),  # a2q5 The Summoner
    14: (0x59C650, 13),  # a2q6 The Seven Tombs
}
BITS = (0, 13, 14, 15)


class Game:
    """A stock-code runner with one synthetic game and player."""

    def __init__(self, exe):
        """Lay out the game, the player's data and quest bits, and hook the callees."""
        self.machine = machine = emu.Emu(str(exe))
        self.game = machine.alloc(0x100)
        self.player = machine.alloc(0x100)
        self.player_data = machine.alloc(0x100)
        self.bits_object = machine.alloc(0x10)
        self.quest_bits = machine.alloc(0x60)
        self.sent = []
        machine.w32(self.player_data + 0x10, self.bits_object)   # difficulty 0's (game +0x6d is 0)
        machine.w32(self.bits_object, self.quest_bits)

        def player_data(current):
            """FUN_006221a0: the player's data."""
            return self.player_data

        def send(current):
            """FUN_00544190 (hooked): note the quest it sends."""
            self.sent.append(current.arg(0))
            return 0

        machine.hook(0x6221A0, player_data, 1)
        machine.hook(QUEST_LOG_SEND, send, 1)

    def sends(self, quest, set_bits):
        """Run `quest`'s callback for a player with `set_bits`; True when it sent that quest's record's log."""
        image = bytearray(0x60)
        for bit in set_bits:
            index = quest * 16 + bit
            image[index >> 3] |= 1 << (index & 7)
        self.machine.mu.mem_write(self.quest_bits, bytes(image))
        self.sent = []
        callback, record = CALLBACKS[quest]
        self.machine.call(callback, 0, ecx=self.game, edx=self.player, limit=10000)
        if self.sent not in ([], [record]):
            raise SystemExit(f"quest {quest}: the callback sent {self.sent}")
        return bool(self.sent)


def model(quest, set_bits, mutation=None):
    """d2d's rules::quest_log_goes_to, with optional negative controls."""
    bit = set_bits.__contains__
    shut = bit(0) or (bit(15) and (quest != 5 or mutation == "tower_bit15"))
    if mutation == "no_14":
        return not shut or bit(13)
    if mutation == "no_bit15":
        return not bit(0) or bit(13) or bit(14)
    if mutation == "act2_no_13" and quest >= 9:
        return not shut or bit(14)
    return not shut or bit(13) or bit(14)


def cases():
    """Each callback's quest over every combination of the bits it reads."""
    return [(quest, tuple(bit for bit, on in zip(BITS, flags) if on))
            for quest in CALLBACKS for flags in product((False, True), repeat=len(BITS))]


def run(game, mutation=None):
    """Compare the stock callbacks and the model; return results and the count of differences."""
    results, differences = [], 0
    for quest, set_bits in cases():
        sent = game.sends(quest, set_bits)
        expected = model(quest, set_bits, mutation)
        results.append((quest, set_bits, sent))
        if sent != expected:
            differences += 1
            if mutation is None:
                print(f"quest {quest}, bits {set_bits}: game {sent}, model {expected}", file=sys.stderr)
    return results, differences


def write_fixture(path, text):
    """Replace the fixture only after a complete successful oracle run."""
    handle = tempfile.NamedTemporaryFile("w", dir=Path(path).parent, prefix=Path(path).name + ".", suffix=".tmp", delete=False)
    try:
        with handle:
            handle.write(text)
        Path(handle.name).replace(path)
    except BaseException:
        Path(handle.name).unlink(missing_ok=True)
        raise


def main():
    """Check the callbacks against the model, or write the fixture (`inc`)."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", default="check", choices=("check", "inc"))
    parser.add_argument("--mutation-check", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args()
    if args.mode == "inc" and args.mutation_check:
        parser.error("inc and --mutation-check are separate runs")
    if args.out and args.mode != "inc":
        parser.error("--out goes with inc")
    exe = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(exe.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    game = Game(exe)
    results, differences = run(game)
    if differences:
        raise SystemExit(f"FAIL: {differences} cases differ")
    if args.mode == "inc":
        lines = ["// Stock game.exe 1.14d, each quest record's per-player log-send callback; generated by tools/emu/quest_log_sends.py inc.",
                 "// quest, bits 0 / 13 / 14 / 15 set, whether the callback sent the log (FUN_00544190)"]
        for quest, set_bits, sent in results:
            flags = ", ".join("true" if bit in set_bits else "false" for bit in BITS)
            lines.append(f"    {{ {quest}, {flags}, {'true' if sent else 'false'} }},")
        text = "\n".join(lines) + "\n"
        if args.out:
            write_fixture(args.out, text)
        else:
            sys.stdout.write(text)
        return
    print(f"ok: {len(results)} quest-log send callback cases")
    if args.mutation_check:
        for mutation in ("tower_bit15", "no_14", "no_bit15", "act2_no_13"):
            _, caught = run(game, mutation)
            if not caught:
                raise SystemExit(f"mutation {mutation}: NOT caught")
            print(f"mutation {mutation}: caught ({caught} differ)")


if __name__ == "__main__":
    main()
