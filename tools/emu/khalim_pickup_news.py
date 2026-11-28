# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a3q2's pickup send, FUN_005b8210 -> FUN_005b81c0, in game.exe 1.14d.

    .venv/bin/python khalim_pickup_news.py [--mutation-check]
    .venv/bin/python khalim_pickup_news.py inc --out ../../tests/khalim_pickup_news_cases.inc

The two functions run for real.  The item-code and carried-item queries are
hooked with synthetic answers, and the final six-byte client send is captured.
The player's quest-bit test runs as stock code; the client lookup and final
send are hooked and their player/client arguments are checked.
The cases cover each Khalim item, every combination of the other four carried
parts and bits 5 / 7, plus both entry gates.  ``inc`` freezes those answers for
test_khalim_pickup_news.  ``--mutation-check`` requires six deliberately wrong
models to disagree with game.exe.
"""
import argparse
import hashlib
import sys
import tempfile
from itertools import product
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

import emu

GAME_SHA256 = "631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf"
PICKUP = 0x5B8210
CODES = ("qey ", "qbr ", "qhr ", "qf1 ", "qf2 ")
LOOKUP_ORDER = ("qf1 ", "qf2 ", "qey ", "qbr ", "qhr ")
CODE_VALUES = {code: int.from_bytes(code.encode("ascii"), "little") for code in CODES}
BITS = {"qey ": 0, "qbr ": 1, "qhr ": 2, "qf1 ": 3, "qf2 ": 4}
MUTATIONS = ("no-gate", "will-sends", "flail-bit", "eye-order", "heart-order", "brain-order")


def model(code, carried, bit5, bit7, prerequisite=True, done=False, mutation=None):
    """Return FUN_005b8210's packet state, or -1 when it sends nothing."""
    if mutation != "no-gate" and (not prerequisite or done):
        return -1
    eye, brain, heart, flail, _will = (name in carried for name in CODES)
    if code == "qf2 ":
        # The record log is changed separately, but the repeated prerequisite
        # test at 0x5b8344 is true and returns before the apparent states 5/6.
        return 6 if mutation == "will-sends" else -1
    if code == "qf1 ":
        if not eye:
            return 1
        if not brain:
            return 2
        if not heart:
            return 4
        answer = 5 if not bit5 else 7
        return 7 if mutation == "flail-bit" and answer == 5 else answer
    if code == "qey ":
        if not brain:
            return 2
        if mutation == "eye-order" and not heart:
            return 4
        if not flail:
            return 3
        if not heart:
            return 4
        return 7
    if code == "qhr ":
        if not brain:
            return 2
        if mutation == "heart-order" and not eye:
            return 1
        if not flail:
            return 3
        if not eye:
            return 1
        return 7
    if code == "qbr ":
        if not heart:
            return 4
        if mutation == "brain-order" and not eye:
            return 1
        if not flail:
            return 3
        if not eye:
            return 1
        return 7
    return -1


def cases():
    """The reachable pickup matrix and explicit failures of each entry gate."""
    result = []
    for code in CODES:
        own = 1 << BITS[code]
        for mask in range(32):
            if not mask & own:
                continue
            carried = {name for name, index in BITS.items() if mask & (1 << index)}
            for bit5, bit7 in product((False, True), repeat=2):
                result.append((code, mask, bit5, bit7, True, False,
                               model(code, carried, bit5, bit7)))
        result.append((code, own, False, False, False, False, -1))
        result.append((code, own, False, False, True, True, -1))
    return result


