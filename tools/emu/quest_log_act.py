# SPDX-License-Identifier: GPL-3.0-or-later
"""Check FUN_00544190's quest-record act gate against stock game.exe 1.14d.

Run: export D2_MPQ_DIR=/path/to/the/local/game/data
     .venv/bin/python quest_log_act.py [--mutation-check]
     .venv/bin/python quest_log_act.py inc --out ../../tests/quest_log_act_cases.inc

The native function walks a synthetic one-record quest list. Its unit-room,
room-level and level-act callees are hooked to provide every player act while
the record +8 byte provides every record act. Reaching the player-data lookup
means the log passed the act gate; downstream log construction and sending are
hooked because this oracle claims only the gate.
"""
import argparse
import hashlib
import sys
import tempfile
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
QUEST_LOG_SEND = 0x544190


class Game:
    """A stock-code runner with one synthetic quest record and player."""

    def __init__(self, exe):
        """Lay out the game, quest record and player, and hook the callees."""
        self.machine = machine = emu.Emu(str(exe))
        self.game = machine.alloc(0x1200)
        self.quest_data = machine.alloc(0x100)
        self.record = machine.alloc(0x100)
        self.player = machine.alloc(0x100)
        self.player_data = machine.alloc(0x200)
        self.quest_bits = machine.alloc(0x60)
        self.room = machine.alloc(0x100)
        self.level = 77
        self.player_act = 0
        self.passed = False
        machine.w32(self.game + 0x10F4, self.quest_data)
        machine.w32(self.quest_data, self.record)
        machine.w32(self.player_data + 0x10, self.quest_bits)

        machine.hook(0x5531C0, lambda current: 0x12345678, 1)
        machine.hook(0x620BB0, lambda current: self.room, 0)
        machine.hook(0x61A1B0, lambda current: self.level, 1)
        machine.hook(0x6427F0, lambda current: self.player_act, 1)

        def player_data(current):
            """FUN_006221a0: reached only past the act gate, so mark the pass."""
            self.passed = True
            return self.player_data

        def default_log(current):
            """FUN_00543f90 (downstream of the gate, hooked): byte 7 at EDX, returns 0."""
            current.mu.mem_write(current.mu.reg_read(UC_X86_REG_EDX), b"\x07")
            return 0

        machine.hook(0x6221A0, player_data, 1)
        machine.hook(0x543F90, default_log, 1)
        machine.hook(0x53D710, lambda current: 0, 0)

    def sends(self, record_act, player_act):
        """Run one record/player act pair and report whether the gate passed."""
        machine = self.machine
        machine.mu.mem_write(self.record, bytes(0x100))
        machine.w32(self.record, 5)
        machine.mu.mem_write(self.record + 8, bytes([record_act]))
        machine.mu.mem_write(self.record + 0xB, b"\x03")
        machine.w32(self.record + 0xE0, 2)
        self.player_act = player_act
        self.passed = False
        machine.call(QUEST_LOG_SEND, 5, ecx=self.game, edx=self.player, limit=10000)
        return self.passed


def model(record_act, player_act, mutation=None):
    """The d2d rule, with optional negative controls."""
    if mutation == "off_by_one":
        return record_act + 1 == player_act
    if mutation == "always":
        return True
    return record_act == player_act


def cases():
    """Every valid act pair and byte-edge record acts."""
    return [(record_act, player_act) for record_act in (*range(5), 0xFF) for player_act in range(5)]


def run(game, mutation=None):
    """Compare the stock gate and the model; return results and differences."""
    results = []
    for record_act, player_act in cases():
        sent = game.sends(record_act, player_act)
        expected = model(record_act, player_act, mutation)
        results.append((record_act, player_act, sent))
        if sent != expected:
            print(f"record act {record_act}, player act {player_act}: game {sent}, model {expected}", file=sys.stderr)
    return results, sum(sent != model(record_act, player_act, mutation) for record_act, player_act, sent in results)


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
    """Check FUN_00544190 against the model, or write the fixture (`inc`)."""
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
        lines = ["// Stock game.exe 1.14d, FUN_00544190; generated by tools/emu/quest_log_act.py inc.",
                 "// record +8 act, player's level act, whether the log send passed its act gate"]
        lines.extend(f"    {{ {record_act}, {player_act}, {'true' if sent else 'false'} }}," for record_act, player_act, sent in results)
        text = "\n".join(lines) + "\n"
        if args.out:
            write_fixture(args.out, text)
        else:
            sys.stdout.write(text)
        return
    print(f"ok: {len(results)} FUN_00544190 quest-log act-gate cases")
    if args.mutation_check:
        for mutation in ("off_by_one", "always"):
            _, caught = run(game, mutation)
            if not caught:
                raise SystemExit(f"mutation {mutation}: NOT caught")
            print(f"mutation {mutation}: caught ({caught} differ)")


if __name__ == "__main__":
    main()