class Game:
    """One synthetic record, event, player and quest-bit buffer."""

    def __init__(self, executable):
        """Allocate the synthetic objects and install the observed boundaries."""
        self.machine = machine = emu.Emu(str(executable))
        self.game = machine.alloc(0x100)
        self.record = machine.alloc(0x100)
        self.event = machine.alloc(0x40)
        self.item = machine.alloc(0x40)
        self.player = machine.alloc(0x100)
        self.player_data = machine.alloc(0x100)
        self.bits_object = machine.alloc(0x10)
        self.quest_bits = machine.alloc(0x60)
        self.client = machine.alloc(0x20)
        machine.mu.mem_write(self.game + 0x6D, b"\x00")
        machine.w32(self.event, self.game)
        machine.w32(self.event + 8, self.item)
        machine.w32(self.event + 0xC, self.player)
        machine.w32(self.player_data + 0x10, self.bits_object)
        machine.w32(self.bits_object, self.quest_bits)
        machine.w32(self.player_data + 0x9C, self.client)
        machine.mu.mem_write(self.record + 0x14, b"\xa6")
        self.code = CODES[0]
        self.carried = set()
        self.lookup_calls = []
        self.packets = []

        def player_data(current):
            """Return the event player's synthetic PlayerData."""
            if current.arg(0) != self.player:
                raise AssertionError("FUN_006221a0 did not receive the event player")
            return self.player_data

        def item_code(current):
            """Return the selected case's event-item code."""
            if current.arg(0) != self.item:
                raise AssertionError("FUN_00628590 did not receive the event item")
            return CODE_VALUES[self.code]

        def client_of(current):
            """Resolve only the event player to the synthetic client."""
            if current.mu.reg_read(UC_X86_REG_ECX) != self.player:
                raise AssertionError("FUN_005531c0 did not receive the event player")
            return self.client

        def carried(current):
            """Answer the five carried-item probes and record their order."""
            if (current.mu.reg_read(UC_X86_REG_ECX), current.mu.reg_read(UC_X86_REG_EDX)) != (self.game, self.player):
                raise AssertionError("FUN_00558110 did not receive the event game/player")
            value = current.arg(0)
            self.lookup_calls.append(value)
            return int(any(CODE_VALUES[name] == value for name in self.carried))

        def send(current):
            """Capture the six-byte packet after checking its client."""
            if current.mu.reg_read(UC_X86_REG_ECX) != self.client:
                raise AssertionError("FUN_0053d710 did not receive the event player's client")
            pointer = current.mu.reg_read(UC_X86_REG_EDX)
            self.packets.append(machine.read(pointer, 6))
            return 0

        machine.hook(0x6221A0, player_data, 1)
        machine.hook(0x628590, item_code, 1)
        machine.hook(0x558110, carried, 1)
        machine.hook(0x5531C0, client_of, 1)
        machine.hook(0x53D710, send, 0)

    def run(self, sample):
        """Run one sample and return (packet state or -1, record log byte)."""
        code, mask, bit5, bit7, prerequisite, done, _expected = sample
        self.code = code
        self.carried = {name for name, index in BITS.items() if mask & (1 << index)}
        image = bytearray(0x60)
        for quest, bit, enabled in ((0xF, 0, prerequisite), (0x12, 0, done),
                                    (0x12, 5, bit5), (0x12, 7, bit7)):
            if enabled:
                offset = quest * 16 + bit
                image[offset >> 3] |= 1 << (offset & 7)
        self.machine.mu.mem_write(self.quest_bits, bytes(image))
        self.machine.mu.mem_write(self.record + 0xB, b"\x03")
        self.lookup_calls.clear()
        self.packets.clear()
        self.machine.call(PICKUP, ecx=self.record, edx=self.event, limit=20000)
        expected_lookups = [CODE_VALUES[name] for name in LOOKUP_ORDER]
        if prerequisite and not done and self.lookup_calls != expected_lookups:
            raise AssertionError(f"{sample}: carried-item calls {self.lookup_calls!r}")
        if not prerequisite or done:
            if self.lookup_calls:
                raise AssertionError(f"{sample}: entry gate still queried carried items")
        if len(self.packets) > 1:
            raise AssertionError(f"{sample}: sent {len(self.packets)} packets")
        state = -1
        if self.packets:
            packet = self.packets[0]
            if packet[:3] != bytes((0x5D, 0x10, 0xA6)) or packet[4:] != b"\x00\x00":
                raise AssertionError(f"{sample}: packet {packet.hex()}")
            state = packet[3]
        return state, self.machine.read(self.record + 0xB, 1)[0]


def run(game, mutation=None):
    """Compare every stock result with the model; return results and differences."""
    results = []
    differences = 0
    for sample in cases():
        code, mask, bit5, bit7, prerequisite, done, expected = sample
        stock_state, stock_log = game.run(sample)
        carried = {name for name, index in BITS.items() if mask & (1 << index)}
        wanted = expected if mutation is None else model(code, carried, bit5, bit7, prerequisite, done, mutation)
        wanted_log = 7 if prerequisite and not done and code == "qf2 " else 3
        results.append((*sample[:-1], stock_state, stock_log))
        if (stock_state, stock_log) != (wanted, wanted_log):
            differences += 1
    return results, differences


def write_fixture(path, text):
    """Replace a fixture only after the complete oracle succeeds."""
    destination = Path(path)
    handle = tempfile.NamedTemporaryFile("w", dir=destination.parent, prefix=destination.name + ".", suffix=".tmp", delete=False)
    try:
        with handle:
            handle.write(text)
        Path(handle.name).replace(destination)
    except BaseException:
        Path(handle.name).unlink(missing_ok=True)
        raise


def main():
    """Run the comparison, mutation controls, or write the C++ fixture."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", nargs="?", default="check", choices=("check", "inc"))
    parser.add_argument("--out")
    parser.add_argument("--mutation-check", action="store_true")
    args = parser.parse_args()
    if args.mode == "inc" and args.mutation_check:
        parser.error("inc and --mutation-check are separate runs")
    if bool(args.out) != (args.mode == "inc"):
        parser.error("inc requires --out, which is invalid in check mode")
    executable = Path(emu.data_path()) / "bin" / "game.exe"
    if hashlib.sha256(executable.read_bytes()).hexdigest() != GAME_SHA256:
        raise SystemExit("requires stock game.exe 1.14d (1.14.3.71)")
    game = Game(executable)
    results, differences = run(game)
    if differences:
        raise SystemExit(f"FAIL: {differences} stock cases differ from the model")
    if args.mode == "inc":
        lines = [
            "// Stock game.exe 1.14d FUN_005b8210 -> FUN_005b81c0; generated by tools/emu/khalim_pickup_news.py inc.",
            "// code, carried mask (eye/brain/heart/flail/will), bits 5/7, prerequisite, done, packet state, record log",
        ]
        for code, mask, bit5, bit7, prerequisite, done, state, record_log in results:
            flags = ", ".join("true" if flag else "false" for flag in (bit5, bit7, prerequisite, done))
            lines.append(f'    {{ "{code.strip()}", {mask}, {flags}, {state}, {record_log} }},')
        write_fixture(args.out, "\n".join(lines) + "\n")
        print(f"wrote {len(results)} FUN_005b8210 pickup cases")
        return 0
    print(f"ok: {len(results)} FUN_005b8210 -> FUN_005b81c0 pickup cases")
    if args.mutation_check:
        for mutation in MUTATIONS:
            _results, caught = run(game, mutation)
            if not caught:
                print(f"NOT CAUGHT: {mutation}")
                return 1
            print(f"caught: {mutation} ({caught} differ)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
